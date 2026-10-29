"""Route selected existing ESPHome climate entities to power-only actions.

Local compatibility adapter, tested on Core 2026.10.4. No operating mode or
power state is stored here. Instance method overrides require upgrade testing.
"""

from dataclasses import dataclass, field
import logging
from types import MethodType
from typing import Any

import probatio as vol

from homeassistant.components.climate import ClimateEntity
from homeassistant.components.climate.const import DATA_COMPONENT
from homeassistant.const import EVENT_HOMEASSISTANT_STOP
from homeassistant.core import HomeAssistant, SupportsResponse, callback
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers import config_validation as cv, entity_registry as er
from homeassistant.helpers.event import async_track_state_change_event

DOMAIN = "rc_ex3_power"
_LOGGER = logging.getLogger(__name__)
_MISSING = object()

MAPPING_SCHEMA = vol.Schema({
    vol.Required("entity_id"): cv.entity_id,
    vol.Required("config_entry_id"): cv.string,
    vol.Required("device_id"): cv.string,
    vol.Required("action"): cv.string,
})
CONFIG_SCHEMA = vol.Schema({
    vol.Optional(DOMAIN): vol.All(cv.ensure_list, [MAPPING_SCHEMA]),
}, extra=vol.ALLOW_EXTRA)


@dataclass
class Binding:
    """Configuration and lifecycle references only; no remembered AC state."""

    config: dict[str, str]
    entity: ClimateEntity | None = None
    originals: dict[str, Any] = field(default_factory=dict)
    replacements: dict[str, Any] = field(default_factory=dict)
    error: str | None = None


class PowerAdapter:
    def __init__(self, hass: HomeAssistant, mappings: list[dict[str, str]]) -> None:
        self.hass = hass
        self.bindings = {m["entity_id"]: Binding(m) for m in mappings}
        self.unsubscribers = []

    @callback
    def reconcile(self, event=None) -> None:
        component = self.hass.data.get(DATA_COMPONENT)
        registry = er.async_get(self.hass)
        for entity_id, binding in self.bindings.items():
            entity = component.get_entity(entity_id) if component else None
            if entity is binding.entity and entity is not None:
                continue
            self.detach(binding)
            entry = registry.async_get(entity_id)
            if entity is None:
                binding.error = "Waiting for climate entity"
                continue
            if (not isinstance(entity, ClimateEntity)
                or type(entity).__module__ != "homeassistant.components.esphome.climate"
                or entry is None or entry.platform != "esphome"
                or entry.config_entry_id != binding.config["config_entry_id"]
                or entry.device_id != binding.config["device_id"]):
                binding.error = "Entity identity does not match configured ESPHome device"
                _LOGGER.error("%s: %s", entity_id, binding.error)
                continue

            binding.entity = entity
            binding.error = None
            for name, power in (("async_turn_on", True), ("async_turn_off", False)):
                binding.originals[name] = entity.__dict__.get(name, _MISSING)

                async def send_power(instance, *, _power=power, _binding=binding):
                    if _binding.entity is not instance or not instance.available:
                        raise HomeAssistantError("RC-EX3 climate is unavailable")
                    action = _binding.config["action"]
                    if not self.hass.services.has_service("esphome", action):
                        raise HomeAssistantError(f"RC-EX3 power action esphome.{action} is unavailable")
                    await self.hass.services.async_call(
                        "esphome", action, {"power": _power}, blocking=True,
                        context=instance._context,
                    )

                replacement = MethodType(send_power, entity)
                binding.replacements[name] = replacement
                setattr(entity, name, replacement)

            @callback
            def removed(_binding=binding, _entity=entity):
                if _binding.entity is _entity:
                    self.detach(_binding)

            entity.async_on_remove(removed)
            _LOGGER.info("Power-only routing attached to %s", entity_id)

    @callback
    def detach(self, binding: Binding) -> None:
        if (entity := binding.entity) is not None:
            for name, replacement in binding.replacements.items():
                # Do not undo a later override made by another integration.
                if entity.__dict__.get(name) is replacement:
                    original = binding.originals[name]
                    if original is _MISSING:
                        delattr(entity, name)
                    else:
                        setattr(entity, name, original)
        binding.entity = None
        binding.originals.clear()
        binding.replacements.clear()

    @callback
    def stop(self, event=None) -> None:
        for unsubscribe in self.unsubscribers:
            unsubscribe()
        self.unsubscribers.clear()
        for binding in self.bindings.values():
            self.detach(binding)
        self.hass.services.async_remove(DOMAIN, "status")

    async def status(self, call) -> dict:
        self.reconcile()
        return {"version": "0.1.0", "bindings": {
            entity_id: {
                "attached": binding.entity is not None,
                "action": "esphome." + binding.config["action"],
                "action_available": self.hass.services.has_service("esphome", binding.config["action"]),
                "error": binding.error,
            } for entity_id, binding in self.bindings.items()
        }}


async def async_setup(hass: HomeAssistant, config: dict) -> bool:
    mappings = config.get(DOMAIN, [])
    ids = [m["entity_id"] for m in mappings]
    if len(set(ids)) != len(ids) or any(not e.startswith("climate.") for e in ids):
        _LOGGER.error("Configure each climate entity exactly once")
        return False
    if any("." in m["action"] or not m["action"].endswith("_ac_set_power") for m in mappings):
        _LOGGER.error("Use the discovered ESPHome ac_set_power action name without its domain")
        return False
    if (previous := hass.data.pop(DOMAIN, None)) is not None:
        previous.stop()
    adapter = hass.data[DOMAIN] = PowerAdapter(hass, mappings)
    adapter.unsubscribers.append(async_track_state_change_event(hass, ids, adapter.reconcile))
    adapter.unsubscribers.append(hass.bus.async_listen_once(EVENT_HOMEASSISTANT_STOP, adapter.stop))
    hass.services.async_register(DOMAIN, "status", adapter.status,
                                 schema=vol.Schema({}), supports_response=SupportsResponse.ONLY)
    adapter.reconcile()
    return True
