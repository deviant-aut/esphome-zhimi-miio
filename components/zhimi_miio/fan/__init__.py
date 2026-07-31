import esphome.codegen as cg
from esphome.components import fan
import esphome.config_validation as cv

from .. import CONF_ZHIMI_MIIO_ID, ZhimiMiio, zhimi_miio_ns

DEPENDENCIES = ["zhimi_miio"]

ZhimiFan = zhimi_miio_ns.class_("ZhimiFan", cg.Component, fan.Fan)

CONFIG_SCHEMA = (
    fan.fan_schema(ZhimiFan)
    .extend(
        {
            cv.GenerateID(CONF_ZHIMI_MIIO_ID): cv.use_id(ZhimiMiio),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = await fan.new_fan(config)
    await cg.register_component(var, config)

    parent = await cg.get_variable(config[CONF_ZHIMI_MIIO_ID])
    cg.add(var.set_parent(parent))
