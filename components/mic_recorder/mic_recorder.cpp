#include "mic_recorder.h"
#ifdef USE_ESP32
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include <cstdlib>
#include <cstdio>

namespace esphome {
namespace mic_recorder {

static const char *const TAG = "mic_recorder";

MicRecorder::MicRecorder() : base_(nullptr) {}

void MicRecorder::setup() {
  // nothing special
}

void MicRecorder::start() {
  auto *base = web_server_base::global_web_server_base;
  if (base == nullptr) {
    ESP_LOGE(TAG, "MicRecorder: web server base not found");
    this->mark_failed();
    return;
  }
  this->base_ = base;
  this->base_->init();
  if (!this->initialized_) {
    this->base_->add_handler(this);
  }
  this->initialized_ = true;
  if (this->mic_) {
    this->mic_->add_data_callback([this](const std::vector<uint8_t> &data) { this->mic_data_callback(data); });
  }
}

void MicRecorder::stop() {
  // Nothing special for now
}

void MicRecorder::mic_data_callback(const std::vector<uint8_t> &data) {
  // If recording, append data to record_buffer_
  if (this->is_recording_) {
    this->record_buffer_.insert(this->record_buffer_.end(), data.begin(), data.end());
    ESP_LOGD(TAG, "mic_data_callback: appended %u bytes, record_buffer=%u bytes", (unsigned)data.size(), (unsigned)this->record_buffer_.size());
  }
}

void MicRecorder::record_for_ms(uint32_t ms) {
  if (!mic_) {
    ESP_LOGW(TAG, "record_for_ms: microphone not configured");
    return;
  }
  if (this->is_recording_) {
    ESP_LOGW(TAG, "record_for_ms: already recording");
    return;
  }
  ESP_LOGI(TAG, "record_for_ms: starting for %u ms", ms);
  this->record_buffer_.clear();
  this->is_recording_ = true;
  if (this->mic_->is_stopped()) {
    this->mic_->start();
  }
  // schedule stop
  App.scheduler.set_timeout(this, "mic_record_stop", ms, [this]() {
    ESP_LOGI(TAG, "record_for_ms: stopping recording, len=%u", (unsigned)this->record_buffer_.size());
    this->is_recording_ = false;
    if (this->mic_ && !this->mic_->is_stopped()) {
      // don't stop the mic globally; leave it running unless users configured otherwise
    }
  });
}

bool MicRecorder::canHandle(AsyncWebServerRequest *request) const {
  // Provide two endpoints: /micrec/start and /micrec/latest
  const auto &url = request->url();
  return (url == ESPHOME_F("/micrec/start") || url == ESPHOME_F("/micrec/latest") || url == ESPHOME_F("/micrec/stop"));
}

static void write_wav_header(std::vector<uint8_t> &out, size_t data_len, uint32_t sample_rate, uint16_t bits_per_sample,
                             uint16_t channels) {
  // RIFF header
  uint32_t byte_rate = sample_rate * channels * bits_per_sample / 8;
  uint16_t block_align = channels * bits_per_sample / 8;
  uint32_t chunk_size = 36 + data_len;
  out.resize(44);
  // RIFF
  out[0] = 'R';
  out[1] = 'I';
  out[2] = 'F';
  out[3] = 'F';
  // chunk size
  out[4] = (uint8_t)(chunk_size & 0xff);
  out[5] = (uint8_t)((chunk_size >> 8) & 0xff);
  out[6] = (uint8_t)((chunk_size >> 16) & 0xff);
  out[7] = (uint8_t)((chunk_size >> 24) & 0xff);
  // WAVE
  out[8] = 'W';
  out[9] = 'A';
  out[10] = 'V';
  out[11] = 'E';
  // fmt  
  out[12] = 'f';
  out[13] = 'm';
  out[14] = 't';
  out[15] = ' ';
  // subchunk1 size 16 for PCM
  out[16] = 16;
  out[17] = 0;
  out[18] = 0;
  out[19] = 0;
  // audio format = 1 PCM
  out[20] = 1;
  out[21] = 0;
  // channels
  out[22] = (uint8_t)(channels & 0xff);
  out[23] = 0;
  // sample rate
  out[24] = (uint8_t)(sample_rate & 0xff);
  out[25] = (uint8_t)((sample_rate >> 8) & 0xff);
  out[26] = (uint8_t)((sample_rate >> 16) & 0xff);
  out[27] = (uint8_t)((sample_rate >> 24) & 0xff);
  // byte rate
  out[28] = (uint8_t)(byte_rate & 0xff);
  out[29] = (uint8_t)((byte_rate >> 8) & 0xff);
  out[30] = (uint8_t)((byte_rate >> 16) & 0xff);
  out[31] = (uint8_t)((byte_rate >> 24) & 0xff);
  // block align
  out[32] = (uint8_t)(block_align & 0xff);
  out[33] = 0;
  // bits per sample
  out[34] = (uint8_t)(bits_per_sample & 0xff);
  out[35] = 0;
  // data
  out[36] = 'd';
  out[37] = 'a';
  out[38] = 't';
  out[39] = 'a';
  // data size
  out[40] = (uint8_t)(data_len & 0xff);
  out[41] = (uint8_t)((data_len >> 8) & 0xff);
  out[42] = (uint8_t)((data_len >> 16) & 0xff);
  out[43] = (uint8_t)((data_len >> 24) & 0xff);
}

void MicRecorder::handleRequest(AsyncWebServerRequest *request) {
  const auto &url = request->url();
  if (url == ESPHOME_F("/micrec/start")) {
    uint32_t ms = 1000;
    if (request->hasArg("ms")) {
      // No exceptions enabled in build; use strtoul instead of stoul with try/catch.
      std::string arg = request->arg("ms");
      if (!arg.empty()) {
        char *endptr = nullptr;
        unsigned long v = strtoul(arg.c_str(), &endptr, 10);
        if (endptr != arg.c_str()) ms = static_cast<uint32_t>(v);
      }
    }
    this->record_for_ms(ms);
    auto *rsp = request->beginResponse(200, ESPHOME_F("application/json"), ESPHOME_F("{\"recording\":true}\""));
    request->send(rsp);
    return;
  } else if (url == ESPHOME_F("/micrec/stop")) {
    this->is_recording_ = false;
    auto *rsp = request->beginResponse(200, ESPHOME_F("application/json"), ESPHOME_F("{\"recording\":false}\""));
    request->send(rsp);
    return;
  } else if (url == ESPHOME_F("/micrec/latest") || url == ESPHOME_F("/micrec/stream")) {
    if (this->record_buffer_.empty()) {
      auto *rsp = request->beginResponse(404, ESPHOME_F("text/plain"), ESPHOME_F("no recording"));
      request->send(rsp);
      return;
    }
    // Build a WAV header and return WAV file
    std::vector<uint8_t> wav_header;
    // Assuming sample_rate=8000, bits_per_sample=16, channels=1
    write_wav_header(wav_header, this->record_buffer_.size(), 8000, 16, 1);
    // Create a single contiguous buffer to send (header + data)
    // Avoid copying: we can allocate a new vector and append
    std::vector<uint8_t> out;
    out.reserve(wav_header.size() + this->record_buffer_.size());
    out.insert(out.end(), wav_header.begin(), wav_header.end());
    out.insert(out.end(), this->record_buffer_.begin(), this->record_buffer_.end());
    auto *rsp = request->beginResponse(200, ESPHOME_F("audio/wav"), reinterpret_cast<const uint8_t *>(out.data()), out.size());
    // Add no-cache headers
    rsp->addHeader(ESPHOME_F("cache-control"), ESPHOME_F("no-cache, no-store, must-revalidate"));
    request->send(rsp);
    return;
  }
  else if (url == ESPHOME_F("/micrec/status")) {
    // Return JSON with recording status and last length in bytes
    char buf[128];
    int len = snprintf(buf, sizeof(buf), "{\"recording\":%s,\"size\":%u}", this->is_recording_ ? "true" : "false",
                       (unsigned)this->record_buffer_.size());
    auto *rsp = request->beginResponse(200, ESPHOME_F("application/json"), std::string(buf, len));
    request->send(rsp);
    return;
  }
  // Not found
  auto *rsp = request->beginResponse(404, ESPHOME_F("text/plain"), ESPHOME_F("Not Found"));
  request->send(rsp);
}

}  // namespace mic_recorder
}  // namespace esphome
#endif
