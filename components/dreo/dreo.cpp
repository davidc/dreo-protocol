#include "dreo.h"
#include "esphome/components/network/util.h"
#include "esphome/core/gpio.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/util.h"

namespace esphome::dreo {

static const char *const TAG = "dreo";
static const int COMMAND_DELAY = 10;
static const int RECEIVE_TIMEOUT = 300;
static const int MAX_RETRIES = 5;
// Max bytes to log for datapoint values (larger values are truncated)
static constexpr size_t MAX_DATAPOINT_LOG_BYTES = 16;

void Dreo::setup() {
  this->set_interval("heartbeat", 15000, [this] { this->send_empty_command_(DreoCommandType::HEARTBEAT); });
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
    } else if (info.type == DreoDatapointType::ENUM) {
      ESP_LOGCONFIG(TAG, "  Datapoint %u: enum (value: %d)", info.id, info.value_enum);
    } else {
      ESP_LOGCONFIG(TAG, "  Datapoint %u: unknown", info.id);
    }
  }
  ESP_LOGCONFIG(TAG, "  Product: '%s'", this->product_.c_str());
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
  // no validation for the following fields:
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
  if (at <= 7) {
    // no validation for these fields
    return true;
  }

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

  // valid message
  const uint8_t *message_data = data + 8;
#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE
  char hex_buf[format_hex_pretty_size(MAX_DATAPOINT_LOG_BYTES)];
  ESP_LOGV(TAG, "Received Dreo: CMD=0x%02X VERSION=%u SEQUENCE=%u DATA=[%s] INIT_STATE=%u", command, version, sequence,
           format_hex_pretty_to(hex_buf, message_data, length), static_cast<uint8_t>(this->init_state_));
#endif
  this->handle_command_(command, version, sequence, message_data, length);

  // return false to reset rx buffer
  return false;
}

void Dreo::handle_char_(uint8_t c) {
  this->rx_message_.push_back(c);
  if (!this->validate_message_()) {
    this->rx_message_.clear();
  } else {
    this->last_rx_char_timestamp_ = millis();
  }
}

