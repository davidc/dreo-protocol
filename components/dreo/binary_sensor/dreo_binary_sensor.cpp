#include "esphome/core/log.h"
#include "dreo_binary_sensor.h"

namespace esphome::dreo {

static const char *const TAG = "dreo.binary_sensor";

void DreoBinarySensor::setup() {
  this->parent_->register_listener(this->sensor_id_, [this](const DreoDatapoint &datapoint) {
    ESP_LOGV(TAG, "MCU reported binary sensor %u is: %s", datapoint.id, ONOFF(datapoint.value_bool));
    this->publish_state(datapoint.value_bool);
  });
}

void DreoBinarySensor::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Dreo Binary Sensor:\n"
                "  Binary Sensor has datapoint ID %u",
                this->sensor_id_);
}

}  // namespace esphome::dreo

