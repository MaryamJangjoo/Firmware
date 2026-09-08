#ifndef CRYPTO_HPP
#define CRYPTO_HPP

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include <vector>

#include <mbedtls/ecp.h>
#include <mbedtls/ecdh.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/md.h>
#include <mbedtls/pk.h>
#include <mbedtls/gcm.h>
#include <mbedtls/base64.h>
#include <mbedtls/hkdf.h>
#include <mbedtls/pkcs5.h>

// ============================================================
// Constants
// ============================================================

static constexpr size_t ECC_PRIVATE_KEY_SIZE = 32;
static constexpr size_t ECC_SHARED_SECRET_SIZE = 32;
static constexpr size_t AES256_KEY_SIZE = 32;
static constexpr size_t AES_GCM_IV_SIZE = 12;
static constexpr size_t AES_GCM_TAG_SIZE = 16;

// ============================================================
// Initialization
// ============================================================

bool cryptoInit();

// ============================================================
// Device keypair
// ============================================================

bool generateDeviceKeypair(mbedtls_ecp_keypair& kp);
bool saveDeviceKeypair(const mbedtls_ecp_keypair& kp);
bool loadDeviceKeypair(mbedtls_ecp_keypair& kp);

// ============================================================
// PEM
// ============================================================

bool cryptoExportPublicKeyPem(
    const mbedtls_ecp_keypair& kp,
    String& pem
);

bool cryptoExportPrivateKeyPem(
    const mbedtls_ecp_keypair& kp,
    String& pem
);

bool cryptoGenerateKeyPairPem(
    String& privateKeyPem,
    String& publicKeyPem
);

bool cryptoComputeSharedSecret(
    const String& privateKeyPem,
    const String& serverPublicKeyPem,
    String& sharedSecretHex
);

// ============================================================
// ECDH
// ============================================================

bool ecdhShared(
    mbedtls_ecp_group& grp,
    const mbedtls_mpi& privateKey,
    const mbedtls_ecp_point& peerPublicKey,
    uint8_t output[ECC_SHARED_SECRET_SIZE]
);

// ============================================================
// HKDF-SHA256
// ============================================================

bool hkdfSha256(
    const uint8_t* ikm,
    size_t ikmLen,
    const uint8_t* salt,
    size_t saltLen,
    const uint8_t* info,
    size_t infoLen,
    uint8_t* output,
    size_t outputLen
);

// ============================================================
// HMAC-SHA256
// ============================================================

bool hmacSha256(
    const uint8_t* key,
    size_t keyLen,
    const uint8_t* message,
    size_t messageLen,
    uint8_t output[32]
);

// ============================================================
// AES-256-GCM
// ============================================================

bool cryptoAesGcmEncrypt(
    const uint8_t* key,
    size_t keyLen,
    const uint8_t* iv,
    size_t ivLen,
    const uint8_t* input,
    size_t inputLen,
    uint8_t* output,
    uint8_t* authTag,
    size_t authTagLen
);

bool cryptoAesGcmDecrypt(
    const uint8_t* key,
    size_t keyLen,
    const uint8_t* iv,
    size_t ivLen,
    const uint8_t* input,
    size_t inputLen,
    const uint8_t* authTag,
    size_t authTagLen,
    uint8_t* output
);

// ============================================================
// HEX
// ============================================================

String cryptoBytesToHex(
    const uint8_t* data,
    size_t length
);

bool cryptoHexToBytes(
    const String& hex,
    uint8_t* output,
    size_t outputLength
);

// ============================================================
// Base64
// ============================================================

String cryptoBytesToBase64(
    const uint8_t* data,
    size_t length
);

// ============================================================
// Random & Secure
// ============================================================

bool cryptoRandomBytes(
    uint8_t* output,
    size_t length
);

void cryptoSecureZero(
    void* buffer,
    size_t length
);

// ============================================================
// ✅ Password Hashing (PBKDF2-HMAC-SHA256)
//
// جایگزین ذخیره‌ی پسورد به‌صورت متن‌ساده (plaintext) در
// CloudStorage::createDefaultUsersFile(). فرمت ذخیره‌سازی:
//
//     "<saltHex>$<hashHex>"
//
//   - salt: 16 بایت تصادفی (32 کاراکتر هگز)
//   - hash: خروجی 32 بایتی PBKDF2-HMAC-SHA256 (64 کاراکتر هگز)
//   - جداکننده: کاراکتر '$'
//
// این رشته‌ی ترکیبی (salt$hash) همان چیزی است که در فیلد
// UserInfo::passwordHash ذخیره می‌شود؛ یعنی ساختار فایل
// users.json تغییری نمی‌کند، فقط محتوای این فیلد دیگر پسورد
// خام نیست.
//
// تعداد iteration پیش‌فرض (10000) روی ESP32 چند ده تا چند صد
// میلی‌ثانیه طول می‌کشد؛ چون فقط در لحظه‌ی لاگین (نه در loop())
// صدا زده می‌شود، قابل قبول است.
// ============================================================

static constexpr size_t PBKDF2_SALT_SIZE = 16;
static constexpr size_t PBKDF2_HASH_SIZE = 32;
static constexpr uint32_t PBKDF2_DEFAULT_ITERATIONS = 10000;
static constexpr char PBKDF2_SEPARATOR = '$';

// یک salt تصادفی جدید تولید می‌کند، PBKDF2 را روی password اجرا
// می‌کند و نتیجه را به‌صورت "saltHex$hashHex" در outCombined
// برمی‌گرداند. برای هر بار فراخوانی (حتی با همان پسورد) یک salt
// جدید تولید می‌شود، پس دو خروجی برای یک پسورد یکسان هم متفاوت
// خواهند بود (این طبیعی و مطلوب است).
bool cryptoHashPassword(
    const String& password,
    String& outCombined,
    uint32_t iterations = PBKDF2_DEFAULT_ITERATIONS
);

// password ورودی را با فرمت ذخیره‌شده‌ی "saltHex$hashHex" مقایسه
// می‌کند. salt از خود storedCombined استخراج می‌شود، پس هر کاربر
// می‌تواند salt متفاوتی داشته باشد. مقایسه‌ی نهایی به‌صورت
// constant-time انجام می‌شود تا در برابر timing attack مقاوم باشد.
bool cryptoVerifyPassword(
    const String& password,
    const String& storedCombined,
    uint32_t iterations = PBKDF2_DEFAULT_ITERATIONS
);

// ============================================================
// Legacy functions for server.cpp compatibility
// ============================================================

inline bool exportUncompressedPub(
    const mbedtls_ecp_keypair& kp,
    std::vector<uint8_t>& out65
) {
    out65.resize(65);

    size_t olen = 0;

    int ret = mbedtls_ecp_point_write_binary(
        &kp.grp,
        &kp.Q,
        MBEDTLS_ECP_PF_UNCOMPRESSED,
        &olen,
        out65.data(),
        out65.size()
    );

    if (ret != 0 || olen != 65) {
        out65.clear();
        return false;
    }

    return true;
}

inline bool importPeerUncompressedPub(
    const std::vector<uint8_t>& in65,
    mbedtls_ecp_point& Qp,
    mbedtls_ecp_group& grp
) {
    if (in65.size() != 65) {
        return false;
    }

    int ret = mbedtls_ecp_point_read_binary(
        &grp,
        &Qp,
        in65.data(),
        in65.size()
    );

    if (ret != 0) {
        return false;
    }

    ret = mbedtls_ecp_check_pubkey(
        &grp,
        &Qp
    );

    return ret == 0;
}

#endif // CRYPTO_HPP