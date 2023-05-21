"""Support for Homekit covers."""
from __future__ import annotations

import logging
import hashlib
from datetime import timedelta
import requests
from typing import Any
import voluptuous as vol

import homeassistant.helpers.config_validation as cv
from homeassistant.components.cover import (
    ATTR_TILT_POSITION,
    PLATFORM_SCHEMA,
    CoverDeviceClass,
    CoverEntity,
    CoverEntityFeature,
)
from homeassistant.config_entries import ConfigEntry
from homeassistant.const import (
    CONF_HOST,
    CONF_NAME,
)
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.helpers.typing import ConfigType, DiscoveryInfoType
from .const import DOMAIN

_LOGGER = logging.getLogger(__name__)

DEFAULT_NAME = 'My Smart Blind'

PLATFORM_SCHEMA = PLATFORM_SCHEMA.extend({
    vol.Required(CONF_HOST): cv.string,
    vol.Optional(CONF_NAME, default=DEFAULT_NAME): cv.string,
})

SCAN_INTERVAL = timedelta(seconds=60)


def setup_platform(
    hass: HomeAssistant,
    config: ConfigType,
    add_entities: AddEntitiesCallback,
    discovery_info: DiscoveryInfoType | None = None
) -> None:
    """Set up Smart Blind."""
    _LOGGER.info('Normal setup')
    host: str = config[CONF_HOST]
    name: str = config[CONF_NAME]
    add_entities([SmartBlind(host=host, name=name)], update_before_add=True)


async def async_setup_entry(
    hass: HomeAssistant,
    config_entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    """Set up Smart Blind."""
    _LOGGER.info('Entry setup')
    host: str = config_entry.data[CONF_HOST]
    name: str = config_entry.data[CONF_NAME]
    async_add_entities([SmartBlind(host=host, name=name)], update_before_add=True)


class SmartBlind(CoverEntity):
    """Representation of a window blind."""

    _attr_device_class = CoverDeviceClass.BLIND
    _attr_supported_features = CoverEntityFeature.SET_TILT_POSITION

    def __init__(self, host: str, name: str) -> None:
      super().__init__()
      self.host = host
      self.position = -1
      self.max_position = -1
      self.moving = False
      self._name = name
    
    @property
    def unique_id(self) -> str:
      """Return a unique, Home Assistant friendly identifier for this entity."""
      return hashlib.sha256(self.host.encode('utf-8')).hexdigest()

    @property
    def name(self) -> str:
      """Return current device's name."""
      return self._name

    @property
    def current_cover_tilt_position(self) -> int:
      """The current tilt position of the cover where 0 means closed/no tilt and 100 means open/maximum tilt."""
      if not self.available:
        return 0
      return int(100 * self.position / self.max_position + 0.5)

    @property
    def is_opening(self) -> bool:
      """If the cover is opening or not."""
      return self.moving
    
    @property
    def is_closing(self) -> bool | None:
        """Return if the cover is closing or not."""
        return self.moving

    @property
    def is_closed(self) -> bool | None:
        """Return if the cover is closed or not."""
        return self.position == 0 or self.position == self.max_position
    
    @property
    def available(self) -> bool:
        """Return True if is available."""
        return self.position >= 0 and self.max_position >= 0

    def set_cover_tilt_position(self, **kwargs):
      """Move the cover tilt to a specific position."""
      tilt_position = kwargs[ATTR_TILT_POSITION]
      move = {'fraction':tilt_position / 100}
      try:
        response = requests.put(f'http://{self.host}/move', json=move)
      except Exception as e:
        _LOGGER.error(str(e))
        self.position = -1
        self.max_position = -1
        return
      _LOGGER.info(f'Move tilt position to {tilt_position}')

    def update(self):
      """Pull the device to get some updates."""
      self.position = -1
      self.max_position = -1
      try:
        response = requests.get(f'http://{self.host}/status')
      except Exception as e:
        _LOGGER.error(str(e))
        return
      if response.status_code != 200:
        # Something is not right
        _LOGGER.warn(f'HTTP response code {response.status_code}')
        return
      data = response.json()
      if 'max_steps' in data and 'current_steps' in data:
        self.position = data['current_steps']
        self.max_position = data['max_steps']
        _LOGGER.info(f'position: {self.position}, max_position: {self.max_position}')
      elif 'msg' in data:
        _LOGGER.warn(data['msg'])
