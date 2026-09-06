#include "crypto.hpp"
#include <Arduino.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ecp.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/md.h>
#include <mbedtls/hkdf.h>

mbedtls_entropy_context entropy;
mbedtls_ctr_drbg_context ctr_drbg;

bool cryptoInit() {
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    const char* pers = "ecosmart-drbg";
    int ret = mbedtls_ctr_drbg_seed(&ctr_drbg,
                                    mbedtls_entropy_func,
                                    &entropy,
                                    reinterpret_cast<const unsigned char*>(pers),
                                    strlen(pers));
    if (ret != 0) {
        Serial.printf("DRBG seed failed: -0x%04X\n", -ret);
        return false;
    }
    return true;
}

// --- Device identity management ---
bool loadDeviceKeypair(mbedtls_ecp_keypair& kp) {
    // TODO: load from NVS/flash
    Serial.println("loadDeviceKeypair() not implemented, generating new keypair");
    return generateDeviceKeypair(kp);
}

bool saveDeviceKeypair(const mbedtls_ecp_keypair& kp) {
    // TODO: save to NVS/flash
    Serial.println("saveDeviceKeypair() not implemented");
    return true;
}

bool generateDeviceKeypair(mbedtls_ecp_keypair& kp) {
    mbedtls_ecp_keypair_init(&kp);
    int ret = mbedtls_ecp_group_load(&kp.grp, MBEDTLS_ECP_DP_SECP256R1);
    if (ret != 0) {
        Serial.printf("Group load failed: -0x%04X\n", -ret);
        return false;
    }
    ret = mbedtls_ecp_gen_keypair(&kp.grp, &kp.d, &kp.Q,
                                  mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret != 0) {
        Serial.printf("Keypair generation failed: -0x%04X\n", -ret);
        return false;
    }
    return true;
}

// --- Public key export/import ---
bool exportUncompressedPub(const mbedtls_ecp_keypair& kp, std::vector<uint8_t>& out65) {
    size_t olen = 0;
    out65.resize(65);
    int ret = mbedtls_ecp_point_write_binary(&kp.grp, &kp.Q,
                                             MBEDTLS_ECP_PF_UNCOMPRESSED,
                                             &olen, out65.data(), out65.size());
    if (ret != 0 || olen != 65) {
        Serial.printf("Pubkey export failed: -0x%04X\n", -ret);
        return false;
    }
    return true;
}

bool importPeerUncompressedPub(const std::vector<uint8_t>& in65,
                               mbedtls_ecp_point& Qp,
                               mbedtls_ecp_group& grp) {
    mbedtls_ecp_point_init(&Qp);
    int ret = mbedtls_ecp_point_read_binary(&grp, &Qp, in65.data(), in65.size());
    if (ret != 0) {
        Serial.printf("Peer pub import failed: -0x%04X\n", -ret);
        return false;
    }
    return true;
}

// --- Shared secret and key derivation ---
bool ecdhShared(mbedtls_ecp_group& grp, const mbedtls_mpi& d,
                const mbedtls_ecp_point& Qp, uint8_t out32[32]) {
    mbedtls_mpi z;
    mbedtls_mpi_init(&z);
    int ret = mbedtls_ecdh_compute_shared(&grp, &z, &Qp, &d,
                                          mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret != 0) {
        Serial.printf("ECDH compute failed: -0x%04X\n", -ret);
        mbedtls_mpi_free(&z);
        return false;
    }
    ret = mbedtls_mpi_write_binary(&z, out32, 32);
    mbedtls_mpi_free(&z);
    if (ret != 0) {
        Serial.printf("Shared secret export failed: -0x%04X\n", -ret);
        return false;
    }
    return true;
}

bool hkdfSha256(const uint8_t* ikm32, const uint8_t* salt, size_t saltLen,
                const uint8_t* info, size_t infoLen,
                uint8_t okm32[32]) {
    const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    uint8_t prk[32];

    // --- Extract: PRK = HMAC(salt, IKM) ---
    mbedtls_md_context_t hctx;
    mbedtls_md_init(&hctx);
    mbedtls_md_setup(&hctx, md, 1);
    mbedtls_md_hmac_starts(&hctx, salt, saltLen);
    mbedtls_md_hmac_update(&hctx, ikm32, 32);
    mbedtls_md_hmac_finish(&hctx, prk);

    // --- Expand: OKM = HMAC(PRK, T(i-1) | info | i) ---
    uint8_t T[32];
    size_t Tlen = 0;
    size_t pos = 0;
    uint8_t counter = 1;

    while (pos < 32) {
        mbedtls_md_hmac_starts(&hctx, prk, sizeof(prk));
        if (Tlen > 0)
            mbedtls_md_hmac_update(&hctx, T, Tlen);
        if (info && infoLen > 0)
            mbedtls_md_hmac_update(&hctx, info, infoLen);
        mbedtls_md_hmac_update(&hctx, &counter, 1);
        mbedtls_md_hmac_finish(&hctx, T);

        size_t to_copy = (32 - pos > 32) ? 32 : (32 - pos);
        memcpy(okm32 + pos, T, to_copy);
        pos += to_copy;
        Tlen = 32;
        counter++;
    }

    mbedtls_md_free(&hctx);
    return true;
}

bool hmacSha256(const uint8_t* key32, const uint8_t* msg, size_t msgLen,
                uint8_t mac32[32]) {
    const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    int ret = mbedtls_md_hmac(md, key32, 32, msg, msgLen, mac32);
    if (ret != 0) {
        Serial.printf("HMAC failed: -0x%04X\n", -ret);
        return false;
    }
    return true;
}