void Dreo::handle_command_(uint8_t command, uint8_t version, uint8_t sequence, const uint8_t *buffer, size_t len) {
  DreoCommandType command_type = (DreoCommandType) command;

  if (this->expected_response_.has_value() && this->expected_response_ == command_type) {
    this->expected_response_.reset();
    this->command_queue_.erase(command_queue_.begin());
    this->init_retries_ = 0;
  }

  switch (command_type) {
    case DreoCommandType::HEARTBEAT:
      ESP_LOGV(TAG, "MCU Heartbeat (0x%02X)", buffer[0]);
      this->protocol_version_ = version;
      if (buffer[0] == 0) {
        ESP_LOGI(TAG, "MCU restarted");
        this->init_state_ = DreoInitState::INIT_HEARTBEAT;
      }
      if (this->init_state_ == DreoInitState::INIT_HEARTBEAT) {
        this->init_state_ = DreoInitState::INIT_PRODUCT;
        this->send_empty_command_(DreoCommandType::PRODUCT_QUERY);
      }
      break;
    case DreoCommandType::PRODUCT_QUERY: {
      // check it is a valid string made up of printable characters
      bool valid = true;
      for (size_t i = 0; i < len; i++) {
        if (!std::isprint(buffer[i])) {
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
        this->send_empty_command_(DreoCommandType::DATAPOINT_QUERY);
        this->init_state_ = DreoInitState::INIT_DATAPOINT;
      }
      break;
    }
    case DreoCommandType::DATAPOINT_DELIVER:
      break;
    case DreoCommandType::DATAPOINT_REPORT:
      if (this->init_state_ == DreoInitState::INIT_DATAPOINT) {
        this->init_state_ = DreoInitState::INIT_DONE;
        this->set_timeout("datapoint_dump", 1000, [this] { this->dump_config(); });
        this->initialized_callback_.call();
      }
      this->handle_datapoints_(buffer, len);

      // # if this was unsolicited, send a reply TODO
      // The MCU doesn't seem to care that we don't ack its unsolicited reports, but we need to make sure this doesn't trigger a memory leak in it
      // if (command_type == DreoCommandType::DATAPOINT_REPORT_SYNC) {
      //   this->send_command_(
      //       DreoCommand{.cmd = DreoCommandType::DATAPOINT_REPORT_ACK, .payload = std::vector<uint8_t>{0x01}});
      // }
      break;
    case DreoCommandType::DATAPOINT_QUERY:
      break;
    case DreoCommandType::DATAPOINT_CHANGE_NOTIFICATION:
      ESP_LOGD(TAG, "MCU informed us of datapoints changing"); // contents make no sense, they don't include the dpId or value
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

    switch (datapoint.type) {
      case DreoDatapointType::BOOLEAN:
        if (data_size != 1) {
          ESP_LOGW(TAG, "Datapoint %u has bad boolean len %zu", datapoint.id, data_size);
          return;
        }
        datapoint.value_bool = data[0];
        ESP_LOGD(TAG, "Datapoint %u update to %s", datapoint.id, ONOFF(datapoint.value_bool));
        break;
      case DreoDatapointType::INTEGER:
        if (data_size != 4) {
          ESP_LOGW(TAG, "Datapoint %u has bad integer len %zu", datapoint.id, data_size);
          return;
        }
        datapoint.value_uint = encode_uint32(data[0], data[1], data[2], data[3]);
        ESP_LOGD(TAG, "Datapoint %u update to %d", datapoint.id, datapoint.value_int);
        break;
      case DreoDatapointType::ENUM:
        if (data_size != 1) {
          ESP_LOGW(TAG, "Datapoint %u has bad enum len %zu", datapoint.id, data_size);
          return;
        }
        datapoint.value_enum = data[0];
        ESP_LOGD(TAG, "Datapoint %u update to %d", datapoint.id, datapoint.value_enum);
        break;
      default:
        ESP_LOGW(TAG, "Datapoint %u has unknown type %#02hhX", datapoint.id, static_cast<uint8_t>(datapoint.type));
        return;
    }

    len -= data_size + 5;
    buffer = data + data_size;

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

    // Update internal datapoints
    bool found = false;
    for (auto &other : this->datapoints_) {
      if (other.id == datapoint.id) {
        other = datapoint;
        found = true;
      }
    }
    if (!found) {
      this->datapoints_.push_back(datapoint);
    }

    // Run through listeners
    for (auto &listener : this->listeners_) {
      if (listener.datapoint_id == datapoint.id)
        listener.on_datapoint(datapoint);
    }
  }
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
    case DreoCommandType::DATAPOINT_DELIVER:
    case DreoCommandType::DATAPOINT_QUERY:
      this->expected_response_ = DreoCommandType::DATAPOINT_REPORT;
      break;
    default:
      break;
  }

#if ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_VERBOSE
  char hex_buf[format_hex_pretty_size(MAX_DATAPOINT_LOG_BYTES)];
  ESP_LOGV(TAG, "Sending Dreo: CMD=0x%02X VERSION=%u DATA=[%s] INIT_STATE=%u", static_cast<uint8_t>(command.cmd),
           version, format_hex_pretty_to(hex_buf, command.payload.data(), command.payload.size()),
           static_cast<uint8_t>(this->init_state_));
#endif

  this->write_array({0x55, 0xAA, version, sequence, (uint8_t) command.cmd, 0, len_hi, len_lo});
  if (!command.payload.empty())
    this->write_array(command.payload.data(), command.payload.size());

  uint8_t checksum = 0x55 + 0xAA + version + sequence + (uint8_t) command.cmd + len_hi + len_lo;
  for (auto &data : command.payload)
    checksum += data;
  this->write_byte(checksum);
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
      if (++this->init_retries_ >= MAX_RETRIES) {
        this->init_failed_ = true;
        ESP_LOGE(TAG, "Initialization failed at init_state %u", static_cast<uint8_t>(this->init_state_));
        this->command_queue_.erase(command_queue_.begin());
        this->init_retries_ = 0;
      }
    } else {
      this->command_queue_.erase(command_queue_.begin());
    }
  }

  // Left check of delay since last command in case there's ever a command sent by calling send_raw_command_ directly
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


void Dreo::set_boolean_datapoint_value(uint8_t datapoint_id, bool value) {
  this->set_numeric_datapoint_value_(datapoint_id, DreoDatapointType::BOOLEAN, value, 1, false);
}

void Dreo::set_integer_datapoint_value(uint8_t datapoint_id, uint32_t value) {
  this->set_numeric_datapoint_value_(datapoint_id, DreoDatapointType::INTEGER, value, 4, false);
}

void Dreo::set_enum_datapoint_value(uint8_t datapoint_id, uint8_t value) {
  this->set_numeric_datapoint_value_(datapoint_id, DreoDatapointType::ENUM, value, 1, false);
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

optional<DreoDatapoint> Dreo::get_datapoint_(uint8_t datapoint_id) {
  for (auto &datapoint : this->datapoints_) {
    if (datapoint.id == datapoint_id)
      return datapoint;
  }
  return {};
}

void Dreo::set_numeric_datapoint_value_(uint8_t datapoint_id, DreoDatapointType datapoint_type, const uint32_t value,
                                        uint8_t length, bool forced) {
  ESP_LOGD(TAG, "Setting datapoint %u to %" PRIu32, datapoint_id, value);
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

void Dreo::send_datapoint_command_(uint8_t datapoint_id, DreoDatapointType datapoint_type, std::vector<uint8_t> data) {
  std::vector<uint8_t> buffer;
  buffer.push_back(datapoint_id);
  buffer.push_back(0);  // Unknown (always 0)
  buffer.push_back(static_cast<uint8_t>(datapoint_type));
  buffer.push_back(data.size() >> 8);
  buffer.push_back(data.size() >> 0);
  buffer.insert(buffer.end(), data.begin(), data.end());

  this->send_command_(DreoCommand{.cmd = DreoCommandType::DATAPOINT_DELIVER, .payload = buffer});
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

DreoInitState Dreo::get_init_state() { return this->init_state_; }

}  // namespace esphome::dreo

