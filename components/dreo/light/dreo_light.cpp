#include <cmath>

#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "dreo_light.h"

namespace esphome::dreo {

static const char *const TAG = "dreo.light";

void DreoLight::setup() {
  auto switch_id = this->switch_id_;
  if (switch_id.has_value()) {
    this->parent_->register_listener(*switch_id, [this](const DreoDatapoint &datapoint) {
      if (this->state_ == nullptr)
        return;
      ESP_LOGV(TAG, "MCU reported light is: %s", ONOFF(datapoint.value_bool));
      auto call = this->state_->make_call();
      call.set_state(datapoint.value_bool);
      this->publish_mcu_call_(call);
    });
  }
  auto brightness_id = this->brightness_id_;
  if (brightness_id.has_value()) {
    this->parent_->register_listener(*brightness_id, [this](const DreoDatapoint &datapoint) {
      if (this->state_ == nullptr)
        return;
      ESP_LOGV(TAG, "MCU reported brightness of: %d", datapoint.value_int);
      auto call = this->state_->make_call();
      call.set_brightness(clamp(static_cast<float>(datapoint.value_int) / this->max_value_, 0.0f, 1.0f));
      this->publish_mcu_call_(call);
    });
  }
  auto color_temperature_id = this->color_temperature_id_;
  if (color_temperature_id.has_value()) {
    this->parent_->register_listener(*color_temperature_id, [this](const DreoDatapoint &datapoint) {
      if (this->state_ == nullptr)
        return;
      ESP_LOGV(TAG, "MCU reported colour temperature of: %d", datapoint.value_int);
      // Stock datapoint is 0..max_value where higher is cooler (higher Kelvin /
      // lower mireds). Map it back onto the configured mired range.
      float fraction = clamp(static_cast<float>(datapoint.value_int) / this->max_value_, 0.0f, 1.0f);
      float mireds = this->warm_white_temperature_ - fraction * (this->warm_white_temperature_ - this->cold_white_temperature_);
      auto call = this->state_->make_call();
      call.set_color_temperature(mireds);
      this->publish_mcu_call_(call);
    });
  }
}

void DreoLight::dump_config() {
  ESP_LOGCONFIG(TAG, "Dreo Light");
  if (this->switch_id_.has_value())
    ESP_LOGCONFIG(TAG, "  Switch has datapoint ID %u", *this->switch_id_);
  if (this->brightness_id_.has_value())
    ESP_LOGCONFIG(TAG, "  Brightness has datapoint ID %u", *this->brightness_id_);
  if (this->color_temperature_id_.has_value())
    ESP_LOGCONFIG(TAG, "  Colour temperature has datapoint ID %u", *this->color_temperature_id_);
}

light::LightTraits DreoLight::get_traits() {
  auto traits = light::LightTraits();
  if (this->color_temperature_id_.has_value()) {
    traits.set_supported_color_modes({light::ColorMode::COLOR_TEMPERATURE});
    traits.set_min_mireds(this->cold_white_temperature_);
    traits.set_max_mireds(this->warm_white_temperature_);
  } else if (this->brightness_id_.has_value()) {
    traits.set_supported_color_modes({light::ColorMode::BRIGHTNESS});
  } else {
    traits.set_supported_color_modes({light::ColorMode::ON_OFF});
  }
  return traits;
}

void DreoLight::write_state(light::LightState *state) {
  if (this->updating_from_mcu_)
    return;

  bool is_on = state->current_values.is_on();

  if (this->switch_id_.has_value()) {
    this->parent_->set_boolean_datapoint_value(*this->switch_id_, is_on);
  }

  // Leave brightness/colour temperature alone while the light is off so we don't
  // fight the stock latch; they are re-applied on the next turn-on.
  if (!is_on)
    return;

  if (this->brightness_id_.has_value()) {
    float brightness = state->current_values.get_brightness();
    auto value = static_cast<uint32_t>(roundf(brightness * this->max_value_));
    if (value == 0)
      value = 1;  // 0 would read as off on the stock scale
    this->parent_->set_integer_datapoint_value(*this->brightness_id_, value);
  }

  if (this->color_temperature_id_.has_value()) {
    float mireds = state->current_values.get_color_temperature();
    float fraction = (this->warm_white_temperature_ - mireds) /
                     (this->warm_white_temperature_ - this->cold_white_temperature_);
    fraction = clamp(fraction, 0.0f, 1.0f);
    auto value = static_cast<uint32_t>(roundf(fraction * this->max_value_));
    this->parent_->set_integer_datapoint_value(*this->color_temperature_id_, value);
  }
}

void DreoLight::publish_mcu_call_(light::LightCall &call) {
  this->updating_from_mcu_ = true;
  call.perform();
  this->updating_from_mcu_ = false;
}

}  // namespace esphome::dreo
