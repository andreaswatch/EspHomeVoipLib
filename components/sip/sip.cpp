#include "sip.h"
#include <MD5Builder.h>
#include <cstring>
#include <cstdarg>

namespace esphome {
namespace sip {

Sip::Sip() : p_buf_(nullptr), l_buf_(2048), i_last_cseq_(0), codec_(1) {
  p_buf_ = new char[l_buf_];
  p_dial_nr_ = "";
  p_dial_desc_ = "";
  audioport = "";
}

void Sip::setup() {
  // Initialization if needed
}

void Sip::loop() {
  handle_udp_packet();
}

void Sip::dump_config() {
  ESP_LOGCONFIG(TAG, "SIP Component");
  ESP_LOGCONFIG(TAG, "  SIP Server: %s:%d", p_sip_ip_.c_str(), i_sip_port_);
  ESP_LOGCONFIG(TAG, "  User: %s", p_sip_user_.c_str());
}

void Sip::init(const std::string &sip_ip, int sip_port, const std::string &my_ip, int my_port, const std::string &sip_user, const std::string &sip_pass) {
  udp_.begin(sip_port);
  memset(ca_read_, 0, sizeof(ca_read_));
  memset(p_buf_, 0, l_buf_);
  p_sip_ip_ = sip_ip;
  i_sip_port_ = sip_port;
  p_sip_user_ = sip_user;
  p_sip_pass_ = sip_pass;
  p_my_ip_ = my_ip;
  i_my_port_ = my_port;
  i_auth_cnt_ = 0;
  i_ring_time_ = 0;
}

bool Sip::dial(const std::string &dial_nr, const std::string &dial_desc) {
  if (i_ring_time_)
    return false;

  ESP_LOGD(TAG, "Dialing %s", dial_nr.c_str());
  audioport = "";
  i_dial_retries_ = 0;
  p_dial_nr_ = dial_nr;
  p_dial_desc_ = dial_desc;
  invite();
  i_dial_retries_++;
  i_ring_time_ = millis();
  return true;
}

void Sip::cancel(int cseq) {
  if (ca_read_[0] == 0)
    return;
  memset(p_buf_, 0, l_buf_);
  add_sip_line("%s sip:%s@%s SIP/2.0", "CANCEL", p_dial_nr_.c_str(), p_sip_ip_.c_str());
  add_sip_line("%s", ca_read_);
  add_sip_line("CSeq: %i %s", cseq, "CANCEL");
  add_sip_line("Max-Forwards: 70");
  add_sip_line("User-Agent: sip-client/0.0.1");
  add_sip_line("Content-Length: 0");
  add_sip_line("");
  send_udp();
}

void Sip::bye(int cseq) {
  audioport = "";
  if (ca_read_[0] == 0)
    return;
  memset(p_buf_, 0, l_buf_);
  add_sip_line("%s sip:%s@%s SIP/2.0", "BYE", p_dial_nr_.c_str(), p_sip_ip_.c_str());
  add_sip_line("%s", ca_read_);
  add_sip_line("CSeq: %i %s", cseq, "BYE");
  add_sip_line("Max-Forwards: 70");
  add_sip_line("User-Agent: sip-client/0.0.1");
  add_sip_line("Content-Length: 0");
  add_sip_line("");
  send_udp();
}

void Sip::ack(const char *p) {
  std::string ca;
  bool b = parse_parameter(ca, "To: <", p, '>');
  if (!b)
    return;

  memset(p_buf_, 0, l_buf_);
  add_sip_line("ACK %s SIP/2.0", ca.c_str());
  add_copy_sip_line(p, "Call-ID: ");
  int cseq = grep_integer(p, "\nCSeq: ");
  add_sip_line("CSeq: %i ACK", cseq);
  add_copy_sip_line(p, "From: ");
  add_copy_sip_line(p, "Via: ");
  add_copy_sip_line(p, "To: ");
  add_sip_line("Content-Length: 0");
  add_sip_line("");
  send_udp();
}

void Sip::ok(const char *p) {
  memset(p_buf_, 0, l_buf_);
  add_sip_line("SIP/2.0 200 OK");
  add_copy_sip_line(p, "Call-ID: ");
  add_copy_sip_line(p, "CSeq: ");
  add_copy_sip_line(p, "From: ");
  add_copy_sip_line(p, "Via: ");
  add_copy_sip_line(p, "To: ");
  add_sip_line("Content-Length: 0");
  add_sip_line("");
  send_udp();
}

void Sip::invite(const char *p) {
  // prevent loops
  if (p && i_auth_cnt_ > 3)
    return;

  // using caRead for temp. store realm and nonce
  char *ca_realm = ca_read_;
  char *ca_nonce = ca_read_ + 128;

  char *ha_resp = nullptr;
  int cseq = 1;
  if (!p) {
    i_auth_cnt_ = 0;
    if (i_dial_retries_ == 0) {
      callid_ = random();
      tagid_ = random();
      branchid_ = random();
    }
  } else {
    cseq = 2;
    if (parse_parameter(std::string(ca_realm), " realm=\"", p) &&
        parse_parameter(std::string(ca_nonce), " nonce=\"", p)) {
      // using output buffer to build the md5 hashes
      // store the md5 haResp to end of buffer
      char *ha1_hex = p_buf_;
      char *ha2_hex = p_buf_ + 33;
      ha_resp = p_buf_ + l_buf_ - 34;
      char *p_temp = p_buf_ + 66;

      snprintf(p_temp, l_buf_ - 100, "%s:%s:%s", p_sip_user_.c_str(), ca_realm, p_sip_pass_.c_str());
      make_md5_digest(ha1_hex, p_temp);

      snprintf(p_temp, l_buf_ - 100, "INVITE:sip:%s@%s", p_dial_nr_.c_str(), p_sip_ip_.c_str());
      make_md5_digest(ha2_hex, p_temp);

      snprintf(p_temp, l_buf_ - 100, "%s:%s:%s", ha1_hex, ca_nonce, ha2_hex);
      make_md5_digest(ha_resp, p_temp);
    } else {
      ca_read_[0] = 0;
      return;
    }
  }
  memset(p_buf_, 0, l_buf_);
  add_sip_line("INVITE sip:%s@%s SIP/2.0", p_dial_nr_.c_str(), p_sip_ip_.c_str());
  add_sip_line("Call-ID: %010u@%s", callid_, p_my_ip_.c_str());
  add_sip_line("CSeq: %i INVITE", cseq);
  add_sip_line("Max-Forwards: 70");
  add_sip_line("User-Agent: sip-client/0.0.1");
  add_sip_line("From: \"%s\"  <sip:%s@%s>;tag=%010u", p_dial_desc_.c_str(), p_sip_user_.c_str(), p_sip_ip_.c_str(), tagid_);
  add_sip_line("Via: SIP/2.0/UDP %s:%i;branch=%010u;rport=%i", p_my_ip_.c_str(), i_my_port_, branchid_, i_my_port_);
  add_sip_line("To: <sip:%s@%s>", p_dial_nr_.c_str(), p_sip_ip_.c_str());
  add_sip_line("Contact: \"%s\" <sip:%s@%s:%i;transport=udp>", p_sip_user_.c_str(), p_sip_user_.c_str(), p_my_ip_.c_str(), i_my_port_);
  if (p) {
    // authentication
    add_sip_line("Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"sip:%s@%s\", response=\"%s\"", p_sip_user_.c_str(), ca_realm, ca_nonce, p_dial_nr_.c_str(), p_sip_ip_.c_str(), ha_resp);
    i_auth_cnt_++;
  }
  add_sip_line("Content-Type: application/sdp");
  add_sip_line("Allow: INVITE, ACK, CANCEL, OPTIONS, BYE, REFER, NOTIFY, MESSAGE, SUBSCRIBE, INFO");
  add_sip_line("Content-Length: %d", 95 + p_my_ip_.length() + p_my_ip_.length());
  add_sip_line("");
  add_sip_line("v=0");
  add_sip_line("o=- 0 4 IN IP4 %s", p_my_ip_.c_str());
  add_sip_line("s=sipcall");
  add_sip_line("c=IN IP4 %s", p_my_ip_.c_str());
  add_sip_line("t=0 0");
  if (codec_ == 0) {
    add_sip_line("m=audio 1234 RTP/AVP 0");
    add_sip_line("a=rtpmap:0 PCMU/8000");
  } else if (codec_ == 1) {
    add_sip_line("m=audio 1234 RTP/AVP 8");
    add_sip_line("a=rtpmap:8 PCMA/8000");
  // } else {
  //   add_sip_line("m=audio 1234 RTP/AVP 120");
  //   add_sip_line("a=rtpmap:120 opus/8000/1");
  //   add_sip_line("a=fmtp:120 maxplaybackrate=8000; sprop-maxcapturerate=8000; maxaveragebitrate=16000");
  }
  ca_read_[0] = 0;
  ESP_LOGD(TAG, "Sending INVITE");
  send_udp();
}

void Sip::add_sip_line(const char *const_format, ...) {
  va_list arglist;
  va_start(arglist, const_format);
  uint16_t l = (uint16_t)strlen(p_buf_);
  char *p = p_buf_ + l;
  vsnprintf(p, l_buf_ - l, const_format, arglist);
  va_end(arglist);
  l = (uint16_t)strlen(p_buf_);
  if (l < (l_buf_ - 2)) {
    p_buf_[l] = '\r';
    p_buf_[l + 1] = '\n';
    p_buf_[l + 2] = 0;
  }
}

bool Sip::parse_parameter(std::string &dest, const char *name, const char *line, char cq) {
  const char *qp;
  const char *r;
  if ((r = strstr(line, name)) != NULL) {
    r = r + strlen(name);
    qp = strchr(r, cq);
    int l = qp - r;
    if (l > 0) {
      dest.assign(r, l);
      return true;
    }
  }
  return false;
}

bool Sip::add_copy_sip_line(const char *p, const char *psearch) {
  char *pa = strstr((char *)p, psearch);
  if (pa) {
    char *pe = strstr(pa, "\r");
    if (pe == 0)
      pe = strstr(pa, "\n");
    if (pe > pa) {
      char c = *pe;
      *pe = 0;
      add_sip_line("%s", pa);
      *pe = c;
      return true;
    }
  }
  return false;
}

int Sip::grep_integer(const char *p, const char *psearch) {
  int param = -1;
  const char *pc = strstr(p, psearch);
  if (pc) {
    param = atoi(pc + strlen(psearch));
  }
  return param;
}

bool Sip::parse_return_params(const char *p) {
  memset(p_buf_, 0, l_buf_);
  add_copy_sip_line(p, "Call-ID: ");
  add_copy_sip_line(p, "From: ");
  add_copy_sip_line(p, "Via: ");
  add_copy_sip_line(p, "To: ");
  if (strlen(p_buf_) >= 2) {
    strcpy(ca_read_, p_buf_);
    ca_read_[strlen(ca_read_) - 2] = 0;
  }
  return true;
}

void Sip::handle_udp_packet() {
  char *p;
  char ca_sip_in[2048];
  int packet_size = udp_.parsePacket();
  if (packet_size > 0) {
    ESP_LOGD(TAG, "Received SIP packet from %s:%d size %d", udp_.remoteIP().toString().c_str(), udp_.remotePort(), packet_size);
    ca_sip_in[0] = 0;
    packet_size = udp_.read(ca_sip_in, sizeof(ca_sip_in));
    if (packet_size > 0) {
      ca_sip_in[packet_size] = 0;
      ESP_LOGD(TAG, "Response status: %s", strstr(ca_sip_in, "SIP/2.0") ? strstr(ca_sip_in, "SIP/2.0") : "No SIP/2.0");
    }
  }
  if (packet_size > 0) {
    p = ca_sip_in;
  } else {
    p = nullptr;
  }

  if (!p) {
    // max 5 dial retry when loos first invite packet
    if (i_auth_cnt_ == 0 && i_dial_retries_ < 5 && i_ring_time_ && (millis() - i_ring_time_) > (i_dial_retries_ * 200)) {
      i_dial_retries_++;
      delay(30);
      invite();
    }
    return;
  }

  if (strstr(p, "SIP/2.0 401 Unauthorized") == p) {
    ESP_LOGD(TAG, "SIP/2.0 401 Unauthorized received");
    ack(p);
    // call Invite with response data (p) to build auth md5 hashes
    invite(p);
  } else if (strstr(p, "BYE") == p) {
    ESP_LOGD(TAG, "SIP/BYE received");
    audioport = "";
    ok(p);
    i_ring_time_ = 0;
  } else if (strstr(p, "SIP/2.0 200") == p) {  // OK
    ESP_LOGD(TAG, "SIP/2.0 200 OK received");
    parse_return_params(p);
    ack(p);
  } else if (strstr(p, "SIP/2.0 183 ") == p  // Session Progress
             || strstr(p, "SIP/2.0 180 ") == p) {  // Ringing
    //
    // Determine the audio port of the SIP server (Fritzbox RTP port)
    //
    // for example:
    // m=audio 7078 RTP/AVP 120
    //
    // Todo: sscanf maybe a better method to do this
    //
    ESP_LOGD(TAG, "SIP/2.0 183 or 180 received");
    char *sdpportptr;
    if (codec_ == 0) {
      sdpportptr = strstr(p, " RTP/AVP 0");
      if (sdpportptr == NULL) {
        ESP_LOGD(TAG, "RTP/AVP 0 not found");
        audioport = "";
        return;
      } else {
        ESP_LOGD(TAG, "RTP/AVP 0 found");
      }
    } else if (codec_ == 1) {
      sdpportptr = strstr(p, " RTP/AVP 8");
      if (sdpportptr == NULL) {
        ESP_LOGD(TAG, "RTP/AVP 8 not found");
        audioport = "";
        return;
      } else {
        ESP_LOGD(TAG, "RTP/AVP 8 found");
      }
    // } else {
    //   sdpportptr = strstr(p, " RTP/AVP 120");
    //   if (sdpportptr == NULL) {
    //     ESP_LOGD(TAG, "RTP/AVP 120 not found");
    //     audioport = "";
    //     return;
    //   } else {
    //     ESP_LOGD(TAG, "RTP/AVP 120 found");
    //   }
    // }
    sdpportptr--;
    int i = 0;
    while (*sdpportptr != ' ' || i > 8) {
      i++;
      sdpportptr--;
    }
    sdpportptr++;
    if (i < 7) {
      i = 0;
      while (*sdpportptr != ' ') {
        audioport += *sdpportptr;
        sdpportptr++;
        i++;
      }
      ESP_LOGD(TAG, "Audio Port: %s", audioport.c_str());
    }
    parse_return_params(p);
  } else if (strstr(p, "SIP/2.0 100 ") == p) {  // Trying
    parse_return_params(p);
    ack(p);
  } else if (strstr(p, "SIP/2.0 486 ") == p  // Busy Here
             || strstr(p, "SIP/2.0 603 ") == p  // Decline
             || strstr(p, "SIP/2.0 487 ") == p) {  // Request Terminated
    ack(p);
    i_ring_time_ = 0;
  } else if (strstr(p, "INFO") == p) {
    i_last_cseq_ = grep_integer(p, "\nCSeq: ");
    ok(p);
  }
}

int Sip::send_udp() {
  ESP_LOGD(TAG, "Sending SIP packet to %s:%d", p_sip_ip_.c_str(), i_sip_port_);
  udp_.beginPacket(p_sip_ip_.c_str(), i_sip_port_);
  udp_.write((uint8_t *)p_buf_, strlen(p_buf_));
  udp_.endPacket();
  return 0;
}

uint32_t Sip::random() {
  return esp_random();
}

uint32_t Sip::millis() {
  return (uint32_t)::millis() + 1;
}

void Sip::make_md5_digest(char *p_out_hex33, char *p_in) {
  MD5Builder a_md5;
  a_md5.begin();
  a_md5.add(p_in);
  a_md5.calculate();
  a_md5.getChars(p_out_hex33);
}

void Sip::hangup() {
  i_last_cseq_ = 3;  // Set to next CSeq after INVITE (1 or 2)
  bye(i_last_cseq_);
}

}  // namespace sip
}  // namespace esphome