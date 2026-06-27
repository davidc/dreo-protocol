#pragma once

#include <cinttypes>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"
#include "esphome/components/uart/uart.h"


namespace esphome::dreo {

enum class DreoDatapointType : uint8_t {
  // RAW = 0x00,      // variable length
  BOOLEAN = 0x01,  // 1 byte (0/1)
  INTEGER = 0x02,  // 4 byte
  // STRING = 0x03,   // variable length
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
    uint32_t value_bitmask;
  };
  // std::string value_string;
  // std::vector<uint8_t> value_raw;
};

struct DreoDatapointListener {
  uint8_t datapoint_id;
  std::function<void(DreoDatapoint)> on_datapoint;
};

enum class DreoCommandType : uint8_t {
  HEARTBEAT = 0x00,
  PRODUCT_QUERY = 0x01,
  WIFI_STATE = 0x03,
  WIFI_RESET = 0x04,
  DATAPOINT_DELIVER = 0x06,
  DATAPOINT_REPORT = 0x07,
  DATAPOINT_QUERY = 0x08,
  DATAPOINT_CHANGE_NOTIFICATION = 0x0E, // Can't decipher this so we'll ignore it
};


// INIT_HEARTBEAT->INIT_PRODUCT->INIT_DATAPOINT->INIT_DONE

enum class DreoInitState : uint8_t {
  INIT_HEARTBEAT = 0x00,
  INIT_PRODUCT,
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
  void set_boolean_datapoint_value(uint8_t datapoint_id, bool value);
  void set_integer_datapoint_value(uint8_t datapoint_id, uint32_t value);
  void set_enum_datapoint_value(uint8_t datapoint_id, uint8_t value);
  void force_set_boolean_datapoint_value(uint8_t datapoint_id, bool value);
  void force_set_integer_datapoint_value(uint8_t datapoint_id, uint32_t value);
  void force_set_enum_datapoint_value(uint8_t datapoint_id, uint8_t value);
  DreoInitState get_init_state();
  void add_ignore_mcu_update_on_datapoints(uint8_t ignore_mcu_update_on_datapoints) {
    this->ignore_mcu_update_on_datapoints_.push_back(ignore_mcu_update_on_datapoints);
  }
  template<typename F> void add_on_initialized_callback(F &&callback) {
    this->initialized_callback_.add(std::forward<F>(callback));
  }

 protected:
  void handle_char_(uint8_t c);
  void handle_datapoints_(const uint8_t *buffer, size_t len);
  optional<DreoDatapoint> get_datapoint_(uint8_t datapoint_id);
  bool validate_message_();

  void handle_command_(uint8_t command, uint8_t version, uint8_t sequence, const uint8_t *buffer, size_t len);
  void send_raw_command_(DreoCommand command);
  void process_command_queue_();
  void send_command_(const DreoCommand &command);
  void send_empty_command_(DreoCommandType command);
  void set_numeric_datapoint_value_(uint8_t datapoint_id, DreoDatapointType datapoint_type, uint32_t value,
                                    uint8_t length, bool forced);
  void send_datapoint_command_(uint8_t datapoint_id, DreoDatapointType datapoint_type, std::vector<uint8_t> data);

  DreoInitState init_state_ = DreoInitState::INIT_HEARTBEAT;
  bool init_failed_{false};
  int init_retries_{0};
  uint8_t protocol_version_ = -1;
  uint32_t last_command_timestamp_ = 0;
  uint32_t last_rx_char_timestamp_ = 0;
  std::string product_;
  std::vector<DreoDatapointListener> listeners_;
  std::vector<DreoDatapoint> datapoints_;
  std::vector<uint8_t> rx_message_;
  std::vector<uint8_t> ignore_mcu_update_on_datapoints_{};
  std::vector<DreoCommand> command_queue_;
  optional<DreoCommandType> expected_response_{};
  CallbackManager<void()> initialized_callback_{};
  uint8_t sequence_ = 0;
};

}  // namespace esphome::dreo

