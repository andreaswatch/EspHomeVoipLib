import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components.i2s_audio.microphone import I2SAudioMicrophone
from esphome.components.i2s_audio.speaker import I2SAudioSpeaker
# removed BinarySensor import - we no longer use ready_sensor bindings
from esphome.const import CONF_ID, CONF_TRIGGER_ID

DEPENDENCIES = ["socket"]
AUTO_LOAD = []

voip_ns = cg.esphome_ns.namespace('voip')
Voip = voip_ns.class_('Voip', cg.Component)
RingingTrigger = voip_ns.class_('RingingTrigger', automation.Trigger)
CallEstablishedTrigger = voip_ns.class_('CallEstablishedTrigger', automation.Trigger)
CallEndedTrigger = voip_ns.class_('CallEndedTrigger', automation.Trigger)
ReadyTrigger = voip_ns.class_('ReadyTrigger', automation.Trigger)
NotReadyTrigger = voip_ns.class_('NotReadyTrigger', automation.Trigger)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(Voip),
    cv.Required('sip_ip'): cv.string,
    cv.Required('sip_user'): cv.string,
    cv.Required('sip_pass'): cv.string,
    cv.Optional('codec', default=0): cv.int_,
    cv.Optional('mic_gain', default=2): cv.int_,
    cv.Optional('amp_gain', default=6): cv.int_,
    cv.Required('mic_id'): cv.use_id(I2SAudioMicrophone),
    cv.Required('speaker_id'): cv.use_id(I2SAudioSpeaker),
    # readiness is now an automation event instead of a binary sensor
    cv.Optional('on_ringing'): automation.validate_automation({
        cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(RingingTrigger),
    }),
    cv.Optional('on_call_established'): automation.validate_automation({
        cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(CallEstablishedTrigger),
    }),
    cv.Optional('on_call_ended'): automation.validate_automation({
        cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(CallEndedTrigger),
    }),
    cv.Optional('on_ready'): automation.validate_automation({
        cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ReadyTrigger),
    }),
    cv.Optional('on_not_ready'): automation.validate_automation({
        cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(NotReadyTrigger),
    }),
    cv.Optional('start_on_boot', default=False): cv.boolean,
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    cg.add(var.init(config['sip_ip'], config['sip_user'], config['sip_pass']))
    cg.add(var.set_codec(config['codec']))
    cg.add(var.set_mic_gain(config['mic_gain']))
    cg.add(var.set_amp_gain(config['amp_gain']))
    mic = await cg.get_variable(config['mic_id'])
    speaker = await cg.get_variable(config['speaker_id'])
    cg.add(var.set_mic(mic))
    cg.add(var.set_speaker(speaker))
    # Deprecated: no ready_sensor_id - use on_ready/on_not_ready automations
    # removed default_dial_number config option
    if 'start_on_boot' in config and config['start_on_boot']:
        cg.add(var.set_start_on_boot(True))
    # PA output control removed; automations (on_call_established/on_call_ended) should manage amplifier
    # Build automations
    for conf in config.get('on_ringing', []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], conf)
    for conf in config.get('on_call_established', []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], conf)
    for conf in config.get('on_call_ended', []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], conf)
    for conf in config.get('on_ready', []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], conf)
    for conf in config.get('on_not_ready', []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], conf)
    await cg.register_component(var, config)