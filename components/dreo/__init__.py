from esphome import automation, pins
import esphome.codegen as cg
from esphome.components import time, uart
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_SENSOR_DATAPOINT, CONF_TRIGGER_ID

DEPENDENCIES = ["uart"]
CODEOWNERS = ["@davidc"]

CONF_IGNORE_MCU_UPDATE_ON_DATAPOINTS = "ignore_mcu_update_on_datapoints"
CONF_LOCAL_DATAPOINTS = "local_datapoints"

CONF_ON_DATAPOINT_UPDATE = "on_datapoint_update"
CONF_ON_BUTTON_EVENT = "on_button_event"
CONF_DATAPOINT_TYPE = "datapoint_type"
CONF_STARTUP_HEARTBEAT_INTERVAL = "startup_heartbeat_interval"
CONF_MAX_INIT_RETRIES = "max_init_retries"
CONF_QUERY_VERSION_ON_SYNC = "query_version_on_sync"
CONF_WIFI_STATUS_ON_SYNC = "wifi_status_on_sync"
CONF_PERIODIC_WIFI_STATUS = "periodic_wifi_status"
CONF_BUTTON_EVENT_REPEAT_GUARD = "button_event_repeat_guard"

dreo_ns = cg.esphome_ns.namespace("dreo")
DreoDatapointType = dreo_ns.enum("DreoDatapointType", is_class=True)
Dreo = dreo_ns.class_("Dreo", cg.Component, uart.UARTDevice)
DreoButtonEventTrigger = dreo_ns.class_(
    "DreoButtonEventTrigger", automation.Trigger.template(cg.uint8)
)

DPTYPE_ANY = "any"
DPTYPE_BOOL = "bool"
DPTYPE_INT = "int"
DPTYPE_UINT = "uint"
DPTYPE_ENUM = "enum"

DATAPOINT_TYPES = {
    DPTYPE_ANY: dreo_ns.struct("DreoDatapoint"),
    DPTYPE_BOOL: cg.bool_,
    DPTYPE_INT: cg.int_,
    DPTYPE_UINT: cg.uint32,
    DPTYPE_ENUM: cg.uint8,
}

DATAPOINT_TRIGGERS = {
    DPTYPE_ANY: dreo_ns.class_(
        "DreoDatapointUpdateTrigger",
        automation.Trigger.template(DATAPOINT_TYPES[DPTYPE_ANY]),
    ),
    DPTYPE_BOOL: dreo_ns.class_(
        "DreoBoolDatapointUpdateTrigger",
        automation.Trigger.template(DATAPOINT_TYPES[DPTYPE_BOOL]),
    ),
    DPTYPE_INT: dreo_ns.class_(
        "DreoIntDatapointUpdateTrigger",
        automation.Trigger.template(DATAPOINT_TYPES[DPTYPE_INT]),
    ),
    DPTYPE_UINT: dreo_ns.class_(
        "DreoUIntDatapointUpdateTrigger",
        automation.Trigger.template(DATAPOINT_TYPES[DPTYPE_UINT]),
    ),
    DPTYPE_ENUM: dreo_ns.class_(
        "DreoEnumDatapointUpdateTrigger",
        automation.Trigger.template(DATAPOINT_TYPES[DPTYPE_ENUM]),
    ),
}


def assign_declare_id(value):
    value = value.copy()
    value[CONF_TRIGGER_ID] = cv.declare_id(
        DATAPOINT_TRIGGERS[value[CONF_DATAPOINT_TYPE]]
    )(value[CONF_TRIGGER_ID].id)
    return value


CONF_DREO_ID = "dreo_id"
CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Dreo),
            cv.Optional(
                CONF_STARTUP_HEARTBEAT_INTERVAL, default="15s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_MAX_INIT_RETRIES, default=5): cv.uint8_t,
            cv.Optional(CONF_QUERY_VERSION_ON_SYNC, default=False): cv.boolean,
            cv.Optional(CONF_WIFI_STATUS_ON_SYNC, default=False): cv.boolean,
            cv.Optional(CONF_PERIODIC_WIFI_STATUS, default=False): cv.boolean,
            cv.Optional(
                CONF_BUTTON_EVENT_REPEAT_GUARD, default="0ms"
            ): cv.time_period,
            cv.Optional(CONF_IGNORE_MCU_UPDATE_ON_DATAPOINTS): cv.ensure_list(
                cv.uint8_t
            ),
            cv.Optional(CONF_LOCAL_DATAPOINTS): cv.ensure_list(cv.uint8_t),
            cv.Optional(CONF_ON_DATAPOINT_UPDATE): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(
                        DATAPOINT_TRIGGERS[DPTYPE_ANY]
                    ),
                    cv.Required(CONF_SENSOR_DATAPOINT): cv.uint8_t,
                    cv.Optional(CONF_DATAPOINT_TYPE, default=DPTYPE_ANY): cv.one_of(
                        *DATAPOINT_TRIGGERS, lower=True
                    ),
                },
                extra_validators=assign_declare_id,
            ),
            cv.Optional(CONF_ON_BUTTON_EVENT): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(
                        DreoButtonEventTrigger
                    ),
                }
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    cg.add(
        var.set_startup_heartbeat_interval(
            config[CONF_STARTUP_HEARTBEAT_INTERVAL].total_milliseconds
        )
    )
    cg.add(var.set_max_init_retries(config[CONF_MAX_INIT_RETRIES]))
    cg.add(var.set_query_version_on_sync(config[CONF_QUERY_VERSION_ON_SYNC]))
    cg.add(var.set_wifi_status_on_sync(config[CONF_WIFI_STATUS_ON_SYNC]))
    cg.add(var.set_periodic_wifi_status(config[CONF_PERIODIC_WIFI_STATUS]))
    cg.add(
        var.set_button_event_repeat_guard(
            config[CONF_BUTTON_EVENT_REPEAT_GUARD].total_milliseconds
        )
    )
    if CONF_IGNORE_MCU_UPDATE_ON_DATAPOINTS in config:
        for dp in config[CONF_IGNORE_MCU_UPDATE_ON_DATAPOINTS]:
            cg.add(var.add_ignore_mcu_update_on_datapoints(dp))
    if CONF_LOCAL_DATAPOINTS in config:
        for dp in config[CONF_LOCAL_DATAPOINTS]:
            cg.add(var.add_local_datapoint(dp))
    for conf in config.get(CONF_ON_DATAPOINT_UPDATE, []):
        trigger = cg.new_Pvariable(
            conf[CONF_TRIGGER_ID], var, conf[CONF_SENSOR_DATAPOINT]
        )
        await automation.build_automation(
            trigger, [(DATAPOINT_TYPES[conf[CONF_DATAPOINT_TYPE]], "x")], conf
        )
    for conf in config.get(CONF_ON_BUTTON_EVENT, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.uint8, "x")], conf)
