#pragma once

#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace esphome {

template<typename T> using optional = std::optional<T>;

namespace setup_priority {
static constexpr float LATE = 0.0f;
}

class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
  virtual float get_setup_priority() const { return 0.0f; }

  template<typename F> void set_interval(const char *, uint32_t, F &&) {}
  template<typename F> void set_timeout(const char *, uint32_t, F &&) {}
};

template<typename Signature> class CallbackManager;

template<typename... Args> class CallbackManager<void(Args...)> {
 public:
  template<typename F> void add(F &&callback) { callbacks_.emplace_back(std::forward<F>(callback)); }
  void call(Args... args) {
    for (auto &callback : callbacks_)
      callback(args...);
  }

 protected:
  std::vector<std::function<void(Args...)>> callbacks_;
};

}  // namespace esphome
