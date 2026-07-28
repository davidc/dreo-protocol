#include "dreo.h"

#include "esphome/components/network/util.h"
#include "esphome/core/gpio.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/util.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace esphome::dreo {

static const char *const TAG = "dreo";
static const int COMMAND_DELAY = 10;
static const int RECEIVE_TIMEOUT = 300;
static constexpr size_t MAX_DATAPOINT_LOG_BYTES = 16;

void Dreo::setup() {
  this->last_status_ = "handshaking";
  // Unconditional: this heartbeat is what recovers a stalled handshake when the
  // MCU comes up after the ESP. Gating it on INIT_DONE strands the component
  // permanently once init_failed_ latches.
  this->set_interval("heartbeat", 15000, [this] {
    this->heartbeat();
    if (this->periodic_wifi_status_ && this->init_state_ == DreoInitState::INIT_DONE)
      this->report_wifi_state(0x01);
  });
  this->heartbeat();
}

void Dreo::loop() {
  // Read all available bytes in batches to reduce UART call overhead.
  size_t avail = this->available();
  uint8_t buf[64];
  while (avail > 0) {
    size_t to_read = std::min(avail, sizeof(buf));
    if (!this->read_array(buf, to_read)) {
      break;
    }
    avail -= to_read;

    for (size_t i = 0; i < to_read; i++) {
      this->handle_char_(buf[i]);
    }
  }

  if (this->init_state_ != DreoInitState::INIT_DONE && !this->init_failed_ &&
      this->startup_heartbeat_interval_ > 0 && this->command_queue_.empty() && !this->expected_response_.has_value()) {
    const uint32_t now = millis();
    if (now - this->last_startup_heartbeat_timestamp_ >= this->startup_heartbeat_interval_) {
      this->last_startup_heartbeat_timestamp_ = now;
      this->heartbeat();
    }
  }

  process_command_queue_();
}

void Dreo::dump_config() {
  ESP_LOGCONFIG(TAG, "Dreo:");
  if (this->init_state_ != DreoInitState::INIT_DONE) {
    if (this->init_failed_) {
      ESP_LOGCONFIG(TAG, "  Initialization failed. Current init_state: %u", static_cast<uint8_t>(this->init_state_));
    } else {
      ESP_LOGCONFIG(TAG, "  Configuration will be reported when setup is complete. Current init_state: %u",
                    static_cast<uint8_t>(this->init_state_));
    }
    ESP_LOGCONFIG(TAG, "  If no further output is received, confirm that this is a supported Dreo device.");
    return;
  }
  for (auto &info : this->datapoints_) {
    if (info.type == DreoDatapointType::BOOLEAN) {
      ESP_LOGCONFIG(TAG, "  Datapoint %u: boolean (value: %s)", info.id, ONOFF(info.value_bool));
    } else if (info.type == DreoDatapointType::INTEGER) {
      ESP_LOGCONFIG(TAG, "  Datapoint %u: int value (value: %d)", info.id, info.value_int);
    } else if (info.type == DreoDatapointType::STRING) {
      ESP_LOGCONFIG(TAG, "  Datapoint %u: string value (len: %zu)", info.id, info.value_string.size());
    } else if (info.type == DreoDatapointType::ENUM) {
      ESP_LOGCONFIG(TAG, "  Datapoint %u: enum (value: %d)", info.id, info.value_enum);
    } else {
      ESP_LOGCONFIG(TAG, "  Datapoint %u: unknown", info.id);
    }
  }
  ESP_LOGCONFIG(TAG, "  Product: '%s'", this->product_.c_str());
  if (!this->mcu_version_.empty())
    ESP_LOGCONFIG(TAG, "  MCU version: '%s'", this->mcu_version_.c_str());
}

