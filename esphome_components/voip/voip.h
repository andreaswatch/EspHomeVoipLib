#ifndef ESPHOME_VOIP_H
#define ESPHOME_VOIP_H

#include "esphome.h"
#include "sip.h"
#include <WiFiUdp.h>
#include <Ticker.h>
#include <driver/i2s.h>
#include <g711.h>
#include <g72x.h>
#include <opus.h>

namespace esphome {
namespace voip {

#define MIC_BITS 24
#define MIC_GAIN_DEFAULT 2
#define AMP_GAIN_DEFAULT 6
#define SAMPLE_RATE 8000
#define SAMPLE_BITS 24
#define SAMPLE_T int32_t
#define MIC_CONVERT(s) ((s >> (SAMPLE_BITS - MIC_BITS)) / 2048)
#define DAC_CONVERT(s) ((s >> (SAMPLE_BITS - MIC_BITS)) / 65536)

class Voip : public Component {
 public:
  Voip();
  ~Voip();
  void setup() override;
  void loop() override;
  void dump_config() override;

  void init(const std::string &sip_ip, const std::string &sip_user, const std::string &sip_pass);
  void dial(const std::string &number, const std::string &id);
  bool is_busy();
  void hangup();
  void set_codec(int codec);
  void set_mic_gain(int gain) { mic_gain_ = gain; }
  void set_amp_gain(int gain) { amp_gain_ = gain; }
  void set_mic_i2s_config(int bck_pin, int ws_pin, int data_pin, int bits, int format, int buf_count, int buf_len);
  void set_amp_i2s_config(int bck_pin, int ws_pin, int data_pin, int bits, int format, int buf_count, int buf_len);

 protected:
  sip::Sip *sip_;
  WiFiUDP rtp_udp_;
  Ticker tx_stream_ticker_;
  bool tx_stream_is_running_ = false;
  bool rx_stream_is_running_ = false;
  int rtppkg_size_ = -1;
  size_t packet_size_;
  uint8_t rtp_buffer_[2048];
  int codec_type_ = 1;
  int mic_gain_ = MIC_GAIN_DEFAULT;
  int amp_gain_ = AMP_GAIN_DEFAULT;
  int sip_port_ = 5060;
  std::string my_ip_;
  std::string sip_ip_;
  std::string sip_user_;
  std::string sip_pass_;
  // I2S configs
  int mic_bck_pin_ = 26;
  int mic_ws_pin_ = 25;
  int mic_data_pin_ = 33;
  int mic_bits_ = 24;
  int mic_format_ = 0;
  int mic_buf_count_ = 4;
  int mic_buf_len_ = 8;
  int amp_bck_pin_ = 14;
  int amp_ws_pin_ = 12;
  int amp_data_pin_ = 27;
  int amp_bits_ = 16;
  int amp_format_ = 0;
  int amp_buf_count_ = 16;
  int amp_buf_len_ = 60;
  // Opus
  OpusEncoder *opus_encoder_ = nullptr;
  OpusDecoder *opus_decoder_ = nullptr;
  uint8_t opus_buffer_[256];
  // G.72x
  struct g72x_state g72x_state_tx_;
  struct g72x_state g72x_state_rx_;

  void handle_incoming_rtp();
  void handle_outgoing_rtp();
  static void tx_rtp_static(Voip *instance);
  void tx_rtp();
  int init_i2s_mic();
  int init_i2s_amp();
  void start_i2s();
  void stop_i2s();
  esp_err_t read_from_mic(void *dest, size_t size, size_t *bytes_read);
  esp_err_t write_to_amp(const void *src, size_t size, size_t *bytes_written);
};

}  // namespace voip
}  // namespace esphome

#endif