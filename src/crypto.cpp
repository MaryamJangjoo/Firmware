#include "crypto.hpp"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

#include <mbedtls/md.h>
#include <mbedtls/pk.h>
#include <mbedtls/gcm.h>
#include <mbedtls/base64.h>
#include <mbedtls/pkcs5.h>

// ============================================================
// Global crypto state
// ============================================================

static mbedtls_entropy_context g_entropy;
static mbedtls_ctr_drbg_context g_ctrDrbg;

static bool g_cryptoInitialized = false;

static Preferences g_keyStore;

// ============================================================
// NVS
// ============================================================

static constexpr const char* NVS_NAMESPACE = "mybus-crypto";
static constexpr const char* NVS_PRIVATE_KEY = "device_priv";

// ============================================================
// DRBG
// ============================================================

static constexpr const char* DRBG_PERSONALIZATION =
    "ecosmart-mybus-v2";

// ============================================================
// Secure Zero
// ============================================================

void cryptoSecureZero(void* buffer, size_t length) {

    if (!buffer || length == 0) {
        return;
    }

    volatile uint8_t* p =
        static_cast<volatile uint8_t*>(buffer);

    while (length--) {
        *p++ = 0;
    }
}

// ============================================================
// CRYPTO INIT
// ============================================================

bool cryptoInit() {

    if (g_cryptoInitialized) {
        return true;
    }

    mbedtls_entropy_init(&g_entropy);
    mbedtls_ctr_drbg_init(&g_ctrDrbg);

    const unsigned char* personalization =
        reinterpret_cast<const unsigned char*>(
            DRBG_PERSONALIZATION
        );

    int ret = mbedtls_ctr_drbg_seed(
        &g_ctrDrbg,
        mbedtls_entropy_func,
        &g_entropy,
        personalization,
        strlen(DRBG_PERSONALIZATION)
    );

    if (ret != 0) {

        Serial.printf(
            "[CRYPTO] DRBG initialization failed: -0x%04X\n",
            -ret
        );

        mbedtls_ctr_drbg_free(&g_ctrDrbg);
        mbedtls_entropy_free(&g_entropy);

        return false;
    }

    g_cryptoInitialized = true;

    Serial.println("[CRYPTO] initialized");

    return true;
}

// ============================================================
// RANDOM
// ============================================================

bool cryptoRandomBytes(
    uint8_t* output,
    size_t length
) {

    if (!output || length == 0) {
        return false;
    }

    if (!cryptoInit()) {
        return false;
    }

    return mbedtls_ctr_drbg_random(
        &g_ctrDrbg,
        output,
        length
    ) == 0;
}

// ============================================================
// GENERATE DEVICE KEYPAIR
// ============================================================

bool generateDeviceKeypair(
    mbedtls_ecp_keypair& kp
) {

    if (!cryptoInit()) {
        return false;
    }

    mbedtls_ecp_keypair_init(&kp);

    int ret = mbedtls_ecp_group_load(
        &kp.grp,
        MBEDTLS_ECP_DP_SECP256R1
    );

    if (ret != 0) {

        Serial.printf(
            "[CRYPTO] ECP group load failed: -0x%04X\n",
            -ret
        );

        mbedtls_ecp_keypair_free(&kp);

        return false;
    }

    ret = mbedtls_ecp_gen_keypair(
        &kp.grp,
        &kp.d,
        &kp.Q,
        mbedtls_ctr_drbg_random,
        &g_ctrDrbg
    );

    if (ret != 0) {

        Serial.printf(
            "[CRYPTO] ECDH key generation failed: -0x%04X\n",
            -ret
        );

        mbedtls_ecp_keypair_free(&kp);

        return false;
    }

    ret = mbedtls_ecp_check_pubkey(
        &kp.grp,
        &kp.Q
    );

    if (ret != 0) {

        Serial.printf(
            "[CRYPTO] Generated public key invalid: -0x%04X\n",
            -ret
        );

        mbedtls_ecp_keypair_free(&kp);

        return false;
    }

    return true;
}

// ============================================================
// SAVE DEVICE PRIVATE KEY
// ============================================================

