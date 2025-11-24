#ifndef ESPHOME_VOIP_H
#define ESPHOME_VOIP_H

#include "esphome.h"
#include <driver/i2s.h>
#include "esphome/core/network.h"
#include "esphome/core/scheduler.h"
#include "esphome/components/i2s_audio/i2s_audio.h"
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
  void set_mic(i2s_audio::I2SAudioMicrophone *mic) { microphone_ = mic; }
  void set_speaker(i2s_audio::I2SAudioSpeaker *speaker) { speaker_ = speaker; }

 protected:
  Sip *sip_;
  network::UdpSocket rtp_udp_;
  esphome::scheduler::IntervalHandle tx_interval_;
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
  std::string mic_id_;
  std::string speaker_id_;
  i2s_audio::I2SAudioMicrophone *microphone_;
  i2s_audio::I2SAudioSpeaker *speaker_;

  void handle_incoming_rtp();
  void handle_outgoing_rtp();
  void tx_rtp();
};

}  // namespace voip
}  // namespace esphome

#endif