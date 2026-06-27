import esphome.codegen as cg
from esphome.components import fan
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_SPEED_COUNT, CONF_SWITCH_DATAPOINT

from .. import CONF_DREO_ID, Dreo, dreo_ns

DEPENDENCIES = ["dreo"]

CONF_SPEED_DATAPOINT = "speed_datapoint"
CONF_OSCILLATION_DATAPOINT = "oscillation_datapoint"
CONF_DIRECTION_DATAPOINT = "direction_datapoint"

DreoFan = dreo_ns.class_("DreoFan", cg.Component, fan.Fan)

CONFIG_SCHEMA = cv.All(
    fan.fan_schema(DreoFan)
    .extend(
        {
            cv.GenerateID(CONF_DREO_ID): cv.use_id(Dreo),
            cv.Optional(CONF_OSCILLATION_DATAPOINT): cv.uint8_t,
            cv.Optional(CONF_SPEED_DATAPOINT): cv.uint8_t,
            cv.Optional(CONF_SWITCH_DATAPOINT): cv.uint8_t,
            cv.Optional(CONF_DIRECTION_DATAPOINT): cv.uint8_t,
            cv.Optional(CONF_SPEED_COUNT, default=3): cv.int_range(min=1, max=256),
        }
    )
    .extend(cv.COMPONENT_SCHEMA),
    cv.has_at_least_one_key(CONF_SPEED_DATAPOINT, CONF_SWITCH_DATAPOINT),
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_DREO_ID])

    var = cg.new_Pvariable(config[CONF_ID], parent, config[CONF_SPEED_COUNT])
    await cg.register_component(var, config)
    await fan.register_fan(var, config)

    if CONF_SPEED_DATAPOINT in config:
        cg.add(var.set_speed_id(config[CONF_SPEED_DATAPOINT]))
    if CONF_SWITCH_DATAPOINT in config:
        cg.add(var.set_switch_id(config[CONF_SWITCH_DATAPOINT]))
    if CONF_OSCILLATION_DATAPOINT in config:
        cg.add(var.set_oscillation_id(config[CONF_OSCILLATION_DATAPOINT]))
    if CONF_DIRECTION_DATAPOINT in config:
        cg.add(var.set_direction_id(config[CONF_DIRECTION_DATAPOINT]))