bool saveDeviceKeypair(
    const mbedtls_ecp_keypair& kp
) {

    uint8_t privateKey[ECC_PRIVATE_KEY_SIZE];

    memset(
        privateKey,
        0,
        sizeof(privateKey)
    );

    int ret = mbedtls_mpi_write_binary(
        &kp.d,
        privateKey,
        sizeof(privateKey)
    );

    if (ret != 0) {

        cryptoSecureZero(
            privateKey,
            sizeof(privateKey)
        );

        return false;
    }

    if (!g_keyStore.begin(
        NVS_NAMESPACE,
        false
    )) {

        Serial.println(
            "[CRYPTO] NVS open for write failed"
        );

        cryptoSecureZero(
            privateKey,
            sizeof(privateKey)
        );

        return false;
    }

    size_t written = g_keyStore.putBytes(
        NVS_PRIVATE_KEY,
        privateKey,
        sizeof(privateKey)
    );

    g_keyStore.end();

    cryptoSecureZero(
        privateKey,
        sizeof(privateKey)
    );

    if (written != sizeof(privateKey)) {

        Serial.println(
            "[CRYPTO] Failed to save private key"
        );

        return false;
    }

    Serial.println(
        "[CRYPTO] Device private key saved"
    );

    return true;
}

// ============================================================
// LOAD DEVICE KEYPAIR
// ============================================================

bool loadDeviceKeypair(
    mbedtls_ecp_keypair& kp
) {

    uint8_t privateKey[ECC_PRIVATE_KEY_SIZE];

    memset(
        privateKey,
        0,
        sizeof(privateKey)
    );

    if (!g_keyStore.begin(
        NVS_NAMESPACE,
        true
    )) {

        Serial.println(
            "[CRYPTO] No key namespace found."
        );

        Serial.println(
            "[CRYPTO] Generating new device key."
        );

        if (!generateDeviceKeypair(kp)) {
            return false;
        }

        if (!saveDeviceKeypair(kp)) {

            mbedtls_ecp_keypair_free(&kp);

            return false;
        }

        return true;
    }

    size_t storedLength =
        g_keyStore.getBytesLength(
            NVS_PRIVATE_KEY
        );

    if (storedLength != sizeof(privateKey)) {

        g_keyStore.end();

        Serial.println(
            "[CRYPTO] No valid device key found."
        );

        Serial.println(
            "[CRYPTO] Generating new device key."
        );

        if (!generateDeviceKeypair(kp)) {
            return false;
        }

        if (!saveDeviceKeypair(kp)) {

            mbedtls_ecp_keypair_free(&kp);

            return false;
        }

        return true;
    }

    size_t read = g_keyStore.getBytes(
        NVS_PRIVATE_KEY,
        privateKey,
        sizeof(privateKey)
    );

    g_keyStore.end();

    if (read != sizeof(privateKey)) {

        cryptoSecureZero(
            privateKey,
            sizeof(privateKey)
        );

        Serial.println(
            "[CRYPTO] Failed to read private key"
        );

        return false;
    }

    mbedtls_ecp_keypair_init(&kp);

    int ret = mbedtls_ecp_group_load(
        &kp.grp,
        MBEDTLS_ECP_DP_SECP256R1
    );

    if (ret != 0) {

        cryptoSecureZero(
            privateKey,
            sizeof(privateKey)
        );

        mbedtls_ecp_keypair_free(&kp);

        return false;
    }

    ret = mbedtls_mpi_read_binary(
        &kp.d,
        privateKey,
        sizeof(privateKey)
    );

    cryptoSecureZero(
        privateKey,
        sizeof(privateKey)
    );

    if (ret != 0) {

        Serial.printf(
            "[CRYPTO] Private key restore failed: -0x%04X\n",
            -ret
        );

        mbedtls_ecp_keypair_free(&kp);

        return false;
    }

    if (mbedtls_mpi_bitlen(&kp.d) == 0) {

        Serial.println(
            "[CRYPTO] Stored private key is zero"
        );

        mbedtls_ecp_keypair_free(&kp);

        return false;
    }

    ret = mbedtls_ecp_mul(
        &kp.grp,
        &kp.Q,
        &kp.d,
        &kp.grp.G,
        mbedtls_ctr_drbg_random,
        &g_ctrDrbg
    );

    if (ret != 0) {

        Serial.printf(
            "[CRYPTO] Public key reconstruction failed: -0x%04X\n",
            -ret
        );

        mbedtls_ecp_keypair_free(&kp);

        return false;
    }

    ret = mbedtls_ecp_check_pubkey(
        &kp.grp,
        &kp.Q
    );

    if (ret != 0) {

        Serial.printf(
            "[CRYPTO] Reconstructed public key invalid: -0x%04X\n",
            -ret
        );

        mbedtls_ecp_keypair_free(&kp);

        return false;
    }

    Serial.println(
        "[CRYPTO] Device keypair loaded from NVS"
    );

    return true;
}

