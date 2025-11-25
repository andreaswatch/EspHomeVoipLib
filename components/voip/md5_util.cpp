#include "md5_util.h"
#include <mbedtls/md.h>
#include <cstring>
#include <cstdio>

std::string md5_hex(const std::string &input) {
  unsigned char output[16];
  const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_MD5);
  if (md_info == nullptr) {
    memset(output, 0, sizeof(output));
  } else {
    mbedtls_md(md_info, (const unsigned char *)input.data(), input.size(), output);
  }
  char hex[33];
  for (int i = 0; i < 16; ++i) {
    snprintf(&hex[i * 2], 3, "%02x", output[i]);
  }
  hex[32] = '\0';
  return std::string(hex);
}
