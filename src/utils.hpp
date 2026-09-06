#ifndef UTILS_HPP
#define UTILS_HPP

#include <string>
#include <vector>
#include <cstdint>

// Hex dump for debugging
void printHex(const char* label, const uint8_t* buf, size_t len);

// Base64 helpers
std::string base64Encode(const uint8_t* data, size_t len);
std::vector<uint8_t> base64Decode(const std::string& b64);

// Random bytes (using CTR-DRBG seeded in crypto.cpp or main)
void randBytes(uint8_t* buf, size_t len);

#endif // UTILS_HPP
