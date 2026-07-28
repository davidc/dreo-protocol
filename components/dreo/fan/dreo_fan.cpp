#include "esphome/core/log.h"
#include "dreo_fan.h"

#include <cinttypes>

namespace esphome::dreo {

static const char *const TAG = "dreo.fan";

// Unlike the Tuya fan component, we don't subtract 1 from the speed value, because that would
// make the lowest speed setting 0, which is illegal, and make us unable to access our highest setting

void DreoFan::setup() {
  auto speed_id = this->speed_id_;
  if (speed_id.has_value()) {
    this->parent_->register_listener(*speed_id, [this](const DreoDatapoint &datapoint) {
      if (datapoint.type == DreoDatapointType::ENUM) {
        ESP_LOGV(TAG, "MCU reported speed of: %d", datapoint.value_enum);
        const int fan_speed = this->raw_speed_to_fan_speed_(datapoint.value_enum);
        if (fan_speed > this->speed_count_) {
          ESP_LOGE(TAG, "Speed has invalid value %d", datapoint.value_enum);
        } else {
          this->speed = fan_speed;
          this->publish_state();
        }
      } else if (datapoint.type == DreoDatapointType::INTEGER) {
        ESP_LOGV(TAG, "MCU reported speed of: %d", datapoint.value_int);
        this->speed = this->raw_speed_to_fan_speed_(datapoint.value_uint);
        this->publish_state();
      }
      this->speed_type_ = datapoint.type;
    });
  }
  auto switch_id = this->switch_id_;
  if (switch_id.has_value()) {
    this->parent_->register_listener(*switch_id, [this](const DreoDatapoint &datapoint) {
      ESP_LOGV(TAG, "MCU reported switch is: %s", ONOFF(datapoint.value_bool));
      this->state = datapoint.value_bool;
      this->publish_state();
    });
  }
  auto oscillation_id = this->oscillation_id_;
  if (oscillation_id.has_value()) {
    this->parent_->register_listener(*oscillation_id, [this](const DreoDatapoint &datapoint) {
      // Whether data type is BOOL or ENUM, it will still be a 1 or a 0, so the functions below are valid in both
      // scenarios
      ESP_LOGV(TAG, "MCU reported oscillation is: %s", ONOFF(datapoint.value_bool));
      this->oscillating = datapoint.value_bool;
      this->publish_state();

      this->oscillation_type_ = datapoint.type;
    });
  }
  auto direction_id = this->direction_id_;
  if (direction_id.has_value()) {
    this->parent_->register_listener(*direction_id, [this](const DreoDatapoint &datapoint) {
      this->direction_type_ = datapoint.type;
      uint32_t value = 0;
      if (datapoint.type == DreoDatapointType::INTEGER) {
        value = datapoint.value_uint;
      } else if (datapoint.type == DreoDatapointType::ENUM) {
        value = datapoint.value_enum;
      } else {
        value = datapoint.value_bool ? 1 : 0;
      }
      ESP_LOGD(TAG, "MCU reported direction value is: %" PRIu32, value);
      if (this->direction_reverse_value_.has_value()) {
        this->direction =
            value == *this->direction_reverse_value_ ? fan::FanDirection::REVERSE : fan::FanDirection::FORWARD;
      } else {
        this->direction = value ? fan::FanDirection::REVERSE : fan::FanDirection::FORWARD;
      }
      this->publish_state();
    });
  }

  this->parent_->add_on_initialized_callback([this]() {
    auto restored = this->restore_state_();
    if (restored)
      restored->to_call(*this).perform();
  });
}

void DreoFan::dump_config() {
  LOG_FAN("", "Dreo Fan", this);
  auto speed_dp_id = this->speed_id_;
  if (speed_dp_id.has_value()) {
    ESP_LOGCONFIG(TAG, "  Speed has datapoint ID %u", *speed_dp_id);
    if (!this->speed_mappings_.empty()) {
      ESP_LOGCONFIG(TAG, "  Speed mappings:");
      for (size_t i = 0; i < this->speed_mappings_.size(); i++)
        ESP_LOGCONFIG(TAG, "    %zu -> %u", i + 1, this->speed_mappings_[i]);
    }
  }
  auto switch_dp_id = this->switch_id_;
  if (switch_dp_id.has_value()) {
    ESP_LOGCONFIG(TAG, "  Switch has datapoint ID %u", *switch_dp_id);
  }
  auto power_dp_id = this->power_id_;
  if (power_dp_id.has_value()) {
    ESP_LOGCONFIG(TAG, "  Power has datapoint ID %u", *power_dp_id);
  }
  auto oscillation_dp_id = this->oscillation_id_;
  if (oscillation_dp_id.has_value()) {
    ESP_LOGCONFIG(TAG, "  Oscillation has datapoint ID %u", *oscillation_dp_id);
  }
  auto direction_dp_id = this->direction_id_;
  if (direction_dp_id.has_value()) {
    ESP_LOGCONFIG(TAG, "  Direction has datapoint ID %u", *direction_dp_id);
    if (this->direction_forward_value_.has_value() && this->direction_reverse_value_.has_value())
      ESP_LOGCONFIG(TAG, "  Direction values: forward=%" PRIu32 ", reverse=%" PRIu32, *this->direction_forward_value_,
                    *this->direction_reverse_value_);
  }
}

fan::FanTraits DreoFan::get_traits() {
  return fan::FanTraits(this->oscillation_id_.has_value(), this->speed_id_.has_value(), this->direction_id_.has_value(),
                        this->speed_count_);
}

void DreoFan::control(const fan::FanCall &call) {
  auto switch_id = this->switch_id_;
  if (switch_id.has_value()) {
    auto state = call.get_state();
    if (state.has_value()) {
      if (*state && this->power_id_.has_value())
        this->parent_->set_boolean_datapoint_value(*this->power_id_, true);
      this->parent_->set_boolean_datapoint_value(*switch_id, *state);
    }
  }
  auto osc_id = this->oscillation_id_;
  if (osc_id.has_value()) {
    auto oscillating = call.get_oscillating();
    if (oscillating.has_value()) {
      if (this->oscillation_type_ == DreoDatapointType::ENUM) {
        this->parent_->set_enum_datapoint_value(*osc_id, *oscillating);
      } else if (this->oscillation_type_ == DreoDatapointType::BOOLEAN) {
        this->parent_->set_boolean_datapoint_value(*osc_id, *oscillating);
      }
    }
  }
  auto dir_id = this->direction_id_;
  if (dir_id.has_value()) {
    auto direction = call.get_direction();
    if (direction.has_value()) {
      uint32_t value = *direction == fan::FanDirection::REVERSE ? 1 : 0;
      if (*direction == fan::FanDirection::REVERSE && this->direction_reverse_value_.has_value())
        value = *this->direction_reverse_value_;
      else if (*direction == fan::FanDirection::FORWARD && this->direction_forward_value_.has_value())
        value = *this->direction_forward_value_;

      if (this->direction_type_ == DreoDatapointType::INTEGER) {
        this->parent_->set_integer_datapoint_value(*dir_id, value);
      } else if (this->direction_type_ == DreoDatapointType::ENUM) {
        this->parent_->set_enum_datapoint_value(*dir_id, value);
      } else {
        this->parent_->set_boolean_datapoint_value(*dir_id, value != 0);
      }
    }
  }
  auto spd_id = this->speed_id_;
  if (spd_id.has_value()) {
    auto speed = call.get_speed();
    if (speed.has_value()) {
      const uint32_t raw_speed = this->fan_speed_to_raw_speed_(*speed);
      if (this->speed_type_ == DreoDatapointType::ENUM) {
        this->parent_->set_enum_datapoint_value(*spd_id, raw_speed);
      } else if (this->speed_type_ == DreoDatapointType::INTEGER) {
        this->parent_->set_integer_datapoint_value(*spd_id, raw_speed);
      }
    }
  }
}

int DreoFan::raw_speed_to_fan_speed_(uint32_t raw_speed) const {
  if (this->speed_mappings_.empty())
    return raw_speed;

  for (size_t i = 0; i < this->speed_mappings_.size(); i++) {
    if (raw_speed <= this->speed_mappings_[i])
      return i + 1;
  }
  return this->speed_mappings_.size();
}

uint32_t DreoFan::fan_speed_to_raw_speed_(int fan_speed) const {
  if (this->speed_mappings_.empty())
    return fan_speed;

  if (fan_speed < 1)
    fan_speed = 1;
  if (fan_speed > static_cast<int>(this->speed_mappings_.size()))
    fan_speed = this->speed_mappings_.size();
  return this->speed_mappings_[fan_speed - 1];
}

}  // namespace esphome::dreo