bool Dreo::validate_message_() {
  uint32_t at = this->rx_message_.size() - 1;
  auto *data = &this->rx_message_[0];
  uint8_t new_byte = data[at];

  // Byte 0: HEADER1 (always 0x55)
  if (at == 0)
    return new_byte == 0x55;
  // Byte 1: HEADER2 (always 0xAA)
  if (at == 1)
    return new_byte == 0xAA;

  // Byte 2: VERSION
  uint8_t version = data[2];
  if (at == 2)
    return true;

  // Byte 3: SEQUENCE
  uint8_t sequence = data[3];
  if (at == 3)
    return true;

  // Byte 4: COMMAND
  uint8_t command = data[4];
  if (at == 4)
    return true;

  // Byte 5: Unknown (always 0)
  if (at == 5)
    return true;

  // Byte 6: LENGTH1
  // Byte 7: LENGTH2
  if (at <= 7)
    return true;

  uint16_t length = (uint16_t(data[6]) << 8) | (uint16_t(data[7]));

  // wait until all data is read
  if (at - 8 < length)
    return true;

  // Byte 8+LEN: CHECKSUM - sum of all bytes (including header) modulo 256
  uint8_t rx_checksum = new_byte;
  uint8_t calc_checksum = 0;
  for (uint32_t i = 0; i < 8 + length; i++)
    calc_checksum += data[i];

  if (rx_checksum != calc_checksum) {
    ESP_LOGW(TAG, "Dreo Received invalid message checksum %02X!=%02X", rx_checksum, calc_checksum);
    return false;
  }

  this->last_rx_frame_hex_ = hex_(this->rx_message_.data(), this->rx_message_.size());

  // valid message
  const uint8_t *message_data = data + 8;
#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE
  const size_t log_len = std::min<size_t>(length, MAX_DATAPOINT_LOG_BYTES);
  char hex_buf[format_hex_pretty_size(MAX_DATAPOINT_LOG_BYTES)];
  ESP_LOGV(TAG, "Received Dreo: CMD=0x%02X VERSION=%u SEQUENCE=%u DATA=[%s%s] INIT_STATE=%u", command, version,
           sequence, format_hex_pretty_to(hex_buf, message_data, log_len), length > log_len ? " ..." : "",
           static_cast<uint8_t>(this->init_state_));
#endif
  this->handle_command_(command, version, sequence, message_data, length);

  // return false to reset rx buffer
  return false;
}

void Dreo::handle_char_(uint8_t c) {
  this->rx_message_.push_back(c);
  if (!this->validate_message_()) {
    const bool restart = this->rx_message_.size() > 1 && c == 0x55;
    this->rx_message_.clear();
    if (restart)
      this->rx_message_.push_back(0x55);
  } else {
    this->last_rx_char_timestamp_ = millis();
  }
}

void Dreo::handle_command_(uint8_t command, uint8_t version, uint8_t sequence, const uint8_t *buffer, size_t len) {
  DreoCommandType command_type = (DreoCommandType) command;
  char status[24];
  std::snprintf(status, sizeof(status), "rx command 0x%02x", command);
  this->last_status_ = status;

  if (this->expected_response_.has_value() && this->expected_response_ == command_type) {
    this->expected_response_.reset();
    if (!this->command_queue_.empty())
      this->command_queue_.erase(command_queue_.begin());
    this->init_retries_ = 0;
  }

  switch (command_type) {
    case DreoCommandType::HEARTBEAT:
      ESP_LOGV(TAG, "MCU Heartbeat (0x%02X)", len > 0 ? buffer[0] : 0);
      this->protocol_version_ = version;
      if (len > 0 && buffer[0] == 0) {
        ESP_LOGI(TAG, "MCU restarted");
        this->init_state_ = DreoInitState::INIT_HEARTBEAT;
      }
      if (this->init_state_ == DreoInitState::INIT_HEARTBEAT) {
        this->init_state_ = DreoInitState::INIT_PRODUCT;
        this->last_status_ = "synced";
        if (this->wifi_status_on_sync_)
          this->report_wifi_state(0x01);
        this->send_empty_command_(DreoCommandType::PRODUCT_QUERY);
      }
      break;
    case DreoCommandType::PRODUCT_QUERY: {
      // check it is a valid string made up of printable characters
      bool valid = true;
      for (size_t i = 0; i < len; i++) {
        if (!std::isprint(static_cast<unsigned char>(buffer[i]))) {
          valid = false;
          break;
        }
      }
      if (valid) {
        this->product_ = std::string(reinterpret_cast<const char *>(buffer), len);
      } else {
        this->product_ = R"({"p":"INVALID"})";
      }
      if (this->init_state_ == DreoInitState::INIT_PRODUCT) {
        if (this->query_version_on_sync_) {
          this->send_empty_command_(DreoCommandType::VERSION_QUERY);
          this->init_state_ = DreoInitState::INIT_VERSION;
        } else {
          this->send_empty_command_(DreoCommandType::DATAPOINT_QUERY);
          this->init_state_ = DreoInitState::INIT_DATAPOINT;
        }
      }
      break;
    }
    case DreoCommandType::VERSION_QUERY: {
      if (len > 0)
        this->mcu_version_ = std::string(reinterpret_cast<const char *>(buffer), len);
      if (this->init_state_ == DreoInitState::INIT_VERSION) {
        this->send_empty_command_(DreoCommandType::DATAPOINT_QUERY);
        this->init_state_ = DreoInitState::INIT_DATAPOINT;
      }
      break;
    }
    case DreoCommandType::DATAPOINT_DELIVER:
      if (len > 0)
        this->handle_datapoints_(buffer, len);
      break;
    case DreoCommandType::DATAPOINT_REPORT:
      if (this->init_state_ == DreoInitState::INIT_DATAPOINT) {
        this->init_state_ = DreoInitState::INIT_DONE;
        this->last_status_ = "initialized";
        this->set_timeout("datapoint_dump", 1000, [this] { this->dump_config(); });
        this->initialized_callback_.call();
      }
      this->handle_datapoints_(buffer, len);
      break;
    case DreoCommandType::DATAPOINT_QUERY:
      break;
    case DreoCommandType::WIFI_STATE:
      break;
    case DreoCommandType::BUTTON_EVENT:
      this->handle_button_event_(buffer, len);
      break;
    default:
      ESP_LOGE(TAG, "Invalid command (0x%02X) received", command);
  }
}