// ============================================================
// EXPORT PUBLIC KEY PEM
// ============================================================

bool cryptoExportPublicKeyPem(
    const mbedtls_ecp_keypair& kp,
    String& pem
) {

    pem = "";

    mbedtls_pk_context pk;

    mbedtls_pk_init(&pk);

    int ret = mbedtls_pk_setup(
        &pk,
        mbedtls_pk_info_from_type(
            MBEDTLS_PK_ECKEY
        )
    );

    if (ret != 0) {

        mbedtls_pk_free(&pk);

        return false;
    }

    mbedtls_ecp_keypair* key =
        mbedtls_pk_ec(pk);

    ret = mbedtls_ecp_group_copy(
        &key->grp,
        &kp.grp
    );

    if (ret != 0) {

        mbedtls_pk_free(&pk);

        return false;
    }

    ret = mbedtls_ecp_copy(
        &key->Q,
        &kp.Q
    );

    if (ret != 0) {

        mbedtls_pk_free(&pk);

        return false;
    }

    uint8_t buffer[512];

    memset(
        buffer,
        0,
        sizeof(buffer)
    );

    ret = mbedtls_pk_write_pubkey_pem(
        &pk,
        buffer,
        sizeof(buffer)
    );

    if (ret != 0) {

        Serial.printf(
            "[CRYPTO] Public PEM export failed: -0x%04X\n",
            -ret
        );

        cryptoSecureZero(
            buffer,
            sizeof(buffer)
        );

        mbedtls_pk_free(&pk);

        return false;
    }

    pem = reinterpret_cast<char*>(buffer);

    cryptoSecureZero(
        buffer,
        sizeof(buffer)
    );

    mbedtls_pk_free(&pk);

    return true;
}

// ============================================================
// EXPORT PRIVATE KEY PEM
// ============================================================

bool cryptoExportPrivateKeyPem(
    const mbedtls_ecp_keypair& kp,
    String& pem
) {

    pem = "";

    if (mbedtls_mpi_bitlen(&kp.d) == 0) {
        return false;
    }

    mbedtls_pk_context pk;

    mbedtls_pk_init(&pk);

    int ret = mbedtls_pk_setup(
        &pk,
        mbedtls_pk_info_from_type(
            MBEDTLS_PK_ECKEY
        )
    );

    if (ret != 0) {

        mbedtls_pk_free(&pk);

        return false;
    }

    mbedtls_ecp_keypair* key =
        mbedtls_pk_ec(pk);

    ret = mbedtls_ecp_group_copy(
        &key->grp,
        &kp.grp
    );

    if (ret == 0) {

        ret = mbedtls_mpi_copy(
            &key->d,
            &kp.d
        );
    }

    if (ret == 0) {

        ret = mbedtls_ecp_copy(
            &key->Q,
            &kp.Q
        );
    }

    if (ret != 0) {

        mbedtls_pk_free(&pk);

        return false;
    }

    uint8_t buffer[1024];

    memset(
        buffer,
        0,
        sizeof(buffer)
    );

    ret = mbedtls_pk_write_key_pem(
        &pk,
        buffer,
        sizeof(buffer)
    );

    if (ret != 0) {

        Serial.printf(
            "[CRYPTO] Private PEM export failed: -0x%04X\n",
            -ret
        );

        cryptoSecureZero(
            buffer,
            sizeof(buffer)
        );

        mbedtls_pk_free(&pk);

        return false;
    }

    pem = reinterpret_cast<char*>(buffer);

    cryptoSecureZero(
        buffer,
        sizeof(buffer)
    );

    mbedtls_pk_free(&pk);

    return true;
}

