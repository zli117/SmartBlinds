"""Config flow for the blinds."""

import voluptuous as vol
from homeassistant import config_entries
import homeassistant.helpers.config_validation as cv
from homeassistant.const import (
    CONF_HOST,
    CONF_NAME,
)
from .cover import DOMAIN


class SmartBlindConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
  """Smart blid config flow."""
  # The schema version of the entries that it creates
  # Home Assistant will call your migrate method if the version changes
  VERSION = 1

  async def async_step_user(self, user_input):
    if user_input is not None:
      self.data = user_input
      return self.async_create_entry(title="Smart Blind", data=self.data)

    return self.async_show_form(
      step_id='user',
      data_schema=vol.Schema({
        vol.Required(CONF_HOST): cv.string, 
        vol.Required(CONF_NAME): cv.string
      })
    )