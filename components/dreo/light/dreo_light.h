#pragma once

#include "esphome/core/component.h"
#include "esphome/components/dreo/dreo.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/light/light_state.h"

namespace esphome::dreo {

// A datapoint-driven light: on/off, brightness and (optionally) colour
// temperature are each backed by a single MCU datapoint. Modelled on the Dreo
// 712S main CCT light (DP4 on/off, DP8 brightness, DP9 colour temperature).
//
// This only covers lights the MCU actually controls over datapoints. A strip
// the ESP drives directly (e.g. the 712S ambient RGB on a GPIO) is a normal
// ESPHome light platform and does not belong here.
class DreoLight final : public Component, public light::LightOutput {
 public:
  DreoLight(Dreo *parent) : parent_(parent) {}
  void setup() override;
  void dump_config() override;

  void set_switch_id(uint8_t switch_id) { this->switch_id_ = switch_id; }
  void set_brightness_id(uint8_t brightness_id) { this->brightness_id_ = brightness_id; }
  void set_color_temperature_id(uint8_t color_temperature_id) { this->color_temperature_id_ = color_temperature_id; }
  void set_max_value(uint32_t max_value) { this->max_value_ = max_value; }
  void set_cold_white_temperature(float cold_white_temperature) {
    this->cold_white_temperature_ = cold_white_temperature;
  }
  void set_warm_white_temperature(float warm_white_temperature) {
    this->warm_white_temperature_ = warm_white_temperature;
  }

  light::LightTraits get_traits() override;
  void setup_state(light::LightState *state) override { this->state_ = state; }
  void write_state(light::LightState *state) override;

 protected:
  void publish_mcu_call_(light::LightCall &call);

  Dreo *parent_;
  light::LightState *state_{nullptr};
  bool updating_from_mcu_{false};
  optional<uint8_t> switch_id_{};
  optional<uint8_t> brightness_id_{};
  optional<uint8_t> color_temperature_id_{};
  // Stock 712S brightness/colour-temperature datapoints run 0..100.
  uint32_t max_value_{100};
  // Mireds at the two ends of the colour-temperature range.
  float cold_white_temperature_{153.0f};
  float warm_white_temperature_{370.0f};
};

}  // namespace esphome::dreo
