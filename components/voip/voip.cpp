#include "voip.h"
#include <cstring>
#include <cstdarg>
#include <cmath>
#include <functional>
#include "esphome/core/helpers.h"
#include "esphome/core/hal.h"
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "mbedtls/md5.h"
#include "mbedtls/md.h"
#include <netinet/in.h>
#include <arpa/inet.h>
#include <algorithm>
#include <errno.h>
#include <new>
#include "md5_util.h"

// Safety helpers
static inline void safe_strncpy(char *dest, const char *src, size_t destSize) {
  if (!dest || !src || destSize == 0) return;
  strncpy(dest, src, destSize - 1);
  dest[destSize - 1] = '\0';
}
static inline size_t safe_strlen(const char *s) { return s ? strlen(s) : 0; }

namespace esphome {
namespace voip {

static const char *const TAG = "voip";
#define SIP_AUTH_DEBUG 1  // set to 1 to enable additional digest debug (DO NOT USE IN PRODUCTION)

Sip::Sip() : p_buf_(nullptr), l_buf_(2048), i_last_cseq_(0), codec_(0) {
  p_buf_ = new (std::nothrow) char[l_buf_];
  if (p_buf_ == nullptr) {
    ESP_LOGE(TAG, "Sip: Failed to allocate p_buf_");
    l_buf_ = 0;
  }
  p_dial_nr_ = "";
  p_dial_desc_ = "";
  audioport = "";
}

// Destructor will be implemented later

void Sip::init(const std::string &sip_ip, int sip_port, const std::string &my_ip, int my_port, const std::string &sip_user, const std::string &sip_pass) {
  p_sip_ip_ = sip_ip;
  i_sip_port_ = sip_port;
  p_my_ip_ = my_ip;
  i_my_port_ = my_port;
  p_sip_user_ = sip_user;
  p_sip_pass_ = sip_pass;
  i_auth_cnt_ = 0;
  i_ring_time_ = 0;
  i_max_time_ = 0;
  i_dial_retries_ = 0;
  i_last_cseq_ = 0;
  codec_ = 0;
  // create SIP socket
    this->udp_ = socket::socket(AF_INET, SOCK_DGRAM, 0);
    ESP_LOGI(TAG, "Sip::init: creating UDP socket for SIP");
  if (this->udp_ != nullptr) {
    this->udp_->setblocking(false);
    struct sockaddr_in local = {};
    local.sin_family = AF_INET;
    local.sin_port = htons(this->i_my_port_);
    local.sin_addr.s_addr = INADDR_ANY;
      if (this->udp_->bind((struct sockaddr *)&local, sizeof(local)) != 0) {
        ESP_LOGW(TAG, "Sip::init: bind failed for SIP port %d", this->i_my_port_);
      } else {
        ESP_LOGI(TAG, "Sip::init: bound SIP socket to port %d", this->i_my_port_);
      }
  } else {
    ESP_LOGW(TAG, "Sip::init: Failed to create UDP socket for SIP");
    // Notify but don't crash
  }
}

void Sip::setup() {
  // SIP-specific setup can be done here if needed
}

void Sip::loop() {
  if (this->udp_) this->handle_udp_packet();
}

void Sip::dump_config() {
  ESP_LOGCONFIG(TAG, "Sip component:");
  ESP_LOGCONFIG(TAG, "  SIP IP: %s", p_sip_ip_.c_str());
  ESP_LOGCONFIG(TAG, "  SIP Port: %d", i_sip_port_);
}

Sip::~Sip() {
  ESP_LOGI(TAG, "Sip destructor called");
  if (p_buf_) {
    delete[] p_buf_;
    p_buf_ = nullptr;
    l_buf_ = 0;
  }
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
  if (p_buf_ && l_buf_) memset(p_buf_, 0, l_buf_);
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
  if (p_buf_ && l_buf_) memset(p_buf_, 0, l_buf_);
  add_sip_line("%s sip:%s@%s SIP/2.0", "BYE", p_dial_nr_.c_str(), p_sip_ip_.c_str());
  add_sip_line("%s", ca_read_);
  add_sip_line("CSeq: %i %s", cseq, "BYE");
  add_sip_line("Max-Forwards: 70");
  add_sip_line("User-Agent: sip-client/0.0.1");
  add_sip_line("Content-Length: 0");
  add_sip_line("");
  send_udp();
}

void Sip::ack(const char *p_in) {
  std::string ca;
  bool b = parse_parameter(ca, "To: <", p_in, '>');
  if (!b)
    return;

  if (p_buf_ && l_buf_) memset(p_buf_, 0, l_buf_);
  add_sip_line("ACK %s SIP/2.0", ca.c_str());
  add_copy_sip_line(p_in, "Call-ID: ");
  int cseq = grep_integer(p_in, "\nCSeq: ");
  add_sip_line("CSeq: %i ACK", cseq);
  add_copy_sip_line(p_in, "From: ");
  add_copy_sip_line(p_in, "Via: ");
  add_copy_sip_line(p_in, "To: ");
  add_sip_line("Content-Length: 0");
  add_sip_line("");
  send_udp();
}

void Sip::ok(const char *p) {
  if (p_buf_ && l_buf_) memset(p_buf_, 0, l_buf_);
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

  // ca_read_ is a small buffer used for copying return params; we now parse
  // realm/nonce into std::string variables instead of relying on ca_read_.
  std::string realm, nonce, qop;
  bool qop_auth = false;

  char *ha_resp = nullptr;
  // local MD5 buffers allocated once per invite
  char ha1_hex[33] = {0};
  char ha2_hex[33] = {0};
  char ha_resp_local[33] = {0};
  char p_temp[256] = {0};
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
    if (parse_parameter(realm, " realm=\"", p) &&
      parse_parameter(nonce, " nonce=\"", p)) {
      (void)parse_parameter(qop, " qop=\"", p); // optional
      if (!qop.empty() && qop.find("auth") != std::string::npos) qop_auth = true;
      if (!p_buf_ || l_buf_ < 132) {
        ESP_LOGE(TAG, "Insufficient buffer for md5 digest building");
        ca_read_[0] = 0;
        return;
      }
      // compute MD5 digest values in local buffers (do not use p_buf_ as it's
      // later cleared when building the SIP packet)

      ESP_LOGD(TAG, "MD5 compute: temp buffer len=%d", (int)sizeof(p_temp));
      snprintf(p_temp, sizeof(p_temp), "%s:%s:%s", p_sip_user_.c_str(), realm.c_str(), p_sip_pass_.c_str());
      make_md5_digest(ha1_hex, p_temp);

      snprintf(p_temp, sizeof(p_temp), "INVITE:sip:%s@%s", p_dial_nr_.c_str(), p_sip_ip_.c_str());
      make_md5_digest(ha2_hex, p_temp);

      // realm and nonce are parsed into std::string variables and used below.
      if (qop_auth) {
        // ensure cnonce and nc handling
        if (last_nonce_.empty() || last_nonce_ != nonce) {
          last_nonce_ = nonce;
          auth_nc_ = 1;
          // generate cnonce using two random 32-bit values
          char cnonce_buf[33];
          snprintf(cnonce_buf, sizeof(cnonce_buf), "%08x%08x", this->random(), this->random());
          cnonce_.assign(cnonce_buf);
        } else {
          auth_nc_++;
        }
        char nc_str[9];
        snprintf(nc_str, sizeof(nc_str), "%08x", auth_nc_);
        // compute HA1:nonce:nc:cnonce:qop:HA2
        snprintf(p_temp, sizeof(p_temp), "%s:%s:%s:%s:%s:%s", ha1_hex, nonce.c_str(), nc_str, cnonce_.c_str(), "auth", ha2_hex);
        make_md5_digest(ha_resp_local, p_temp);
        ha_resp = ha_resp_local;
      } else {
        // old-style digest (no qop)
        snprintf(p_temp, sizeof(p_temp), "%s:%s:%s", ha1_hex, nonce.c_str(), ha2_hex);
        make_md5_digest(ha_resp_local, p_temp);
        ha_resp = ha_resp_local;
      }
    #if SIP_AUTH_DEBUG
      // Print only partial masked response to avoid leaking full auth response
      char res_mask[9] = {0};
      if (ha_resp) {
        strncpy(res_mask, ha_resp, 8);
        res_mask[8] = '\0';
      }
      ESP_LOGD(TAG, "SIP digest computed: realm=%s nonce=%s response[0..7]=%s cseq=%d", realm.c_str(), nonce.c_str(), res_mask, cseq);
    #endif
    } else {
      ca_read_[0] = 0;
      return;
    }
  }
  if (p_buf_ && l_buf_) memset(p_buf_, 0, l_buf_);
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
    if (qop_auth) {
      // include qop, nc, and cnonce
      char nc_str[9];
      snprintf(nc_str, sizeof(nc_str), "%08x", auth_nc_);
      add_sip_line("Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"sip:%s@%s\", response=\"%s\", qop=auth, nc=%s, cnonce=\"%s\"",
                   p_sip_user_.c_str(), realm.c_str(), nonce.c_str(), p_dial_nr_.c_str(), p_sip_ip_.c_str(), ha_resp, nc_str, cnonce_.c_str());
      #if SIP_AUTH_DEBUG
      // Mask cnonce in logs (show first 8 characters)
      char cnonce_mask[9] = {0};
      if (!cnonce_.empty()) {
        strncpy(cnonce_mask, cnonce_.c_str(), 8);
        cnonce_mask[8] = '\0';
      }
      ESP_LOGD(TAG, "Authorization (qop=auth): qop=auth, nc=%s, cnonce[0..7]=%s", nc_str, cnonce_mask);
      #endif
    } else {
      add_sip_line("Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"sip:%s@%s\", response=\"%s\"",
                   p_sip_user_.c_str(), realm.c_str(), nonce.c_str(), p_dial_nr_.c_str(), p_sip_ip_.c_str(), ha_resp);
    }
    // Do not log Authorization header to avoid leaking auth details.
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
  } else {
    add_sip_line("m=audio 1234 RTP/AVP 18");
    add_sip_line("a=rtpmap:18 G721/8000");
  }
  ca_read_[0] = 0;
  ESP_LOGD(TAG, "Sending INVITE");
  send_udp();
}

