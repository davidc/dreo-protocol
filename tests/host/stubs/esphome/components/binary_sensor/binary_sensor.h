#pragma once

#include <cstddef>

namespace esphome::binary_sensor {

class BinarySensor {
 public:
  void publish_state(bool value) {
    state = value;
    publish_count++;
  }

  bool state = false;
  size_t publish_count = 0;
};

}  // namespace esphome::binary_sensor
