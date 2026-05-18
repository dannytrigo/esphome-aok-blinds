"""Button platform for A-OK: pairing and change-direction helpers."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import CONF_ID

from . import aok_ns
from .cover import AOKCover

DEPENDENCIES = ["aok"]

CONF_COVER_ID = "cover_id"
CONF_ACTION = "action"

AOKButton = aok_ns.class_("AOKButton", button.Button, cg.Component)
AOKButtonAction = aok_ns.enum("AOKButtonAction")

ACTIONS = {
    "pair": AOKButtonAction.AOK_BUTTON_PAIR,
    "change_direction": AOKButtonAction.AOK_BUTTON_CHANGE_DIR,
}

CONFIG_SCHEMA = button.button_schema(AOKButton).extend(
    {
        cv.Required(CONF_COVER_ID): cv.use_id(AOKCover),
        cv.Required(CONF_ACTION): cv.enum(ACTIONS, lower=True),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await button.register_button(var, config)

    cov = await cg.get_variable(config[CONF_COVER_ID])
    cg.add(var.set_cover(cov))
    cg.add(var.set_action(config[CONF_ACTION]))
