#include "utils.hpp"
#include <Arduino.h>
#include "mbedtls/base64.h"
#include "mbedtls/ctr_drbg.h"

// External DRBG context (seeded in crypto or main)
extern mbedtls_ctr_drbg_context ctr_drbg;

void printHex(const char* label, const uint8_t* buf, size_t len) {
    Serial.print(label);
    for (size_t i = 0; i < len; i++) {
        Serial.printf("%02X", buf[i]);
    }
    Serial.println();
}

std::string base64Encode(const uint8_t* data, size_t len) {
    size_t olen = 0;
    size_t outSize = 4 * ((len + 2) / 3) + 4;
    std::vector<unsigned char> out(outSize);
    int ret = mbedtls_base64_encode(out.data(), out.size(), &olen, data, len);
    if (ret != 0) {
        Serial.printf("Base64 encode failed: -0x%04X\n", -ret);
        return "";
    }
    return std::string(reinterpret_cast<char*>(out.data()), olen);
}

std::vector<uint8_t> base64Decode(const std::string& b64) {
    size_t olen = 0;
    std::vector<uint8_t> out((b64.size() * 3) / 4 + 4);
    int ret = mbedtls_base64_decode(out.data(), out.size(), &olen,
                                    reinterpret_cast<const unsigned char*>(b64.c_str()),
                                    b64.size());
    if (ret != 0) {
        Serial.printf("Base64 decode failed: -0x%04X\n", -ret);
        return {};
    }
    out.resize(olen);
    return out;
}

void randBytes(uint8_t* buf, size_t len) {
    mbedtls_ctr_drbg_random(&ctr_drbg, buf, len);
}