void Sip::add_sip_line(const char *const_format, ...) {
  if (!p_buf_ || l_buf_ == 0) {
    ESP_LOGW(TAG, "add_sip_line called with invalid buffer");
    return;
  }
  va_list arglist;
  va_start(arglist, const_format);
  uint16_t l = (uint16_t)safe_strlen(p_buf_);
  char *p = p_buf_ + l;
  if (l >= l_buf_) {
    // no space
    va_end(arglist);
    ESP_LOGW(TAG, "Buffer full, dropping line: %s", const_format);
    return;
  }
  size_t remain = l_buf_ - l;
  int written = vsnprintf(p, remain, const_format, arglist);
  va_end(arglist);
  // Recompute effective length after append. If truncated, we still compute current length.
  l = (uint16_t)safe_strlen(p_buf_);
  // Add CRLF only if there is at least room for CRLF and null terminator.
  if (l <= (l_buf_ - 3)) {
    p_buf_[l] = '\r';
    p_buf_[l + 1] = '\n';
    p_buf_[l + 2] = 0;
  }
  if (written < 0) {
    ESP_LOGW(TAG, "vsnprintf failed or truncated output");
  } else if ((size_t)written >= remain) {
    // truncated
    ESP_LOGW(TAG, "Line truncated by vsnprintf (len=%d, remain=%u)", written, (unsigned)remain);
  }
}

bool Sip::parse_parameter(std::string &dest, const char *name, const char *line, char cq) {
  const char *qp;
  const char *r;
  if (!line || !name) return false;
  if ((r = strstr(line, name)) != NULL) {
    r = r + strlen(name);
    qp = strchr(r, cq);
    if (!qp)
      return false;
    int l = qp - r;
    if (l > 0) {
      dest.assign(r, l);
      return true;
    }
  }
  return false;
}

