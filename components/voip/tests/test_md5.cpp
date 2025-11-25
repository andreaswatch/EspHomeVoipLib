#include "../md5_util.h"
#include <iostream>
#include <string>

int main() {
  struct Test { std::string input; std::string expected; };
  Test tests[] = {
    {"", "d41d8cd98f00b204e9800998ecf8427e"},
    {"a", "0cc175b9c0f1b6a831c399e269772661"},
    {"abc", "900150983cd24fb0d6963f7d28e17f72"},
    // RFC2617 example (HA1 and HA2 check); check HA1 example
    {"Mufasa:testrealm@host.com:Circle Of Life", "939e7578ed9e3c518a452acee763bce9"},
    {"GET:/dir/index.html", "39aff3a2bab6126f332b942af96d3366"}
  };

  int failures = 0;
  for (auto &t : tests) {
    std::string out = md5_hex(t.input);
    std::cout << "md5_hex('" << t.input << "') = " << out << std::endl;
    if (out != t.expected) {
      std::cerr << "FAILED: got " << out << " expected " << t.expected << std::endl;
      ++failures;
    }
  }

  // RFC2617 response test (full response) using known values
  std::string ha1 = md5_hex("Mufasa:testrealm@host.com:Circle Of Life");
  std::string ha2 = md5_hex("GET:/dir/index.html");
  std::string response = md5_hex(ha1 + ":dcd98b7102dd2f0e8b11d0f600bfb0c093:" + ha2);
  std::cout << "Digest response test: " << response << std::endl;
  if (response != "6629fae49393a05397450978507c4ef1") {
    std::cerr << "FAILED RFC2617 response expected 6629fae49393a05397450978507c4ef1" << std::endl;
    ++failures;
  }

  // qop=auth case with cnonce and nc
  std::string nonce = "dcd98b7102dd2f0e8b11d0f600bfb0c093";
  std::string cnonce = "0a4f113b";
  std::string nc = "00000001";
  std::string response_qop = md5_hex(ha1 + ":" + nonce + ":" + nc + ":" + cnonce + ":auth:" + ha2);
  std::cout << "Digest response (qop=auth) test: " << response_qop << std::endl;
  if (response_qop != "6629fae49393a05397450978507c4ef1") {
    std::cerr << "FAILED RFC2617 qop=auth response expected 6629fae49393a05397450978507c4ef1" << std::endl;
    ++failures;
  }

  if (failures) {
    std::cerr << failures << " tests failed" << std::endl;
    return 1;
  }
  std::cout << "All tests passed." << std::endl;
  return 0;
}
