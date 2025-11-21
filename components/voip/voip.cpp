#include "voip.h"
#include <cstring>
#include <Arduino.h>

namespace esphome {
namespace voip {

Voip::Voip() {}

Voip::~Voip() {
  // if (opus_encoder_) opus_encoder_destroy(opus_encoder_);
  // if (opus_decoder_) opus_decoder_destroy(opus_decoder_);
}

void Voip::setup() {
  my_ip_ = WiFi.localIP().toString();
  int conn_ok = rtp_udp_.begin(1234);
  if (conn_ok == 0) {
    ESP_LOGE(TAG, "rtp_udp (port:1234) could not get socket");
    return;
  }
  ESP_LOGI(TAG, "rtp_udp listen on (port:1234)");
  sip_ = new sip::Sip();
  sip_->init(sip_ip_, sip_port_, my_ip_, sip_port_, sip_user_, sip_pass_);

  if (init_i2s_mic() != 0) {
    ESP_LOGE(TAG, "Failed to init I2S mic");
    return;
  }
  if (init_i2s_amp() != 0) {
    ESP_LOGE(TAG, "Failed to init I2S amp");
    return;
  }

  // Initialize Opus
  // int opus_error;
  // opus_encoder_ = opus_encoder_create(8000, 1, OPUS_APPLICATION_VOIP, &opus_error);
  // if (opus_error != OPUS_OK) {
  //   ESP_LOGE(TAG, "Opus encoder init failed: %d", opus_error);
  //   return;
  // }
  // opus_encoder_ctl(opus_encoder_, OPUS_SET_BITRATE(8000));
  // opus_encoder_ctl(opus_encoder_, OPUS_SET_COMPLEXITY(0));

  // opus_decoder_ = opus_decoder_create(8000, 1, &opus_error);
  // if (opus_error != OPUS_OK) {
  //   ESP_LOGE(TAG, "Opus decoder init failed: %d", opus_error);
  //   return;
  // }
  // Initialize G.72x states
  g72x_init_state(&g72x_state_tx_);
  g72x_init_state(&g72x_state_rx_);
}

void Voip::loop() {
  if (WiFi.status() == WL_CONNECTED) {
    handle_incoming_rtp();
    handle_outgoing_rtp();
    sip_->loop();
  }
}

void Voip::dump_config() {
  ESP_LOGCONFIG(TAG, "VoIP Component");
  ESP_LOGCONFIG(TAG, "  SIP IP: %s", sip_ip_.c_str());
  ESP_LOGCONFIG(TAG, "  SIP User: %s", sip_user_.c_str());
  ESP_LOGCONFIG(TAG, "  Codec: %d", codec_type_);
}

void Voip::init(const std::string &sip_ip, const std::string &sip_user, const std::string &sip_pass) {
  sip_ip_ = sip_ip;
  sip_user_ = sip_user;
  sip_pass_ = sip_pass;
}

void Voip::dial(const std::string &number, const std::string &id) {
  ESP_LOGI(TAG, "Dialing %s", number.c_str());
  start_i2s();
  rx_stream_is_running_ = true;
  sip_->dial(number, id);
}

bool Voip::is_busy() {
  return sip_->is_busy();
}

void Voip::hangup() {
  sip_->hangup();
}

void Voip::set_codec(int codec) {
  codec_type_ = codec;
  sip_->set_codec(codec);
}

void Voip::set_mic_i2s_config(int bck_pin, int ws_pin, int data_pin, int bits, int format, int buf_count, int buf_len) {
  mic_bck_pin_ = bck_pin;
  mic_ws_pin_ = ws_pin;
  mic_data_pin_ = data_pin;
  mic_bits_ = bits;
  mic_format_ = format;
  mic_buf_count_ = buf_count;
  mic_buf_len_ = buf_len;
}

void Voip::set_amp_i2s_config(int bck_pin, int ws_pin, int data_pin, int bits, int format, int buf_count, int buf_len) {
  amp_bck_pin_ = bck_pin;
  amp_ws_pin_ = ws_pin;
  amp_data_pin_ = data_pin;
  amp_bits_ = bits;
  amp_format_ = format;
  amp_buf_count_ = buf_count;
  amp_buf_len_ = buf_len;
}

int Voip::init_i2s_mic() {
  esp_err_t err;
  i2s_bits_per_sample_t bits_per_sample;
  switch (mic_bits_) {
    case 16: bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT; break;
    case 24: bits_per_sample = I2S_BITS_PER_SAMPLE_24BIT; break;
    case 32: bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT; break;
    default: bits_per_sample = I2S_BITS_PER_SAMPLE_24BIT; break;
  }
  i2s_channel_fmt_t channel_format;
  switch (mic_format_) {
    case 0: channel_format = I2S_CHANNEL_FMT_ONLY_LEFT; break;
    case 1: channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT; break;
    case 2: channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT; break;
    default: channel_format = I2S_CHANNEL_FMT_ONLY_LEFT; break;
  }
  const i2s_config_t i2s_config = {
      .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = bits_per_sample,
      .channel_format = channel_format,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = mic_buf_count_,
      .dma_buf_len = mic_buf_len_
  };
  const i2s_pin_config_t pin_config = {
      .bck_io_num = mic_bck_pin_,
      .ws_io_num = mic_ws_pin_,
      .data_out_num = -1,
      .data_in_num = mic_data_pin_
  };
  err = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed installing mic driver: %d", err);
    return -1;
  }
  err = i2s_set_pin(I2S_NUM_0, &pin_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed setting mic pin: %d", err);
    return -1;
  }
  return 0;
}

int Voip::init_i2s_amp() {
  esp_err_t err;
  i2s_bits_per_sample_t bits_per_sample;
  switch (amp_bits_) {
    case 16: bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT; break;
    case 24: bits_per_sample = I2S_BITS_PER_SAMPLE_24BIT; break;
    case 32: bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT; break;
    default: bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT; break;
  }
  i2s_channel_fmt_t channel_format;
  switch (amp_format_) {
    case 0: channel_format = I2S_CHANNEL_FMT_ONLY_LEFT; break;
    case 1: channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT; break;
    case 2: channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT; break;
    default: channel_format = I2S_CHANNEL_FMT_ONLY_LEFT; break;
  }
  const i2s_config_t i2s_config = {
      .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX),
      .sample_rate = SAMPLE_RATE,
      .bits_per_sample = bits_per_sample,
      .channel_format = channel_format,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = amp_buf_count_,
      .dma_buf_len = amp_buf_len_
  };
  const i2s_pin_config_t pin_config = {
      .bck_io_num = amp_bck_pin_,
      .ws_io_num = amp_ws_pin_,
      .data_out_num = amp_data_pin_,
      .data_in_num = -1
  };
  err = i2s_driver_install(I2S_NUM_1, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed installing amp driver: %d", err);
    return -1;
  }
  err = i2s_set_pin(I2S_NUM_1, &pin_config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed setting amp pin: %d", err);
    return -1;
  }
  return 0;
}

