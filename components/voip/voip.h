#ifndef ESPHOME_VOIP_H
#define ESPHOME_VOIP_H

#include "esphome.h"
#include <driver/i2s_std.h>
#include "g711.h"

namespace esphome {
namespace voip {

class Sip : public Component {
 public:
  Sip();
  void setup() override;
  void loop() override;
  void dump_config() override;

  void init(const std::string &sip_ip, int sip_port, const std::string &my_ip, int my_port, const std::string &sip_user, const std::string &sip_pass);
  bool dial(const std::string &dial_nr, const std::string &dial_desc = "");
  bool is_busy() { return i_ring_time_ != 0; }
  void hangup();
  const std::string &get_sip_server_ip() { return p_sip_ip_; }
  void set_codec(int codec) { codec_ = codec; }
  std::string audioport;

 protected:
  network::UdpSocket udp_;
  char *p_buf_;
  size_t l_buf_;
  char ca_read_[256];

  std::string p_sip_ip_;
  int i_sip_port_;
  std::string p_sip_user_;
  std::string p_sip_pass_;
  std::string p_my_ip_;
  int i_my_port_;
  std::string p_dial_nr_;
  std::string p_dial_desc_;

  uint32_t callid_;
  uint32_t tagid_;
  uint32_t branchid_;

  int i_auth_cnt_;
  uint32_t i_ring_time_;
  uint32_t i_max_time_;
  int i_dial_retries_;
  int i_last_cseq_;
  int codec_;  // 0 = G711 PCMU, 1 = G711 PCMA, 2 = G.721

  void add_sip_line(const char *const_format, ...);
  bool add_copy_sip_line(const char *p, const char *psearch);
  bool parse_parameter(std::string &dest, const char *name, const char *line, char cq = '\"');
  bool parse_return_params(const char *p);
  int grep_integer(const char *p, const char *psearch);
  void ack(const char *p_in);
  void cancel(int seqn);
  void bye(int cseq);
  void ok(const char *p_in);
  void invite(const char *p_in = nullptr);
  void handle_udp_packet();

  uint32_t millis();
  uint32_t random();
  int send_udp();
  void make_md5_digest(char *p_out_hex33, char *p_in);
};

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
  Sip *sip_;
  network::UdpSocket rtp_udp_;
  scheduler::IntervalHandle tx_interval_;
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
  // OpusEncoder *opus_encoder_ = nullptr;
  // OpusDecoder *opus_decoder_ = nullptr;
  uint8_t opus_buffer_[256];

  void handle_incoming_rtp();
  void handle_outgoing_rtp();
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