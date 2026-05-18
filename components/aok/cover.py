"""Cover platform for A-OK blinds."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import cover
from esphome.const import CONF_ID, CONF_CHANNEL

from . import AOKHub, aok_ns

DEPENDENCIES = ["aok"]

CONF_AOK_ID = "aok_id"
CONF_TRAVEL_TIME = "travel_time"
CONF_AFTER_DELAY = "after_delay"
CONF_SEND_AFTER = "send_after"
CONF_INVERTED = "inverted"

AOKCover = aok_ns.class_("AOKCover", cover.Cover, cg.Component)

CONFIG_SCHEMA = cover.cover_schema(AOKCover).extend(
    {
        cv.GenerateID(CONF_AOK_ID): cv.use_id(AOKHub),
        cv.Required(CONF_CHANNEL): cv.int_range(min=1, max=16),
        cv.Optional(CONF_TRAVEL_TIME, default="30s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_AFTER_DELAY, default="250ms"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_SEND_AFTER, default=True): cv.boolean,
        cv.Optional(CONF_INVERTED, default=False): cv.boolean,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await cover.register_cover(var, config)

    hub = await cg.get_variable(config[CONF_AOK_ID])
    cg.add(var.set_hub(hub))

    # Channel is 1-16 in YAML; convert to the 16-bit bitmask the protocol uses.
    channel_bit = 1 << (config[CONF_CHANNEL] - 1)
    cg.add(var.set_channel(channel_bit))

    cg.add(var.set_travel_time(config[CONF_TRAVEL_TIME].total_milliseconds))
    cg.add(var.set_after_delay(config[CONF_AFTER_DELAY].total_milliseconds))
    cg.add(var.set_send_after(config[CONF_SEND_AFTER]))
    cg.add(var.set_inverted(config[CONF_INVERTED]))