// ============================================================
// GENERATE / LOAD PEM KEYPAIR
// ============================================================

bool cryptoGenerateKeyPairPem(
    String& privateKeyPem,
    String& publicKeyPem
) {

    privateKeyPem = "";
    publicKeyPem = "";

    mbedtls_ecp_keypair kp;

    if (!loadDeviceKeypair(kp)) {
        return false;
    }

    if (!cryptoExportPublicKeyPem(
        kp,
        publicKeyPem
    )) {

        mbedtls_ecp_keypair_free(&kp);

        return false;
    }

    if (!cryptoExportPrivateKeyPem(
        kp,
        privateKeyPem
    )) {

        publicKeyPem = "";

        mbedtls_ecp_keypair_free(&kp);

        return false;
    }

    mbedtls_ecp_keypair_free(&kp);

    return true;
}

// ============================================================
// ECDH SHARED SECRET
// ============================================================

bool ecdhShared(
    mbedtls_ecp_group& grp,
    const mbedtls_mpi& privateKey,
    const mbedtls_ecp_point& peerPublicKey,
    uint8_t output[ECC_SHARED_SECRET_SIZE]
) {

    if (!output) {
        return false;
    }

    if (!cryptoInit()) {
        return false;
    }

    if (mbedtls_mpi_bitlen(&privateKey) == 0) {
        return false;
    }

    int ret = mbedtls_ecp_check_pubkey(
        &grp,
        &peerPublicKey
    );

    if (ret != 0) {

        Serial.printf(
            "[CRYPTO] Invalid peer public key: -0x%04X\n",
            -ret
        );

        return false;
    }

    mbedtls_mpi shared;

    mbedtls_mpi_init(&shared);

    ret = mbedtls_ecdh_compute_shared(
        &grp,
        &shared,
        &peerPublicKey,
        &privateKey,
        mbedtls_ctr_drbg_random,
        &g_ctrDrbg
    );

    if (ret != 0) {

        Serial.printf(
            "[CRYPTO] ECDH failed: -0x%04X\n",
            -ret
        );

        mbedtls_mpi_free(&shared);

        return false;
    }

    ret = mbedtls_mpi_write_binary(
        &shared,
        output,
        ECC_SHARED_SECRET_SIZE
    );

    mbedtls_mpi_free(&shared);

    if (ret != 0) {

        cryptoSecureZero(
            output,
            ECC_SHARED_SECRET_SIZE
        );

        return false;
    }

    return true;
}

// ============================================================
// COMPUTE ECDH FROM PEM
// ============================================================

