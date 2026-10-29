import logging
from collections import defaultdict
from types import SimpleNamespace
from unittest.mock import AsyncMock, MagicMock, patch

import pytest
import pytest_asyncio
from aioesphomeapi import APIVersion, ClimateInfo, ClimateMode, ClimateState, DeviceInfo
from homeassistant.components.climate import ClimateEntity
from homeassistant.components.climate.const import DATA_COMPONENT, HVACMode
from homeassistant.components.esphome.climate import EsphomeClimateEntity
from homeassistant.core import HomeAssistant, Context
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.entity_component import EntityComponent
from homeassistant.helpers import entity_registry, device_registry, area_registry
from homeassistant.components.climate.services import async_setup_services
from custom_components.rc_ex3_power import async_setup, DOMAIN, CONFIG_SCHEMA

ID='climate.example_ac'
ACTION='mhi_rc_ex3_example_ac_set_power'
MAPPING={'entity_id':ID,'config_entry_id':'example_entry','device_id':'example_device','action':ACTION}

@pytest_asyncio.fixture
async def rig(tmp_path):
    hass=HomeAssistant(str(tmp_path))
    device_registry.async_setup(hass)
    for registry_module in (area_registry, device_registry, entity_registry):
        await registry_module.async_load(hass, load_empty=True)
    calls=[]
    async def power(call): calls.append((dict(call.data),call.context))
    hass.services.async_register('esphome',ACTION,power)
    component=EntityComponent(logging.getLogger(__name__),'climate',hass)
    hass.data[DATA_COMPONENT]=component
    async_setup_services(hass)
    registry=SimpleNamespace(async_get=lambda entity_id:SimpleNamespace(platform='esphome',config_entry_id='example_entry',device_id='example_device'))
    def make():
        data=MagicMock()
        data.device_info=DeviceInfo(name='example',mac_address='02:00:00:00:00:01')
        data.api_version=APIVersion(1,13)
        data.available=True
        data.state=defaultdict(dict)
        data.client.climate_command=MagicMock()
        info=ClimateInfo(key=1,name='Example AC',supported_modes=[ClimateMode.OFF,ClimateMode.COOL,ClimateMode.HEAT],visual_min_temperature=16,visual_max_temperature=30,visual_target_temperature_step=.5)
        e=EsphomeClimateEntity(data,info,ClimateState)
        e.entity_id=ID;e.hass=hass
        e._attr_device_info=None
        e._has_state=True;e._state=ClimateState(key=1,mode=ClimateMode.OFF,target_temperature=21)
        # Real ESPHome entity with fake transport; no live device subscriptions.
        e.async_added_to_hass=AsyncMock()
        return e
    entity=make()
    # Service dispatch uses the real domain platform and real entity methods.
    with patch('custom_components.rc_ex3_power.er',SimpleNamespace(async_get=lambda _:registry)):
        await component.async_add_entities([entity])
        assert await async_setup(hass,CONFIG_SCHEMA({DOMAIN:[MAPPING]}))
        yield SimpleNamespace(hass=hass,entity=entity,component=component,calls=calls,make=make,registry=registry)
        hass.data[DOMAIN].stop()
        await component._async_reset()
        await hass.async_block_till_done()

@pytest.mark.asyncio
async def test_real_services_on_off_toggle_and_explicit_mode(rig):
    r=rig;context=Context()
    for service,mode,power in [('turn_on',ClimateMode.OFF,True),('turn_off',ClimateMode.COOL,False),('toggle',ClimateMode.OFF,True),('toggle',ClimateMode.COOL,False)]:
        r.entity._state=ClimateState(key=1,mode=mode,target_temperature=21)
        await r.hass.services.async_call('climate',service,{'entity_id':ID},blocking=True,context=context)
        assert r.calls[-1]==({'power':power},context)
    r.entity._client.climate_command.assert_not_called()
    await r.hass.services.async_call('climate','set_hvac_mode',{'entity_id':ID,'hvac_mode':'cool'},blocking=True)
    r.entity._client.climate_command.assert_called_once()
    assert r.entity._client.climate_command.call_args.kwargs['mode']==ClimateMode.COOL

@pytest.mark.asyncio
async def test_missing_action_fails_without_mode_fallback(rig):
    rig.hass.services.async_remove('esphome',ACTION)
    with pytest.raises(HomeAssistantError,match='unavailable'):
        await rig.entity.async_turn_on()
    rig.entity._client.climate_command.assert_not_called()

@pytest.mark.asyncio
async def test_offline_fails_and_transport_error_propagates(rig):
    rig.entity._attr_available=False
    with pytest.raises(HomeAssistantError): await rig.entity.async_turn_on()
    assert not rig.calls
    rig.entity._attr_available=True
    async def fail(call): raise HomeAssistantError('device disconnected')
    rig.hass.services.async_register('esphome',ACTION,fail)
    with pytest.raises(HomeAssistantError,match='disconnected'): await rig.entity.async_turn_on()

@pytest.mark.asyncio
async def test_repeated_setup_restores_and_reattaches(rig):
    first=rig.hass.data[DOMAIN]
    await async_setup(rig.hass,{DOMAIN:[MAPPING]})
    assert first.bindings[ID].entity is None
    await rig.entity.async_turn_on()
    assert len(rig.calls)==1
    rig.hass.data[DOMAIN].stop()
    assert 'async_turn_on' not in rig.entity.__dict__
    assert rig.entity.async_turn_on.__func__ is ClimateEntity.async_turn_on

@pytest.mark.asyncio
async def test_entity_reload_reattaches_to_new_instance(rig):
    old=rig.entity
    await old.async_remove()
    assert rig.hass.data[DOMAIN].bindings[ID].entity is None
    replacement=rig.make()
    await rig.component.async_add_entities([replacement])
    await rig.hass.async_block_till_done()
    assert rig.hass.data[DOMAIN].bindings[ID].entity is replacement
    assert 'async_turn_on' not in old.__dict__
    await replacement.async_turn_on()
    assert rig.calls[-1][0]=={'power':True}

@pytest.mark.asyncio
async def test_wrong_device_does_not_attach(rig):
    rig.hass.data[DOMAIN].stop()
    await async_setup(rig.hass,{DOMAIN:[{**MAPPING,'device_id':'wrong_device'}]})
    assert 'async_turn_on' not in rig.entity.__dict__
    report=await rig.hass.data[DOMAIN].status(None)
    assert not report['bindings'][ID]['attached']
    assert report['bindings'][ID]['error']