void Dreo::handle_datapoints_(const uint8_t *buffer, size_t len) {
  while (len >= 5) {
    DreoDatapoint datapoint{};
    datapoint.id = buffer[0];
    datapoint.type = (DreoDatapointType) buffer[2];
    datapoint.value_uint = 0;

    size_t data_size = (buffer[3] << 8) + buffer[4];
    const uint8_t *data = buffer + 5;
    size_t data_len = len - 5;
    if (data_size > data_len) {
      ESP_LOGW(TAG, "Datapoint %u is truncated and cannot be parsed (%zu > %zu)", datapoint.id, data_size, data_len);
      return;
    }

    datapoint.len = data_size;
    datapoint.value_raw.assign(data, data + data_size);
    bool parsed = true;

    switch (datapoint.type) {
      case DreoDatapointType::BOOLEAN:
        if (data_size != 1) {
          ESP_LOGW(TAG, "Datapoint %u has bad boolean len %zu", datapoint.id, data_size);
          parsed = false;
          break;
        }
        datapoint.value_bool = data[0] != 0;
        datapoint.value_uint = datapoint.value_bool ? 1 : 0;
        ESP_LOGD(TAG, "Datapoint %u update to %s", datapoint.id, ONOFF(datapoint.value_bool));
        break;
      case DreoDatapointType::INTEGER:
        if (data_size == 0 || data_size > 4) {
          ESP_LOGW(TAG, "Datapoint %u has bad integer len %zu", datapoint.id, data_size);
          parsed = false;
          break;
        }
        for (size_t i = 0; i < data_size; i++)
          datapoint.value_uint = (datapoint.value_uint << 8) | data[i];
        datapoint.value_int = static_cast<int32_t>(datapoint.value_uint);
        ESP_LOGD(TAG, "Datapoint %u update to %d", datapoint.id, datapoint.value_int);
        break;
      case DreoDatapointType::STRING:
        datapoint.value_string = std::string(reinterpret_cast<const char *>(data), data_size);
        ESP_LOGD(TAG, "Datapoint %u update to string len %zu", datapoint.id, datapoint.value_string.size());
        break;
      case DreoDatapointType::ENUM:
        if (data_size != 1) {
          ESP_LOGW(TAG, "Datapoint %u has bad enum len %zu", datapoint.id, data_size);
          parsed = false;
          break;
        }
        datapoint.value_enum = data[0];
        datapoint.value_uint = datapoint.value_enum;
        datapoint.value_int = datapoint.value_enum;
        ESP_LOGD(TAG, "Datapoint %u update to %d", datapoint.id, datapoint.value_enum);
        break;
      default:
        ESP_LOGW(TAG, "Datapoint %u has unknown type %#02hhX", datapoint.id, static_cast<uint8_t>(datapoint.type));
        parsed = false;
        break;
    }

    len -= data_size + 5;
    buffer = data + data_size;

    if (!parsed)
      continue;

    // drop update if datapoint is in ignore_mcu_datapoint_update list
    bool skip = false;
    for (auto i : this->ignore_mcu_update_on_datapoints_) {
      if (datapoint.id == i) {
        ESP_LOGV(TAG, "Datapoint %u found in ignore_mcu_update_on_datapoints list, dropping MCU update", datapoint.id);
        skip = true;
        break;
      }
    }
    if (skip)
      continue;

    this->publish_datapoint_(datapoint);
  }
}

