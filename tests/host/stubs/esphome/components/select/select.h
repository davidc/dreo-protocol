#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace esphome::select {

class SelectTraits {
 public:
  const std::vector<std::string> &get_options() const { return options_; }
  std::vector<std::string> options_;
};

class Select {
 public:
  virtual ~Select() = default;
  void publish_state(size_t index) {
    last_state = index;
    publish_count++;
  }
  const char *option_at(size_t index) const { return traits.options_.at(index).c_str(); }

  SelectTraits traits;
  size_t last_state = std::numeric_limits<size_t>::max();
  size_t publish_count = 0;

 protected:
  virtual void control(size_t) {}
};

}  // namespace esphome::select