bool cryptoComputeSharedSecret(
    const String& privateKeyPem,
    const String& serverPublicKeyPem,
    String& sharedSecretHex
) {

    sharedSecretHex = "";

    if (
        privateKeyPem.length() == 0 ||
        serverPublicKeyPem.length() == 0
    ) {
        return false;
    }

    if (!cryptoInit()) {
        return false;
    }

    mbedtls_pk_context privatePk;
    mbedtls_pk_context serverPk;

    mbedtls_pk_init(&privatePk);
    mbedtls_pk_init(&serverPk);

    int ret = mbedtls_pk_parse_key(
        &privatePk,
        reinterpret_cast<const unsigned char*>(
            privateKeyPem.c_str()
        ),
        privateKeyPem.length() + 1,
        nullptr,
        0
    );

    if (ret != 0) {

        Serial.printf(
            "[CRYPTO] Private PEM parse failed: -0x%04X\n",
            -ret
        );

        mbedtls_pk_free(&privatePk);
        mbedtls_pk_free(&serverPk);

        return false;
    }

    ret = mbedtls_pk_parse_public_key(
        &serverPk,
        reinterpret_cast<const unsigned char*>(
            serverPublicKeyPem.c_str()
        ),
        serverPublicKeyPem.length() + 1
    );

    if (ret != 0) {

        Serial.printf(
            "[CRYPTO] Server public PEM parse failed: -0x%04X\n",
            -ret
        );

        mbedtls_pk_free(&privatePk);
        mbedtls_pk_free(&serverPk);

        return false;
    }

    if (
        !mbedtls_pk_can_do(
            &privatePk,
            MBEDTLS_PK_ECKEY
        ) ||
        !mbedtls_pk_can_do(
            &serverPk,
            MBEDTLS_PK_ECKEY
        )
    ) {

        Serial.println(
            "[CRYPTO] Keys are not EC keys"
        );

        mbedtls_pk_free(&privatePk);
        mbedtls_pk_free(&serverPk);

        return false;
    }

    mbedtls_ecp_keypair* deviceKey =
        mbedtls_pk_ec(privatePk);

    mbedtls_ecp_keypair* serverKey =
        mbedtls_pk_ec(serverPk);

    if (
        deviceKey->grp.id !=
        serverKey->grp.id
    ) {

        Serial.println(
            "[CRYPTO] Curve mismatch"
        );

        mbedtls_pk_free(&privatePk);
        mbedtls_pk_free(&serverPk);

        return false;
    }

    uint8_t sharedSecret[
        ECC_SHARED_SECRET_SIZE
    ];

    memset(
        sharedSecret,
        0,
        sizeof(sharedSecret)
    );

    bool success = ecdhShared(
        deviceKey->grp,
        deviceKey->d,
        serverKey->Q,
        sharedSecret
    );

    if (success) {

        sharedSecretHex =
            cryptoBytesToHex(
                sharedSecret,
                sizeof(sharedSecret)
            );
    }

    cryptoSecureZero(
        sharedSecret,
        sizeof(sharedSecret)
    );

    mbedtls_pk_free(&privatePk);
    mbedtls_pk_free(&serverPk);

    return success;
}

// ============================================================
// HKDF-SHA256 - پیاده‌سازی دستی (RFC 5869)
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
) {

    if (
        !ikm ||
        ikmLen == 0 ||
        !output ||
        outputLen == 0
    ) {
        return false;
    }

    if (outputLen > (255 * 32)) {
        return false;
    }

    // ✅ رفع باگ بالقوه: بافر داخلی buffer[64 + 256 + 1] در پایین فرض
    // می‌کند info حداکثر 256 بایت است، اما قبلاً هیچ چک صریحی روی
    // infoLen وجود نداشت. اگر این تابع در آینده با یک info طولانی‌تر
    // صدا زده می‌شد، این باعث buffer overflow روی استک می‌شد. الان
    // به‌جای رفتار نامشخص، به‌صراحت شکست می‌خورد.
    if (infoLen > 256) {
        Serial.println(
            "[CRYPTO] hkdfSha256: infoLen too large"
        );
        return false;
    }

    // ============================================================
    // STEP 1: HKDF-Extract
    // PRK = HMAC-SHA256(salt, IKM)
    // اگر salt داده نشده باشد، از 32 بایت صفر استفاده می‌شود
    // ============================================================

    uint8_t prk[32];
    memset(prk, 0, sizeof(prk));

    uint8_t zeroSalt[32];
    memset(zeroSalt, 0, sizeof(zeroSalt));

    const uint8_t* actualSalt = (salt != nullptr && saltLen > 0) ? salt : zeroSalt;
    size_t actualSaltLen = (salt != nullptr && saltLen > 0) ? saltLen : sizeof(zeroSalt);

    if (!hmacSha256(actualSalt, actualSaltLen, ikm, ikmLen, prk)) {
        cryptoSecureZero(prk, sizeof(prk));
        cryptoSecureZero(zeroSalt, sizeof(zeroSalt));
        return false;
    }

    cryptoSecureZero(zeroSalt, sizeof(zeroSalt));

    // ============================================================
    // STEP 2: HKDF-Expand
    // T(0) = empty
    // T(1) = HMAC(PRK, T(0) | info | 0x01)
    // ============================================================

    uint8_t T[32];
    memset(T, 0, sizeof(T));

    size_t position = 0;
    size_t Tlen = 0;
    uint8_t counter = 1;

    while (position < outputLen) {

        // ساخت بافر برای HMAC: T(prev) + info + counter
        uint8_t buffer[64 + 256 + 1]; // 32 + 256 + 1
        size_t bufferLen = 0;

        // اضافه کردن T قبلی (اگر وجود داشته باشد)
        if (Tlen > 0) {
            memcpy(buffer + bufferLen, T, Tlen);
            bufferLen += Tlen;
        }

        // اضافه کردن info
        if (info != nullptr && infoLen > 0) {
            memcpy(buffer + bufferLen, info, infoLen);
            bufferLen += infoLen;
        }

        // اضافه کردن counter
        buffer[bufferLen++] = counter;

        // محاسبه T(n) = HMAC-SHA256(PRK, buffer)
        if (!hmacSha256(prk, sizeof(prk), buffer, bufferLen, T)) {
            cryptoSecureZero(prk, sizeof(prk));
            cryptoSecureZero(T, sizeof(T));
            return false;
        }

        // کپی به خروجی
        size_t remaining = outputLen - position;
        size_t toCopy = (remaining > sizeof(T)) ? sizeof(T) : remaining;
        memcpy(output + position, T, toCopy);

        position += toCopy;
        Tlen = sizeof(T);
        counter++;
    }

    // پاک کردن اطلاعات حساس
    cryptoSecureZero(prk, sizeof(prk));
    cryptoSecureZero(T, sizeof(T));

    return true;
}

