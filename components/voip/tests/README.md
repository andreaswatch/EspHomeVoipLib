# VoIP MD5 unit tests

This folder contains a small test `test_md5` to validate MD5 hex calculations used for SIP Digest authentication (RFC2617 examples).

## Build and run (Linux / macOS)

Install mbedtls dev dependencies (example for Debian/Ubuntu):

```bash
sudo apt-get install libmbedtls-dev cmake build-essential pkg-config
```

Then build and run:

```bash
cd components/voip/tests
mkdir build
cd build
cmake ..
make
./test_md5
```

The test executable uses `md5_util.cpp` which relies on mbedtls. If you use ESP-IDF/PlatformIO, the unit tests may be built within your environment; this small test is supplied to be runnable on a host for quick verification.
