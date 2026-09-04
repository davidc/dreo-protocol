#include "esphome/core/log.h"
#include "dreo_select.h"

namespace esphome::dreo {

static const char *const TAG = "dreo.select";

void DreoSelect::setup() {
  this->parent_->register_listener(this->select_id_, [this](const DreoDatapoint &datapoint) {
    int32_t enum_value;
    if (this->is_int_ && datapoint.type == DreoDatapointType::INTEGER) {
      enum_value = datapoint.value_int;
    } else if (!this->is_int_ && datapoint.type == DreoDatapointType::ENUM) {
      enum_value = datapoint.value_enum;
    } else {
      ESP_LOGW(TAG, "Reported type (%d) does not match configured select type", static_cast<int>(datapoint.type));
      return;
    }
    ESP_LOGV(TAG, "MCU reported select %u value %d", this->select_id_, enum_value);
    auto mappings = this->mappings_;
    auto it = std::find(mappings.cbegin(), mappings.cend(), enum_value);
    if (it == mappings.end()) {
      ESP_LOGW(TAG, "Invalid value %d", enum_value);
      return;
    }
    size_t mapping_idx = std::distance(mappings.cbegin(), it);
    this->publish_state(mapping_idx);
  });
}

void DreoSelect::control(size_t index) {
  if (this->optimistic_)
    this->publish_state(index);

  uint8_t mapping = this->mappings_.at(index);
  ESP_LOGV(TAG, "Setting %u datapoint value to %u:%s", this->select_id_, mapping, this->option_at(index));
  if (this->is_int_) {
    this->parent_->set_integer_datapoint_value(this->select_id_, mapping);
  } else {
    this->parent_->set_enum_datapoint_value(this->select_id_, mapping);
  }
}

void DreoSelect::dump_config() {
  LOG_SELECT("", "Dreo Select", this);
  ESP_LOGCONFIG(TAG,
                "  Select has datapoint ID %u\n"
                "  Data type: %s\n"
                "  Options are:",
                this->select_id_, this->is_int_ ? "int" : "enum");
  const auto &options = this->traits.get_options();
  for (size_t i = 0; i < this->mappings_.size(); i++) {
    ESP_LOGCONFIG(TAG, "    %i: %s", this->mappings_.at(i), options.at(i));
  }
}

}  // namespace esphome::dreo
