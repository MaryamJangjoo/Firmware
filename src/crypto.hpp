#ifndef CRYPTO_HPP
#define CRYPTO_HPP

#include <vector>
#include <array>
#include <string>
#include "mbedtls/ecp.h"
#include "mbedtls/ecdh.h"

bool cryptoInit();
// Device identity management
bool loadDeviceKeypair(mbedtls_ecp_keypair& kp);
bool saveDeviceKeypair(const mbedtls_ecp_keypair& kp);
bool generateDeviceKeypair(mbedtls_ecp_keypair& kp);

// Public key export/import
bool exportUncompressedPub(const mbedtls_ecp_keypair& kp, std::vector<uint8_t>& out65);
bool importPeerUncompressedPub(const std::vector<uint8_t>& in65, mbedtls_ecp_point& Qp, mbedtls_ecp_group& grp);

// Shared secret and key derivation
bool ecdhShared(mbedtls_ecp_group& grp, const mbedtls_mpi& d,
                const mbedtls_ecp_point& Qp, uint8_t out32[32]);
bool hkdfSha256(const uint8_t* ikm32, const uint8_t* salt, size_t saltLen,
                const uint8_t* info, size_t infoLen,
                uint8_t okm32[32]);
bool hmacSha256(const uint8_t* key32, const uint8_t* msg, size_t msgLen,
                uint8_t mac32[32]);

#endif // CRYPTO_HPP
