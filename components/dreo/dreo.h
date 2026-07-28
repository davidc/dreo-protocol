#pragma once

#include <cinttypes>
#include <functional>
#include <string>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "esphome/components/uart/uart.h"


namespace esphome::dreo {

enum class DreoDatapointType : uint8_t {
  // RAW = 0x00,      // variable length
  BOOLEAN = 0x01,  // 1 byte (0/1)
  INTEGER = 0x02,  // 1/2/4 byte numeric value, model-dependent
  STRING = 0x03,   // variable length
  ENUM = 0x04,     // 1 byte
  // BITMASK = 0x05,  // 1/2/4 bytes
};

struct DreoDatapoint {
  uint8_t id;
  DreoDatapointType type;
  size_t len;
  union {
    bool value_bool;
    int value_int;
    uint32_t value_uint;
    uint8_t value_enum;
  };
  std::string value_string;
  std::vector<uint8_t> value_raw;
};

struct DreoDatapointListener {
  uint8_t datapoint_id;
  std::function<void(DreoDatapoint)> on_datapoint;
};

struct DreoButtonEvent {
  uint8_t code;
  std::string name;
};

struct DreoButtonEventListener {
  std::function<void(DreoButtonEvent)> on_button_event;
};

enum class DreoCommandType : uint8_t {
  HEARTBEAT = 0x00,
  PRODUCT_QUERY = 0x01,
  WIFI_STATE = 0x03,
  WIFI_RESET = 0x04,
  DATAPOINT_DELIVER = 0x06,
  DATAPOINT_REPORT = 0x07,
  DATAPOINT_QUERY = 0x08,
  VERSION_QUERY = 0x09,
  BUTTON_EVENT = 0x0E,
};


// INIT_HEARTBEAT->INIT_PRODUCT->INIT_DATAPOINT->INIT_DONE

enum class DreoInitState : uint8_t {
  INIT_HEARTBEAT = 0x00,
  INIT_PRODUCT,
  INIT_VERSION,
  INIT_DATAPOINT,
  INIT_DONE,
};

struct DreoCommand {
  DreoCommandType cmd;
  std::vector<uint8_t> payload;
};

class Dreo final : public Component, public uart::UARTDevice {
 public:
  float get_setup_priority() const override { return setup_priority::LATE; }
  void setup() override;
  void loop() override;
  void dump_config() override;
  void register_listener(uint8_t datapoint_id, const std::function<void(DreoDatapoint)> &func);
  void register_button_listener(const std::function<void(DreoButtonEvent)> &func);
  void set_boolean_datapoint_value(uint8_t datapoint_id, bool value);
  void set_integer_datapoint_value(uint8_t datapoint_id, uint32_t value);
  void set_enum_datapoint_value(uint8_t datapoint_id, uint8_t value);
  void set_string_datapoint_value(uint8_t datapoint_id, const std::string &value);
  void force_set_boolean_datapoint_value(uint8_t datapoint_id, bool value);
  void force_set_integer_datapoint_value(uint8_t datapoint_id, uint32_t value);
  void force_set_enum_datapoint_value(uint8_t datapoint_id, uint8_t value);
  void publish_boolean_datapoint_value(uint8_t datapoint_id, bool value);
  void publish_integer_datapoint_value(uint8_t datapoint_id, uint32_t value);
  void publish_enum_datapoint_value(uint8_t datapoint_id, uint8_t value);
  void publish_string_datapoint_value(uint8_t datapoint_id, const std::string &value);
  optional<bool> bool_state(int datapoint_id) const;
  optional<float> number_state(int datapoint_id) const;
  std::string string_state(int datapoint_id) const;
  std::string last_rx_frame_hex() const { return this->last_rx_frame_hex_; }
  std::string last_tx_frame_hex() const { return this->last_tx_frame_hex_; }
  std::string last_status() const { return this->last_status_; }
  std::string last_button_event() const { return this->last_button_event_; }
  std::string product() const { return this->product_; }
  std::string mcu_version() const { return this->mcu_version_; }
  std::string seen_dps_summary() const;
  bool initialized() const { return this->init_state_ == DreoInitState::INIT_DONE; }
  void heartbeat();
  void request_state();
  void query_product_info();
  void query_mcu_version();
  void report_wifi_state(uint8_t state = 0x01);
  DreoInitState get_init_state();
  void set_startup_heartbeat_interval(uint32_t startup_heartbeat_interval) {
    this->startup_heartbeat_interval_ = startup_heartbeat_interval;
  }
  void set_max_init_retries(uint8_t max_init_retries) { this->max_init_retries_ = max_init_retries; }
  void set_query_version_on_sync(bool query_version_on_sync) { this->query_version_on_sync_ = query_version_on_sync; }
  void set_wifi_status_on_sync(bool wifi_status_on_sync) { this->wifi_status_on_sync_ = wifi_status_on_sync; }
  void set_periodic_wifi_status(bool periodic_wifi_status) { this->periodic_wifi_status_ = periodic_wifi_status; }
  void set_button_event_repeat_guard(uint32_t button_event_repeat_guard) {
    this->button_event_repeat_guard_ = button_event_repeat_guard;
  }
  void add_ignore_mcu_update_on_datapoints(uint8_t ignore_mcu_update_on_datapoints) {
    this->ignore_mcu_update_on_datapoints_.push_back(ignore_mcu_update_on_datapoints);
  }
  void add_local_datapoint(uint8_t local_datapoint) { this->local_datapoints_.push_back(local_datapoint); }
  template<typename F> void add_on_initialized_callback(F &&callback) {
    this->initialized_callback_.add(std::forward<F>(callback));
  }