// ============================================================
// HMAC-SHA256
// ============================================================

bool hmacSha256(
    const uint8_t* key,
    size_t keyLen,
    const uint8_t* message,
    size_t messageLen,
    uint8_t output[32]
) {

    if (
        !key ||
        keyLen == 0 ||
        !message ||
        !output
    ) {
        return false;
    }

    const mbedtls_md_info_t* md =
        mbedtls_md_info_from_type(
            MBEDTLS_MD_SHA256
        );

    if (!md) {
        return false;
    }

    int ret = mbedtls_md_hmac(
        md,
        key,
        keyLen,
        message,
        messageLen,
        output
    );

    return ret == 0;
}

// ============================================================
// AES-256-GCM ENCRYPT
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
) {

    if (
        !key ||
        !iv ||
        !input ||
        !output ||
        !authTag
    ) {
        return false;
    }

    if (
        keyLen != AES256_KEY_SIZE ||
        ivLen != AES_GCM_IV_SIZE ||
        authTagLen != AES_GCM_TAG_SIZE
    ) {
        return false;
    }

    mbedtls_gcm_context ctx;

    mbedtls_gcm_init(&ctx);

    int ret = mbedtls_gcm_setkey(
        &ctx,
        MBEDTLS_CIPHER_ID_AES,
        key,
        256
    );

    if (ret != 0) {

        mbedtls_gcm_free(&ctx);

        return false;
    }

    ret = mbedtls_gcm_crypt_and_tag(
        &ctx,
        MBEDTLS_GCM_ENCRYPT,
        inputLen,
        iv,
        ivLen,
        nullptr,
        0,
        input,
        output,
        authTagLen,
        authTag
    );

    mbedtls_gcm_free(&ctx);

    return ret == 0;
}

// ============================================================
// AES-256-GCM DECRYPT
// ============================================================

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
) {

    if (
        !key ||
        !iv ||
        !input ||
        !authTag ||
        !output
    ) {
        return false;
    }

    if (
        keyLen != AES256_KEY_SIZE ||
        ivLen != AES_GCM_IV_SIZE ||
        authTagLen != AES_GCM_TAG_SIZE
    ) {
        return false;
    }

    mbedtls_gcm_context ctx;

    mbedtls_gcm_init(&ctx);

    int ret = mbedtls_gcm_setkey(
        &ctx,
        MBEDTLS_CIPHER_ID_AES,
        key,
        256
    );

    if (ret != 0) {

        mbedtls_gcm_free(&ctx);

        return false;
    }

    ret = mbedtls_gcm_auth_decrypt(
        &ctx,
        inputLen,
        iv,
        ivLen,
        nullptr,
        0,
        authTag,
        authTagLen,
        input,
        output
    );

    mbedtls_gcm_free(&ctx);

    return ret == 0;
}

// ============================================================
// BYTES -> HEX
// ============================================================

