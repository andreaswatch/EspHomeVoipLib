import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.i2s_audio.microphone import I2SAudioMicrophone
from esphome.components.i2s_audio.speaker import I2SAudioSpeaker
from esphome.const import CONF_ID

DEPENDENCIES = []
AUTO_LOAD = []

voip_ns = cg.esphome_ns.namespace('voip')
Voip = voip_ns.class_('Voip', cg.Component)

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
}).extend(cv.COMPONENT_SCHEMA)

def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    cg.add(var.init(config['sip_ip'], config['sip_user'], config['sip_pass']))
    cg.add(var.set_codec(config['codec']))
    cg.add(var.set_mic_gain(config['mic_gain']))
    cg.add(var.set_amp_gain(config['amp_gain']))
    mic = yield cg.get_variable(config['mic_id'])
    speaker = yield cg.get_variable(config['speaker_id'])
    cg.add(var.set_mic(mic))
    cg.add(var.set_speaker(speaker))
    yield cg.register_component(var, config)