void Dreo::handle_button_event_(const uint8_t *buffer, size_t len) {
  if (buffer == nullptr || len < 3) {
    this->last_button_event_ = "malformed";
    ESP_LOGW(TAG, "Malformed button event len=%zu", len);
    return;
  }

  const uint8_t code = buffer[2];
  const std::string name = button_name_(code);
  char text[48];
  std::snprintf(text, sizeof(text), "0x%02x %s", code, name.c_str());
  this->last_button_event_ = text;
  ESP_LOGD(TAG, "Remote/panel button event: %s", this->last_button_event_.c_str());

  const uint32_t now = millis();
  if (this->button_event_repeat_guard_ > 0 && !this->last_rx_frame_hex_.empty() &&
      this->last_button_event_frame_hex_ == this->last_rx_frame_hex_ &&
      now - this->last_button_event_frame_timestamp_ < this->button_event_repeat_guard_) {
    ESP_LOGD(TAG, "Ignoring repeated button event frame: %s", this->last_rx_frame_hex_.c_str());
    return;
  }

  this->last_button_event_frame_hex_ = this->last_rx_frame_hex_;
  this->last_button_event_frame_timestamp_ = now;

  DreoButtonEvent event{.code = code, .name = name};
  for (auto &listener : this->button_listeners_)
    listener.on_button_event(event);
}

void Dreo::send_raw_command_(DreoCommand command) {
  uint8_t len_hi = (uint8_t) (command.payload.size() >> 8);
  uint8_t len_lo = (uint8_t) (command.payload.size() & 0xFF);
  uint8_t version = 0;
  uint8_t sequence = this->sequence_++;

  this->last_command_timestamp_ = millis();
  switch (command.cmd) {
    case DreoCommandType::HEARTBEAT:
      this->expected_response_ = DreoCommandType::HEARTBEAT;
      break;
    case DreoCommandType::PRODUCT_QUERY:
      this->expected_response_ = DreoCommandType::PRODUCT_QUERY;
      break;
    case DreoCommandType::VERSION_QUERY:
      this->expected_response_ = DreoCommandType::VERSION_QUERY;
      break;
    case DreoCommandType::DATAPOINT_DELIVER:
    case DreoCommandType::DATAPOINT_QUERY:
      this->expected_response_ = DreoCommandType::DATAPOINT_REPORT;
      break;
    default:
      break;
  }

#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE
  const size_t log_len = std::min<size_t>(command.payload.size(), MAX_DATAPOINT_LOG_BYTES);
  char hex_buf[format_hex_pretty_size(MAX_DATAPOINT_LOG_BYTES)];
  ESP_LOGV(TAG, "Sending Dreo: CMD=0x%02X VERSION=%u DATA=[%s%s] INIT_STATE=%u", static_cast<uint8_t>(command.cmd),
           version, format_hex_pretty_to(hex_buf, command.payload.data(), log_len),
           command.payload.size() > log_len ? " ..." : "", static_cast<uint8_t>(this->init_state_));
#endif

  std::vector<uint8_t> frame;
  frame.reserve(command.payload.size() + 9);
  frame.push_back(0x55);
  frame.push_back(0xAA);
  frame.push_back(version);
  frame.push_back(sequence);
  frame.push_back(static_cast<uint8_t>(command.cmd));
  frame.push_back(0);
  frame.push_back(len_hi);
  frame.push_back(len_lo);
  frame.insert(frame.end(), command.payload.begin(), command.payload.end());

  uint8_t checksum = 0;
  for (auto byte : frame)
    checksum += byte;
  frame.push_back(checksum);

  this->last_tx_frame_hex_ = hex_(frame.data(), frame.size());
  this->write_array(frame.data(), frame.size());
}