String cryptoBytesToHex(
    const uint8_t* data,
    size_t length
) {

    if (!data || length == 0) {
        return "";
    }

    static const char hex[] =
        "0123456789abcdef";

    String result;

    result.reserve(
        length * 2
    );

    for (size_t i = 0; i < length; ++i) {

        result +=
            hex[(data[i] >> 4) & 0x0F];

        result +=
            hex[data[i] & 0x0F];
    }

    return result;
}

// ============================================================
// HEX -> BYTES
// ============================================================

bool cryptoHexToBytes(
    const String& hex,
    uint8_t* output,
    size_t outputLength
) {

    if (
        !output ||
        hex.length() != outputLength * 2
    ) {
        return false;
    }

    auto hexValue = [](char c) -> int {

        if (
            c >= '0' &&
            c <= '9'
        ) {
            return c - '0';
        }

        if (
            c >= 'a' &&
            c <= 'f'
        ) {
            return c - 'a' + 10;
        }

        if (
            c >= 'A' &&
            c <= 'F'
        ) {
            return c - 'A' + 10;
        }

        return -1;
    };

    for (
        size_t i = 0;
        i < outputLength;
        ++i
    ) {

        int hi =
            hexValue(hex[i * 2]);

        int lo =
            hexValue(hex[i * 2 + 1]);

        if (
            hi < 0 ||
            lo < 0
        ) {

            cryptoSecureZero(
                output,
                outputLength
            );

            return false;
        }

        output[i] =
            static_cast<uint8_t>(
                (hi << 4) | lo
            );
    }

    return true;
}

// ============================================================
// ✅ Base64 - تبدیل bytes به Base64
// ============================================================

String cryptoBytesToBase64(
    const uint8_t* data,
    size_t length
) {
    if (data == nullptr || length == 0) {
        return "";
    }

    size_t outputLen = 0;
    int ret = mbedtls_base64_encode(nullptr, 0, &outputLen, data, length);
    if (ret != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        Serial.printf("[CRYPTO] Base64 buffer size calc failed: -0x%04X\n", -ret);
        return "";
    }

    uint8_t* output = new uint8_t[outputLen + 1];
    if (output == nullptr) {
        Serial.println("[CRYPTO] Base64 memory allocation failed");
        return "";
    }

    ret = mbedtls_base64_encode(output, outputLen + 1, &outputLen, data, length);
    if (ret != 0) {
        Serial.printf("[CRYPTO] Base64 encode failed: -0x%04X\n", -ret);
        delete[] output;
        return "";
    }

    String result = String(reinterpret_cast<char*>(output));
    delete[] output;

    return result;
}

// ============================================================
// ✅ Password Hashing (PBKDF2-HMAC-SHA256, format: "saltHex$hashHex")
// ============================================================

// هسته‌ی مشترک PBKDF2: با یک salt مشخص، هش را روی password محاسبه
// می‌کند. هم توسط cryptoHashPassword (با salt تصادفی تازه) و هم
// توسط cryptoVerifyPassword (با salt استخراج‌شده از رشته‌ی ذخیره‌شده)
// استفاده می‌شود تا منطق PBKDF2 فقط در یک نقطه وجود داشته باشد.
static bool pbkdf2Compute(
    const String& password,
    const uint8_t* salt,
    size_t saltLen,
    uint32_t iterations,
    uint8_t* outHash,
    size_t outHashLen
) {

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);

    const mbedtls_md_info_t* info =
        mbedtls_md_info_from_type(
            MBEDTLS_MD_SHA256
        );

    if (!info) {
        mbedtls_md_free(&ctx);
        return false;
    }

    // آرگومان دوم (hmac) باید 1 باشد تا context برای HMAC آماده شود
    // (پیش‌نیاز mbedtls_pkcs5_pbkdf2_hmac).
    int ret = mbedtls_md_setup(&ctx, info, 1);

    if (ret != 0) {
        Serial.printf(
            "[CRYPTO] PBKDF2 md_setup failed: -0x%04X\n",
            -ret
        );
        mbedtls_md_free(&ctx);
        return false;
    }

    ret = mbedtls_pkcs5_pbkdf2_hmac(
        &ctx,
        reinterpret_cast<const unsigned char*>(password.c_str()),
        password.length(),
        salt,
        saltLen,
        iterations,
        outHashLen,
        outHash
    );

    mbedtls_md_free(&ctx);

    if (ret != 0) {
        Serial.printf(
            "[CRYPTO] PBKDF2 computation failed: -0x%04X\n",
            -ret
        );
        return false;
    }

    return true;
}