void Voip::handle_incoming_rtp() {
  if (codec_type_ == 0) {
    // PCMU
    int16_t buffer[500];
    packet_size_ = rtp_udp_.parsePacket();
    if (packet_size_ > 0 && rx_stream_is_running_) {
      packet_size_ = rtp_udp_.read(rtp_buffer_, sizeof(rtp_buffer_));
      uint8_t *payload = rtp_buffer_ + 12; // Skip RTP header
      rtppkg_size_ = packet_size_ - 12;
      for (int i = 0; i < rtppkg_size_; i++) {
        buffer[i] = ulaw2linear(payload[i]) * amp_gain_;
      }
      size_t bytes_written;
      write_to_amp(buffer, sizeof(int16_t) * rtppkg_size_, &bytes_written);
    } else {
      rtp_udp_.flush();
    }
  } else if (codec_type_ == 1) {
    // PCMA
    int16_t buffer[500];
    packet_size_ = rtp_udp_.parsePacket();
    if (packet_size_ > 0 && rx_stream_is_running_) {
      packet_size_ = rtp_udp_.read(rtp_buffer_, sizeof(rtp_buffer_));
      uint8_t *payload = rtp_buffer_ + 12;
      rtppkg_size_ = packet_size_ - 12;
      for (int i = 0; i < rtppkg_size_; i++) {
        buffer[i] = alaw2linear(payload[i]) * amp_gain_;
      }
      size_t bytes_written;
      write_to_amp(buffer, sizeof(int16_t) * rtppkg_size_, &bytes_written);
    } else {
      rtp_udp_.flush();
    }
  // } else if (codec_type_ == 2) {
  //   // Opus
  //   int16_t decoded_samples[160];
  //   packet_size_ = rtp_udp_.parsePacket();
  //   if (packet_size_ > 0 && rx_stream_is_running_) {
  //     packet_size_ = rtp_udp_.read(rtp_buffer_, sizeof(rtp_buffer_));
  //     uint8_t *payload = rtp_buffer_ + 12;
  //     rtppkg_size_ = packet_size_ - 12;
  //     int decoded_size = opus_decode(opus_decoder_, payload, rtppkg_size_, decoded_samples, 160, 0);
  //     if (decoded_size < 0) {
  //       ESP_LOGE(TAG, "Opus decode failed: %d", decoded_size);
  //       rtp_udp_.flush();
  //       return;
  //     }
  //     for (int i = 0; i < decoded_size; i++) {
  //       decoded_samples[i] *= amp_gain_;
  //     }
  //     size_t bytes_written;
  //     write_to_amp(decoded_samples, sizeof(int16_t) * decoded_size, &bytes_written);
  //   } else {
  //     rtp_udp_.flush();
  //   }
  } else if (codec_type_ == 3) {
    // G.721
    int16_t buffer[160];
    packet_size_ = rtp_udp_.parsePacket();
    if (packet_size_ > 0 && rx_stream_is_running_) {
      packet_size_ = rtp_udp_.read(rtp_buffer_, sizeof(rtp_buffer_));
      uint8_t *payload = rtp_buffer_ + 12;
      rtppkg_size_ = packet_size_ - 12;
      for (int i = 0; i < rtppkg_size_; i++) {
        uint8_t byte = payload[i];
        int code1 = (byte >> 4) & 0xF;
        int code2 = byte & 0xF;
        buffer[i * 2] = g721_decoder(code1, AUDIO_ENCODING_LINEAR, &g72x_state_rx_) * amp_gain_;
        buffer[i * 2 + 1] = g721_decoder(code2, AUDIO_ENCODING_LINEAR, &g72x_state_rx_) * amp_gain_;
      }
      size_t bytes_written;
      write_to_amp(buffer, sizeof(int16_t) * 160, &bytes_written);
    } else {
      rtp_udp_.flush();
    }
  }
}