void Dreo::process_command_queue_() {
  uint32_t now = millis();
  uint32_t delay = now - this->last_command_timestamp_;

  if (now - this->last_rx_char_timestamp_ > RECEIVE_TIMEOUT) {
    this->rx_message_.clear();
  }

  if (this->expected_response_.has_value() && delay > RECEIVE_TIMEOUT) {
    this->expected_response_.reset();
    if (init_state_ != DreoInitState::INIT_DONE) {
      if (++this->init_retries_ >= this->max_init_retries_) {
        this->init_failed_ = true;
        this->last_status_ = "initialization failed";
        ESP_LOGE(TAG, "Initialization failed at init_state %u", static_cast<uint8_t>(this->init_state_));
        if (!this->command_queue_.empty())
          this->command_queue_.erase(command_queue_.begin());
        this->init_retries_ = 0;
      }
    } else {
      if (!this->command_queue_.empty())
        this->command_queue_.erase(command_queue_.begin());
    }
  }

  // Left check of delay since last command in case there's ever a command sent by calling send_raw_command_ directly.
  if (delay > COMMAND_DELAY && !this->command_queue_.empty() && this->rx_message_.empty() &&
      !this->expected_response_.has_value()) {
    this->send_raw_command_(command_queue_.front());
    if (!this->expected_response_.has_value())
      this->command_queue_.erase(command_queue_.begin());
  }
}

void Dreo::send_command_(const DreoCommand &command) {
  command_queue_.push_back(command);
  process_command_queue_();
}

void Dreo::send_empty_command_(DreoCommandType command) {
  send_command_(DreoCommand{.cmd = command, .payload = std::vector<uint8_t>{}});
}

void Dreo::heartbeat() { this->send_empty_command_(DreoCommandType::HEARTBEAT); }

void Dreo::request_state() {
  this->heartbeat();
  this->send_empty_command_(DreoCommandType::DATAPOINT_QUERY);
}

void Dreo::query_product_info() {
  this->send_empty_command_(DreoCommandType::PRODUCT_QUERY);
  if (this->query_version_on_sync_)
    this->send_empty_command_(DreoCommandType::VERSION_QUERY);
  this->send_empty_command_(DreoCommandType::DATAPOINT_QUERY);
}

void Dreo::query_mcu_version() { this->send_empty_command_(DreoCommandType::VERSION_QUERY); }

void Dreo::report_wifi_state(uint8_t state) {
  this->send_command_(DreoCommand{.cmd = DreoCommandType::WIFI_STATE, .payload = std::vector<uint8_t>{0x00, state}});
}

void Dreo::set_boolean_datapoint_value(uint8_t datapoint_id, bool value) {
  this->set_numeric_datapoint_value_(datapoint_id, DreoDatapointType::BOOLEAN, value, 1, false);
}

void Dreo::set_integer_datapoint_value(uint8_t datapoint_id, uint32_t value) {
  this->set_numeric_datapoint_value_(datapoint_id, DreoDatapointType::INTEGER, value, 4, false);
}

void Dreo::set_enum_datapoint_value(uint8_t datapoint_id, uint8_t value) {
  this->set_numeric_datapoint_value_(datapoint_id, DreoDatapointType::ENUM, value, 1, false);
}

void Dreo::set_string_datapoint_value(uint8_t datapoint_id, const std::string &value) {
  this->set_string_datapoint_value_(datapoint_id, value, false);
}

void Dreo::force_set_boolean_datapoint_value(uint8_t datapoint_id, bool value) {
  this->set_numeric_datapoint_value_(datapoint_id, DreoDatapointType::BOOLEAN, value, 1, true);
}

void Dreo::force_set_integer_datapoint_value(uint8_t datapoint_id, uint32_t value) {
  this->set_numeric_datapoint_value_(datapoint_id, DreoDatapointType::INTEGER, value, 4, true);
}

