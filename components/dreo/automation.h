#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "dreo.h"

#include <vector>

namespace esphome::dreo {

class DreoDatapointUpdateTrigger final : public Trigger<DreoDatapoint> {
 public:
  explicit DreoDatapointUpdateTrigger(Dreo *parent, uint8_t sensor_id) {
    parent->register_listener(sensor_id, [this](const DreoDatapoint &dp) { this->trigger(dp); });
  }
};

class DreoBoolDatapointUpdateTrigger final : public Trigger<bool> {
 public:
  explicit DreoBoolDatapointUpdateTrigger(Dreo *parent, uint8_t sensor_id);
};

class DreoIntDatapointUpdateTrigger final : public Trigger<int> {
 public:
  explicit DreoIntDatapointUpdateTrigger(Dreo *parent, uint8_t sensor_id);
};

class DreoUIntDatapointUpdateTrigger final : public Trigger<uint32_t> {
 public:
  explicit DreoUIntDatapointUpdateTrigger(Dreo *parent, uint8_t sensor_id);
};

class DreoEnumDatapointUpdateTrigger final : public Trigger<uint8_t> {
 public:
  explicit DreoEnumDatapointUpdateTrigger(Dreo *parent, uint8_t sensor_id);
};

class DreoButtonEventTrigger final : public Trigger<uint8_t> {
 public:
  explicit DreoButtonEventTrigger(Dreo *parent) {
    parent->register_button_listener([this](const DreoButtonEvent &event) { this->trigger(event.code); });
  }
};

}  // namespace esphome::dreo
