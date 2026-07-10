#include "rc_ex3.h"
#include "esphome/core/log.h"

namespace esphome {
namespace rc_ex3 {

static const char *const TAG = "rc_ex3";

// ─── Traits ──────────────────────────────────────────────────────────────────

climate::ClimateTraits RcEx3Climate::traits() {
  auto traits = climate::ClimateTraits();
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  traits.set_supported_modes({
    climate::CLIMATE_MODE_OFF,
    climate::CLIMATE_MODE_HEAT_COOL,
    climate::CLIMATE_MODE_COOL,
    climate::CLIMATE_MODE_HEAT,
    climate::CLIMATE_MODE_DRY,
    climate::CLIMATE_MODE_FAN_ONLY,
  });
  traits.set_supported_fan_modes({climate::CLIMATE_FAN_AUTO});
  traits.set_visual_min_temperature(16.0f);
  traits.set_visual_max_temperature(30.0f);
  traits.set_visual_temperature_step(0.5f);
  return traits;
}

// ─── Lifecycle ───────────────────────────────────────────────────────────────

void RcEx3Climate::setup() {
  this->set_supported_custom_fan_modes({"1", "2", "3", "4"});
  this->mode                = climate::CLIMATE_MODE_OFF;
  this->target_temperature  = 22.0f;
  this->current_temperature = NAN;
}

void RcEx3Climate::update() {
  // Do not interleave a scheduled query with a command response.
  if (pending_fields_ != PENDING_NONE && (millis() - last_command_ms_) < COMMAND_SETTLE_MS) {
    ESP_LOGD(TAG, "status poll skipped while command is settling");
    return;
  }

  send_status_request();

  op_data_requested_ = false;
  if (op_data_interval_minutes_ == 0)
    return;

  const uint32_t now = millis();
  const uint32_t interval_ms = op_data_interval_minutes_ * 60000UL;
  if (last_op_data_ms_ == 0 || (now - last_op_data_ms_) >= interval_ms)
    op_data_requested_ = true;
}

// ─── Serial RX loop ──────────────────────────────────────────────────────────

void RcEx3Climate::loop() {
  while (this->available()) {
    uint8_t c;
    if (!this->read_byte(&c))
      break;

    if (rx_state_ == RxState::WAITING_FOR_SOF) {
      if (c == 0x02) {
        rx_len_ = 0;
        rx_overflowed_ = false;
        rx_state_ = RxState::READING_PAYLOAD;
      }
    } else {
      if (c == 0x03) {
        if (rx_overflowed_) {
          ESP_LOGW(TAG, "rx frame dropped after overflow");
        } else {
          rx_buf_[rx_len_] = '\0';
          parse_packet(rx_buf_, rx_len_);
        }
        rx_state_ = RxState::WAITING_FOR_SOF;
        rx_len_   = 0;
      } else if (rx_len_ < RX_BUF_SIZE - 1) {
        rx_buf_[rx_len_++] = static_cast<char>(c);
      } else {
        rx_overflowed_ = true;
      }
    }
  }

  // Send the op_data request once the status response has been received,
  // separated by one loop tick to avoid overlapping Tx.
  if (op_data_pending_) {
    op_data_pending_ = false;
    send_operational_data_request(false);
  }
}

// ─── HA control call ─────────────────────────────────────────────────────────

void RcEx3Climate::control(const climate::ClimateCall &call) {
  // The RC-EX3 protocol uses FF for fields that should remain unchanged.  Only
  // send values present in this ClimateCall so a stale local cache cannot
  // overwrite a newer setting made at the wall controller.
  uint8_t power = 0xFF;
  uint8_t mode = 0xFF;
  uint8_t fan = 0xFF;
  uint8_t temp_wire = 0xFF;
  bool has_change = false;

  if (call.get_mode().has_value()) {
    this->mode = *call.get_mode();
    // Selecting a mode also turns the unit on; OFF only changes power.
    power = (this->mode == climate::CLIMATE_MODE_OFF) ? 0 : 1;
    if (this->mode != climate::CLIMATE_MODE_OFF)
      mode = climate_mode_to_wire(this->mode);
    pending_mode_ = this->mode;
    pending_fields_ |= PENDING_MODE;
    has_change = true;
  }

  if (call.get_target_temperature().has_value()) {
    this->target_temperature = *call.get_target_temperature();
    temp_wire = static_cast<uint8_t>(this->target_temperature * 2.0f);
    pending_temperature_ = this->target_temperature;
    pending_fields_ |= PENDING_TEMPERATURE;
    has_change = true;
  }

  if (call.get_fan_mode().has_value()) {
    this->set_fan_mode_(*call.get_fan_mode());
    fan = fan_mode_to_wire(*call.get_fan_mode());
    pending_fan_wire_ = fan;
    pending_fields_ |= PENDING_FAN;
    has_change = true;
  }

  auto custom_fan_mode = call.get_custom_fan_mode();
  if (!custom_fan_mode.empty()) {
    if (custom_fan_mode == "1") fan = 0x00;
    else if (custom_fan_mode == "2") fan = 0x01;
    else if (custom_fan_mode == "3") fan = 0x02;
    else if (custom_fan_mode == "4") fan = 0x06;
    this->set_custom_fan_mode_(custom_fan_mode);
    pending_fan_wire_ = fan;
    pending_fields_ |= PENDING_FAN;
    has_change = true;
  }

  if (!has_change)
    return;

  char buf[64];
  size_t len;
  // Temperature uses the longer RSSL13 packet; other changes use RSSL12.
  if (call.get_target_temperature().has_value()) {
    len = snprintf(buf, sizeof(buf),
      "RSSL13FF0001%.2x02%.2x03%.2x04FF0503%.2x06FF0FFF43FF",
      power, mode, fan, temp_wire);
  } else {
    len = snprintf(buf, sizeof(buf),
      "RSSL12FF0001%.2x02%.2x03%.2x04FF05FF06FF0FFF43FF",
      power, mode, fan);
  }

  ESP_LOGI(TAG, "tx → partial power=0x%02x mode=0x%02x fan=0x%02x temp=0x%02x",
           power, mode, fan, temp_wire);

  // Publish optimistically now; a later status frame confirms or corrects it.
  last_command_ms_ = millis();
  send_command(buf, len);
  this->publish_state();
}

// ─── Packet dispatch ─────────────────────────────────────────────────────────

void RcEx3Climate::parse_packet(const char *raw, size_t len) {
  char payload[256];
  size_t payload_len = 0;
  if (!validate_checksum_and_extract_payload_(raw, len, payload, sizeof(payload), payload_len))
    return;

  char buf[256];
  size_t buflen = 0;
  bool started = false;

  for (size_t i = 0; i < payload_len && buflen < sizeof(buf) - 1; i++) {
    uint8_t c = static_cast<uint8_t>(payload[i]);
    if (!started) {
      if (payload[i] == 'R') {
        buf[buflen++] = payload[i];
        started = true;
      }
    } else if (c >= 32 && c < 127) {
      buf[buflen++] = payload[i];
    }
  }
  buf[buflen] = '\0';

  if (buflen < 5)
    return;

  ESP_LOGV(TAG, "rx: %s", buf);

  // RSSL1x is authoritative climate state; RSSL0x is a command ACK and
  // contains no state to publish.
  if (buf[0] == 'R' && buf[1] == 'S' && buf[2] == 'S' && buf[3] == 'L') {
    if (buf[4] == '1') {
      parse_status_response(buf, buflen);
      if (op_data_requested_) {
        op_data_requested_ = false;
        op_data_pending_   = true;
      }
    } else if (buf[4] == '0') {
      ESP_LOGD(TAG, "rx ← command ACK");
    } else {
      ESP_LOGD(TAG, "rx unhandled RSSL: %s", buf);
    }
    return;
  }

  // RSR → operational data handshake / response
  if (buf[0] == 'R' && buf[1] == 'S' && buf[2] == 'R') {
    if (buf[3] == '2') {
      // Unit not yet ready; echo RSR2 immediately and it will eventually respond RSR1
      send_operational_data_request(true);
    } else if (buf[3] == '1') {
      parse_operational_data(buf, buflen);
    }
    return;
  }

  ESP_LOGD(TAG, "rx unhandled: %s", buf);
}


bool RcEx3Climate::validate_checksum_and_extract_payload_(const char *raw, size_t len, char *payload,
                                                          size_t payload_size, size_t &payload_len) {
  payload_len = 0;
  if (len < 3) {
    ESP_LOGW(TAG, "rx frame too short for checksum");
    return false;
  }

  const size_t body_len = len - 2;
  const char rx_hi = raw[body_len];
  const char rx_lo = raw[body_len + 1];
  if (!isxdigit(static_cast<uint8_t>(rx_hi)) || !isxdigit(static_cast<uint8_t>(rx_lo))) {
    ESP_LOGW(TAG, "rx frame missing checksum hex");
    return false;
  }

  char rx_sum_hex[3] = {rx_hi, rx_lo, '\0'};
  uint8_t rx_sum = static_cast<uint8_t>(strtol(rx_sum_hex, nullptr, 16));
  uint8_t calc_sum = calc_checksum(raw, body_len);
  if (rx_sum != calc_sum) {
    ESP_LOGW(TAG, "rx checksum mismatch: got=%02X expected=%02X", rx_sum, calc_sum);
    return false;
  }

  payload_len = (body_len < (payload_size - 1)) ? body_len : (payload_size - 1);
  memcpy(payload, raw, payload_len);
  payload[payload_len] = '\0';
  return true;
}

// ─── Status response parser ───────────────────────────────────────────────────
//
//   [0-3]  "RSSL"
//   [4]    '1'
//   [13]   power  ('0'=off, '1'=on)
//   [17]   mode   ('0'=auto,'1'=dry,'2'=cool,'3'=fan,'4'=heat)
//   [21]   fan    ('0'=spd1,'1'=spd2,'2'=spd3,'6'=spd4,other=auto)
//   [30-31] temp  (2 hex chars, value * 0.5 = °C)

void RcEx3Climate::parse_status_response(const char *buf, size_t len) {
  if (len < 32)
    return;

  char pwr_c  = buf[13];
  char mode_c = buf[17];
  char fan_c  = buf[21];

  char tmp[3] = {buf[30], buf[31], '\0'};
  unsigned int raw_temp = static_cast<unsigned int>(strtol(tmp, nullptr, 16));
  float temp_c = raw_temp * 0.5f;

  bool is_on = (pwr_c == '1');
  climate::ClimateMode new_mode = is_on ? wire_to_climate_mode(mode_c - '0') : climate::CLIMATE_MODE_OFF;
  uint8_t new_fan_wire = static_cast<uint8_t>(fan_c - '0');

  ESP_LOGD(TAG, "status: power=%c mode=%c fan=%c temp=%.1f°C", pwr_c, mode_c, fan_c, temp_c);

  const bool settling = pending_fields_ != PENDING_NONE &&
                        (millis() - last_command_ms_) < COMMAND_SETTLE_MS;

  // A matching field confirms the command. During settling, keep the
  // optimistic value when the controller briefly reports its previous state.
  if ((pending_fields_ & PENDING_MODE) == 0 || new_mode == pending_mode_) {
    this->mode = new_mode;
    if (new_mode == pending_mode_)
      pending_fields_ &= ~PENDING_MODE;
  } else if (settling) {
    ESP_LOGD(TAG, "ignoring stale mode while command settles");
  }

  if ((pending_fields_ & PENDING_FAN) == 0 || new_fan_wire == pending_fan_wire_) {
    apply_wire_fan_mode_(fan_c);
    if (new_fan_wire == pending_fan_wire_)
      pending_fields_ &= ~PENDING_FAN;
  } else if (settling) {
    ESP_LOGD(TAG, "ignoring stale fan while command settles");
  }

  if ((pending_fields_ & PENDING_TEMPERATURE) == 0 ||
      std::fabs(temp_c - pending_temperature_) < 0.01f) {
    this->target_temperature = temp_c;
    if (std::fabs(temp_c - pending_temperature_) < 0.01f)
      pending_fields_ &= ~PENDING_TEMPERATURE;
  } else if (settling) {
    ESP_LOGD(TAG, "ignoring stale temperature while command settles");
  }

  // Once the short stale-response window has expired, the controller is
  // authoritative even if it rejected or normalised the requested value.
  if (!settling && pending_fields_ != PENDING_NONE) {
    this->mode = new_mode;
    apply_wire_fan_mode_(fan_c);
    this->target_temperature = temp_c;
    pending_fields_ = PENDING_NONE;
  }

  if (std::isnan(this->current_temperature) && indoor_temperature_sensor_ &&
      !std::isnan(indoor_temperature_sensor_->state)) {
    this->current_temperature = indoor_temperature_sensor_->state;
  }
  this->publish_state();
}

void RcEx3Climate::apply_wire_fan_mode_(char wire_value) {
  // ESPHome stores numbered speeds as custom modes, separate from AUTO.
  switch (wire_value) {
    case '0': this->set_custom_fan_mode_("1"); break;
    case '1': this->set_custom_fan_mode_("2"); break;
    case '2': this->set_custom_fan_mode_("3"); break;
    case '6': this->set_custom_fan_mode_("4"); break;
    default: this->set_fan_mode_(climate::CLIMATE_FAN_AUTO); break;
  }
}

// ─── Operational data parser ──────────────────────────────────────────────────
//
// Request:  RSR10000E8  → page 1
// Response: RSR1<hex-encoded binary blob>
// After HEADER_LEN=4 ("RSR1"), the rest is hex pairs encoding raw bytes.

void RcEx3Climate::parse_operational_data(const char *buf, size_t len) {
  const char *hex_data = buf + HEADER_LEN;
  uint8_t data[256] = {};
  size_t data_len = hex_to_bytes(hex_data, data, sizeof(data));

  if (data_len < (POS_INDOOR_FAN_SPEED - HEADER_LEN + 1)) {
    ESP_LOGW(TAG, "op-data too short (%d bytes)", (int)data_len);
    return;
  }

  auto idx = [](uint8_t pos) { return pos - HEADER_LEN; };

  float indoor_air  = static_cast<float>(static_cast<int8_t>(data[idx(POS_INDOOR_AIR_TEMP)]));
  float outdoor_air = static_cast<float>(static_cast<uint8_t>(data[idx(POS_OUTDOOR_AIR_TEMP)]) / 4 - 22);
  float return_air  = static_cast<float>(data[idx(POS_RETURN_AIR_TEMP)]) / 10.0f;
  uint8_t comp_hz   = data[idx(POS_COMPRESSOR_HZ)];
  uint8_t in_fan    = data[idx(POS_INDOOR_FAN_SPEED)];

  ESP_LOGD(TAG, "op-data raw: indoor=%d outdoor=%d return=%d",
           data[idx(POS_INDOOR_AIR_TEMP)], data[idx(POS_OUTDOOR_AIR_TEMP)], data[idx(POS_RETURN_AIR_TEMP)]);
  ESP_LOGI(TAG, "op-data → indoor=%.1f°C outdoor=%.1f°C return=%.1f°C comp=%dHz fan=%d",
           indoor_air, outdoor_air, return_air, comp_hz, in_fan);

  if (indoor_temperature_sensor_)     indoor_temperature_sensor_->publish_state(indoor_air);
  if (outdoor_temperature_sensor_)    outdoor_temperature_sensor_->publish_state(outdoor_air);
  if (return_air_temperature_sensor_) return_air_temperature_sensor_->publish_state(return_air);
  if (compressor_frequency_sensor_)   compressor_frequency_sensor_->publish_state(comp_hz);
  if (indoor_fan_speed_sensor_)       indoor_fan_speed_sensor_->publish_state(in_fan);

  last_op_data_ms_ = millis();
  this->current_temperature = indoor_air;
  this->publish_state();
}

// ─── Packet send helpers ─────────────────────────────────────────────────────

uint8_t RcEx3Climate::calc_checksum(const char *data, size_t len) {
  uint8_t sum = 0;
  for (size_t i = 0; i < len; i++)
    sum += static_cast<uint8_t>(data[i]);
  return sum;
}

void RcEx3Climate::send_command(const char *payload, size_t len) {
  uint8_t sum = calc_checksum(payload, len);
  char hex_sum[3];
  snprintf(hex_sum, sizeof(hex_sum), "%02X", sum);

  this->write_byte(0x02);
  for (size_t i = 0; i < len; i++)
    this->write_byte(static_cast<uint8_t>(payload[i]));
  this->write_byte(static_cast<uint8_t>(hex_sum[0]));
  this->write_byte(static_cast<uint8_t>(hex_sum[1]));
  this->write_byte(0x03);
}

void RcEx3Climate::send_status_request() {
  const char *query = "RSSL12FF0001FF02FF03FF04FF05FF06FF0FFF43FF25";
  this->write_byte(0x02);
  for (const char *p = query; *p; p++)
    this->write_byte(static_cast<uint8_t>(*p));
  this->write_byte(0x03);
}

void RcEx3Climate::send_operational_data_request(bool second_page) {
  const char *query = second_page ? "RSR20000E9" : "RSR10000E8";
  this->write_byte(0x02);
  for (const char *p = query; *p; p++)
    this->write_byte(static_cast<uint8_t>(*p));
  this->write_byte(0x03);
}

// ─── Encoding helpers ─────────────────────────────────────────────────────────

uint8_t RcEx3Climate::climate_mode_to_wire(climate::ClimateMode mode) {
  switch (mode) {
    case climate::CLIMATE_MODE_HEAT_COOL: return 0;
    case climate::CLIMATE_MODE_DRY:       return 1;
    case climate::CLIMATE_MODE_COOL:      return 2;
    case climate::CLIMATE_MODE_FAN_ONLY:  return 3;
    case climate::CLIMATE_MODE_HEAT:      return 4;
    default:                              return 0;
  }
}

climate::ClimateMode RcEx3Climate::wire_to_climate_mode(uint8_t v) {
  switch (v) {
    case 0: return climate::CLIMATE_MODE_HEAT_COOL;
    case 1: return climate::CLIMATE_MODE_DRY;
    case 2: return climate::CLIMATE_MODE_COOL;
    case 3: return climate::CLIMATE_MODE_FAN_ONLY;
    case 4: return climate::CLIMATE_MODE_HEAT;
    default: return climate::CLIMATE_MODE_HEAT_COOL;
  }
}

uint8_t RcEx3Climate::fan_mode_to_wire(climate::ClimateFanMode) {
  return 0x07;
}

climate::ClimateFanMode RcEx3Climate::wire_to_fan_mode(char c) {
  return (c == '7') ? climate::CLIMATE_FAN_AUTO : climate::CLIMATE_FAN_ON;
}

size_t RcEx3Climate::hex_to_bytes(const char *hex, uint8_t *out, size_t max_out) {
  size_t count = 0;
  char tmp[3] = {0};
  while (hex[0] && hex[1] && count < max_out) {
    if (!isxdigit(static_cast<uint8_t>(hex[0])) || !isxdigit(static_cast<uint8_t>(hex[1])))
      break;
    tmp[0] = hex[0];
    tmp[1] = hex[1];
    out[count++] = static_cast<uint8_t>(strtol(tmp, nullptr, 16));
    hex += 2;
  }
  return count;
}

}  // namespace rc_ex3
}  // namespace esphome
