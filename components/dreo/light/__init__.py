import esphome.codegen as cg
from esphome.components import light
import esphome.config_validation as cv
from esphome.const import (
    CONF_COLD_WHITE_COLOR_TEMPERATURE,
    CONF_MAX_VALUE,
    CONF_OUTPUT_ID,
    CONF_SWITCH_DATAPOINT,
    CONF_WARM_WHITE_COLOR_TEMPERATURE,
)

from .. import CONF_DREO_ID, Dreo, dreo_ns

DEPENDENCIES = ["dreo"]

CONF_BRIGHTNESS_DATAPOINT = "brightness_datapoint"
CONF_COLOR_TEMPERATURE_DATAPOINT = "color_temperature_datapoint"

DreoLight = dreo_ns.class_("DreoLight", light.LightOutput, cg.Component)

CONFIG_SCHEMA = cv.All(
    light.BRIGHTNESS_ONLY_LIGHT_SCHEMA.extend(
        {
            cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(DreoLight),
            cv.GenerateID(CONF_DREO_ID): cv.use_id(Dreo),
            cv.Optional(CONF_SWITCH_DATAPOINT): cv.uint8_t,
            cv.Optional(CONF_BRIGHTNESS_DATAPOINT): cv.uint8_t,
            cv.Optional(CONF_COLOR_TEMPERATURE_DATAPOINT): cv.uint8_t,
            cv.Optional(CONF_MAX_VALUE, default=100): cv.int_,
            cv.Optional(
                CONF_COLD_WHITE_COLOR_TEMPERATURE, default="153 mireds"
            ): cv.color_temperature,
            cv.Optional(
                CONF_WARM_WHITE_COLOR_TEMPERATURE, default="370 mireds"
            ): cv.color_temperature,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.has_at_least_one_key(CONF_SWITCH_DATAPOINT, CONF_BRIGHTNESS_DATAPOINT),
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_DREO_ID])

    var = cg.new_Pvariable(config[CONF_OUTPUT_ID], parent)
    await cg.register_component(var, config)
    await light.register_light(var, config)

    if CONF_SWITCH_DATAPOINT in config:
        cg.add(var.set_switch_id(config[CONF_SWITCH_DATAPOINT]))
    if CONF_BRIGHTNESS_DATAPOINT in config:
        cg.add(var.set_brightness_id(config[CONF_BRIGHTNESS_DATAPOINT]))
    if CONF_COLOR_TEMPERATURE_DATAPOINT in config:
        cg.add(var.set_color_temperature_id(config[CONF_COLOR_TEMPERATURE_DATAPOINT]))

    cg.add(var.set_max_value(config[CONF_MAX_VALUE]))
    cg.add(var.set_cold_white_temperature(config[CONF_COLD_WHITE_COLOR_TEMPERATURE]))
    cg.add(var.set_warm_white_temperature(config[CONF_WARM_WHITE_COLOR_TEMPERATURE]))
