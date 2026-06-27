#pragma once

#include "esphome/core/component.h"
#include "esphome/components/dreo/dreo.h"
#include "esphome/components/sensor/sensor.h"

namespace esphome::dreo {

class DreoSensor final : public sensor::Sensor, public Component {
 public:
  void setup() override;
  void dump_config() override;
  void set_sensor_id(uint8_t sensor_id) { this->sensor_id_ = sensor_id; }

  void set_dreo_parent(Dreo *parent) { this->parent_ = parent; }

 protected:
  Dreo *parent_;
  uint8_t sensor_id_{0};
};

}  // namespace esphome::dreo