void Dreo::force_set_enum_datapoint_value(uint8_t datapoint_id, uint8_t value) {
  this->set_numeric_datapoint_value_(datapoint_id, DreoDatapointType::ENUM, value, 1, true);
}

void Dreo::publish_boolean_datapoint_value(uint8_t datapoint_id, bool value) {
  DreoDatapoint datapoint{};
  datapoint.id = datapoint_id;
  datapoint.type = DreoDatapointType::BOOLEAN;
  datapoint.len = 1;
  datapoint.value_bool = value;
  datapoint.value_uint = value ? 1 : 0;
  datapoint.value_raw = {static_cast<uint8_t>(value ? 1 : 0)};
  this->publish_datapoint_(datapoint);
}

void Dreo::publish_integer_datapoint_value(uint8_t datapoint_id, uint32_t value) {
  DreoDatapoint datapoint{};
  datapoint.id = datapoint_id;
  datapoint.type = DreoDatapointType::INTEGER;
  datapoint.len = 4;
  datapoint.value_uint = value;
  datapoint.value_int = static_cast<int32_t>(value);
  datapoint.value_raw = {static_cast<uint8_t>((value >> 24) & 0xff), static_cast<uint8_t>((value >> 16) & 0xff),
                         static_cast<uint8_t>((value >> 8) & 0xff), static_cast<uint8_t>(value & 0xff)};
  this->publish_datapoint_(datapoint);
}

void Dreo::publish_enum_datapoint_value(uint8_t datapoint_id, uint8_t value) {
  DreoDatapoint datapoint{};
  datapoint.id = datapoint_id;
  datapoint.type = DreoDatapointType::ENUM;
  datapoint.len = 1;
  datapoint.value_enum = value;
  datapoint.value_uint = value;
  datapoint.value_int = value;
  datapoint.value_raw = {value};
  this->publish_datapoint_(datapoint);
}

void Dreo::publish_string_datapoint_value(uint8_t datapoint_id, const std::string &value) {
  DreoDatapoint datapoint{};
  datapoint.id = datapoint_id;
  datapoint.type = DreoDatapointType::STRING;
  datapoint.len = value.size();
  datapoint.value_string = value;
  datapoint.value_raw.assign(value.begin(), value.end());
  this->publish_datapoint_(datapoint);
}

optional<DreoDatapoint> Dreo::get_datapoint_(uint8_t datapoint_id) {
  for (auto &datapoint : this->datapoints_) {
    if (datapoint.id == datapoint_id)
      return datapoint;
  }
  return {};
}

optional<DreoDatapoint> Dreo::get_datapoint_(uint8_t datapoint_id) const {
  for (auto &datapoint : this->datapoints_) {
    if (datapoint.id == datapoint_id)
      return datapoint;
  }
  return {};
}

optional<bool> Dreo::bool_state(int datapoint_id) const {
  auto datapoint = this->get_datapoint_(dp_index_(datapoint_id));
  if (!datapoint.has_value())
    return {};
  if (datapoint->type == DreoDatapointType::BOOLEAN)
    return datapoint->value_bool;
  if (datapoint->type == DreoDatapointType::INTEGER || datapoint->type == DreoDatapointType::ENUM)
    return datapoint->value_uint != 0;
  return {};
}

optional<float> Dreo::number_state(int datapoint_id) const {
  auto datapoint = this->get_datapoint_(dp_index_(datapoint_id));
  if (!datapoint.has_value())
    return {};
  if (datapoint->type == DreoDatapointType::BOOLEAN)
    return datapoint->value_bool ? 1.0f : 0.0f;
  if (datapoint->type == DreoDatapointType::INTEGER)
    return static_cast<float>(datapoint->value_int);
  if (datapoint->type == DreoDatapointType::ENUM)
    return static_cast<float>(datapoint->value_enum);
  return {};
}

std::string Dreo::string_state(int datapoint_id) const {
  auto datapoint = this->get_datapoint_(dp_index_(datapoint_id));
  if (!datapoint.has_value())
    return {};
  if (datapoint->type == DreoDatapointType::STRING)
    return datapoint->value_string;
  if (datapoint->type == DreoDatapointType::BOOLEAN)
    return datapoint->value_bool ? "true" : "false";
  return to_string(datapoint->value_int);
}

