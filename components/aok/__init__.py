"""A-OK 433 MHz blinds/shades component for ESPHome.

Implements the A-OK protocol used by AC114/AC123 remotes and AM25 motors,
based on reverse engineering by Jason von Nieda and Antti Kirjavainen.
See: https://github.com/akirjavainen/A-OK
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import remote_transmitter
from esphome.components import cover as cover_component
from esphome.components import button as button_component
from esphome.const import CONF_ID, CONF_CHANNEL, CONF_NAME, CONF_DISABLED_BY_DEFAULT, ENTITY_CATEGORY_DIAGNOSTIC, \
    CONF_ENTITY_CATEGORY

CODEOWNERS = ["@you"]
DEPENDENCIES = ["remote_transmitter"]
AUTO_LOAD = ["cover", "button"]
MULTI_CONF = True

aok_ns = cg.esphome_ns.namespace("aok")
AOKHub = aok_ns.class_("AOKHub", cg.Component)

# These class references are declared here (not in cover.py / button.py) so
# that BLIND_SCHEMA below can reference them without creating a circular import.
AOKCover = aok_ns.class_("AOKCover", cover_component.Cover, cg.Component)
AOKButton = aok_ns.class_("AOKButton", button_component.Button, cg.Component)
AOKButtonAction = aok_ns.enum("AOKButtonAction")

# Hub-level keys
CONF_TRANSMITTER_ID = "transmitter_id"
CONF_REMOTE_ID = "remote_id"
CONF_REPEATS = "repeats"
CONF_BLINDS = "blinds"

# Per-blind keys (also re-exported so cover.py / button.py can import them
# from here instead of defining them locally, avoiding duplicate symbols).
CONF_AOK_ID = "aok_id"
CONF_TRAVEL_TIME = "travel_time"
CONF_AFTER_DELAY = "after_delay"
CONF_SEND_AFTER = "send_after"
CONF_INVERTED = "inverted"
CONF_PROGRAM_BUTTON_ID = "program_button_id"
CONF_DIR_BUTTON_ID = "dir_button_id"

# ---------------------------------------------------------------------------
# Schema for a single blind declared inside the `aok:` hub block.
# Builds on cover_schema so all standard entity options (icon, internal, etc.)
# are supported, and IDs for both helper buttons are auto-generated.
# ---------------------------------------------------------------------------
BLIND_SCHEMA = cover_component.cover_schema(AOKCover).extend(
    {
        cv.GenerateID(CONF_PROGRAM_BUTTON_ID): cv.declare_id(AOKButton),
        cv.GenerateID(CONF_DIR_BUTTON_ID): cv.declare_id(AOKButton),
        cv.Required(CONF_CHANNEL): cv.int_range(min=1, max=16),
        cv.Optional(CONF_TRAVEL_TIME, default="30s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_AFTER_DELAY, default="250ms"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_SEND_AFTER, default=True): cv.boolean,
        cv.Optional(CONF_INVERTED, default=False): cv.boolean,
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(AOKHub),
        cv.Required(CONF_TRANSMITTER_ID): cv.use_id(
            remote_transmitter.RemoteTransmitterComponent
        ),
        cv.Required(CONF_REMOTE_ID): cv.hex_int_range(min=0, max=0xFFFFFF),
        cv.Optional(CONF_REPEATS, default=8): cv.int_range(min=1, max=20),
        # Optional compact syntax: each entry auto-creates a cover + two buttons.
        cv.Optional(CONF_BLINDS): cv.ensure_list(BLIND_SCHEMA),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    transmitter = await cg.get_variable(config[CONF_TRANSMITTER_ID])
    hub = cg.new_Pvariable(
        config[CONF_ID],
        transmitter,
        config[CONF_REMOTE_ID],
        config[CONF_REPEATS],
    )
    await cg.register_component(hub, config)

    for blind in config.get(CONF_BLINDS, []):
        # ------------------------------------------------------------------ #
        # Cover entity                                                         #
        # ------------------------------------------------------------------ #
        cov = cg.new_Pvariable(blind[CONF_ID])
        await cg.register_component(cov, blind)
        await cover_component.register_cover(cov, blind)

        cg.add(cov.set_hub(hub))
        cg.add(cov.set_channel(1 << (blind[CONF_CHANNEL] - 1)))
        cg.add(cov.set_travel_time(blind[CONF_TRAVEL_TIME].total_milliseconds))
        cg.add(cov.set_after_delay(blind[CONF_AFTER_DELAY].total_milliseconds))
        cg.add(cov.set_send_after(blind[CONF_SEND_AFTER]))
        cg.add(cov.set_inverted(blind[CONF_INVERTED]))

        # ------------------------------------------------------------------ #
        # Program button  ("{name} Program")                                  #
        # ------------------------------------------------------------------ #
        prog_btn = cg.new_Pvariable(blind[CONF_PROGRAM_BUTTON_ID])
        await cg.register_component(prog_btn, {})
        await button_component.register_button(
            prog_btn,
            {
                CONF_ID: blind[CONF_PROGRAM_BUTTON_ID],
                CONF_NAME: f"{blind[CONF_NAME]} Program",
                CONF_DISABLED_BY_DEFAULT: False,
                CONF_ENTITY_CATEGORY: ENTITY_CATEGORY_DIAGNOSTIC,
            },
        )
        cg.add(prog_btn.set_cover(cov))
        cg.add(prog_btn.set_action(AOKButtonAction.AOK_BUTTON_PROGRAM))

        # ------------------------------------------------------------------ #
        # Reverse button  ("{name} Reverse")                                   #
        # ------------------------------------------------------------------ #
        dir_btn = cg.new_Pvariable(blind[CONF_DIR_BUTTON_ID])
        await cg.register_component(dir_btn, {})
        await button_component.register_button(
            dir_btn,
            {
                CONF_ID: blind[CONF_DIR_BUTTON_ID],
                CONF_NAME: f"{blind[CONF_NAME]} Reverse",
                CONF_DISABLED_BY_DEFAULT: False,
                CONF_ENTITY_CATEGORY: ENTITY_CATEGORY_DIAGNOSTIC,
            },
        )
        cg.add(dir_btn.set_cover(cov))
        cg.add(dir_btn.set_action(AOKButtonAction.AOK_BUTTON_CHANGE_DIR))
