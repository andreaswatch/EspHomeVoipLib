#ifndef ESPHOME_VOIP_MD5_UTIL_H
#define ESPHOME_VOIP_MD5_UTIL_H

#include <string>
#include <mbedtls/md.h>
#include <cstring>
#include <cstdio>

// Compute the MD5 digest of `input` and return 32-char lowercase hex string
static inline std::string md5_hex(const std::string &input) {
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

#endif // ESPHOME_VOIP_MD5_UTIL_H
