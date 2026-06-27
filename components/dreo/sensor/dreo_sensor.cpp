#include "esphome/core/log.h"
#include "dreo_sensor.h"
#include <cinttypes>

namespace esphome::dreo {

static const char *const TAG = "dreo.sensor";

void DreoSensor::setup() {
  this->parent_->register_listener(this->sensor_id_, [this](const DreoDatapoint &datapoint) {
    if (datapoint.type == DreoDatapointType::BOOLEAN) {
      ESP_LOGV(TAG, "MCU reported sensor %u is: %s", datapoint.id, ONOFF(datapoint.value_bool));
      this->publish_state(datapoint.value_bool);
    } else if (datapoint.type == DreoDatapointType::INTEGER) {
      ESP_LOGV(TAG, "MCU reported sensor %u is: %d", datapoint.id, datapoint.value_int);
      this->publish_state(datapoint.value_int);
    } else if (datapoint.type == DreoDatapointType::ENUM) {
      ESP_LOGV(TAG, "MCU reported sensor %u is: %u", datapoint.id, datapoint.value_enum);
      this->publish_state(datapoint.value_enum);
    }
  });
}

void DreoSensor::dump_config() {
  LOG_SENSOR("", "Dreo Sensor", this);
  ESP_LOGCONFIG(TAG, "  Sensor has datapoint ID %u", this->sensor_id_);
}

}  // namespace esphome::dreo