std::string Dreo::seen_dps_summary() const {
  std::string out;
  char item[64];
  for (const auto &datapoint : this->datapoints_) {
    if (datapoint.type == DreoDatapointType::BOOLEAN) {
      std::snprintf(item, sizeof(item), "%u:01=%u", datapoint.id, datapoint.value_bool ? 1 : 0);
    } else if (datapoint.type == DreoDatapointType::STRING) {
      std::snprintf(item, sizeof(item), "%u:03[%u]", datapoint.id,
                    static_cast<unsigned>(datapoint.value_string.size()));
    } else if (datapoint.type == DreoDatapointType::ENUM) {
      std::snprintf(item, sizeof(item), "%u:04=%u", datapoint.id, datapoint.value_enum);
    } else {
      std::snprintf(item, sizeof(item), "%u:02=%ld", datapoint.id, static_cast<long>(datapoint.value_int));
    }

    const size_t extra = out.empty() ? 0 : 2;
    if (out.size() + extra + std::strlen(item) > 240) {
      if (!out.empty())
        out.append(", ...");
      break;
    }
    if (!out.empty())
      out.append(", ");
    out.append(item);
  }
  return out;
}

void Dreo::set_numeric_datapoint_value_(uint8_t datapoint_id, DreoDatapointType datapoint_type, const uint32_t value,
                                        uint8_t length, bool forced) {
  ESP_LOGD(TAG, "Setting datapoint %u to %" PRIu32, datapoint_id, value);
  // Local (virtual) datapoints live only in the ESP's cache - update it and
  // notify listeners, but never send them to the MCU.
  for (auto i : this->local_datapoints_) {
    if (datapoint_id == i) {
      // Change-gate local publishes like MCU sends: entity listeners typically
      // respond with a call whose write_state runs on the NEXT loop tick (past
      // any re-entrancy guard) and sets this datapoint again - publishing an
      // unchanged value turns that into a self-sustaining loop at loop rate.
      if (!forced) {
        optional<DreoDatapoint> existing = this->get_datapoint_(datapoint_id);
        if (existing.has_value() && existing->value_uint == value) {
          ESP_LOGV(TAG, "Not publishing unchanged local value");
          return;
        }
      }
      switch (datapoint_type) {
        case DreoDatapointType::BOOLEAN:
          this->publish_boolean_datapoint_value(datapoint_id, value != 0);
          break;
        case DreoDatapointType::ENUM:
          this->publish_enum_datapoint_value(datapoint_id, static_cast<uint8_t>(value));
          break;
        default:
          this->publish_integer_datapoint_value(datapoint_id, value);
          break;
      }
      return;
    }
  }
  optional<DreoDatapoint> datapoint = this->get_datapoint_(datapoint_id);
  if (!datapoint.has_value()) {
    ESP_LOGW(TAG, "Setting unknown datapoint %u", datapoint_id);
  } else if (datapoint->type != datapoint_type) {
    ESP_LOGE(TAG, "Attempt to set datapoint %u with incorrect type", datapoint_id);
    return;
  } else if (!forced && datapoint->value_uint == value) {
    ESP_LOGV(TAG, "Not sending unchanged value");
    return;
  }

  std::vector<uint8_t> data;
  switch (length) {
    case 4:
      data.push_back(value >> 24);
      data.push_back(value >> 16);
      [[fallthrough]];
    case 2:
      data.push_back(value >> 8);
      [[fallthrough]];
    case 1:
      data.push_back(value >> 0);
      break;
    default:
      ESP_LOGE(TAG, "Unexpected datapoint length %u", length);
      return;
  }
  this->send_datapoint_command_(datapoint_id, datapoint_type, data);
}

void Dreo::set_string_datapoint_value_(uint8_t datapoint_id, const std::string &value, bool forced) {
  for (auto i : this->local_datapoints_) {
    if (datapoint_id == i) {
      // Same change gate as the numeric path (see set_numeric_datapoint_value_).
      if (!forced) {
        optional<DreoDatapoint> existing = this->get_datapoint_(datapoint_id);
        if (existing.has_value() && existing->value_string == value)
          return;
      }
      this->publish_string_datapoint_value(datapoint_id, value);
      return;
    }
  }
  optional<DreoDatapoint> datapoint = this->get_datapoint_(datapoint_id);
  if (!datapoint.has_value()) {
    ESP_LOGW(TAG, "Setting unknown datapoint %u", datapoint_id);
  } else if (datapoint->type != DreoDatapointType::STRING) {
    ESP_LOGE(TAG, "Attempt to set datapoint %u with incorrect type", datapoint_id);
    return;
  } else if (!forced && datapoint->value_string == value) {
    ESP_LOGV(TAG, "Not sending unchanged value");
    return;
  }

  std::vector<uint8_t> data(value.begin(), value.end());
  this->send_datapoint_command_(datapoint_id, DreoDatapointType::STRING, data);
}

