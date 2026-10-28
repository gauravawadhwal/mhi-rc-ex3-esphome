#pragma once
#include "esphome/core/component.h"
namespace esphome::climate {
enum ClimateMode { CLIMATE_MODE_OFF,CLIMATE_MODE_HEAT_COOL,CLIMATE_MODE_COOL,CLIMATE_MODE_HEAT,CLIMATE_MODE_FAN_ONLY,CLIMATE_MODE_DRY };
enum ClimateFanMode {CLIMATE_FAN_ON,CLIMATE_FAN_AUTO,CLIMATE_FAN_LOW,CLIMATE_FAN_MEDIUM,CLIMATE_FAN_HIGH};
constexpr int CLIMATE_SUPPORTS_CURRENT_TEMPERATURE=1;
struct ClimateTraits {
 void add_feature_flags(int) {} void set_supported_modes(std::set<ClimateMode>) {} void add_supported_mode(ClimateMode) {}
 void set_supported_fan_modes(std::set<ClimateFanMode>) {} void add_supported_fan_mode(ClimateFanMode) {}
 void set_visual_min_temperature(float) {} void set_visual_max_temperature(float) {} void set_visual_temperature_step(float) {}
};
struct ClimateCall {
 std::optional<ClimateMode> requested_mode;
 std::optional<float> requested_temp;
 std::optional<ClimateFanMode> requested_fan;
 std::string custom_fan;
 auto get_mode() const { return requested_mode; }
 auto get_target_temperature() const { return requested_temp; }
 auto get_fan_mode() const { return requested_fan; }
 auto get_custom_fan_mode() const { return custom_fan; }
};
class Climate { public:
 ClimateMode mode=CLIMATE_MODE_OFF;
 ClimateFanMode fan_mode=CLIMATE_FAN_AUTO;
 float target_temperature=NAN,current_temperature=NAN;
 int publishes=0;
 virtual void control(const ClimateCall&) {} virtual ClimateTraits traits() { return {}; }
 void set_supported_custom_fan_modes(std::set<std::string>) {}
 void set_fan_mode_(ClimateFanMode m) {fan_mode=m;}
 void set_custom_fan_mode_(std::string) {}
 void publish_state() {++publishes;}
}; }
