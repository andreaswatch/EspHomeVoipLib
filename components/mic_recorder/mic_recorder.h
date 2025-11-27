#pragma once

#include "esphome/core/component.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include "esphome/components/i2s_audio/microphone/i2s_audio_microphone.h"

#include <vector>

namespace esphome {
namespace mic_recorder {

class MicRecorder : public AsyncWebHandler, public Component {
 public:
  MicRecorder();
  void set_mic(i2s_audio::I2SAudioMicrophone *mic) { mic_ = mic; }

  void setup() override;
  void start();
  void stop();

  void mic_data_callback(const std::vector<uint8_t> &data);

  // Control through API
  void record_for_ms(uint32_t ms);

  // AsyncWebHandler methods
  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;
  void handleUpload(AsyncWebServerRequest *request, const PlatformString &filename, size_t index, uint8_t *data,
                    size_t len, bool final) override {}
  void handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) override {}

 protected:
  web_server_base::WebServerBase *base_{};
  bool initialized_{false};
  i2s_audio::I2SAudioMicrophone *mic_{};

  std::vector<uint8_t> buffer_{};  // accumulate for streaming, but we use record_buffer_ for saves
  std::vector<uint8_t> record_buffer_{};
  bool is_recording_ = false;
};

}  // namespace mic_recorder
}  // namespace esphome