void Dreo::send_datapoint_command_(uint8_t datapoint_id, DreoDatapointType datapoint_type, std::vector<uint8_t> data) {
  std::vector<uint8_t> buffer;
  buffer.push_back(datapoint_id);
  buffer.push_back(0);  // Unknown (always 0 on the HTF-018S; 712S accepts it too)
  buffer.push_back(static_cast<uint8_t>(datapoint_type));
  buffer.push_back(data.size() >> 8);
  buffer.push_back(data.size() >> 0);
  buffer.insert(buffer.end(), data.begin(), data.end());

  this->send_command_(DreoCommand{.cmd = DreoCommandType::DATAPOINT_DELIVER, .payload = buffer});
}

void Dreo::publish_datapoint_(const DreoDatapoint &datapoint) {
  this->update_datapoint_(datapoint);
  this->notify_datapoint_listeners_(datapoint);
}

void Dreo::update_datapoint_(const DreoDatapoint &datapoint) {
  for (auto &other : this->datapoints_) {
    if (other.id == datapoint.id) {
      other = datapoint;
      return;
    }
  }
  this->datapoints_.push_back(datapoint);
}

void Dreo::notify_datapoint_listeners_(const DreoDatapoint &datapoint) {
  for (auto &listener : this->listeners_) {
    if (listener.datapoint_id == datapoint.id)
      listener.on_datapoint(datapoint);
  }
}

void Dreo::register_listener(uint8_t datapoint_id, const std::function<void(DreoDatapoint)> &func) {
  auto listener = DreoDatapointListener{
      .datapoint_id = datapoint_id,
      .on_datapoint = func,
  };
  this->listeners_.push_back(listener);

  // Run through existing datapoints
  for (auto &datapoint : this->datapoints_) {
    if (datapoint.id == datapoint_id)
      func(datapoint);
  }
}

void Dreo::register_button_listener(const std::function<void(DreoButtonEvent)> &func) {
  this->button_listeners_.push_back(DreoButtonEventListener{.on_button_event = func});
}

std::string Dreo::hex_(const uint8_t *data, size_t len) {
  std::string out;
  out.reserve(len * 3);
  char byte_text[4];
  for (size_t i = 0; i < len; i++) {
    if (i != 0)
      out.push_back(' ');
    std::snprintf(byte_text, sizeof(byte_text), "%02x", data[i]);
    out.append(byte_text);
  }
  return out;
}

std::string Dreo::button_name_(uint8_t code) {
  switch (code) {
    case 0x01:
      return "fan-speed-1";
    case 0x02:
      return "fan-speed-2";
    case 0x03:
      return "fan-speed-3";
    case 0x04:
      return "fan-speed-4";
    case 0x05:
      return "fan-speed-5";
    case 0x06:
      return "fan-speed-6";
    case 0x07:
      return "fan-on-off";
    case 0x08:
      return "brightness-down";
    case 0x09:
      return "light-on-off";
    case 0x0a:
      return "brightness-up";
    case 0x0b:
      return "fan-reverse-direction";
    case 0x0d:
      return "kelvin-change";
    case 0x0f:
      return "timer-1h";
    case 0x10:
      return "timer-4h";
    case 0x11:
      return "timer-8h";
    case 0x12:
      return "rgb-change";
    default:
      return "unknown";
  }
}

uint8_t Dreo::dp_index_(int datapoint_id) {
  if (datapoint_id < 0)
    return static_cast<uint8_t>(static_cast<int8_t>(datapoint_id));
  return static_cast<uint8_t>(datapoint_id);
}

DreoInitState Dreo::get_init_state() { return this->init_state_; }

}  // namespace esphome::dreo
