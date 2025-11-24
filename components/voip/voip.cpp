#include "voip.h"
#include <cstring>
#include <cstdarg>
#include "esphome/core/helpers.h"
#include "esphome/core/hal.h"
#include <esp_system.h>
#include "mbedtls/md5.h"

namespace esphome {
namespace voip {

static const char *const TAG = "voip";

Sip::Sip() : p_buf_(nullptr), l_buf_(2048), i_last_cseq_(0), codec_(0) {
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
  udp_.bind(sip_port);
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
    std::string realm, nonce;
    if (parse_parameter(realm, " realm=\"", p) &&
        parse_parameter(nonce, " nonce=\"", p)) {
      // using output buffer to build the md5 hashes
      // store the md5 haResp to end of buffer
      char *ha1_hex = p_buf_;
      char *ha2_hex = p_buf_ + 33;
      ha_resp = p_buf_ + l_buf_ - 34;
      char *p_temp = p_buf_ + 66;

      snprintf(p_temp, l_buf_ - 100, "%s:%s:%s", p_sip_user_.c_str(), realm.c_str(), p_sip_pass_.c_str());
      make_md5_digest(ha1_hex, p_temp);

      snprintf(p_temp, l_buf_ - 100, "INVITE:sip:%s@%s", p_dial_nr_.c_str(), p_sip_ip_.c_str());
      make_md5_digest(ha2_hex, p_temp);

      snprintf(p_temp, l_buf_ - 100, "%s:%s:%s", ha1_hex, nonce.c_str(), ha2_hex);
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
  } else {
    add_sip_line("m=audio 1234 RTP/AVP 18");
    add_sip_line("a=rtpmap:18 G721/8000");
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
  int packet_size = 0;
  if (udp_.has_packet()) {
    packet_size = udp_.read(ca_sip_in, sizeof(ca_sip_in));
    ESP_LOGD(TAG, "Received SIP packet from %s:%d size %d", udp_.get_remote_ip().str().c_str(), udp_.get_remote_port(), packet_size);
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
  udp_.sendto((uint8_t *)p_buf_, strlen(p_buf_), network::IPAddress(p_sip_ip_.c_str()), i_sip_port_);
  return 0;
}

uint32_t Sip::random() {
  return esp_random();
}

uint32_t Sip::millis() {
  return (uint32_t)esphome::millis() + 1;
}

void Sip::make_md5_digest(char *p_out_hex33, char *p_in) {
  mbedtls_md5_context ctx;
  unsigned char output[16];
  char hex[3];

  mbedtls_md5_init(&ctx);
  mbedtls_md5_starts(&ctx);
  mbedtls_md5_update(&ctx, (const unsigned char *)p_in, strlen(p_in));
  mbedtls_md5_finish(&ctx, output);
  mbedtls_md5_free(&ctx);

  for (int i = 0; i < 16; i++) {
    sprintf(hex, "%02x", output[i]);
    p_out_hex33[i * 2] = hex[0];
    p_out_hex33[i * 2 + 1] = hex[1];
  }
  p_out_hex33[32] = '\0';
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
  // if (opus_encoder_) opus_encoder_destroy(opus_encoder_);
  // if (opus_decoder_) opus_decoder_destroy(opus_decoder_);
}

void Voip::setup() {
  my_ip_ = network::get_ip_address().str();
  rtp_udp_.bind(1234);
  ESP_LOGI(TAG, "rtp_udp listen on (port:1234)");
  sip_ = new Sip();
  sip_->init(sip_ip_, sip_port_, my_ip_, sip_port_, sip_user_, sip_pass_);

  if (init_i2s_mic() != 0) {
    ESP_LOGE(TAG, "Failed to init I2S mic");
    return;
  }
  if (init_i2s_amp() != 0) {
    ESP_LOGE(TAG, "Failed to init I2S amp");
    return;
  }
}

void Voip::loop() {
  if (network::is_connected()) {
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
    if (rtp_udp_.has_packet() && rx_stream_is_running_) {
      packet_size_ = rtp_udp_.read(rtp_buffer_, sizeof(rtp_buffer_));
      uint8_t *payload = rtp_buffer_ + 12; // Skip RTP header
      rtppkg_size_ = packet_size_ - 12;
      for (int i = 0; i < rtppkg_size_; i++) {
        buffer[i] = ulaw2linear(payload[i]) * amp_gain_;
      }
      size_t bytes_written;
      write_to_amp(buffer, sizeof(int16_t) * rtppkg_size_, &bytes_written);
    } else {
      // No flush in ESPHome UdpSocket, perhaps just ignore
    }
  } else if (codec_type_ == 1) {
    // PCMA
    int16_t buffer[500];
    if (rtp_udp_.has_packet() && rx_stream_is_running_) {
      packet_size_ = rtp_udp_.read(rtp_buffer_, sizeof(rtp_buffer_));
      uint8_t *payload = rtp_buffer_ + 12;
      rtppkg_size_ = packet_size_ - 12;
      for (int i = 0; i < rtppkg_size_; i++) {
        buffer[i] = alaw2linear(payload[i]) * amp_gain_;
      }
      size_t bytes_written;
      write_to_amp(buffer, sizeof(int16_t) * rtppkg_size_, &bytes_written);
    } else {
      // No flush
    }
  } else {
    // No flush
  }
}

void Voip::handle_outgoing_rtp() {
  if (!sip_->audioport.empty() && !tx_stream_is_running_) {
    tx_stream_is_running_ = true;
    ESP_LOGI(TAG, "Starting RTP stream");
    tx_interval_ = scheduler::set_interval(this, [this]() { tx_rtp(); }, 20_ms);
  } else if (sip_->audioport.empty() && tx_stream_is_running_) {
    tx_stream_is_running_ = false;
    rx_stream_is_running_ = false;
    rtppkg_size_ = -1;
    ESP_LOGI(TAG, "RTP stream stopped");
    scheduler::cancel_interval(tx_interval_);
    stop_i2s();
  }
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
    network::IPAddress ip(sip_->get_sip_server_ip().c_str());
    rtp_udp_.sendto(packet_buffer, 12 + 160, ip, atoi(sip_->audioport.c_str()));
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
    network::IPAddress ip(sip_->get_sip_server_ip().c_str());
    rtp_udp_.sendto(packet_buffer, 12 + 160, ip, atoi(sip_->audioport.c_str()));
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
