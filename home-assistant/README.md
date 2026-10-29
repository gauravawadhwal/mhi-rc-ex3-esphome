# Optional Home Assistant power adapter

This companion integration routes existing ESPHome climate entities' on/off
methods to the firmware's `ac_set_power` action. It keeps the existing entity
identity. Standard `climate.toggle` and Google Assistant thermostat on/off
requests use those methods; explicit HVAC-mode requests remain unchanged.
No previous operating mode or power state is stored by this adapter.

This is a local compatibility workaround tested on HA Core **2026.10.4**.
It overrides selected entity-instance methods using HA internals. It is not
a native ESPHome power capability, and needs testing before HA upgrades.
See [future upstream work](../UPSTREAM_CONTRIBUTION_PLAN.md#future-work-standard-esphome-climate-power-control).

## Installation

1. Back up HA configuration and the device's current firmware.
2. Flash firmware containing `set_power(bool)` and expose `ac_set_power` as in
   the repository's example YAML. Confirm the action appears in HA.
3. Copy `custom_components/rc_ex3_power/` into HA's `custom_components/`.
4. Add a mapping to `configuration.yaml`, using the existing climate entity's
   actual ESPHome config-entry/device IDs and the discovered action name:

```yaml
rc_ex3_power:
  - entity_id: climate.example_ac
    config_entry_id: REPLACE_WITH_EXISTING_ESPHOME_ENTRY_ID
    device_id: REPLACE_WITH_EXISTING_DEVICE_ID
    action: example_node_ac_set_power  # omit the esphome. domain prefix
```

Validate configuration and restart HA. Call `rc_ex3_power.status` with response
data enabled and check `attached` and `action_available`. Pilot one device and
verify its controller-reported mode, temperature and fan survive on/off before
adding other mappings. Do not copy private HA configuration, firmware binaries,
credentials or device-specific deployment scripts into this repository.

A missing action raises an error while its override is attached. If the adapter
fails to load or cannot attach, stock HA behavior can remain. To roll back, remove
the mapping and restart HA; the additive firmware action can stay installed.

## Local tests

The test suite exercises the real HA ESPHome climate class, climate service
dispatch and entity replacement, using simulated transport/registry data.
It never contacts an AC or a live HA instance. In a Python environment with
HA 2026.10.4, its ESPHome integration dependencies, pytest and pytest-asyncio:

```sh
PYTHONPATH=home-assistant python -m pytest -q home-assistant/tests
```

Power packets and firmware state reconciliation are tested separately by
`tests/test_power.cpp` in the repository root.
