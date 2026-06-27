import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv
from esphome.const import CONF_SWITCH_DATAPOINT

from .. import CONF_DREO_ID, Dreo, dreo_ns

DEPENDENCIES = ["dreo"]

DreoSwitch = dreo_ns.class_("DreoSwitch", switch.Switch, cg.Component)

CONFIG_SCHEMA = (
    switch.switch_schema(DreoSwitch)
    .extend(
        {
            cv.GenerateID(CONF_DREO_ID): cv.use_id(Dreo),
            cv.Required(CONF_SWITCH_DATAPOINT): cv.uint8_t,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await switch.new_switch(config)
    await cg.register_component(var, config)

    paren = await cg.get_variable(config[CONF_DREO_ID])
    cg.add(var.set_dreo_parent(paren))

    cg.add(var.set_switch_id(config[CONF_SWITCH_DATAPOINT]))

