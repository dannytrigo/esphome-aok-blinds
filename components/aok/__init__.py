"""A-OK 433 MHz blinds/shades component for ESPHome.

Implements the A-OK protocol used by AC114/AC123 remotes and AM25 motors,
based on reverse engineering by Jason von Nieda and Antti Kirjavainen.
See: https://github.com/akirjavainen/A-OK
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import remote_transmitter
from esphome.const import CONF_ID

CODEOWNERS = ["@you"]
DEPENDENCIES = ["remote_transmitter"]
AUTO_LOAD = ["cover", "button"]
MULTI_CONF = True

aok_ns = cg.esphome_ns.namespace("aok")
AOKHub = aok_ns.class_("AOKHub", cg.Component)

CONF_TRANSMITTER_ID = "transmitter_id"
CONF_REMOTE_ID = "remote_id"
CONF_REPEATS = "repeats"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(AOKHub),
        cv.Required(CONF_TRANSMITTER_ID): cv.use_id(
            remote_transmitter.RemoteTransmitterComponent
        ),
        cv.Required(CONF_REMOTE_ID): cv.hex_int_range(min=0, max=0xFFFFFF),
        cv.Optional(CONF_REPEATS, default=8): cv.int_range(min=1, max=20),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    transmitter = await cg.get_variable(config[CONF_TRANSMITTER_ID])
    var = cg.new_Pvariable(
        config[CONF_ID],
        transmitter,
        config[CONF_REMOTE_ID],
        config[CONF_REPEATS],
    )
    await cg.register_component(var, config)