 protected:
  void handle_char_(uint8_t c);
  void handle_datapoints_(const uint8_t *buffer, size_t len);
  void handle_button_event_(const uint8_t *buffer, size_t len);
  optional<DreoDatapoint> get_datapoint_(uint8_t datapoint_id);
  optional<DreoDatapoint> get_datapoint_(uint8_t datapoint_id) const;
  bool validate_message_();

  void handle_command_(uint8_t command, uint8_t version, uint8_t sequence, const uint8_t *buffer, size_t len);
  void send_raw_command_(DreoCommand command);
  void process_command_queue_();
  void send_command_(const DreoCommand &command);
  void send_empty_command_(DreoCommandType command);
  void set_numeric_datapoint_value_(uint8_t datapoint_id, DreoDatapointType datapoint_type, uint32_t value,
                                    uint8_t length, bool forced);
  void set_string_datapoint_value_(uint8_t datapoint_id, const std::string &value, bool forced);
  void send_datapoint_command_(uint8_t datapoint_id, DreoDatapointType datapoint_type, std::vector<uint8_t> data);
  void publish_datapoint_(const DreoDatapoint &datapoint);
  void update_datapoint_(const DreoDatapoint &datapoint);
  void notify_datapoint_listeners_(const DreoDatapoint &datapoint);
  static std::string hex_(const uint8_t *data, size_t len);
  static std::string button_name_(uint8_t code);
  static uint8_t dp_index_(int datapoint_id);

  DreoInitState init_state_ = DreoInitState::INIT_HEARTBEAT;
  bool init_failed_{false};
  int init_retries_{0};
  uint8_t max_init_retries_{5};
  uint8_t protocol_version_ = -1;
  uint32_t last_command_timestamp_ = 0;
  uint32_t last_rx_char_timestamp_ = 0;
  uint32_t last_startup_heartbeat_timestamp_ = 0;
  std::string product_;
  std::string mcu_version_;
  std::vector<DreoDatapointListener> listeners_;
  std::vector<DreoButtonEventListener> button_listeners_;
  std::vector<DreoDatapoint> datapoints_;
  std::vector<uint8_t> rx_message_;
  std::vector<uint8_t> ignore_mcu_update_on_datapoints_{};
  std::vector<uint8_t> local_datapoints_{};
  std::vector<DreoCommand> command_queue_;
  optional<DreoCommandType> expected_response_{};
  CallbackManager<void()> initialized_callback_{};
  std::string last_rx_frame_hex_{};
  std::string last_tx_frame_hex_{};
  std::string last_status_{"not started"};
  std::string last_button_event_{};
  std::string last_button_event_frame_hex_{};
  uint32_t last_button_event_frame_timestamp_{0};
  uint32_t startup_heartbeat_interval_{15000};
  bool query_version_on_sync_{false};
  bool wifi_status_on_sync_{false};
  bool periodic_wifi_status_{false};
  uint32_t button_event_repeat_guard_{0};
  uint8_t sequence_ = 0;
};

}  // namespace esphome::dreo
