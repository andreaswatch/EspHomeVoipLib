#ifndef ESPHOME_SIP_H
#define ESPHOME_SIP_H

#include "esphome.h"
#include <WiFiUdp.h>

namespace esphome {
namespace sip {

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
  WiFiUDP udp_;
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
  int codec_;  // 0 = G711, 1 = Opus

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

  uint32_t millis();
  uint32_t random();
  int send_udp();
  void make_md5_digest(char *p_out_hex33, char *p_in);
};

}  // namespace sip
}  // namespace esphome

#endif