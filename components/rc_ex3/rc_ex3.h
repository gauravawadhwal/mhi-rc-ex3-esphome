#pragma once

#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"

#include <string>

namespace esphome {
namespace rc_ex3 {

static const uint8_t HEADER_LEN           = 4;
static const uint8_t POS_INDOOR_AIR_TEMP  = 9;
static const uint8_t POS_OUTDOOR_AIR_TEMP = 26;
static const uint8_t POS_RETURN_AIR_TEMP  = 27;
static const uint8_t POS_COMPRESSOR_HZ    = 32;
static const uint8_t POS_INDOOR_FAN_SPEED = 45;

enum class RxState : uint8_t {
  WAITING_FOR_SOF,
  READING_PAYLOAD,
};

class RcEx3Climate : public climate::Climate, public uart::UARTDevice, public PollingComponent {
 public:
  RcEx3Climate() = default;

  void setup() override;
  void loop() override;
  void update() override;
  void control(const climate::ClimateCall &call) override;
  climate::ClimateTraits traits() override;

  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_op_data_interval(uint32_t minutes) { op_data_interval_minutes_ = minutes; }
  void set_fan_speed_count(uint8_t count) { fan_speed_count_ = count; }
  void set_use_standard_fan_modes(bool enabled) { use_standard_fan_modes_ = enabled; }
  void set_auto_mode(bool enabled) { auto_mode_ = enabled; }

  void set_indoor_temperature_sensor(sensor::Sensor *s)    { indoor_temperature_sensor_    = s; }
  void set_outdoor_temperature_sensor(sensor::Sensor *s)   { outdoor_temperature_sensor_   = s; }
  void set_return_air_temperature_sensor(sensor::Sensor *s){ return_air_temperature_sensor_ = s; }
  void set_compressor_frequency_sensor(sensor::Sensor *s)  { compressor_frequency_sensor_  = s; }
  void set_indoor_fan_speed_sensor(sensor::Sensor *s)      { indoor_fan_speed_sensor_      = s; }

 protected:
  void send_command(const char *payload, size_t len);
  void send_status_request();
  void send_operational_data_request(bool second_page = false);

  void parse_packet(const char *raw, size_t len);
  bool validate_checksum_and_extract_payload_(const char *raw, size_t len, char *payload, size_t payload_size, size_t &payload_len);
  void parse_status_response(const char *buf, size_t len);
  void parse_operational_data(const char *buf, size_t len);
  void apply_wire_fan_mode_(char wire_value);

  uint8_t calc_checksum(const char *data, size_t len);
  size_t  hex_to_bytes(const char *hex, uint8_t *out, size_t max_out);

  static uint8_t              fan_mode_to_wire(climate::ClimateFanMode mode);
  static climate::ClimateFanMode wire_to_fan_mode(char c);
  static uint8_t              climate_mode_to_wire(climate::ClimateMode mode);
  static climate::ClimateMode wire_to_climate_mode(uint8_t wire_val);

  static const size_t RX_BUF_SIZE = 256;
  char    rx_buf_[RX_BUF_SIZE];
  size_t  rx_len_{0};
  RxState rx_state_{RxState::WAITING_FOR_SOF};

  uint32_t op_data_interval_minutes_{0};
  uint8_t fan_speed_count_{4};
  bool use_standard_fan_modes_{false};
  bool auto_mode_{true};
  uint32_t last_op_data_ms_{0};
  bool op_data_pending_{false};
  bool op_data_requested_{false};  // set in update(); cleared when status response chains op_data
  bool rx_overflowed_{false};

  // Fields awaiting confirmation after an optimistic HA update. The RC-EX3
  // can report its previous state briefly after accepting a command.
  enum PendingField : uint8_t {
    PENDING_NONE        = 0,
    PENDING_MODE        = 1 << 0,
    PENDING_TEMPERATURE = 1 << 1,
    PENDING_FAN         = 1 << 2,
  };

  static const uint32_t COMMAND_SETTLE_MS = 750;  // measured stale reply arrives in ~30-60 ms
  uint8_t pending_fields_{PENDING_NONE};
  uint32_t last_command_ms_{0};
  climate::ClimateMode pending_mode_{climate::CLIMATE_MODE_OFF};
  float pending_temperature_{NAN};
  uint8_t pending_fan_wire_{0xFF};

  sensor::Sensor *indoor_temperature_sensor_    {nullptr};
  sensor::Sensor *outdoor_temperature_sensor_   {nullptr};
  sensor::Sensor *return_air_temperature_sensor_{nullptr};
  sensor::Sensor *compressor_frequency_sensor_  {nullptr};
  sensor::Sensor *indoor_fan_speed_sensor_      {nullptr};
};

}  // namespace rc_ex3
}  // namespace esphome
