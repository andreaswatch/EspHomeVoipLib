#ifndef ESPHOME_VOIP_H
#define ESPHOME_VOIP_H

#include "esphome.h"
#include <driver/i2s_std.h>
#include "g711.h"
#include <memory>
#include <string>
#include <vector>
#include "esphome/components/socket/socket.h"
#include "esphome/components/i2s_audio/microphone/i2s_audio_microphone.h"
#include "esphome/components/i2s_audio/speaker/i2s_audio_speaker.h"
#include "esphome/core/scheduler.h"
#include <chrono>
#include "esphome/core/defines.h"
// Removed include of automation.h here to avoid circular include - automation.h includes voip.h
#include <esphome/core/defines.h>
// Output forward declaration removed - VoIP no longer controls PA output directly

namespace esphome {
namespace voip {

class Sip : public Component {
 public:
  Sip();
  ~Sip();
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
  ::std::unique_ptr<socket::Socket> udp_;
  char packetBuffer[1024];
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
  // For qop=auth support
  std::string cnonce_;
  std::string last_nonce_;
  uint32_t auth_nc_ = 0;
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
  void start_component();
  void finish_start_component();
  void stop_component();
  void set_mic_gain(int gain) { mic_gain_ = gain; }
  void set_amp_gain(int gain) { amp_gain_ = gain; }
  void set_mic(i2s_audio::I2SAudioMicrophone *mic) { microphone_ = mic; }
  void set_speaker(i2s_audio::I2SAudioSpeaker *speaker) { speaker_ = speaker; }
  // ready sensor removed - use on_ready/on_not_ready automation events instead
  // Automation register methods - exposed publicly for automation triggers
  void add_on_ringing_callback(std::function<void()> &&cb) { on_ringing_callbacks_.push_back(std::move(cb)); }
  void add_on_call_established_callback(std::function<void()> &&cb) { on_call_established_callbacks_.push_back(std::move(cb)); }
  void add_on_call_ended_callback(std::function<void()> &&cb) { on_call_ended_callbacks_.push_back(std::move(cb)); }
  void add_on_ready_callback(std::function<void()> &&cb) { on_ready_callbacks_.push_back(std::move(cb)); }
  void add_on_not_ready_callback(std::function<void()> &&cb) { on_not_ready_callbacks_.push_back(std::move(cb)); }
  void set_start_on_boot(bool v) { start_on_boot_ = v; }
  void record_and_playback_1s();
  void play_beep_ms(int duration_ms);
  // default dial number removed from API

  i2s_audio::I2SAudioMicrophone *microphone_ = nullptr;
  i2s_audio::I2SAudioSpeaker *speaker_ = nullptr;
  // removed ready_sensor_ (exposed via automation events now)
  bool last_hw_ready_ = false;

 protected:
  Sip *sip_ = nullptr;
  ::std::unique_ptr<socket::Socket> rtp_udp_;
  char rtpPacketBuffer[1024];
  bool tx_stream_is_running_ = false;
  bool rx_stream_is_running_ = false;
  int rtppkg_size_ = -1;
  // signed on purpose: will be -1 on recv error; avoid unsigned which hides errors
  int packet_size_;
  uint8_t rtp_buffer_[2048];
  int codec_type_ = 1;
  int mic_gain_ = MIC_GAIN_DEFAULT;
  int amp_gain_ = AMP_GAIN_DEFAULT;
  int sip_port_ = 5060;
  std::string my_ip_;
  std::string sip_ip_;
  std::string sip_user_;
  std::string sip_pass_;
  std::vector<uint8_t> mic_buffer_;
  // default_dial_number_ removed
  bool started_ = false;
  bool start_pending_ = false;
  int start_retries_ = 0;
  bool start_on_boot_ = false;
  // internal state tracking for automations
  bool last_sip_busy_ = false;
  bool last_tx_stream_is_running_ = false;
  // recording buffer and flag for record-and-play-back button
  std::vector<uint8_t> record_buffer_{};
  bool is_recording_ = false;
  // Automation callbacks
  std::vector<std::function<void()>> on_ringing_callbacks_{};
  std::vector<std::function<void()>> on_call_established_callbacks_{};
  std::vector<std::function<void()>> on_call_ended_callbacks_{};
  std::vector<std::function<void()>> on_ready_callbacks_{};
  std::vector<std::function<void()>> on_not_ready_callbacks_{};
  void mic_data_callback(const std::vector<uint8_t> &data);
  void handle_incoming_rtp();
  void handle_outgoing_rtp();
  void tx_rtp();

  // Duplicate automation registration methods removed (they are public now)

  // Notify helpers (implemented in voip.cpp)
  void notify_ringing();
  void notify_call_established();
  void notify_call_ended();
  void notify_ready();
  void notify_not_ready();

};  // class Voip
}  // namespace voip
}  // namespace esphome

#endif