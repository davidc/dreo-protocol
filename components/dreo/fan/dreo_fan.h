#pragma once

#include "esphome/core/component.h"
#include "esphome/components/dreo/dreo.h"
#include "esphome/components/fan/fan.h"

#include <utility>

namespace esphome::dreo {

class DreoFan final : public Component, public fan::Fan {
 public:
  DreoFan(Dreo *parent, int speed_count) : parent_(parent), speed_count_(speed_count) {}
  void setup() override;
  void dump_config() override;
  void set_speed_id(uint8_t speed_id) { this->speed_id_ = speed_id; }
  void set_switch_id(uint8_t switch_id) { this->switch_id_ = switch_id; }
  void set_power_id(uint8_t power_id) { this->power_id_ = power_id; }
  void set_oscillation_id(uint8_t oscillation_id) { this->oscillation_id_ = oscillation_id; }
  void set_direction_id(uint8_t direction_id) { this->direction_id_ = direction_id; }
  void set_speed_mappings(std::vector<uint8_t> speed_mappings) { this->speed_mappings_ = std::move(speed_mappings); }
  void set_speed_type(DreoDatapointType speed_type) { this->speed_type_ = speed_type; }
  void set_direction_type(DreoDatapointType direction_type) { this->direction_type_ = direction_type; }
  void set_direction_values(uint32_t forward_value, uint32_t reverse_value) {
    this->direction_forward_value_ = forward_value;
    this->direction_reverse_value_ = reverse_value;
  }

  fan::FanTraits get_traits() override;

 protected:
  void control(const fan::FanCall &call) override;
  int raw_speed_to_fan_speed_(uint32_t raw_speed) const;
  uint32_t fan_speed_to_raw_speed_(int fan_speed) const;

  Dreo *parent_;
  optional<uint8_t> speed_id_{};
  optional<uint8_t> switch_id_{};
  optional<uint8_t> power_id_{};
  optional<uint8_t> oscillation_id_{};
  optional<uint8_t> direction_id_{};
  int speed_count_{};
  std::vector<uint8_t> speed_mappings_{};
  DreoDatapointType speed_type_{};
  DreoDatapointType oscillation_type_{};
  DreoDatapointType direction_type_{};
  optional<uint32_t> direction_forward_value_{};
  optional<uint32_t> direction_reverse_value_{};
};

}  // namespace esphome::dreo
