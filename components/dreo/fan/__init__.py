import esphome.codegen as cg
from esphome.components import fan
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_SPEED_COUNT, CONF_SWITCH_DATAPOINT

from .. import CONF_DREO_ID, Dreo, DreoDatapointType, dreo_ns

DEPENDENCIES = ["dreo"]

CONF_SPEED_DATAPOINT = "speed_datapoint"
CONF_OSCILLATION_DATAPOINT = "oscillation_datapoint"
CONF_DIRECTION_DATAPOINT = "direction_datapoint"
CONF_POWER_DATAPOINT = "power_datapoint"
CONF_SPEED_MAPPINGS = "speed_mappings"
CONF_SPEED_DATAPOINT_TYPE = "speed_datapoint_type"
CONF_DIRECTION_DATAPOINT_TYPE = "direction_datapoint_type"
CONF_DIRECTION_FORWARD_VALUE = "direction_forward_value"
CONF_DIRECTION_REVERSE_VALUE = "direction_reverse_value"

DreoFan = dreo_ns.class_("DreoFan", cg.Component, fan.Fan)

DATAPOINT_TYPES = {
    "bool": DreoDatapointType.BOOLEAN,
    "int": DreoDatapointType.INTEGER,
    "enum": DreoDatapointType.ENUM,
}


def validate_speed_mappings(config):
    mappings = config.get(CONF_SPEED_MAPPINGS)
    if mappings is not None and len(mappings) != config[CONF_SPEED_COUNT]:
        raise cv.Invalid(f"{CONF_SPEED_MAPPINGS} length must match {CONF_SPEED_COUNT}")
    return config


CONFIG_SCHEMA = cv.All(
    fan.fan_schema(DreoFan)
    .extend(
        {
            cv.GenerateID(CONF_DREO_ID): cv.use_id(Dreo),
            cv.Optional(CONF_OSCILLATION_DATAPOINT): cv.uint8_t,
            cv.Optional(CONF_SPEED_DATAPOINT): cv.uint8_t,
            cv.Optional(CONF_SWITCH_DATAPOINT): cv.uint8_t,
            cv.Optional(CONF_POWER_DATAPOINT): cv.uint8_t,
            cv.Optional(CONF_DIRECTION_DATAPOINT): cv.uint8_t,
            cv.Optional(CONF_SPEED_COUNT, default=3): cv.int_range(min=1, max=256),
            cv.Optional(CONF_SPEED_MAPPINGS): cv.ensure_list(cv.uint8_t),
            cv.Optional(CONF_SPEED_DATAPOINT_TYPE): cv.enum(
                DATAPOINT_TYPES, lower=True
            ),
            cv.Optional(CONF_DIRECTION_DATAPOINT_TYPE): cv.enum(
                DATAPOINT_TYPES, lower=True
            ),
            cv.Optional(CONF_DIRECTION_FORWARD_VALUE): cv.uint32_t,
            cv.Optional(CONF_DIRECTION_REVERSE_VALUE): cv.uint32_t,
        }
    )
    .extend(cv.COMPONENT_SCHEMA),
    cv.has_at_least_one_key(CONF_SPEED_DATAPOINT, CONF_SWITCH_DATAPOINT),
    validate_speed_mappings,
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
    if CONF_POWER_DATAPOINT in config:
        cg.add(var.set_power_id(config[CONF_POWER_DATAPOINT]))
    if CONF_OSCILLATION_DATAPOINT in config:
        cg.add(var.set_oscillation_id(config[CONF_OSCILLATION_DATAPOINT]))
    if CONF_DIRECTION_DATAPOINT in config:
        cg.add(var.set_direction_id(config[CONF_DIRECTION_DATAPOINT]))
    if CONF_SPEED_MAPPINGS in config:
        cg.add(var.set_speed_mappings(config[CONF_SPEED_MAPPINGS]))
    if CONF_SPEED_DATAPOINT_TYPE in config:
        cg.add(var.set_speed_type(config[CONF_SPEED_DATAPOINT_TYPE]))
    if CONF_DIRECTION_DATAPOINT_TYPE in config:
        cg.add(var.set_direction_type(config[CONF_DIRECTION_DATAPOINT_TYPE]))
    if (
        CONF_DIRECTION_FORWARD_VALUE in config
        and CONF_DIRECTION_REVERSE_VALUE in config
    ):
        cg.add(
            var.set_direction_values(
                config[CONF_DIRECTION_FORWARD_VALUE],
                config[CONF_DIRECTION_REVERSE_VALUE],
            )
        )