void Voip::handle_outgoing_rtp() {
  if (!sip_->audioport.empty() && !tx_stream_is_running_) {
    tx_stream_is_running_ = true;
    ESP_LOGI(TAG, "Starting RTP stream");
    tx_stream_ticker_.attach_ms(20, std::bind(&Voip::tx_rtp_static, this));
  } else if (sip_->audioport.empty() && tx_stream_is_running_) {
    tx_stream_is_running_ = false;
    rx_stream_is_running_ = false;
    rtppkg_size_ = -1;
    ESP_LOGI(TAG, "RTP stream stopped");
    tx_stream_ticker_.detach();
    stop_i2s();
  }
}

void Voip::tx_rtp_static(Voip *instance) {
  instance->tx_rtp();
}

void Voip::tx_rtp() {
  static uint16_t sequence_number = 0;
  static uint32_t timestamp = 0;
  const uint32_t ssrc = 3124150;
  uint8_t packet_buffer[255];

  if (codec_type_ == 0) {
    // PCMU
    uint8_t temp[160];
    for (int i = 0; i < 160; i++) {
      SAMPLE_T sample = 0;
      size_t bytes_read;
      read_from_mic(&sample, sizeof(SAMPLE_T), &bytes_read);
      if (bytes_read > 0) {
        temp[i] = linear2ulaw(MIC_CONVERT(sample) * mic_gain_);
      }
    }
    uint8_t *rtp_header = packet_buffer;
    rtp_header[0] = 0x80;
    rtp_header[1] = 0;
    *(uint16_t *)(rtp_header + 2) = htons(sequence_number++);
    *(uint32_t *)(rtp_header + 4) = htonl(timestamp += 160);
    *(uint32_t *)(rtp_header + 8) = htonl(ssrc);
    memcpy(rtp_header + 12, temp, 160);
    IPAddress ip;
    ip.fromString(sip_->get_sip_server_ip().c_str());
    rtp_udp_.beginPacket(ip, atoi(sip_->audioport.c_str()));
    rtp_udp_.write(packet_buffer, 12 + 160);
    rtp_udp_.endPacket();
  } else if (codec_type_ == 1) {
    // PCMA
    uint8_t temp[160];
    for (int i = 0; i < 160; i++) {
      SAMPLE_T sample = 0;
      size_t bytes_read;
      read_from_mic(&sample, sizeof(SAMPLE_T), &bytes_read);
      if (bytes_read > 0) {
        temp[i] = linear2alaw(MIC_CONVERT(sample) * mic_gain_);
      }
    }
    uint8_t *rtp_header = packet_buffer;
    rtp_header[0] = 0x80;
    rtp_header[1] = 8;
    *(uint16_t *)(rtp_header + 2) = htons(sequence_number++);
    *(uint32_t *)(rtp_header + 4) = htonl(timestamp += 160);
    *(uint32_t *)(rtp_header + 8) = htonl(ssrc);
    memcpy(rtp_header + 12, temp, 160);
    IPAddress ip;
    ip.fromString(sip_->get_sip_server_ip().c_str());
    rtp_udp_.beginPacket(ip, atoi(sip_->audioport.c_str()));
    rtp_udp_.write(packet_buffer, 12 + 160);
    rtp_udp_.endPacket();
  // } else if (codec_type_ == 2) {
  //   // Opus
  //   int16_t samples[160];
  //   for (int i = 0; i < 160; i++) {
  //     SAMPLE_T sample = 0;
  //     size_t bytes_read;
  //     read_from_mic(&sample, sizeof(SAMPLE_T), &bytes_read);
  //     if (bytes_read > 0) {
  //       samples[i] = MIC_CONVERT(sample) * mic_gain_;
  //     }
  //   }
  //   int encoded_size = opus_encode(opus_encoder_, samples, 160, opus_buffer_, sizeof(opus_buffer_));
  //   if (encoded_size < 0) {
  //     ESP_LOGE(TAG, "Opus encode failed: %d", encoded_size);
  //     return;
  //   }
  //   uint8_t *rtp_header = packet_buffer;
  //   rtp_header[0] = 0x80;
  //   rtp_header[1] = 120;
  //   *(uint16_t *)(rtp_header + 2) = htons(sequence_number++);
  //   *(uint32_t *)(rtp_header + 4) = htonl(timestamp += 160);
  //   *(uint32_t *)(rtp_header + 8) = htonl(ssrc);
  //   memcpy(rtp_header + 12, opus_buffer_, encoded_size);
  //   IPAddress ip;
  //   ip.fromString(sip_->get_sip_server_ip().c_str());
  //   rtp_udp_.beginPacket(ip, atoi(sip_->audioport.c_str()));
  //   rtp_udp_.write(packet_buffer, 12 + encoded_size);
  //   rtp_udp_.endPacket();
  } else if (codec_type_ == 3) {
    // G.721
    uint8_t temp[80];
    for (int i = 0; i < 160; i++) {
      SAMPLE_T sample = 0;
      size_t bytes_read;
      read_from_mic(&sample, sizeof(SAMPLE_T), &bytes_read);
      if (bytes_read > 0) {
        int code = g721_encoder(MIC_CONVERT(sample) * mic_gain_, AUDIO_ENCODING_LINEAR, &g72x_state_tx_);
        if (i % 2 == 0) {
          temp[i / 2] = (code & 0xF) << 4;
        } else {
          temp[i / 2] |= (code & 0xF);
        }
      }
    }
    uint8_t *rtp_header = packet_buffer;
    rtp_header[0] = 0x80;
    rtp_header[1] = 18;
    *(uint16_t *)(rtp_header + 2) = htons(sequence_number++);
    *(uint32_t *)(rtp_header + 4) = htonl(timestamp += 160);
    *(uint32_t *)(rtp_header + 8) = htonl(ssrc);
    memcpy(rtp_header + 12, temp, 80);
    IPAddress ip;
    ip.fromString(sip_->get_sip_server_ip().c_str());
    rtp_udp_.beginPacket(ip, atoi(sip_->audioport.c_str()));
    rtp_udp_.write(packet_buffer, 12 + 80);
    rtp_udp_.endPacket();
  }
}

void Voip::start_i2s() {
  i2s_start(I2S_NUM_1);
  ESP_LOGI(TAG, "I2S amp started");
}

void Voip::stop_i2s() {
  i2s_stop(I2S_NUM_0);
  i2s_stop(I2S_NUM_1);
  ESP_LOGI(TAG, "I2S stopped");
}

esp_err_t Voip::read_from_mic(void *dest, size_t size, size_t *bytes_read) {
  return i2s_read(I2S_NUM_0, dest, size, bytes_read, portMAX_DELAY);
}

esp_err_t Voip::write_to_amp(const void *src, size_t size, size_t *bytes_written) {
  return i2s_write(I2S_NUM_1, src, size, bytes_written, portMAX_DELAY);
}

}  // namespace voip
}  // namespace esphome