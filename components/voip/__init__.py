import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import i2s_audio
from esphome.const import CONF_ID

DEPENDENCIES = ['i2s_audio']
AUTO_LOAD = ['sip']

voip_ns = cg.esphome_ns.namespace('voip')
Voip = voip_ns.class_('Voip', cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(Voip),
    cv.Required('sip_ip'): cv.string,
    cv.Required('sip_user'): cv.string,
    cv.Required('sip_pass'): cv.string,
    cv.Optional('codec', default=1): cv.int_,
    cv.Optional('mic_gain', default=2): cv.int_,
    cv.Optional('amp_gain', default=6): cv.int_,
    cv.Optional('mic_bck_pin', default=26): cv.int_,
    cv.Optional('mic_ws_pin', default=25): cv.int_,
    cv.Optional('mic_data_pin', default=33): cv.int_,
    cv.Optional('mic_bits', default=24): cv.int_,
    cv.Optional('mic_format', default=0): cv.int_,
    cv.Optional('mic_buf_count', default=4): cv.int_,
    cv.Optional('mic_buf_len', default=8): cv.int_,
    cv.Optional('amp_bck_pin', default=14): cv.int_,
    cv.Optional('amp_ws_pin', default=12): cv.int_,
    cv.Optional('amp_data_pin', default=27): cv.int_,
    cv.Optional('amp_bits', default=16): cv.int_,
    cv.Optional('amp_format', default=0): cv.int_,
    cv.Optional('amp_buf_count', default=16): cv.int_,
    cv.Optional('amp_buf_len', default=60): cv.int_,
}).extend(cv.COMPONENT_SCHEMA)

def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    cg.add(var.init(config['sip_ip'], config['sip_user'], config['sip_pass']))
    cg.add(var.set_codec(config['codec']))
    cg.add(var.set_mic_gain(config['mic_gain']))
    cg.add(var.set_amp_gain(config['amp_gain']))
    cg.add(var.set_mic_i2s_config(config['mic_bck_pin'], config['mic_ws_pin'], config['mic_data_pin'], config['mic_bits'], config['mic_format'], config['mic_buf_count'], config['mic_buf_len']))
    cg.add(var.set_amp_i2s_config(config['amp_bck_pin'], config['amp_ws_pin'], config['amp_data_pin'], config['amp_bits'], config['amp_format'], config['amp_buf_count'], config['amp_buf_len']))
    yield cg.register_component(var, config)