bool Sip::add_copy_sip_line(const char *p, const char *psearch) {
  if (!p || !psearch || !p_buf_)
    return false;
  char *pa = strstr((char *)p, psearch);
  if (pa) {
    char *pe = strstr(pa, "\r");
    if (pe == 0)
      pe = strstr(pa, "\n");
    if (pe && pe > pa) {
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
  if (!p || !psearch) return -1;
  const char *pc = strstr(p, psearch);
  if (pc) {
    param = atoi(pc + strlen(psearch));
  }
  return param;
}

bool Sip::parse_return_params(const char *p) {
  if (p_buf_ && l_buf_) memset(p_buf_, 0, l_buf_);
  add_copy_sip_line(p, "Call-ID: ");
  add_copy_sip_line(p, "From: ");
  add_copy_sip_line(p, "Via: ");
  add_copy_sip_line(p, "To: ");
  if (p_buf_ && safe_strlen(p_buf_) >= 2) {
    // copy safely to ca_read_ which has fixed small size
    strncpy(ca_read_, p_buf_, sizeof(ca_read_) - 1);
    ca_read_[sizeof(ca_read_) - 1] = '\0';
    size_t slen = strlen(ca_read_);
    if (slen >= 2) {
      ca_read_[slen - 2] = 0;  // remove trailing CRLF if present
    }
      if (safe_strlen(p_buf_) >= sizeof(ca_read_)) {
        ESP_LOGW(TAG, "parse_return_params: p_buf_ content truncated copying to ca_read_");
      }
  }
  return true;
}

void Sip::handle_udp_packet() {
  char *p;
  char ca_sip_in[2048];
  int packet_size = 0;
  struct sockaddr_in remote;
  socklen_t addrlen = sizeof(remote);
  packet_size = this->udp_->recvfrom(ca_sip_in, sizeof(ca_sip_in), (struct sockaddr *)&remote, &addrlen);
  if (packet_size > 0) {
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &remote.sin_addr, ip_str, sizeof(ip_str));
    ESP_LOGD(TAG, "Received SIP packet from %s:%d size %d", ip_str, ntohs(remote.sin_port), packet_size);
    if (packet_size >= (int)sizeof(ca_sip_in)) {
      // truncated packet, ensure last char zero
      ca_sip_in[sizeof(ca_sip_in) - 1] = 0;
      ESP_LOGW(TAG, "SIP packet truncated to %u bytes", (unsigned)sizeof(ca_sip_in));
    } else {
      ca_sip_in[packet_size] = 0;
    }
    ESP_LOGD(TAG, "Response status: %s", strstr(ca_sip_in, "SIP/2.0") ? strstr(ca_sip_in, "SIP/2.0") : "No SIP/2.0");
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
      ESP_LOGD(TAG, "Scheduling INVITE retry #%d", i_dial_retries_);
      // avoid double scheduling
      App.scheduler.cancel_timeout(this, "sip_invite_retry");
      App.scheduler.set_timeout(this, "sip_invite_retry", 30, [this]() {
        ESP_LOGD(TAG, "Running scheduled INVITE retry");
        this->invite();
      });
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
    } else {
      sdpportptr = strstr(p, " RTP/AVP 18");
      if (sdpportptr == NULL) {
        ESP_LOGD(TAG, "RTP/AVP 18 not found");
        audioport = "";
        return;
      } else {
        ESP_LOGD(TAG, "RTP/AVP 18 found");
      }
    }
    sdpportptr--;
    int i = 0;
    // walk backwards up to 8 chars or until we find a space (safely stop at start of buffer)
    while (sdpportptr > p && *sdpportptr != '\0' && *sdpportptr != ' ' && i < 8) {
      i++;
      sdpportptr--;
    }
    sdpportptr++;
    if (i < 7) {
      i = 0;
      // only copy up to 7 chars or until a space/terminator
      while (*sdpportptr != ' ' && *sdpportptr != '\0' && i < 7) {
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
  if (!p_buf_) {
    ESP_LOGE(TAG, "send_udp: p_buf_ == nullptr");
    return -1;
  }
  ESP_LOGD(TAG, "Sending SIP packet to %s:%d", p_sip_ip_.c_str(), i_sip_port_);
  // Create a redacted copy for logging so Authorization headers are not leaked
  char pbuf_masked[2048];
  size_t sl = safe_strlen(p_buf_);
  if (sl >= sizeof(pbuf_masked)) sl = sizeof(pbuf_masked) - 1;
  memcpy(pbuf_masked, p_buf_, sl);
  pbuf_masked[sl] = '\0';
  const char *auth_str = "Authorization:";
  char *pos = strstr(pbuf_masked, auth_str);
  if (pos) {
    // find end of line
    char *eol = strstr(pos, "\r\n");
    if (!eol) eol = strstr(pos, "\n");
    if (eol) {
      // replace the entire authorization header line with redaction text
      const char *redaction = "Authorization: Digest <REDACTED>";
      size_t redlen = strlen(redaction);
      size_t rest = strlen(eol);
      // move tail forward/backwards depending on length
      if (redlen <= (size_t)(eol - pos)) {
        memcpy(pos, redaction, redlen);
        // pad remainder with spaces to keep lengths same
        for (size_t i = redlen; i < (size_t)(eol - pos); ++i) pos[i] = ' ';
      } else {
        // truncated: write redaction and then shift tail if space
        memcpy(pos, redaction, redlen);
        // then write eol
        // If redlen larger than available, just truncate the buffer at pos + redlen
      }
    }
  }
  ESP_LOGD(TAG, "SIP packet content:\n%s", pbuf_masked);
  struct sockaddr_in remote = {};
  remote.sin_family = AF_INET;
  remote.sin_port = htons(i_sip_port_);
  inet_pton(AF_INET, p_sip_ip_.c_str(), &remote.sin_addr);
  if (!this->udp_) {
    ESP_LOGE(TAG, "send_udp: udp socket is null");
    return -1;
  }
  size_t len = safe_strlen(p_buf_);
  this->udp_->sendto((uint8_t *)p_buf_, len, 0, (struct sockaddr *)&remote, sizeof(remote));
  return 0;
}

uint32_t Sip::random() {
  return esp_random();
}

uint32_t Sip::millis() {
  return (uint32_t)esphome::millis() + 1;
}

void Sip::make_md5_digest(char *p_out_hex33, char *p_in) {
  if (!p_out_hex33 || !p_in) return;
  // reuse md5_hex helper to return lowercase hex string and copy to p_out
  std::string s = md5_hex(std::string(p_in));
  if (s.size() != 32) {
    // Failed - ensure zeroed string
    memset(p_out_hex33, 0, 33);
    return;
  }
  memcpy(p_out_hex33, s.c_str(), 33); // including null terminator
}

void Sip::hangup() {
  i_last_cseq_ = 3;  // Set to next CSeq after INVITE (1 or 2)
  bye(i_last_cseq_);
}

// G.711 codec implementations
#define SIGN_BIT (0x80)		/* Sign bit for a A-law byte. */
#define QUANT_MASK (0xf)		/* Quantization field mask. */
#define NSEGS (8)		/* Number of A-law segments. */
#define SEG_SHIFT (4)		/* Left shift for segment number. */
#define SEG_MASK (0x70)		/* Segment field mask. */

static short seg_end[8] = {0xFF, 0x1FF, 0x3FF, 0x7FF, 0xFFF, 0x1FFF, 0x3FFF, 0x7FFF};

/* copy from CCITT G.711 specifications */
unsigned char _u2a[128] = {			/* u- to A-law conversions */
	1,	1,	2,	2,	3,	3,	4,	4,
	5,	5,	6,	6,	7,	7,	8,	8,
	9,	10,	11,	12,	13,	14,	15,	16,
	17,	18,	19,	20,	21,	22,	23,	24,
	25,	27,	29,	31,	33,	34,	35,	36,
	37,	38,	39,	40,	41,	42,	43,	44,
	46,	48,	49,	50,	51,	52,	53,	54,
	55,	56,	57,	58,	59,	60,	61,	62,
	64,	65,	66,	67,	68,	69,	70,	71,
	72,	73,	74,	75,	76,	77,	78,	79,
	81,	82,	83,	84,	85,	86,	87,	88,
	89,	90,	91,	92,	93,	94,	95,	96,
	97,	98,	99,	100,	101,	102,	103,	104,
	105,	106,	107,	108,	109,	110,	111,	112,
	113,	114,	115,	116,	117,	118,	119,	120,
	121,	122,	123,	124,	125,	126,	127,	128
};

unsigned char _a2u[128] = {			/* A- to u-law conversions */
	1,	3,	5,	7,	9,	11,	13,	15,
	16,	17,	18,	19,	20,	21,	22,	23,
	24,	25,	26,	27,	28,	29,	30,	31,
	32,	32,	33,	33,	34,	34,	35,	35,
	36,	37,	38,	39,	40,	41,	42,	43,
	44,	45,	46,	47,	48,	48,	49,	49,
	50,	51,	52,	53,	54,	55,	56,	57,
	58,	59,	60,	61,	62,	63,	64,	64,
	65,	66,	67,	68,	69,	70,	71,	72,
	73,	74,	75,	76,	77,	78,	79,	79,
	80,	81,	82,	83,	84,	85,	86,	87,
	88,	89,	90,	91,	92,	93,	94,	95,
	96,	97,	98,	99,	100,	101,	102,	103,
	104,	105,	106,	107,	108,	109,	110,	111,
	112,	113,	114,	115,	116,	117,	118,	119,
	120,	121,	122,	123,	124,	125,	126,	127
};

static int search(int val, short *table, int size)
{
	int	i;

	for (i = 0; i < size; i++) {
		if (val <= *table++)
			return (i);
	}
	return (size);
}

unsigned char linear2alaw(int pcm_val)	/* 2's complement (16-bit range) */
{
	int		mask;
	int		seg;
	unsigned char	aval;

	if (pcm_val >= 0) {
		mask = 0xD5;		/* sign (7th) bit = 1 */
	} else {
		mask = 0x55;		/* sign bit = 0 */
		pcm_val = -pcm_val - 8;
	}

	/* Convert the scaled magnitude to segment number. */
	seg = search(pcm_val, seg_end, 8);

	/* Combine the sign, segment, and quantization bits. */

	if (seg >= 8)		/* out of range, return maximum value. */
		return (0x7F ^ mask);
	else {
		aval = seg << SEG_SHIFT;
		if (seg < 2)
			aval |= (pcm_val >> 4) & QUANT_MASK;
		else
			aval |= (pcm_val >> (seg + 3)) & QUANT_MASK;
		return (aval ^ mask);
	}
}

int alaw2linear(unsigned char a_val)
{
	int t;
	int seg;

	a_val ^= 0x55;

	t = (a_val & QUANT_MASK) << 4;
	seg = ((unsigned)a_val & SEG_MASK) >> SEG_SHIFT;
	switch (seg) {
	case 0:
		t += 8;
		break;
	case 1:
		t += 0x108;
		break;
	default:
		t += 0x108;
		t <<= seg - 1;
	}
	return ((a_val & SIGN_BIT) ? t : -t);
}

#define	BIAS		(0x84)		/* Bias for linear code. */

unsigned char linear2ulaw(int pcm_val)	/* 2's complement (16-bit range) */
{
	int mask;
	int seg;
	unsigned char uval;

	/* Get the sign and the magnitude of the value. */
	if (pcm_val < 0) {
		pcm_val = BIAS - pcm_val;
		mask = 0x7F;
	} else {
		pcm_val += BIAS;
		mask = 0xFF;
	}

	/* Convert the scaled magnitude to segment number. */
	seg = search(pcm_val, seg_end, 8);

	/*
	 * Combine the sign, segment, quantization bits;
	 * and complement the code word.
	 */
	if (seg >= 8)		/* out of range, return maximum value. */
		return (0x7F ^ mask);
	else {
		uval = (seg << 4) | ((pcm_val >> (seg + 3)) & 0xF);
		return (uval ^ mask);
	}
}

int ulaw2linear(unsigned char u_val)
{
	int t;

	/* Complement to obtain normal u-law value. */
	u_val = ~u_val;

	/*
	 * Extract and bias the quantization bits. Then
	 * shift up by the segment number and subtract out the bias.
	 */
	t = ((u_val & QUANT_MASK) << 3) + BIAS;
	t <<= ((unsigned)u_val & SEG_MASK) >> SEG_SHIFT;

	return (((u_val & SIGN_BIT) ? (BIAS - t) : (t - BIAS)));
}

unsigned char alaw2ulaw(unsigned char aval)
{
	aval &= 0xff;
	return ((aval & 0x80) ? (0xFF ^ _a2u[aval ^ 0xD5]) :
				(0x7F ^ _a2u[aval ^ 0x55]));
}

unsigned char ulaw2alaw(unsigned char uval)
{
	uval &= 0xff;
	return ((uval & 0x80) ? (0xD5 ^ (_u2a[0xFF ^ uval] - 1)) :
				(0x55 ^ (_u2a[0x7F ^ uval] - 1)));
}







Voip::Voip() {}

Voip::~Voip() {
  // ensure we stop and free resources
  stop_component();
}

void Voip::setup() {
  ESP_LOGI(TAG, "VoIP setup called");
  started_ = false;
  // Report hardware readiness via automation events instead of a binary sensor
  bool hw_ready = (this->microphone_ != nullptr) && (this->speaker_ != nullptr);
  if (hw_ready && !this->last_hw_ready_) {
    ESP_LOGI(TAG, "VoIP ready: true (notify)");
    this->notify_ready();
  } else if (!hw_ready && this->last_hw_ready_) {
    ESP_LOGI(TAG, "VoIP ready: false (notify)");
    this->notify_not_ready();
  }
  this->last_hw_ready_ = hw_ready;
  ESP_LOGI(TAG, "VoIP setup finished: mic=%p speaker=%p", microphone_, speaker_);
  // If configured, attempt to start VoIP automatically when hardware is ready
  if (start_on_boot_) {
    bool hw_ready = (this->microphone_ != nullptr) && (this->speaker_ != nullptr);
    if (hw_ready && !started_) {
      ESP_LOGI(TAG, "start_on_boot enabled and hardware ready, starting VoIP component");
      this->start_component();
    } else if (!hw_ready) {
      ESP_LOGI(TAG, "start_on_boot enabled but hardware not ready, will not start yet");
    }
  }
}

void Voip::loop() {
  // Maintain readiness state (hardware ready = microphone + speaker available)
  // Continuously monitor hardware readiness and notify on changes
  bool hw_ready_loop = (this->microphone_ != nullptr) && (this->speaker_ != nullptr);
  if (hw_ready_loop && !this->last_hw_ready_) {
    this->notify_ready();
  } else if (!hw_ready_loop && this->last_hw_ready_) {
    this->notify_not_ready();
  }
  this->last_hw_ready_ = hw_ready_loop;
  // if (network::is_connected()) {
    if (this->rtp_udp_) {
      handle_incoming_rtp();
    }
    if (sip_) {
      handle_outgoing_rtp();
      sip_->loop();
    }
  // Automations: detect SIP/stream state transitions
  if (sip_) {
    bool current_busy = sip_->is_busy();
    if (current_busy && !this->last_sip_busy_) {
      // just started ringing
      this->notify_ringing();
    } else if (!current_busy && this->last_sip_busy_) {
      // stopped busy -> call ended
      this->notify_call_ended();
    }
    this->last_sip_busy_ = current_busy;
  }
  if (this->tx_stream_is_running_ && !this->last_tx_stream_is_running_) {
    this->notify_call_established();
  } else if (!this->tx_stream_is_running_ && this->last_tx_stream_is_running_) {
    this->notify_call_ended();
  }
  this->last_tx_stream_is_running_ = this->tx_stream_is_running_;
  // }
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
  if (!started_) {
    ESP_LOGW(TAG, "dial called but VoIP not started");
    return;
  }
  rx_stream_is_running_ = true;
  if (sip_) sip_->dial(number, id);
}

bool Voip::is_busy() {
  return sip_ ? sip_->is_busy() : false;
}

void Voip::hangup() {
  if (sip_) sip_->hangup();
}

void Voip::set_codec(int codec) {
  codec_type_ = codec;
  if (sip_) sip_->set_codec(codec);
}

void Voip::start_component() {
  if (started_) {
    ESP_LOGW(TAG, "VoIP already started");
    return;
  }
  if (start_pending_) {
    ESP_LOGW(TAG, "VoIP start already scheduled");
    return;
  }
  // Defer the actual heavy initialization to loop context / scheduler to avoid creating sockets during early setup
  ESP_LOGI(TAG, "Scheduling VoIP component start (deferred)");
  start_pending_ = true;
  // Schedule the finish on the scheduler (short delay to allow system initialization to complete)
  App.scheduler.set_timeout(this, "voip_finish_start", 10, [this]() { this->finish_start_component(); });
}

void Voip::finish_start_component() {
  start_pending_ = false;
  if (started_) {
    ESP_LOGW(TAG, "finish_start_component called but VoIP already started");
    return;
  }
  ESP_LOGI(TAG, "Starting VoIP component...");
  ESP_LOGD(TAG, "VoIP finish_start_component: entering start sequence (core=%d)", xPortGetCoreID());
  // create RTP socket and SIP component
  ESP_LOGI(TAG, "Free heap before RTP socket creation: %u", esp_get_free_heap_size());
  ESP_LOGD(TAG, "VoIP finish_start_component: creating RTP UDP socket");
  this->rtp_udp_ = socket::socket(AF_INET, SOCK_DGRAM, 0);
  if (this->rtp_udp_ == nullptr) {
    ESP_LOGE(TAG, "Failed to create RTP UDP socket");
    if (start_retries_ < 10) {
      start_retries_++;
      uint32_t delay_ms = 1000 * start_retries_;
      ESP_LOGW(TAG, "Will retry voip start in %u ms (attempt %d)", delay_ms, start_retries_);
      this->start_pending_ = true;
      App.scheduler.set_timeout(this, "voip_finish_start_retry", delay_ms, [this]() { this->finish_start_component(); });
    }
    return;
  }
  ESP_LOGD(TAG, "VoIP finish_start_component: created RTP UDP socket: fd=%d", this->rtp_udp_->get_fd());
  this->rtp_udp_->setblocking(false);
  struct sockaddr_in rtp_addr = {};
  rtp_addr.sin_family = AF_INET;
  rtp_addr.sin_port = htons(1234);
  rtp_addr.sin_addr.s_addr = INADDR_ANY;
  if (this->rtp_udp_->bind((struct sockaddr *)&rtp_addr, sizeof(rtp_addr)) != 0) {
    ESP_LOGE(TAG, "Failed to bind RTP UDP socket");
    return;
  }
  ESP_LOGI(TAG, "RTP listen on port 1234");
  ESP_LOGD(TAG, "VoIP finish_start_component: allocating Sip object");
  sip_ = new (std::nothrow) Sip();
  if (!sip_) {
    ESP_LOGE(TAG, "Failed to allocate Sip instance");
    if (start_retries_ < 10) {
      start_retries_++;
      uint32_t delay_ms = 1000 * start_retries_;
      ESP_LOGW(TAG, "Will retry voip start in %u ms (attempt %d)", delay_ms, start_retries_);
      this->start_pending_ = true;
      App.scheduler.set_timeout(this, "voip_finish_start_retry", delay_ms, [this]() { this->finish_start_component(); });
    }
    return;
  }
  ESP_LOGI(TAG, "Free heap after Sip allocation: %u", esp_get_free_heap_size());
  ESP_LOGD(TAG, "VoIP finish_start_component: Sip allocated: %p", sip_);
  ESP_LOGI(TAG, "Initializing SIP subcomponent: server=%s port=%d user=%s", sip_ip_.c_str(), sip_port_, sip_user_.c_str());
  ESP_LOGD(TAG, "VoIP finish_start_component: initializing Sip subcomponent");
  sip_->init(sip_ip_, sip_port_, "192.168.1.100", sip_port_, sip_user_, sip_pass_);
  ESP_LOGD(TAG, "VoIP finish_start_component: Sip initialized");
  ESP_LOGI(TAG, "Sip initialized: %p", sip_);
  if (microphone_) {
    ESP_LOGD(TAG, "VoIP finish_start_component: registering microphone data callback");
    microphone_->add_data_callback([this](const std::vector<uint8_t> &data) { this->mic_data_callback(data); });
  } else {
    ESP_LOGW(TAG, "VoIP finish_start_component: microphone_ is null, continuing without mic callbacks");
  }
  ESP_LOGD(TAG, "VoIP finish_start_component: mic=%p, speaker=%p, sample_rate=%d, bits=%d", microphone_, speaker_, SAMPLE_RATE, SAMPLE_BITS);
  started_ = true;
  start_retries_ = 0;
  ESP_LOGI(TAG, "Voip finish_start_component: started_ set to true");
  // Notify that hardware is ready when starting
  if (!this->last_hw_ready_) {
    this->notify_ready();
    this->last_hw_ready_ = true;
  }
  // PA control removed; use automations (on_call_established) to toggle PA instead
}

void Voip::stop_component() {
  if (!started_) return;
  ESP_LOGI(TAG, "Stopping VoIP component...");
  // Cancel any scheduled timeouts related to recording/playback to avoid callbacks
  App.scheduler.cancel_timeout(this, "voip_record_warmup");
  App.scheduler.cancel_timeout(this, "voip_record_stop");
  App.scheduler.cancel_timeout(this, "voip_playback_wait");
  if (microphone_) {
    microphone_->stop();
    ESP_LOGI(TAG, "VoIP stop_component: microphone stop invoked, is_stopped=%d", microphone_->is_stopped());
  }
  if (this->rtp_udp_) {
    // no explicit close on socket::Socket in this component; releasing unique_ptr would close
    this->rtp_udp_.reset();
  }
  if (sip_) {
    sip_->hangup();
    delete sip_;
    sip_ = nullptr;
  }
  started_ = false;
  // Notify that hardware is not ready when stopping
  if (this->last_hw_ready_) {
    this->notify_not_ready();
    this->last_hw_ready_ = false;
  }
  // PA control removed; use automations (on_call_ended) to toggle PA instead
}

void Voip::notify_ringing() {
  for (auto &cb : on_ringing_callbacks_) {
    cb();
  }
}

void Voip::notify_call_established() {
  for (auto &cb : on_call_established_callbacks_) {
    cb();
  }
}

void Voip::notify_call_ended() {
  for (auto &cb : on_call_ended_callbacks_) {
    cb();
  }
}

void Voip::notify_ready() {
  for (auto &cb : on_ready_callbacks_) {
    cb();
  }
}

void Voip::notify_not_ready() {
  for (auto &cb : on_not_ready_callbacks_) {
    cb();
  }
}

void Voip::handle_incoming_rtp() {
  if (codec_type_ == 0) {
    // PCMU
    int16_t buffer[500];
    struct sockaddr_in remote;
    socklen_t addrlen = sizeof(remote);
    packet_size_ = this->rtp_udp_->recvfrom(rtp_buffer_, sizeof(rtp_buffer_), (struct sockaddr *)&remote, &addrlen);
    if (packet_size_ < 0) {
      // Non-blocking sockets return -1 with errno==EAGAIN/EWOULDBLOCK when
      // there's no data available; ignore silently in that case.
      if (errno == EAGAIN || errno == EWOULDBLOCK) return;
      static uint32_t last_rtp_recv_error_log = 0;
      uint32_t now = (uint32_t)esphome::millis();
      if ((int32_t)(now - last_rtp_recv_error_log) > 5000) {
        last_rtp_recv_error_log = now;
        ESP_LOGW(TAG, "RTP recvfrom error=%d errno=%d (%s)", packet_size_, errno, strerror(errno));
      }
      return;
    }
    if (packet_size_ == 0) return;
    if (packet_size_ > (int)sizeof(rtp_buffer_)) {
      ESP_LOGW(TAG, "RTP packet too large: %d, truncating to %u", packet_size_, (unsigned)sizeof(rtp_buffer_));
      packet_size_ = sizeof(rtp_buffer_);
    }
    if (packet_size_ > 0 && rx_stream_is_running_) {
        if (!speaker_) {
          ESP_LOGW(TAG, "Received RTP but speaker_ is null");
          return;
        }
      uint8_t *payload = rtp_buffer_ + 12; // Skip RTP header
      rtppkg_size_ = packet_size_ - 12;
      if (rtppkg_size_ > 500) rtppkg_size_ = 500; // clamp to buffer size
      for (int i = 0; i < rtppkg_size_; i++) {
        buffer[i] = ulaw2linear(payload[i]) * amp_gain_;
      }
      ESP_LOGD(TAG, "handle_incoming_rtp: speaker->play called for incoming RTP (PCMU), bytes=%u", (unsigned)(sizeof(int16_t) * rtppkg_size_));
      speaker_->play((const uint8_t *)buffer, sizeof(int16_t) * rtppkg_size_);
    }
  } else if (codec_type_ == 1) {
    // PCMA
    int16_t buffer[500];
    struct sockaddr_in remote;
    socklen_t addrlen = sizeof(remote);
    packet_size_ = this->rtp_udp_->recvfrom(rtp_buffer_, sizeof(rtp_buffer_), (struct sockaddr *)&remote, &addrlen);
    if (packet_size_ < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) return;
      static uint32_t last_rtp_recv_error_log2 = 0;
      uint32_t now2 = (uint32_t)esphome::millis();
      if ((int32_t)(now2 - last_rtp_recv_error_log2) > 5000) {
        last_rtp_recv_error_log2 = now2;
        ESP_LOGW(TAG, "RTP recvfrom error=%d errno=%d (%s)", packet_size_, errno, strerror(errno));
      }
      return;
    }
    if (packet_size_ == 0) return;
    if (packet_size_ > (int)sizeof(rtp_buffer_)) {
      ESP_LOGW(TAG, "RTP packet too large: %d, truncating to %u", packet_size_, (unsigned)sizeof(rtp_buffer_));
      packet_size_ = sizeof(rtp_buffer_);
    }
    if (packet_size_ > 0 && rx_stream_is_running_) {
      uint8_t *payload = rtp_buffer_ + 12;
      rtppkg_size_ = packet_size_ - 12;
      if (rtppkg_size_ > 500) rtppkg_size_ = 500; // clamp to buffer size
      for (int i = 0; i < rtppkg_size_; i++) {
        buffer[i] = alaw2linear(payload[i]) * amp_gain_;
      }
      ESP_LOGD(TAG, "handle_incoming_rtp: speaker->play called for incoming RTP (PCMA), bytes=%u", (unsigned)(sizeof(int16_t) * rtppkg_size_));
      speaker_->play((const uint8_t *)buffer, sizeof(int16_t) * rtppkg_size_);
    }
  }
}

void Voip::handle_outgoing_rtp() {
  if (sip_ && !sip_->audioport.empty() && !tx_stream_is_running_) {
    tx_stream_is_running_ = true;
    ESP_LOGI(TAG, "Starting RTP stream");
    if (microphone_) {
      ESP_LOGD(TAG, "handle_outgoing_rtp: attempting to start microphone, is_stopped=%d", microphone_->is_stopped());
      if (microphone_->is_stopped()) {
        microphone_->start();
        ESP_LOGI(TAG, "handle_outgoing_rtp: microphone started for RTP TX");
      }
    }
    App.scheduler.set_interval(this, "rtp_tx", 20, [this]() { tx_rtp(); });
  } else if ((sip_ && sip_->audioport.empty()) && tx_stream_is_running_) {
    tx_stream_is_running_ = false;
    rx_stream_is_running_ = false;
    rtppkg_size_ = -1;
    ESP_LOGI(TAG, "RTP stream stopped");
    if (microphone_) microphone_->stop();
    App.scheduler.cancel_interval(this, "rtp_tx");
  }
}

// Play a simple beep tone through speaker for a specified duration (ms)
void Voip::play_beep_ms(int duration_ms) {
  if (!this->speaker_) {
    ESP_LOGW(TAG, "play_beep_ms: speaker_ is null, cannot play beep");
    return;
  }
  if (duration_ms <= 0) {
    ESP_LOGW(TAG, "play_beep_ms: duration_ms <= 0 (%d), ignoring", duration_ms);
    return;
  }
  ESP_LOGI(TAG, "play_beep_ms: playing beep for %d ms", duration_ms);
  // Generate a 1kHz sine wave at SAMPLE_RATE with int16 samples
  const int sample_rate = SAMPLE_RATE; // 8000
  const int freq = 1000; // 1kHz beep
  int samples = (duration_ms * sample_rate) / 1000;
  if (samples <= 0) samples = 1;
  std::vector<int16_t> buf(samples);
  static constexpr float PI = 3.14159265358979323846f;
  for (int i = 0; i < samples; ++i) {
    float t = (float)i / (float)sample_rate;
    float val = std::sin(2.0f * PI * (float)freq * t);
    // scale amplitude to reasonable level and apply amp_gain_
    int16_t s = (int16_t)(val * (INT16_MAX / 4) * (amp_gain_ / (float)AMP_GAIN_DEFAULT));
    buf[i] = s;
  }
  // Play raw int16 samples
  this->speaker_->play((const uint8_t *)buf.data(), samples * sizeof(int16_t));
  ESP_LOGD(TAG, "play_beep_ms: done scheduling play, samples=%d", samples);
}

void Voip::tx_rtp() {
  static uint16_t sequence_number = 0;
  static uint32_t timestamp = 0;
  const uint32_t ssrc = 3124150;
  uint8_t packet_buffer[255];

  if (!started_) return;  // ensure started
  if (codec_type_ == 0) {
    if (!microphone_) {
      ESP_LOGW(TAG, "tx_rtp: microphone_ is null");
      return;
    }
    // PCMU
    uint8_t temp[160];
    int bytes_per_sample = 4;
    size_t required = 640; // 160 samples * 4 bytes
    if (mic_buffer_.size() < required) {
      // maybe 16-bit samples => 2 bytes per sample (160 * 2 = 320 bytes)
      if (mic_buffer_.size() >= 320) {
        bytes_per_sample = 2;
        required = 320;
      }
    }
    if (mic_buffer_.size() >= required) {
      for (int i = 0; i < 160; i++) {
        SAMPLE_T sample = 0;
        if (bytes_per_sample == 4) {
          memcpy(&sample, &mic_buffer_[i * 4], sizeof(sample));
        } else {
          int16_t s16 = 0;
          memcpy(&s16, &mic_buffer_[i * 2], sizeof(s16));
          // scale 16-bit to SAMPLE_T (24-bit internal representation)
          sample = ((SAMPLE_T)s16) << (SAMPLE_BITS - 16);
        }
        temp[i] = linear2ulaw(MIC_CONVERT(sample) * mic_gain_);
      }
      mic_buffer_.erase(mic_buffer_.begin(), mic_buffer_.begin() + required);
    } else {
      return; // not enough data
    }
    uint8_t *rtp_header = packet_buffer;
    rtp_header[0] = 0x80;
    rtp_header[1] = 0;
    uint16_t seq_net = htons(sequence_number++);
    rtp_header[2] = (seq_net >> 8) & 0xFF;
    rtp_header[3] = seq_net & 0xFF;
    uint32_t ts_net = htonl(timestamp += 160);
    rtp_header[4] = (ts_net >> 24) & 0xFF;
    rtp_header[5] = (ts_net >> 16) & 0xFF;
    rtp_header[6] = (ts_net >> 8) & 0xFF;
    rtp_header[7] = ts_net & 0xFF;
    uint32_t ssrc_net = htonl(ssrc);
    rtp_header[8] = (ssrc_net >> 24) & 0xFF;
    rtp_header[9] = (ssrc_net >> 16) & 0xFF;
    rtp_header[10] = (ssrc_net >> 8) & 0xFF;
    rtp_header[11] = ssrc_net & 0xFF;
    memcpy(rtp_header + 12, temp, 160);
    struct sockaddr_in remote = {};
    remote.sin_family = AF_INET;
    if (!sip_) {
      ESP_LOGW(TAG, "tx_rtp: sip_ is null");
      return;
    }
    int dest_port = atoi(sip_->audioport.c_str());
    if (dest_port <= 0) {
      ESP_LOGW(TAG, "Invalid audio port: %s", sip_->audioport.c_str());
      return;
    }
    remote.sin_port = htons(dest_port);
    if (sip_->get_sip_server_ip().empty()) {
      ESP_LOGW(TAG, "tx_rtp: SIP server IP is empty!");
      return;
    }
    inet_pton(AF_INET, sip_->get_sip_server_ip().c_str(), &remote.sin_addr);
    this->rtp_udp_->sendto(packet_buffer, 12 + 160, 0, (struct sockaddr *)&remote, sizeof(remote));
  } else if (codec_type_ == 1) {
    if (!started_) return; // ensure started
    // PCMA
    uint8_t temp[160];
    int bytes_per_sample = 4;
    size_t required = 640; // 160 samples * 4 bytes
    if (mic_buffer_.size() < required) {
      if (mic_buffer_.size() >= 320) {
        bytes_per_sample = 2;
        required = 320;
      }
    }
    if (mic_buffer_.size() >= required) {
      for (int i = 0; i < 160; i++) {
        SAMPLE_T sample = 0;
        if (bytes_per_sample == 4) {
          memcpy(&sample, &mic_buffer_[i * 4], sizeof(sample));
        } else {
          int16_t s16 = 0;
          memcpy(&s16, &mic_buffer_[i * 2], sizeof(s16));
          sample = ((SAMPLE_T)s16) << (SAMPLE_BITS - 16);
        }
        temp[i] = linear2alaw(MIC_CONVERT(sample) * mic_gain_);
      }
      mic_buffer_.erase(mic_buffer_.begin(), mic_buffer_.begin() + required);
    } else {
      return; // not enough data
    }
    uint8_t *rtp_header = packet_buffer;
    rtp_header[0] = 0x80;
    rtp_header[1] = 8;
    uint16_t seq_net = htons(sequence_number++);
    rtp_header[2] = (seq_net >> 8) & 0xFF;
    rtp_header[3] = seq_net & 0xFF;
    uint32_t ts_net = htonl(timestamp += 160);
    rtp_header[4] = (ts_net >> 24) & 0xFF;
    rtp_header[5] = (ts_net >> 16) & 0xFF;
    rtp_header[6] = (ts_net >> 8) & 0xFF;
    rtp_header[7] = ts_net & 0xFF;
    uint32_t ssrc_net = htonl(ssrc);
    rtp_header[8] = (ssrc_net >> 24) & 0xFF;
    rtp_header[9] = (ssrc_net >> 16) & 0xFF;
    rtp_header[10] = (ssrc_net >> 8) & 0xFF;
    rtp_header[11] = ssrc_net & 0xFF;
    memcpy(rtp_header + 12, temp, 160);
    struct sockaddr_in remote = {};
    remote.sin_family = AF_INET;
    if (!sip_) {
      ESP_LOGW(TAG, "tx_rtp: sip_ is null");
      return;
    }
    int dest_port = atoi(sip_->audioport.c_str());
    if (dest_port <= 0) {
      ESP_LOGW(TAG, "Invalid audio port: %s", sip_->audioport.c_str());
      return;
    }
    remote.sin_port = htons(dest_port);
    if (sip_->get_sip_server_ip().empty()) {
      ESP_LOGW(TAG, "tx_rtp: SIP server IP is empty!");
      return;
    }
    inet_pton(AF_INET, sip_->get_sip_server_ip().c_str(), &remote.sin_addr);
    this->rtp_udp_->sendto(packet_buffer, 12 + 160, 0, (struct sockaddr *)&remote, sizeof(remote));
  }
}

void Voip::mic_data_callback(const std::vector<uint8_t> &data) {
  mic_buffer_.insert(mic_buffer_.end(), data.begin(), data.end());
  ESP_LOGD(TAG, "mic_data_callback: received %u bytes, mic_buffer=%u bytes", (unsigned)data.size(), (unsigned)mic_buffer_.size());
  if (this->is_recording_) {
    this->record_buffer_.insert(this->record_buffer_.end(), data.begin(), data.end());
    ESP_LOGD(TAG, "mic_data_callback: appended to record_buffer, now %u bytes", (unsigned)this->record_buffer_.size());
  }
}

// Record 1s of audio via microphone, then playback via speaker
void Voip::record_and_playback_1s() {
  if (!microphone_ || !speaker_) {
    ESP_LOGW(TAG, "record_and_playback_1s: microphone or speaker not configured");
    return;
  }
  if (is_recording_) {
    ESP_LOGW(TAG, "record_and_playback_1s: already recording");
    return;
  }
  ESP_LOGI(TAG, "record_and_playback_1s: starting 1s recording");
  // Clear previous buffer and mark recording
  record_buffer_.clear();
  is_recording_ = true;
  // existing mic_data_callback will append to record_buffer_ while is_recording_ is true
  // Start mic and schedule stop after 1000ms
  // Only start microphone if it's currently stopped; if already running, leave it as-is
  if (this->microphone_->is_stopped()) {
    ESP_LOGD(TAG, "record_and_playback_1s: microphone is_stopped()=true, starting mic");
    microphone_->start();
    ESP_LOGI(TAG, "record_and_playback_1s: microphone started");
  } else {
    ESP_LOGD(TAG, "record_and_playback_1s: microphone is_stopped()=false, not starting");
  }
  // Warm up the microphone for a short duration to reduce driver read timeouts; then record for 1s
  // Schedule the stop after desired record length (1s)
  App.scheduler.set_timeout(this, "voip_record_stop", 1000, [this]() {
    ESP_LOGI(TAG, "record_and_playback_1s: stopping recording, len=%u", (unsigned)this->record_buffer_.size());
    // Stop microphone
    microphone_->stop();
    ESP_LOGI(TAG, "record_and_playback_1s: microphone stop invoked, is_stopped=%d", this->microphone_->is_stopped());
    this->is_recording_ = false;
    // Playback using speaker if buffer has data
    if (!this->record_buffer_.empty()) {
      ESP_LOGI(TAG, "record_and_playback_1s: waiting for microphone to stop before playback, len=%u",
           (unsigned)this->record_buffer_.size());
      // The microphone stop is asynchronous: the driver may still hold the I2S bus
      // (see i2s_audio.microphone loop/stop logic). We poll microphone->is_stopped() until the
      // driver is fully unloaded, then start playback. If the mic doesn't stop within the
      // timeout, we fall back to starting playback to avoid blocking forever.
      const int max_retries = 10;         // up to ~1 second (max_retries * retry_interval_ms)
      const int retry_interval_ms = 100;  // ms
      auto retries = std::make_shared<int>(0);
          auto cb = std::make_shared<std::function<void()>>();
          *cb = [this, retries, max_retries, retry_interval_ms, cb]() mutable {
        ESP_LOGD(TAG, "record_and_playback_1s: playback wait callback, retries=%d", *retries);
        if (this->microphone_ == nullptr) {
          ESP_LOGW(TAG, "record_and_playback_1s: microphone vanished, attempting playback");
          this->speaker_->play(this->record_buffer_.data(), this->record_buffer_.size());
          // shared_ptr will free automatically when no longer referenced
          return;
        }
        if (this->microphone_->is_stopped()) {
          ESP_LOGI(TAG, "record_and_playback_1s: microphone stopped, starting playback");
          ESP_LOGD(TAG, "record_and_playback_1s: speaker play called with %u bytes", (unsigned)this->record_buffer_.size());
          this->speaker_->play(this->record_buffer_.data(), this->record_buffer_.size());
          return;
        }
        if ((*retries) >= max_retries) {
          ESP_LOGW(TAG, "record_and_playback_1s: microphone didn't stop after %d ms, starting playback anyway",
                   max_retries * retry_interval_ms);
          this->speaker_->play(this->record_buffer_.data(), this->record_buffer_.size());
          return;
        }
        (*retries)++;
        App.scheduler.set_timeout(this, "voip_playback_wait", retry_interval_ms, *cb);
      };
      App.scheduler.set_timeout(this, "voip_playback_wait", retry_interval_ms, *cb);
    } else {
      ESP_LOGW(TAG, "record_and_playback_1s: no audio recorded to playback");
    }
  });
}

}  // namespace voip
}  // namespace esphome