bool cryptoHashPassword(
    const String& password,
    String& outCombined,
    uint32_t iterations
) {

    outCombined = "";

    if (password.isEmpty()) {
        Serial.println(
            "[CRYPTO] cryptoHashPassword: empty password"
        );
        return false;
    }

    if (!cryptoInit()) {
        return false;
    }

    uint8_t salt[PBKDF2_SALT_SIZE];

    if (!cryptoRandomBytes(salt, sizeof(salt))) {
        Serial.println(
            "[CRYPTO] cryptoHashPassword: salt generation failed"
        );
        return false;
    }

    uint8_t hash[PBKDF2_HASH_SIZE];
    memset(hash, 0, sizeof(hash));

    const bool ok = pbkdf2Compute(
        password,
        salt,
        sizeof(salt),
        iterations,
        hash,
        sizeof(hash)
    );

    if (!ok) {
        cryptoSecureZero(salt, sizeof(salt));
        cryptoSecureZero(hash, sizeof(hash));
        return false;
    }

    outCombined =
        cryptoBytesToHex(salt, sizeof(salt)) +
        String(PBKDF2_SEPARATOR) +
        cryptoBytesToHex(hash, sizeof(hash));

    cryptoSecureZero(salt, sizeof(salt));
    cryptoSecureZero(hash, sizeof(hash));

    return true;
}

bool cryptoVerifyPassword(
    const String& password,
    const String& storedCombined,
    uint32_t iterations
) {

    if (password.isEmpty() || storedCombined.isEmpty()) {
        return false;
    }

    if (!cryptoInit()) {
        return false;
    }

    const int sep = storedCombined.indexOf(PBKDF2_SEPARATOR);

    if (sep <= 0 || sep >= static_cast<int>(storedCombined.length()) - 1) {
        Serial.println(
            "[CRYPTO] cryptoVerifyPassword: malformed stored hash (no separator)"
        );
        return false;
    }

    const String saltHex = storedCombined.substring(0, sep);
    const String expectedHashHex = storedCombined.substring(sep + 1);

    if (saltHex.length() != PBKDF2_SALT_SIZE * 2 ||
        expectedHashHex.length() != PBKDF2_HASH_SIZE * 2) {

        Serial.println(
            "[CRYPTO] cryptoVerifyPassword: unexpected salt/hash length"
        );
        return false;
    }

    uint8_t salt[PBKDF2_SALT_SIZE];

    if (!cryptoHexToBytes(saltHex, salt, sizeof(salt))) {
        Serial.println(
            "[CRYPTO] cryptoVerifyPassword: salt hex decode failed"
        );
        return false;
    }

    uint8_t computedHash[PBKDF2_HASH_SIZE];
    memset(computedHash, 0, sizeof(computedHash));

    const bool ok = pbkdf2Compute(
        password,
        salt,
        sizeof(salt),
        iterations,
        computedHash,
        sizeof(computedHash)
    );

    cryptoSecureZero(salt, sizeof(salt));

    if (!ok) {
        cryptoSecureZero(computedHash, sizeof(computedHash));
        return false;
    }

    const String computedHashHex =
        cryptoBytesToHex(computedHash, sizeof(computedHash));

    cryptoSecureZero(computedHash, sizeof(computedHash));

    // ------------------------------------------------------
    // مقایسه‌ی constant-time (نه ==) برای مقاومت در برابر
    // timing attack روی طول/محتوای هش.
    // ------------------------------------------------------

    if (computedHashHex.length() != expectedHashHex.length()) {
        return false;
    }

    uint8_t diff = 0;

    for (size_t i = 0; i < computedHashHex.length(); ++i) {
        diff |= static_cast<uint8_t>(computedHashHex[i]) ^
                static_cast<uint8_t>(expectedHashHex[i]);
    }

    return diff == 0;
}