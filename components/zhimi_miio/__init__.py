from esphome import automation
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import ota, uart
from esphome.const import CONF_ID, CONF_TRIGGER_ID

DEPENDENCIES = ["uart"]

zhimi_miio_ns = cg.esphome_ns.namespace("zhimi_miio")
ZhimiMiio = zhimi_miio_ns.class_("ZhimiMiio", cg.Component, uart.UARTDevice)
ButtonPressTrigger = zhimi_miio_ns.class_(
    "ButtonPressTrigger", automation.Trigger.template(cg.std_string)
)

CONF_ZHIMI_MIIO_ID = "zhimi_miio_id"
CONF_OTA_NET_INDICATOR = "ota_net_indicator"
CONF_ON_BUTTON_PRESS = "on_button_press"
CONF_POLL_PROPERTIES = "poll_properties"
CONF_POLL_INTERVAL = "poll_interval"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ZhimiMiio),
            cv.Optional(CONF_OTA_NET_INDICATOR, default="updating"): cv.string,
            # read back with: get_prop "name","name",...
            cv.Optional(CONF_POLL_PROPERTIES, default=[]): cv.ensure_list(
                cv.string_strict
            ),
            cv.Optional(
                CONF_POLL_INTERVAL, default="10s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_ON_BUTTON_PRESS): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ButtonPressTrigger),
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
    cg.add(var.set_ota_net_indicator(config[CONF_OTA_NET_INDICATOR]))

    if properties := config[CONF_POLL_PROPERTIES]:
        cg.add(var.set_poll_properties(properties))
        cg.add(var.set_poll_interval(config[CONF_POLL_INTERVAL]))

    for conf in config.get(CONF_ON_BUTTON_PRESS, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [(cg.std_string, "x")], conf)

    ota.request_ota_state_listeners()
