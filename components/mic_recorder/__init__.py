import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.i2s_audio.microphone import I2SAudioMicrophone
from esphome.const import CONF_ID

mic_recorder_ns = cg.esphome_ns.namespace('mic_recorder')
MicRecorder = mic_recorder_ns.class_('MicRecorder', cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(MicRecorder),
    cv.Required('mic_id'): cv.use_id(I2SAudioMicrophone),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    mic = await cg.get_variable(config['mic_id'])
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_mic(mic))
