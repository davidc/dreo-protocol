#include "esphome/core/log.h"
#include "dreo_binary_sensor.h"

namespace esphome::dreo {

static const char *const TAG = "dreo.binary_sensor";

void DreoBinarySensor::setup() {
  this->parent_->register_listener(this->sensor_id_, [this](const DreoDatapoint &datapoint) {
    bool value;
    switch (datapoint.type) {
      case DreoDatapointType::BOOLEAN:
        value = datapoint.value_bool;
        break;
      case DreoDatapointType::INTEGER:
        value = datapoint.value_int != 0;
        break;
      case DreoDatapointType::ENUM:
        value = datapoint.value_enum != 0;
        break;
      default:
        ESP_LOGW(TAG, "Reported type (%d) is not supported by binary sensor", static_cast<int>(datapoint.type));
        return;
    }
    ESP_LOGV(TAG, "MCU reported binary sensor %u is: %s", datapoint.id, ONOFF(value));
    this->publish_state(value);
  });
}

void DreoBinarySensor::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Dreo Binary Sensor:\n"
                "  Binary Sensor has datapoint ID %u",
                this->sensor_id_);
}

}  // namespace esphome::dreo
