#include "esphome/core/log.h"

#include "automation.h"

static const char *const TAG = "dreo.automation";

namespace esphome::dreo {

void check_expected_datapoint(const DreoDatapoint &dp, DreoDatapointType expected) {
  if (dp.type != expected) {
    ESP_LOGW(TAG, "Dreo sensor %u expected datapoint type %#02hhX but got %#02hhX", dp.id,
             static_cast<uint8_t>(expected), static_cast<uint8_t>(dp.type));
  }
}

DreoBoolDatapointUpdateTrigger::DreoBoolDatapointUpdateTrigger(Dreo *parent, uint8_t sensor_id) {
  parent->register_listener(sensor_id, [this](const DreoDatapoint &dp) {
    check_expected_datapoint(dp, DreoDatapointType::BOOLEAN);
    this->trigger(dp.value_bool);
  });
}

DreoIntDatapointUpdateTrigger::DreoIntDatapointUpdateTrigger(Dreo *parent, uint8_t sensor_id) {
  parent->register_listener(sensor_id, [this](const DreoDatapoint &dp) {
    check_expected_datapoint(dp, DreoDatapointType::INTEGER);
    this->trigger(dp.value_int);
  });
}

DreoUIntDatapointUpdateTrigger::DreoUIntDatapointUpdateTrigger(Dreo *parent, uint8_t sensor_id) {
  parent->register_listener(sensor_id, [this](const DreoDatapoint &dp) {
    check_expected_datapoint(dp, DreoDatapointType::INTEGER);
    this->trigger(dp.value_uint);
  });
}

DreoEnumDatapointUpdateTrigger::DreoEnumDatapointUpdateTrigger(Dreo *parent, uint8_t sensor_id) {
  parent->register_listener(sensor_id, [this](const DreoDatapoint &dp) {
    check_expected_datapoint(dp, DreoDatapointType::ENUM);
    this->trigger(dp.value_enum);
  });
}

}  // namespace esphome::dreo

