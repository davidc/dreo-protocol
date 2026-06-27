#pragma once

#include "esphome/core/component.h"
#include "esphome/components/dreo/dreo.h"
#include "esphome/components/switch/switch.h"

namespace esphome::dreo {

class DreoSwitch final : public switch_::Switch, public Component {
 public:
  void setup() override;
  void dump_config() override;
  void set_switch_id(uint8_t switch_id) { this->switch_id_ = switch_id; }

  void set_dreo_parent(Dreo *parent) { this->parent_ = parent; }

 protected:
  void write_state(bool state) override;

  Dreo *parent_;
  uint8_t switch_id_{0};
};

}  // namespace esphome::dreo

