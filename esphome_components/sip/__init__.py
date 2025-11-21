import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import CONF_ID

DEPENDENCIES = []
AUTO_LOAD = []

sip_ns = cg.esphome_ns.namespace('sip')
Sip = sip_ns.class_('Sip', cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(Sip),
    cv.Required('sip_ip'): cv.string,
    cv.Required('sip_port'): cv.int_,
    cv.Required('my_ip'): cv.string,
    cv.Required('my_port'): cv.int_,
    cv.Required('sip_user'): cv.string,
    cv.Required('sip_pass'): cv.string,
    cv.Optional('codec', default=1): cv.int_,
}).extend(cv.COMPONENT_SCHEMA)

def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    cg.add(var.init(config['sip_ip'], config['sip_port'], config['my_ip'], config['my_port'], config['sip_user'], config['sip_pass']))
    cg.add(var.set_codec(config['codec']))
    yield cg.register_component(var, config)