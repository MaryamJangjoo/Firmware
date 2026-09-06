#include "CloudManager.h"

#include <Arduino.h>
#include <esp_system.h>
#include <string.h>

#include <HTTPClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <Preferences.h>

#include <FS.h>

#ifdef USE_LittleFS
#include <LittleFS.h>
#else
#include <SPIFFS.h>
#endif

// ============================================================
// mYBUS v2 Constants
// ============================================================

static constexpr uint32_t HTTP_TIMEOUT_MS = 15000;

static constexpr char HKDF_SALT[] = "mYBUS-v2-Salt";
static constexpr char HKDF_INFO[] = "mYBUS-v2-Session";

// ============================================================
// ✅ تعریف ثابت‌های استاتیک خارج از کلاس (برای لینکر)
// ============================================================

constexpr uint8_t CloudManager::MYBUS_INTERFACE_WIFI;
constexpr uint8_t CloudManager::MYBUS_ZONE_DEFAULT;
constexpr uint8_t CloudManager::MYBUS_QOS_DEFAULT;
constexpr uint8_t CloudManager::MYBUS_OPTIONS_DEFAULT;
constexpr uint8_t CloudManager::MYBUS_COMPRESSION_NONE;
constexpr uint8_t CloudManager::MYBUS_FLAG_REQUEST;
constexpr uint8_t CloudManager::MYBUS_SECURITY_HANDSHAKE;
constexpr uint8_t CloudManager::MYBUS_SECURITY_ENCRYPTED;
constexpr uint8_t CloudManager::MYBUS_COMMAND_HANDSHAKE;
constexpr uint8_t CloudManager::MYBUS_COMMAND_REGISTRY;

// ============================================================
// Static instance
// ============================================================

CloudManager* CloudManager::s_instance = nullptr;

// ============================================================
// Constructor
// ============================================================

CloudManager::CloudManager()
    : _filesystemMounted(false),
      _hasRequestedData(false),
      server(nullptr),
      ws(nullptr),
      wsClient(nullptr),
      wsConnected(false),
      isAuthenticated(false),
      deviceKeypairInitialized(false),
      sessionKeyValid(false),
      requestNumber(0)
{
    memset(sessionKey, 0, sizeof(sessionKey));

    mbedtls_ecp_keypair_init(&deviceKeypair);

    if (!preferences.begin("cloud", false)) {
        Serial.println("[CLOUD] Preferences begin failed");
    }

    deviceId = generateDeviceId();

    String storedDeviceId = preferences.getString("deviceId", "");
    if (!storedDeviceId.isEmpty()) {
        deviceId = storedDeviceId;
    }

    loadToken();

    // ✅ بارگذاری Session Key از NVS
    Preferences mybusPrefs;
    if (mybusPrefs.begin("mybus", true)) {
        size_t len = mybusPrefs.getBytesLength("sessionKey");
        if (len == sizeof(sessionKey)) {
            mybusPrefs.getBytes("sessionKey", sessionKey, sizeof(sessionKey));
            sessionKeyValid = true;
            Serial.println("[mYBUS] 🔑 Session key loaded from NVS");
        } else {
            Serial.println("[mYBUS] ℹ️ No valid session key found in NVS");
        }
        mybusPrefs.end();
    }

    if (!cryptoInit()) {
        Serial.println("[CLOUD] Crypto initialization failed");
    } else {
        Serial.println("[CLOUD] Crypto initialized");
    }

    if (!initializeDeviceKeypair()) {
        Serial.println("[CLOUD] Device keypair initialization failed");
    }

    _filesystemMounted = initFilesystem();

    s_instance = this;

    Serial.println();
    Serial.println("========================================");
    Serial.println("[CLOUD] CloudManager initialized");
    Serial.println("========================================");
    Serial.print("[CLOUD] Device ID: ");
    Serial.println(deviceId);
    Serial.print("[CLOUD] API URL: ");
    Serial.println(apiBaseUrl);
    Serial.print("[CLOUD] Authenticated: ");
    Serial.println(isAuthenticated ? "YES" : "NO");
    Serial.print("[CLOUD] Device keypair: ");
    Serial.println(deviceKeypairInitialized ? "READY" : "NOT READY");
    Serial.print("[CLOUD] Secure session: ");
    Serial.println(sessionKeyValid ? "✅ ACTIVE" : "❌ NOT ACTIVE");
}

// ============================================================
// Destructor
// ============================================================

CloudManager::~CloudManager()
{
    if (ws != nullptr) {
        ws->closeAll();
        delete ws;
        ws = nullptr;
    }

    if (server != nullptr) {
        delete server;
        server = nullptr;
    }

    wsClient = nullptr;
    wsConnected = false;

    clearSecureSession();

    if (deviceKeypairInitialized) {
        mbedtls_ecp_keypair_free(&deviceKeypair);
        deviceKeypairInitialized = false;
    } else {
        mbedtls_ecp_keypair_free(&deviceKeypair);
    }

    preferences.end();

    if (s_instance == this) {
        s_instance = nullptr;
    }
}

// ============================================================
// Device ID
// ============================================================

String CloudManager::generateDeviceId()
{
    uint64_t mac = ESP.getEfuseMac();
    char buffer[40];
    snprintf(buffer, sizeof(buffer), "ESP32_ECOSMART_%012llX",
             static_cast<unsigned long long>(mac));
    return String(buffer);
}

// ============================================================
// Filesystem
// ============================================================

bool CloudManager::initFilesystem()
{
    if (_filesystemMounted) return true;

#ifdef USE_LittleFS
    if (!LittleFS.begin(true)) {
        Serial.println("[FS] LittleFS mount failed");
        return false;
    }
#else
    if (!SPIFFS.begin(true)) {
        Serial.println("[FS] SPIFFS mount failed");
        return false;
    }
#endif

    _filesystemMounted = true;
    Serial.println("[FS] Filesystem mounted successfully");
    listFiles();
    createDefaultUsersFile();
    createDefaultSiteInfoFile();

    return true;
}

bool CloudManager::isFilesystemMounted() const
{
    return _filesystemMounted;
}

bool CloudManager::readFile(const String& path, String& content)
{
    content = "";
    if (!_filesystemMounted && !initFilesystem()) return false;

#ifdef USE_LittleFS
    File file = LittleFS.open(path, "r");
#else
    File file = SPIFFS.open(path, "r");
#endif

    if (!file) {
        Serial.printf("[FS] Failed to open: %s\n", path.c_str());
        return false;
    }

    content = file.readString();
    file.close();
    return true;
}

bool CloudManager::writeFile(const String& path, const String& content)
{
    if (!_filesystemMounted && !initFilesystem()) return false;

#ifdef USE_LittleFS
    File file = LittleFS.open(path, "w");
#else
    File file = SPIFFS.open(path, "w");
#endif

    if (!file) {
        Serial.printf("[FS] Failed to write: %s\n", path.c_str());
        return false;
    }

    size_t written = file.print(content);
    file.close();

    if (written != content.length()) {
        Serial.printf("[FS] Write error: %u/%u bytes\n",
                      static_cast<unsigned>(written),
                      static_cast<unsigned>(content.length()));
        return false;
    }

    Serial.printf("[FS] Written: %s (%u bytes)\n",
                  path.c_str(), static_cast<unsigned>(written));
    return true;
}

void CloudManager::listFiles()
{
    if (!_filesystemMounted && !initFilesystem()) return;

    Serial.println("[FS] Listing files:");

#ifdef USE_LittleFS
    File root = LittleFS.open("/");
#else
    File root = SPIFFS.open("/");
#endif

    if (!root) {
        Serial.println("[FS] Failed to open root");
        return;
    }

    File file = root.openNextFile();
    while (file) {
        Serial.printf("  - %s (%u bytes)\n",
                      file.name(),
                      static_cast<unsigned>(file.size()));
        file = root.openNextFile();
    }
    root.close();
}

void CloudManager::printFileContent(const String& path)
{
    String content;
    if (!readFile(path, content)) {
        Serial.printf("[FS] Failed to read: %s\n", path.c_str());
        return;
    }
    Serial.printf("[FS] Content of %s:\n", path.c_str());
    Serial.println("--- START ---");
    Serial.println(content);
    Serial.println("--- END ---");
}

// ============================================================
// Default users
// ============================================================

bool CloudManager::createDefaultUsersFile()
{
    String existing;
    if (readFile("/users.json", existing)) {
        Serial.println("[FS] users.json already exists");
        return true;
    }

    JsonDocument doc;
    JsonArray users = doc["users"].to<JsonArray>();
    JsonObject user = users.add<JsonObject>();
    user["username"] = "tes29t_operator";
    user["passwordHash"] = "SecurePassword@2026";
    user["publicKey"] = "-----BEGIN PUBLIC KEY-----...";
    user["role"] = "OWNER";
    user["lastLogin"] = 0;
    doc["totalUsers"] = 1;
    doc["updatedAt"] = millis() / 1000;
    doc["version"] = millis() / 1000;

    String jsonContent;
    serializeJson(doc, jsonContent);

    if (writeFile("/users.json", jsonContent)) {
        Serial.println("[FS] users.json created");
        return true;
    }
    Serial.println("[FS] Failed to create users.json");
    return false;
}

bool CloudManager::createDefaultSiteInfoFile()
{
    String existing;
    if (readFile("/site_info.json", existing)) {
        Serial.println("[FS] site_info.json already exists");
        return true;
    }

    JsonDocument doc;
    doc["siteId"] = "4a550d6b-0332-4c34-a121-cf2163b8de2c";
    doc["siteName"] = "Home Site";
    doc["licenseKey"] = "LIC-2026-XXXX-YYYY";
    doc["expiryDate"] = 0;
    doc["maxUsers"] = 10;
    doc["updatedAt"] = millis() / 1000;

    String jsonContent;
    serializeJson(doc, jsonContent);

    if (writeFile("/site_info.json", jsonContent)) {
        Serial.println("[FS] site_info.json created");
        return true;
    }
    Serial.println("[FS] Failed to create site_info.json");
    return false;
}

// ============================================================
// Load users
// ============================================================

bool CloudManager::loadUsers(std::vector<UserInfo>& users)
{
    users.clear();
    String content;
    if (!readFile("/users.json", content)) {
        Serial.println("[FS] users.json not found");
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, content);
    if (error) {
        Serial.printf("[FS] users.json parse error: %s\n", error.c_str());
        return false;
    }

    JsonVariant usersVariant = doc["users"];
    if (!usersVariant.is<JsonArray>()) {
        Serial.println("[FS] Invalid users.json format");
        return false;
    }

    JsonArray arr = usersVariant.as<JsonArray>();
    for (JsonObject obj : arr) {
        UserInfo user;
        user.username = obj["username"] | "";
        user.passwordHash = obj["passwordHash"] | "";
        user.publicKey = obj["publicKey"] | "";
        user.role = obj["role"] | "USER";
        user.lastLogin = obj["lastLogin"] | 0;
        if (!user.username.isEmpty()) {
            users.push_back(user);
        }
    }

    Serial.printf("[FS] Loaded %u users\n",
                  static_cast<unsigned>(users.size()));
    return true;
}

bool CloudManager::saveUsers(const std::vector<UserInfo>& users)
{
    JsonDocument doc;
    JsonArray arr = doc["users"].to<JsonArray>();
    for (const auto& user : users) {
        JsonObject obj = arr.add<JsonObject>();
        obj["username"] = user.username;
        obj["passwordHash"] = user.passwordHash;
        obj["publicKey"] = user.publicKey;
        obj["role"] = user.role;
        obj["lastLogin"] = user.lastLogin;
    }
    doc["totalUsers"] = users.size();
    doc["updatedAt"] = millis() / 1000;
    doc["version"] = millis() / 1000;

    String content;
    serializeJson(doc, content);
    return writeFile("/users.json", content);
}

// ============================================================
// Load site info
// ============================================================

bool CloudManager::loadSiteInfo(SiteInfo& info)
{
    String content;
    if (!readFile("/site_info.json", content)) {
        Serial.println("[FS] site_info.json not found");
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, content);
    if (error) {
        Serial.printf("[FS] site_info.json parse error: %s\n", error.c_str());
        return false;
    }

    info.siteId = doc["siteId"] | "";
    info.siteName = doc["siteName"] | "";
    info.licenseKey = doc["licenseKey"] | "";
    info.expiryDate = doc["expiryDate"] | 0;
    info.maxUsers = doc["maxUsers"] | 10;

    Serial.printf("[FS] Site Info: ID=%s Name=%s\n",
                  info.siteId.c_str(), info.siteName.c_str());
    return true;
}

bool CloudManager::saveSiteInfo(const SiteInfo& info)
{
    JsonDocument doc;
    doc["siteId"] = info.siteId;
    doc["siteName"] = info.siteName;
    doc["licenseKey"] = info.licenseKey;
    doc["expiryDate"] = info.expiryDate;
    doc["maxUsers"] = info.maxUsers;
    doc["updatedAt"] = millis() / 1000;

    String content;
    serializeJson(doc, content);
    return writeFile("/site_info.json", content);
}

// ============================================================
// Offline login
// ============================================================

bool CloudManager::loginOffline(const String& username, const String& password)
{
    if (username.isEmpty() || password.isEmpty()) return false;

    std::vector<UserInfo> users;
    if (!loadUsers(users)) {
        Serial.println("[AUTH] No users available for offline login");
        return false;
    }

    for (const auto& user : users) {
        if (user.username == username) {
            return verifyUserPassword(username, password);
        }
    }

    Serial.printf("[AUTH] User '%s' not found\n", username.c_str());
    return false;
}

bool CloudManager::verifyUserPassword(const String& username, const String& password)
{
    std::vector<UserInfo> users;
    if (!loadUsers(users)) return false;

    for (const auto& user : users) {
        if (user.username == username) {
            if (user.passwordHash == password) {
                Serial.printf("[AUTH] User '%s' verified offline\n", username.c_str());
                return true;
            }
            Serial.printf("[AUTH] Invalid password for '%s'\n", username.c_str());
            return false;
        }
    }
    return false;
}

// ============================================================
// Request site info
// ============================================================

void CloudManager::requestSiteInfo()
{
    if (!isWebSocketConnected()) {
        Serial.println("[CLOUD] WebSocket not connected");
        return;
    }

    JsonDocument request;
    request["action"] = "GET_SITE_INFO";
    request["deviceId"] = deviceId;
    sendRealtimeData(request);
}

void CloudManager::requestUsersList()
{
    if (!isWebSocketConnected()) {
        Serial.println("[CLOUD] WebSocket not connected");
        return;
    }

    JsonDocument request;
    request["action"] = "GET_USERS";
    request["deviceId"] = deviceId;
    sendRealtimeData(request);
}

// ============================================================
// Device keypair initialization
// ============================================================

bool CloudManager::initializeDeviceKeypair()
{
    if (deviceKeypairInitialized) return true;

    if (!cryptoInit()) {
        Serial.println("[CLOUD] cryptoInit failed");
        return false;
    }

    if (!loadDeviceKeypair(deviceKeypair)) {
        Serial.println("[CLOUD] Failed to load/generate device keypair");
        return false;
    }

    deviceKeypairInitialized = true;

    devicePublicKeyPem = "";
    if (!cryptoExportPublicKeyPem(deviceKeypair, devicePublicKeyPem)) {
        Serial.println("[CLOUD] Public key PEM export failed");
        mbedtls_ecp_keypair_free(&deviceKeypair);
        mbedtls_ecp_keypair_init(&deviceKeypair);
        deviceKeypairInitialized = false;
        return false;
    }

    devicePrivateKeyPem = "";
    if (!cryptoExportPrivateKeyPem(deviceKeypair, devicePrivateKeyPem)) {
        Serial.println("[CLOUD] Private key PEM export failed");
        mbedtls_ecp_keypair_free(&deviceKeypair);
        mbedtls_ecp_keypair_init(&deviceKeypair);
        deviceKeypairInitialized = false;
        return false;
    }

    if (devicePublicKeyPem.isEmpty() || devicePrivateKeyPem.isEmpty()) {
        Serial.println("[CLOUD] PEM export produced empty key");
        mbedtls_ecp_keypair_free(&deviceKeypair);
        mbedtls_ecp_keypair_init(&deviceKeypair);
        deviceKeypairInitialized = false;
        return false;
    }

    Serial.println("[CLOUD] Device ECDH keypair ready");
    Serial.printf("[CLOUD] Public PEM length: %u\n",
                  static_cast<unsigned>(devicePublicKeyPem.length()));
    Serial.printf("[CLOUD] Private PEM length: %u\n",
                  static_cast<unsigned>(devicePrivateKeyPem.length()));

    return true;
}

// ============================================================
// Login
// ============================================================

bool CloudManager::loginUser(const String& username, const String& password,
                             const String& requestedDeviceId)
{
    Serial.println("[AUTH] ========================================");
    Serial.println("[AUTH] PHASE 0: Device Login Started");
    Serial.printf("[AUTH] Username: %s\n", username.c_str());
    Serial.printf("[AUTH] Device ID: %s\n", requestedDeviceId.c_str());
    Serial.println("[AUTH] ========================================");

    if (username.isEmpty() || password.isEmpty() || requestedDeviceId.isEmpty()) {
        Serial.println("[AUTH] ❌ Invalid login parameters");
        return false;
    }

    deviceId = requestedDeviceId;

    JsonDocument doc;
    doc["username"] = username;
    doc["password"] = password;
    doc["deviceId"] = deviceId;

    String body;
    serializeJson(doc, body);

    Serial.println("[AUTH] Sending login request...");

    String response = sendRequest("/devices/login", "POST", body, false);

    if (response.isEmpty()) {
        Serial.println("[AUTH] ❌ Login failed - empty response");
        return false;
    }

    JsonDocument responseDoc;
    DeserializationError error = deserializeJson(responseDoc, response);
    if (error) {
        Serial.printf("[AUTH] ❌ JSON parse error: %s\n", error.c_str());
        return false;
    }

    const char* accessToken = responseDoc["accessToken"];
    if (accessToken == nullptr || strlen(accessToken) == 0) {
        Serial.println("[AUTH] ❌ Login response has no accessToken");
        Serial.println(response);
        return false;
    }

    jwtToken = String(accessToken);
    saveToken(jwtToken);

    const char* refreshToken = responseDoc["refreshToken"];
    if (refreshToken != nullptr) {
        refreshTokenValue = String(refreshToken);
        preferences.putString("refreshToken", refreshTokenValue);
    }

    const char* responseDeviceId = responseDoc["deviceId"];
    if (responseDeviceId != nullptr) {
        deviceId = String(responseDeviceId);
    }
    preferences.putString("deviceId", deviceId);

    const char* responseSiteId = responseDoc["siteId"];
    if (responseSiteId != nullptr) {
        siteId = String(responseSiteId);
    }

    isAuthenticated = true;
    clearSecureSession();

    Serial.println("[AUTH] ✅ Device login successful");
    Serial.println("[AUTH] ========================================");
    return true;
}

// ============================================================
// Refresh token
// ============================================================

bool CloudManager::refreshToken()
{
    if (refreshTokenValue.isEmpty()) {
        Serial.println("[AUTH] No refresh token");
        return false;
    }

    JsonDocument doc;
    String body;
    serializeJson(doc, body);

    String response = sendRequest("/auth/refresh", "POST", body, true);

    if (response.isEmpty()) {
        Serial.println("[AUTH] Refresh failed");
        return false;
    }

    JsonDocument responseDoc;
    DeserializationError error = deserializeJson(responseDoc, response);
    if (error) {
        Serial.printf("[AUTH] Refresh JSON error: %s\n", error.c_str());
        return false;
    }

    String newToken;
    if (responseDoc["accessToken"].is<const char*>()) {
        newToken = responseDoc["accessToken"].as<String>();
    } else if (responseDoc["access_token"].is<const char*>()) {
        newToken = responseDoc["access_token"].as<String>();
    }

    if (newToken.isEmpty()) {
        Serial.println("[AUTH] Refresh response has no access token");
        return false;
    }

    jwtToken = newToken;
    saveToken(jwtToken);

    const char* newRefresh = responseDoc["refreshToken"];
    if (newRefresh != nullptr) {
        refreshTokenValue = String(newRefresh);
        preferences.putString("refreshToken", refreshTokenValue);
    }

    isAuthenticated = true;
    Serial.println("[AUTH] Access token refreshed");
    return true;
}

// ============================================================
// Auth state
// ============================================================

bool CloudManager::isLoggedIn() const
{
    return isAuthenticated && !jwtToken.isEmpty();
}

bool CloudManager::isTokenValid() const
{
    return !jwtToken.isEmpty();
}

// ============================================================
// Nonce
// ============================================================

String CloudManager::createNonce()
{
    uint8_t randomBytes[32];
    memset(randomBytes, 0, sizeof(randomBytes));

    if (!cryptoRandomBytes(randomBytes, sizeof(randomBytes))) {
        cryptoSecureZero(randomBytes, sizeof(randomBytes));
        return "";
    }

    String nonce = cryptoBytesToHex(randomBytes, sizeof(randomBytes));
    cryptoSecureZero(randomBytes, sizeof(randomBytes));
    return nonce;
}

// ============================================================
// Handshake nonce
// ============================================================

bool CloudManager::generateHandshakeNonce()
{
    handshakeNonce = createNonce();
    Serial.printf("[mYBUS] 🔑 Nonce generated: %s\n", handshakeNonce.c_str());
    return !handshakeNonce.isEmpty();
}

// ============================================================
// HMAC HEX
// ============================================================

String CloudManager::hmacHex(const uint8_t* key, size_t keyLen,
                              const uint8_t* message, size_t messageLen)
{
    uint8_t mac[32];
    memset(mac, 0, sizeof(mac));

    if (!hmacSha256(key, keyLen, message, messageLen, mac)) {
        cryptoSecureZero(mac, sizeof(mac));
        return "";
    }

    String result = cryptoBytesToHex(mac, sizeof(mac));
    cryptoSecureZero(mac, sizeof(mac));
    return result;
}

// ============================================================
// Challenge HMAC
// ============================================================

bool CloudManager::createChallengeHmac(String& hmacHexOut)
{
    hmacHexOut = "";
    if (!sessionKeyValid || handshakeNonce.isEmpty()) {
        Serial.println("[mYBUS] ❌ Cannot create HMAC: session invalid or nonce empty");
        return false;
    }

    hmacHexOut = hmacHex(sessionKey, sizeof(sessionKey),
                         reinterpret_cast<const uint8_t*>(handshakeNonce.c_str()),
                         handshakeNonce.length());

    Serial.printf("[mYBUS] 🔑 Challenge HMAC: %s\n", hmacHexOut.c_str());
    return hmacHexOut.length() == 64;
}

// ============================================================
// Compute session key
// ============================================================

bool CloudManager::computeSessionKey()
{
    Serial.println("[mYBUS] 🔐 ========================================");
    Serial.println("[mYBUS] 🔐 Computing Session Key");

    if (serverPublicKeyPem.isEmpty()) {
        Serial.println("[mYBUS] ❌ Missing server public key");
        return false;
    }

    if (!deviceKeypairInitialized) {
        Serial.println("[mYBUS] ❌ Device keypair not initialized");
        return false;
    }

    if (devicePrivateKeyPem.isEmpty()) {
        Serial.println("[mYBUS] ❌ Device private key PEM is empty");
        return false;
    }

    Serial.printf("[mYBUS] 🔐 Server Public Key length: %u\n", serverPublicKeyPem.length());

    String sharedSecretHex;
    if (!cryptoComputeSharedSecret(devicePrivateKeyPem, serverPublicKeyPem, sharedSecretHex)) {
        Serial.println("[mYBUS] ❌ ECDH shared secret failed");
        return false;
    }

    if (sharedSecretHex.isEmpty()) {
        Serial.println("[mYBUS] ❌ Empty ECDH shared secret");
        return false;
    }

    Serial.printf("[mYBUS] 🔐 Shared Secret (hex): %s\n", sharedSecretHex.c_str());

    uint8_t sharedSecret[32];
    memset(sharedSecret, 0, sizeof(sharedSecret));

    if (!cryptoHexToBytes(sharedSecretHex, sharedSecret, sizeof(sharedSecret))) {
        cryptoSecureZero(sharedSecret, sizeof(sharedSecret));
        Serial.println("[mYBUS] ❌ Shared secret conversion failed");
        return false;
    }

    cryptoSecureZero(sessionKey, sizeof(sessionKey));

    bool success = hkdfSha256(
        sharedSecret, sizeof(sharedSecret),
        reinterpret_cast<const uint8_t*>(HKDF_SALT), strlen(HKDF_SALT),
        reinterpret_cast<const uint8_t*>(HKDF_INFO), strlen(HKDF_INFO),
        sessionKey, sizeof(sessionKey)
    );

    cryptoSecureZero(sharedSecret, sizeof(sharedSecret));

    if (!success) {
        cryptoSecureZero(sessionKey, sizeof(sessionKey));
        Serial.println("[mYBUS] ❌ HKDF derivation failed");
        return false;
    }

    sessionKeyValid = true;

    // ✅ ذخیره Session Key در NVS
    Preferences mybusPrefs;
    if (mybusPrefs.begin("mybus", false)) {
        mybusPrefs.putBytes("sessionKey", sessionKey, sizeof(sessionKey));
        mybusPrefs.end();
        Serial.println("[mYBUS] 💾 Session key saved to NVS");
    } else {
        Serial.println("[mYBUS] ⚠️ Failed to save session key to NVS");
    }

    Serial.printf("[mYBUS] 🔑 Session Key (hex): ");
    for (int i = 0; i < 32; i++) {
        Serial.printf("%02x", sessionKey[i]);
    }
    Serial.println();

    Serial.printf("[mYBUS] 🔑 Session Key (base64): ");
    String base64Key = cryptoBytesToBase64(sessionKey, sizeof(sessionKey));
    Serial.println(base64Key);

    Serial.println("[mYBUS] 🔐 ========================================");
    Serial.println("[mYBUS] ✅ Session key derived successfully");
    return true;
}

// ============================================================
// Phase 2
// ============================================================

bool CloudManager::authenticateHandshakeSession()
{
    Serial.println("[mYBUS] 🔑 ========================================");
    Serial.println("[mYBUS] 🔑 PHASE 2: HMAC Verification Started");

    String hmac;
    if (!createChallengeHmac(hmac)) {
        Serial.println("[mYBUS] ❌ Challenge HMAC creation failed");
        return false;
    }

    JsonDocument phase2;
    phase2["protocolVersion"] = MYBUS_PROTOCOL_VERSION;
    phase2["communicationInterface"] = MYBUS_INTERFACE_WIFI;
    phase2["zoneId"] = MYBUS_ZONE_DEFAULT;
    phase2["deviceId"] = deviceId;
    phase2["requestNumber"] = nextRequestNumber();
    phase2["flags"] = 0;
    phase2["security"] = MYBUS_SECURITY_HANDSHAKE;
    phase2["command"] = MYBUS_COMMAND_HANDSHAKE;

    JsonObject data = phase2["data"].to<JsonObject>();
    data["nonce"] = handshakeNonce;
    data["hmac"] = hmac;

    String body;
    serializeJson(phase2, body);

    Serial.printf("[mYBUS] 📤 Nonce: %s\n", handshakeNonce.c_str());
    Serial.printf("[mYBUS] 📤 HMAC: %s\n", hmac.c_str());
    Serial.println("[mYBUS] 🔑 ========================================");

    String response = sendRequest("/devices/handshake", "POST", body, true);

    if (response.isEmpty()) {
        Serial.println("[mYBUS] ❌ Phase 2 failed - empty response");
        return false;
    }

    JsonDocument responseDoc;
    DeserializationError error = deserializeJson(responseDoc, response);
    if (error) {
        Serial.printf("[mYBUS] ❌ Phase 2 JSON parse error: %s\n", error.c_str());
        return false;
    }

    bool authenticated = responseDoc["isAuthenticated"].is<bool>() &&
                         responseDoc["isAuthenticated"].as<bool>();

    if (!authenticated) {
        Serial.println("[mYBUS] ❌ Server rejected Phase 2");
        Serial.println(response);
        return false;
    }

    Serial.println("[mYBUS] ✅ Phase 2 authenticated");
    Serial.println("[mYBUS] 🔑 ========================================");
    return true;
}

// ============================================================
// Full handshake
// ============================================================

bool CloudManager::performHandshake()
{
    Serial.println();
    Serial.println("========== mYBUS v2 HANDSHAKE ==========");

    if (!isLoggedIn()) {
        Serial.println("[mYBUS] ❌ Device is not authenticated");
        return false;
    }

    if (!deviceKeypairInitialized) {
        if (!initializeDeviceKeypair()) {
            Serial.println("[mYBUS] ❌ Device keypair unavailable");
            return false;
        }
    }

    clearSecureSession();

    if (!generateHandshakeNonce()) {
        Serial.println("[mYBUS] ❌ Nonce generation failed");
        return false;
    }

    // --------------------------------------------------------
    // Phase 1 - ECDH
    // --------------------------------------------------------

    Serial.println("[mYBUS] 🔐 ========================================");
    Serial.println("[mYBUS] 🔐 PHASE 1: ECDH Key Exchange Started");

    JsonDocument phase1;
    phase1["protocolVersion"] = MYBUS_PROTOCOL_VERSION;
    phase1["communicationInterface"] = MYBUS_INTERFACE_WIFI;
    phase1["zoneId"] = MYBUS_ZONE_DEFAULT;
    phase1["deviceId"] = deviceId;
    phase1["requestNumber"] = nextRequestNumber();
    phase1["flags"] = 0;
    phase1["security"] = MYBUS_SECURITY_HANDSHAKE;
    phase1["command"] = MYBUS_COMMAND_HANDSHAKE;

    JsonObject data = phase1["data"].to<JsonObject>();
    data["publicKeyPem"] = devicePublicKeyPem;
    data["nonce"] = handshakeNonce;

    String body;
    serializeJson(phase1, body);

    Serial.println("[mYBUS] Sending Phase 1 (ECDH)");
    Serial.printf("[mYBUS] Device public key length: %u\n",
                  static_cast<unsigned>(devicePublicKeyPem.length()));
    Serial.printf("[mYBUS] Nonce: %s\n", handshakeNonce.c_str());
    Serial.println("[mYBUS] 🔐 ========================================");

    String response = sendRequest("/devices/handshake", "POST", body, true);

    if (response.isEmpty()) {
        Serial.println("[mYBUS] ❌ Phase 1 failed - empty response");
        clearSecureSession();
        return false;
    }

    JsonDocument responseDoc;
    DeserializationError error = deserializeJson(responseDoc, response);
    if (error) {
        Serial.printf("[mYBUS] ❌ Phase 1 JSON error: %s\n", error.c_str());
        clearSecureSession();
        return false;
    }

    const char* serverKey = responseDoc["serverPublicKeyPem"];
    if (serverKey == nullptr) {
        Serial.println("[mYBUS] ❌ Missing server public key");
        clearSecureSession();
        return false;
    }

    serverPublicKeyPem = String(serverKey);
    if (serverPublicKeyPem.isEmpty()) {
        Serial.println("[mYBUS] ❌ Server public key is empty");
        clearSecureSession();
        return false;
    }

    Serial.println("[mYBUS] ✅ Server public key received");
    Serial.printf("[mYBUS] Server key length: %u\n",
                  static_cast<unsigned>(serverPublicKeyPem.length()));

    // --------------------------------------------------------
    // ECDH + HKDF
    // --------------------------------------------------------

    if (!computeSessionKey()) {
        Serial.println("[mYBUS] ❌ Session key derivation failed");
        clearSecureSession();
        return false;
    }

    // --------------------------------------------------------
    // Phase 2 - HMAC
    // --------------------------------------------------------

    if (!authenticateHandshakeSession()) {
        Serial.println("[mYBUS] ❌ Phase 2 authentication failed");
        clearSecureSession();
        return false;
    }

    Serial.println("[mYBUS] ✅ Secure session established");
    Serial.println("========================================");
    return true;
}

// ============================================================
// Session status
// ============================================================

bool CloudManager::isSecureSessionEstablished() const
{
    return sessionKeyValid;
}

// ============================================================
// Clear secure session
// ============================================================

void CloudManager::clearSecureSession()
{
    cryptoSecureZero(sessionKey, sizeof(sessionKey));
    sessionKeyValid = false;
    serverPublicKeyPem = "";
    handshakeNonce = "";
    Serial.println("[mYBUS] 🗑️ Secure session cleared");
}

// ============================================================
// Encrypt payload (legacy - for JSON mode)
// ============================================================

bool CloudManager::encryptPayload(const String& plainText, String& encryptedHex,
                                   String& ivHex, String& authTagHex)
{
    encryptedHex = "";
    ivHex = "";
    authTagHex = "";

    if (!sessionKeyValid) {
        Serial.println("[mYBUS] ❌ No secure session");
        return false;
    }

    if (plainText.isEmpty()) return false;

    const size_t plaintextLength = plainText.length();
    if (plaintextLength > MYBUS_MAX_PAYLOAD_SIZE) {
        Serial.printf("[mYBUS] ❌ Payload too large: %u\n",
                      static_cast<unsigned>(plaintextLength));
        return false;
    }

    uint8_t iv[12];
    uint8_t tag[16];
    uint8_t ciphertext[MYBUS_MAX_PAYLOAD_SIZE];

    memset(iv, 0, sizeof(iv));
    memset(tag, 0, sizeof(tag));
    memset(ciphertext, 0, sizeof(ciphertext));

    if (!cryptoRandomBytes(iv, sizeof(iv))) {
        cryptoSecureZero(iv, sizeof(iv));
        cryptoSecureZero(tag, sizeof(tag));
        cryptoSecureZero(ciphertext, sizeof(ciphertext));
        return false;
    }

    Serial.printf("[mYBUS] 🔒 Encrypting payload (%u bytes)\n", plaintextLength);
    Serial.printf("[mYBUS] 📤 IV (hex): %s\n", cryptoBytesToHex(iv, sizeof(iv)).c_str());

    bool success = cryptoAesGcmEncrypt(
        sessionKey, sizeof(sessionKey),
        iv, sizeof(iv),
        reinterpret_cast<const uint8_t*>(plainText.c_str()),
        plaintextLength,
        ciphertext,
        tag, sizeof(tag)
    );

    if (success) {
        encryptedHex = cryptoBytesToHex(ciphertext, plaintextLength);
        ivHex = cryptoBytesToHex(iv, sizeof(iv));
        authTagHex = cryptoBytesToHex(tag, sizeof(tag));

        Serial.printf("[mYBUS] 📤 Ciphertext (hex): %s\n", encryptedHex.c_str());
        Serial.printf("[mYBUS] 📤 AuthTag (hex): %s\n", authTagHex.c_str());
    }

    cryptoSecureZero(ciphertext, sizeof(ciphertext));
    cryptoSecureZero(iv, sizeof(iv));
    cryptoSecureZero(tag, sizeof(tag));

    return success && !encryptedHex.isEmpty() &&
           ivHex.length() == 24 && authTagHex.length() == 32;
}

// ============================================================
// Decrypt payload (legacy - for JSON mode)
// ============================================================

bool CloudManager::decryptPayload(const String& encryptedHex, const String& ivHex,
                                   const String& authTagHex, String& plainTextOut)
{
    plainTextOut = "";

    if (!sessionKeyValid) {
        Serial.println("[mYBUS] ❌ No secure session");
        return false;
    }

    if (encryptedHex.isEmpty() || ivHex.length() != 24 || authTagHex.length() != 32) {
        Serial.println("[mYBUS] ❌ Invalid encrypted payload format");
        return false;
    }

    if (encryptedHex.length() % 2 != 0) return false;

    const size_t cipherLen = encryptedHex.length() / 2;
    if (cipherLen > MYBUS_MAX_PAYLOAD_SIZE) return false;

    uint8_t iv[12];
    uint8_t tag[16];
    uint8_t cipherBuf[MYBUS_MAX_PAYLOAD_SIZE];
    uint8_t plainBuf[MYBUS_MAX_PAYLOAD_SIZE + 1];

    memset(iv, 0, sizeof(iv));
    memset(tag, 0, sizeof(tag));
    memset(cipherBuf, 0, sizeof(cipherBuf));
    memset(plainBuf, 0, sizeof(plainBuf));

    Serial.println("[mYBUS] 🔓 Decrypting payload");
    Serial.printf("[mYBUS] 📥 IV (hex): %s\n", ivHex.c_str());
    Serial.printf("[mYBUS] 📥 AuthTag (hex): %s\n", authTagHex.c_str());

    if (!cryptoHexToBytes(ivHex, iv, sizeof(iv))) goto decrypt_cleanup;
    if (!cryptoHexToBytes(authTagHex, tag, sizeof(tag))) goto decrypt_cleanup;
    if (!cryptoHexToBytes(encryptedHex, cipherBuf, cipherLen)) goto decrypt_cleanup;

    {
        bool success = cryptoAesGcmDecrypt(
            sessionKey, sizeof(sessionKey),
            iv, sizeof(iv),
            cipherBuf, cipherLen,
            tag, sizeof(tag),
            plainBuf
        );

        if (success) {
            plainBuf[cipherLen] = '\0';
            plainTextOut = String(reinterpret_cast<char*>(plainBuf));
            Serial.printf("[mYBUS] ✅ Decrypted: %s\n", plainTextOut.c_str());
        }

        cryptoSecureZero(cipherBuf, sizeof(cipherBuf));
        cryptoSecureZero(plainBuf, sizeof(plainBuf));
        cryptoSecureZero(iv, sizeof(iv));
        cryptoSecureZero(tag, sizeof(tag));

        return success;
    }

decrypt_cleanup:
    cryptoSecureZero(cipherBuf, sizeof(cipherBuf));
    cryptoSecureZero(plainBuf, sizeof(plainBuf));
    cryptoSecureZero(iv, sizeof(iv));
    cryptoSecureZero(tag, sizeof(tag));
    return false;
}

// ============================================================
// Request number
// ============================================================

uint32_t CloudManager::nextRequestNumber()
{
    ++requestNumber;
    if (requestNumber == 0) requestNumber = 1;
    return requestNumber;
}

// ============================================================
// HTTP success
// ============================================================

bool CloudManager::isHttpSuccess(int httpCode)
{
    return httpCode >= 200 && httpCode < 300;
}

// ============================================================
// HTTP request
// ============================================================

String CloudManager::sendRequest(const String& endpoint, const String& method,
                                  const String& body, bool useAuth)
{
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[HTTP] ❌ WiFi disconnected");
        return "";
    }

    HTTPClient http;
    String url = apiBaseUrl + endpoint;

    Serial.printf("[HTTP] %s %s\n", method.c_str(), url.c_str());

    if (!http.begin(url)) {
        Serial.println("[HTTP] ❌ begin() failed");
        return "";
    }

    http.setTimeout(HTTP_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");

    if (useAuth && !jwtToken.isEmpty()) {
        http.addHeader("Authorization", "Bearer " + jwtToken);
    }

    int httpCode = -1;

    if (method.equalsIgnoreCase("POST")) {
        httpCode = http.POST(body);
    } else if (method.equalsIgnoreCase("GET")) {
        httpCode = http.GET();
    } else if (method.equalsIgnoreCase("PUT")) {
        httpCode = http.PUT(body);
    } else if (method.equalsIgnoreCase("DELETE")) {
        httpCode = http.sendRequest("DELETE",
                                    reinterpret_cast<uint8_t*>(const_cast<char*>(body.c_str())),
                                    body.length());
    } else {
        Serial.printf("[HTTP] Unsupported method: %s\n", method.c_str());
        http.end();
        return "";
    }

    String response = http.getString();
    http.end();

    if (isHttpSuccess(httpCode)) {
        Serial.printf("[HTTP] ✅ Success: %d\n", httpCode);
        return response;
    }

    Serial.printf("[HTTP] ❌ Request failed: %d\n", httpCode);
    if (!response.isEmpty()) {
        Serial.println("[HTTP] Response:");
        Serial.println(response);
    }
    return "";
}

// ============================================================
// mYBUS v2 Binary Frame
// ============================================================

bool CloudManager::sendMybusBinaryFrame(
    uint8_t sequence,
    uint8_t interfaceId,
    uint8_t zone,
    uint8_t deviceId,
    uint16_t requestNumber,
    uint8_t qos,
    uint8_t options,
    uint8_t flags,
    uint8_t security,
    uint8_t compression,
    uint8_t command,
    const uint8_t* payload,
    size_t payloadLen,
    JsonDocument* outResponse
) {
    Serial.println("[mYBUS] 📦 ========================================");
    Serial.println("[mYBUS] 📦 sendMybusBinaryFrame Called");
    Serial.printf("[mYBUS] 📦 Sequence: %u\n", sequence);
    Serial.printf("[mYBUS] 📦 Interface: %u\n", interfaceId);
    Serial.printf("[mYBUS] 📦 Zone: %u\n", zone);
    Serial.printf("[mYBUS] 📦 Device ID: %u\n", deviceId);
    Serial.printf("[mYBUS] 📦 Request Number: %u\n", requestNumber);
    Serial.printf("[mYBUS] 📦 Command: %u\n", command);
    Serial.printf("[mYBUS] 📦 Flags (before SCU): 0x%02X\n", flags);
    Serial.printf("[mYBUS] 📦 Payload length: %u\n", payloadLen);

    // ✅ بررسی سایز قبل از هر تخصیص
    if (payloadLen > MYBUS_MAX_PAYLOAD_SIZE) {
        Serial.printf("[mYBUS] ❌ Payload too large: %u (max %u)\n",
                      static_cast<unsigned>(payloadLen),
                      static_cast<unsigned>(MYBUS_MAX_PAYLOAD_SIZE));
        return false;
    }

    if (payloadLen > 0) {
        Serial.printf("[mYBUS] 📦 Payload (hex): ");
        for (size_t i = 0; i < payloadLen && i < 32; i++) {
            Serial.printf("%02x", payload[i]);
        }
        if (payloadLen > 32) Serial.print("...");
        Serial.println();
    }
    Serial.println("[mYBUS] 📦 ========================================");

    if (!sessionKeyValid) {
        Serial.println("[mYBUS] ❌ No secure session");
        return false;
    }

    flags |= (1 << MYBUS_FLAG_SCU_BIT);
    Serial.printf("[mYBUS] 📦 Flags (after SCU): 0x%02X\n", flags);

    Serial.println("[mYBUS] 🔑 ========================================");
    Serial.printf("[mYBUS] 🔑 Session Key (hex): ");
    for (int i = 0; i < 32; i++) {
        Serial.printf("%02x", sessionKey[i]);
    }
    Serial.println();
    Serial.printf("[mYBUS] 🔑 Session Key (base64): ");
    String base64Key = cryptoBytesToBase64(sessionKey, sizeof(sessionKey));
    Serial.println(base64Key);
    Serial.println("[mYBUS] 🔑 ========================================");

    MyBusHeader hdr;
    hdr.protocolVersion = MYBUS_PROTOCOL_VERSION;
    hdr.length = 0;
    hdr.sequence = sequence;
    hdr.interfaceId = interfaceId;
    hdr.zone = zone;
    hdr.deviceId = deviceId;
    hdr.requestNumber = requestNumber;
    hdr.qos = qos;
    hdr.options = options;
    hdr.flags = flags;
    hdr.security = security;
    hdr.compression = compression;
    hdr.command = command;

    // ============================================================
    // ✅ استفاده از std::vector به جای VLA روی Stack
    // این کار از Stack Overflow و WDT جلوگیری می‌کند
    // ============================================================
    const size_t maxFrameSize = MYBUS_HEADER_SIZE + MYBUS_MAX_PAYLOAD_SIZE + MYBUS_CRC_SIZE + 64;

    std::vector<uint8_t> plainFrame(maxFrameSize);
    std::vector<uint8_t> ciphertext(maxFrameSize);
    std::vector<uint8_t> wireMsg(maxFrameSize + MYBUS_AES_IV_SIZE + MYBUS_AES_TAG_SIZE);

    // صفر کردن بافرها
    memset(plainFrame.data(), 0, maxFrameSize);
    memset(ciphertext.data(), 0, maxFrameSize);
    memset(wireMsg.data(), 0, maxFrameSize + MYBUS_AES_IV_SIZE + MYBUS_AES_TAG_SIZE);

    size_t plainLen = mybus_buildFrame(hdr, payload, payloadLen, plainFrame.data(), maxFrameSize);
    if (plainLen == 0) {
        Serial.println("[mYBUS] ❌ Frame build failed");
        return false;
    }

    Serial.printf("[mYBUS] 📦 Plain frame: %u bytes\n", plainLen);
    Serial.printf("[mYBUS] 📦 Plain frame (hex): ");
    for (size_t i = 0; i < plainLen && i < 64; i++) {
        Serial.printf("%02x", plainFrame.data()[i]);
    }
    if (plainLen > 64) Serial.print("...");
    Serial.println();

    uint8_t iv[MYBUS_AES_IV_SIZE];
    uint8_t tag[MYBUS_AES_TAG_SIZE];

    memset(iv, 0, sizeof(iv));
    memset(tag, 0, sizeof(tag));

    if (!mybus_encryptFrame(plainFrame.data(), plainLen, sessionKey, ciphertext.data(), iv, tag)) {
        Serial.println("[mYBUS] ❌ Encryption failed");
        return false;
    }

    Serial.printf("[mYBUS] 🔐 Encrypted: %u bytes\n", plainLen);
    Serial.printf("[mYBUS] 📤 IV (hex): ");
    for (int i = 0; i < 12; i++) {
        Serial.printf("%02x", iv[i]);
    }
    Serial.println();

    Serial.printf("[mYBUS] 📤 AuthTag (hex): ");
    for (int i = 0; i < 16; i++) {
        Serial.printf("%02x", tag[i]);
    }
    Serial.println();

    Serial.printf("[mYBUS] 📤 Ciphertext (hex): ");
    for (size_t i = 0; i < plainLen && i < 64; i++) {
        Serial.printf("%02x", ciphertext.data()[i]);
    }
    if (plainLen > 64) Serial.print("...");
    Serial.println();

    // ============================================================
    // ✅ [ IV ][ Ciphertext ][ AuthTag ]
    // ============================================================

    size_t wireLen = MYBUS_AES_IV_SIZE + plainLen + MYBUS_AES_TAG_SIZE;

    size_t offset = 0;

    memcpy(wireMsg.data() + offset, iv, MYBUS_AES_IV_SIZE);
    offset += MYBUS_AES_IV_SIZE;

    memcpy(wireMsg.data() + offset, ciphertext.data(), plainLen);
    offset += plainLen;

    memcpy(wireMsg.data() + offset, tag, MYBUS_AES_TAG_SIZE);
    offset += MYBUS_AES_TAG_SIZE;

    Serial.printf("[mYBUS] 📤 Final packet: %u bytes\n", wireLen);
    Serial.printf("[mYBUS] 📤 Packet (hex): ");
    for (size_t i = 0; i < wireLen && i < 64; i++) {
        Serial.printf("%02x", wireMsg.data()[i]);
    }
    if (wireLen > 64) Serial.print("...");
    Serial.println();

    Serial.println("[mYBUS] 📤 ========================================");
    Serial.printf("[mYBUS] 📤 IV (first 12): ");
    for (int i = 0; i < 12; i++) {
        Serial.printf("%02x", wireMsg.data()[i]);
    }
    Serial.println();

    Serial.printf("[mYBUS] 📤 Ciphertext (next %u): ", plainLen);
    for (size_t i = 0; i < plainLen && i < 32; i++) {
        Serial.printf("%02x", wireMsg.data()[12 + i]);
    }
    if (plainLen > 32) Serial.print("...");
    Serial.println();

    Serial.printf("[mYBUS] 📤 AuthTag (last 16): ");
    for (int i = 0; i < 16; i++) {
        Serial.printf("%02x", wireMsg.data()[12 + plainLen + i]);
    }
    Serial.println();
    Serial.println("[mYBUS] 📤 ========================================");

    Serial.println("[mYBUS] 📦 ========================================");

    return sendRawBinaryToBackend(wireMsg.data(), wireLen, outResponse);
}

// ============================================================
// sendRawBinaryToBackend
// ✅ بدنه‌ی باینری پاسخ واقعاً خوانده و در صورت درخواست
//    (outResponse != nullptr) رمزگشایی/parse می‌شود.
// ✅ استفاده از yield() برای جلوگیری از WDT
// ============================================================

bool CloudManager::sendRawBinaryToBackend(const uint8_t* data, size_t len, JsonDocument* outResponse) {
    Serial.println("[mYBUS] 📡 ========================================");
    Serial.println("[mYBUS] 📡 Sending raw binary to backend");
    Serial.printf("[mYBUS] 📡 Data length: %u bytes\n", len);
    Serial.printf("[mYBUS] 📡 Data (hex): ");
    for (size_t i = 0; i < len && i < 64; i++) {
        Serial.printf("%02x", data[i]);
    }
    if (len > 64) Serial.print("...");
    Serial.println();
    Serial.println("[mYBUS] 📡 ========================================");

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[HTTP] ❌ WiFi disconnected");
        return false;
    }

    HTTPClient http;
    String url = apiBaseUrl + "/devices/data";

    Serial.printf("[HTTP] POST %s (%u bytes)\n", url.c_str(), len);

    if (!http.begin(url)) {
        Serial.println("[HTTP] ❌ begin() failed");
        return false;
    }

    http.setTimeout(HTTP_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/octet-stream");
    http.addHeader("Accept", "application/octet-stream");

    if (!jwtToken.isEmpty()) {
        http.addHeader("Authorization", "Bearer " + jwtToken);
    }

    int httpCode = http.POST(const_cast<uint8_t*>(data), len);

    bool success = false;

    if (isHttpSuccess(httpCode)) {
        Serial.printf("[HTTP] ✅ Success: %d\n", httpCode);
        success = true;

        // ✅ خواندن باینری با WiFiClient + yield() برای جلوگیری از WDT
        WiFiClient& stream = http.getStream();
        std::vector<uint8_t> responseBuf;

        unsigned long startMs = millis();
        const unsigned long TIMEOUT_MS = 2000;

        while (stream.available() || (millis() - startMs) < TIMEOUT_MS) {
            if (stream.available()) {
                uint8_t byte = stream.read();
                responseBuf.push_back(byte);
                startMs = millis();
            } else {
                delay(1);
                yield();  // ✅ اجازه می‌دهد سایر وظایف اجرا شوند
            }

            // ✅ اگر داده‌ای نرسید و زمان تمام شد
            if ((millis() - startMs) > TIMEOUT_MS) {
                Serial.println("[HTTP] ⚠️ Timeout waiting for response");
                break;
            }
        }

        Serial.printf("[HTTP] 📥 Response body: %u bytes\n",
                      static_cast<unsigned>(responseBuf.size()));

        if (responseBuf.size() > 0) {
            Serial.printf("[HTTP] 📥 First 16 bytes: ");
            for (size_t i = 0; i < min(responseBuf.size(), (size_t)16); i++) {
                Serial.printf("%02x ", responseBuf[i]);
            }
            Serial.println();
        }

        if (outResponse != nullptr && !responseBuf.empty()) {
            // ✅ پشتیبانی از JSON Buffer format (اگر بک‌اند به‌جای باینری خام،
            // یک JSON با ساختار {"type":"Buffer","data":[...]} بفرستد)
            if (responseBuf[0] == 0x7B) {
                Serial.println("[mYBUS] ✅ Response is JSON Buffer format, extracting...");

                String jsonStr = String(reinterpret_cast<const char*>(responseBuf.data()), responseBuf.size());

                JsonDocument jsonDoc;
                if (deserializeJson(jsonDoc, jsonStr) == DeserializationError::Ok) {
                    if (jsonDoc["type"] == "Buffer" && jsonDoc["data"].is<JsonArray>()) {
                        JsonArray dataArray = jsonDoc["data"].as<JsonArray>();
                        std::vector<uint8_t> binaryData;
                        binaryData.reserve(dataArray.size());

                        for (JsonVariant v : dataArray) {
                            binaryData.push_back(v.as<uint8_t>());
                        }

                        Serial.printf("[mYBUS] ✅ Extracted %u bytes from JSON Buffer\n",
                                      static_cast<unsigned>(binaryData.size()));

                        if (!decryptAndParseMybusResponse(binaryData.data(), binaryData.size(), *outResponse)) {
                            Serial.println("[mYBUS] ⚠️ Failed to decrypt/parse extracted binary");
                            outResponse->clear();
                            (*outResponse)["rawData"] = cryptoBytesToHex(binaryData.data(), binaryData.size());
                            (*outResponse)["rawLength"] = binaryData.size();
                        }
                        return true;
                    } else {
                        outResponse->clear();
                        (*outResponse)["error"] = "JSON response";
                        (*outResponse)["message"] = jsonDoc["message"] | "Unknown";
                        return true;
                    }
                }
                return false;
            }

            if (!decryptAndParseMybusResponse(responseBuf.data(), responseBuf.size(), *outResponse)) {
                Serial.println("[mYBUS] ⚠️ Failed to decrypt/parse response frame");
            }
        }

    } else {
        Serial.printf("[HTTP] ❌ Failed: %d\n", httpCode);
        String response = http.getString();
        if (!response.isEmpty()) {
            Serial.println("[HTTP] Response: " + response);
        }
    }

    http.end();

    Serial.printf("[mYBUS] 📡 Result: %s\n", success ? "✅ SUCCESS" : "❌ FAILED");
    Serial.println("[mYBUS] 📡 ========================================");

    return success;
}

// ============================================================
// decryptAndParseMybusResponse
// ورودی: بدنه‌ی خام پاسخ بک‌اند به فرمت [IV(12)][Ciphertext(N)][Tag(16)]
// خروجی: outDoc با فیلدهای هدر + payload خام (hex) + وضعیت موفقیت
//
// ✅ اصلاح شده: دیگر آدرس رجیستری را از payload حدس نمی‌زند (بک‌اند آن را
// echo نمی‌کند). فقط هدر + payload خام + بیت SF (موفق/ناموفق) را استخراج
// می‌کند. دیکد نهایی مقدار در decodeRegistryResponseValue انجام می‌شود،
// چون فقط تابعی که regAddr درخواست را در اختیار دارد (sendRegistryFrame)
// می‌تواند dataType درست را تعیین کند.
// ============================================================

bool CloudManager::decryptAndParseMybusResponse(const uint8_t* wireData, size_t wireLen, JsonDocument& outDoc)
{
    if (!sessionKeyValid) {
        Serial.println("[mYBUS] ❌ Cannot parse response: no secure session");
        return false;
    }

    if (wireLen < MYBUS_AES_IV_SIZE + MYBUS_AES_TAG_SIZE + MYBUS_MIN_FRAME_SIZE) {
        Serial.printf("[mYBUS] ❌ Response too short: %u bytes\n",
                      static_cast<unsigned>(wireLen));
        return false;
    }

    const uint8_t* iv     = wireData;
    const uint8_t* cipher = wireData + MYBUS_AES_IV_SIZE;
    size_t cipherLen      = wireLen - MYBUS_AES_IV_SIZE - MYBUS_AES_TAG_SIZE;
    const uint8_t* tag    = wireData + MYBUS_AES_IV_SIZE + cipherLen;

    // سقف امن در برابر پاسخ خراب/غیرمنتظره
    const size_t maxPlainLen = MYBUS_HEADER_SIZE + MYBUS_MAX_PAYLOAD_SIZE + MYBUS_CRC_SIZE + 64;
    if (cipherLen == 0 || cipherLen > maxPlainLen) {
        Serial.printf("[mYBUS] ❌ Response ciphertext length invalid: %u\n",
                      static_cast<unsigned>(cipherLen));
        return false;
    }

    Serial.println("[mYBUS] 📦 ========================================");
    Serial.println("[mYBUS] 📦 PHASE 3: Decrypt & Parse Started");
    Serial.printf("[mYBUS] 📥 IV (hex): %s\n", cryptoBytesToHex(iv, MYBUS_AES_IV_SIZE).c_str());
    Serial.printf("[mYBUS] 📥 Ciphertext (hex): %s\n", cryptoBytesToHex(cipher, cipherLen).c_str());
    Serial.printf("[mYBUS] 📥 AuthTag (hex): %s\n", cryptoBytesToHex(tag, MYBUS_AES_TAG_SIZE).c_str());

    std::vector<uint8_t> plainFrame(cipherLen);

    if (!mybus_decryptFrame(cipher, cipherLen, sessionKey, iv, tag, plainFrame.data())) {
        Serial.println("[mYBUS] ❌ Response decryption failed (bad key or tampered data)");
        Serial.println("[mYBUS] 📦 ========================================");
        return false;
    }

    Serial.printf("[mYBUS] ✅ Decrypted plain frame (hex): %s\n",
                  cryptoBytesToHex(plainFrame.data(), plainFrame.size()).c_str());

    MyBusHeader hdr;
    const uint8_t* payload = nullptr;
    size_t payloadLen = 0;

    if (!mybus_parseFrame(plainFrame.data(), plainFrame.size(), hdr, &payload, &payloadLen)) {
        Serial.println("[mYBUS] ❌ Response frame parse failed (bad CRC or protocol version)");
        Serial.println("[mYBUS] 📦 ========================================");
        return false;
    }

    Serial.printf("[mYBUS] ✅ Response parsed: command=%u flags=0x%02X requestNumber=%u payloadLen=%u\n",
                  hdr.command, hdr.flags, hdr.requestNumber, static_cast<unsigned>(payloadLen));

    outDoc.clear();
    outDoc["command"] = hdr.command;
    outDoc["flags"] = hdr.flags;
    outDoc["security"] = hdr.security;
    outDoc["requestNumber"] = hdr.requestNumber;
    outDoc["payloadLen"] = payloadLen;

    // ✅ بیت SF (bit 2) در flags: 0 = موفق، 1 = شکست (طبق مستند پروتکل mYBUS)
    bool responseSuccess = ((hdr.flags >> MYBUS_FLAG_SF_BIT) & 0x01) == 0;
    outDoc["success"] = responseSuccess;

    if (payloadLen > 0) {
        outDoc["payloadHex"] = cryptoBytesToHex(payload, payloadLen);
    }

    // ⚠️ پاسخ بک‌اند دیگر آدرس رجیستری را echo نمی‌کند — payload فقط:
    //   - در حالت شکست: 1 بایت کد خطا (مطابق MybusErrorCode سمت بک‌اند،
    //     مثلاً 5 = REGISTRY_NOT_FOUND)
    //   - در حالت موفقِ Read: bytes خام مقدار (بدون پیشوند آدرس)
    //   - در حالت موفقِ Write: خالی (payloadLen == 0)
    // دیکد واقعی مقدار (که نیاز به regAddr درخواست دارد تا dataType را
    // تعیین کند) در decodeRegistryResponseValue انجام می‌شود، نه اینجا.
    if (!responseSuccess && payloadLen == 1) {
        outDoc["errorCode"] = payload[0];
    }

    Serial.println("[mYBUS] 📦 ========================================");
    return true;
}

// ============================================================
// decodeRegistryResponseValue
// چون پاسخ بک‌اند آدرس رجیستری را echo نمی‌کند، از regAddr درخواست
// (که ESP32 از قبل می‌داند، در sendRegistryFrame) برای تعیین dataType
// و دیکد صحیح مقدار موفقِ Read استفاده می‌کنیم.
// ============================================================

void CloudManager::decodeRegistryResponseValue(JsonDocument& doc, uint16_t regAddr)
{
    doc["regAddr"] = regAddr;

    bool success = doc["success"] | false;
    if (!success) {
        return; // در حالت شکست فقط errorCode معتبر است (که قبلاً ست شده)
    }

    if (!doc["payloadHex"].is<const char*>()) {
        return; // Write موفق: payload خالی، مقداری برای دیکد نیست
    }

    String hex = doc["payloadHex"].as<String>();
    size_t valueLen = hex.length() / 2;
    if (valueLen == 0 || hex.length() % 2 != 0) return;

    uint8_t value[MYBUS_MAX_PAYLOAD_SIZE];
    if (valueLen > sizeof(value)) return;

    if (!cryptoHexToBytes(hex, value, valueLen)) return;

    MyBusDataType dataType = static_cast<MyBusDataType>((regAddr >> 8) & 0x0F);

    switch (dataType) {
        case DT_FLOAT: {
            if (valueLen >= sizeof(float)) {
                float f;
                memcpy(&f, value, sizeof(float));
                doc["value"] = f;
            }
            break;
        }
        case DT_INT32: {
            if (valueLen >= sizeof(int32_t)) {
                int32_t v;
                memcpy(&v, value, sizeof(int32_t));
                doc["value"] = v;
            }
            break;
        }
        case DT_UINT16: {
            if (valueLen >= sizeof(uint16_t)) {
                uint16_t v;
                memcpy(&v, value, sizeof(uint16_t));
                doc["value"] = v;
            }
            break;
        }
        case DT_BIT: {
            doc["value"] = (value[0] != 0);
            break;
        }
        case DT_STRING:
        default: {
            String s;
            s.reserve(valueLen + 1);
            for (size_t i = 0; i < valueLen; ++i) {
                s += static_cast<char>(value[i]);
            }
            doc["value"] = s;
            break;
        }
    }
}

// ============================================================
// sendRegistryFrame
// ✅ چک سایز قبل از VLA
// ✅ deviceId دیگر هارد-کد نیست، پارامتر شده (busDeviceId)
// ✅ بعد از دریافت پاسخ، مقدار را با regAddr درخواست دیکد می‌کند
// ============================================================

bool CloudManager::sendRegistryFrame(
    uint16_t regAddr,
    const uint8_t* regValue,
    size_t valueLen,
    bool isWrite,
    uint8_t busDeviceId,
    JsonDocument* outResponse
) {
    Serial.println("[mYBUS] 📝 ========================================");
    Serial.println("[mYBUS] 📝 sendRegistryFrame Called");
    Serial.printf("[mYBUS] 📝 Register Address: 0x%04X\n", regAddr);
    Serial.printf("[mYBUS] 📝 Is Write: %s\n", isWrite ? "YES" : "NO");
    Serial.printf("[mYBUS] 📝 Bus Device ID: %u\n", busDeviceId);
    Serial.printf("[mYBUS] 📝 Value length: %u\n", valueLen);

    // ✅ بررسی سایز قبل از ساخت payload/VLA
    if (valueLen > MYBUS_MAX_PAYLOAD_SIZE - 2) {
        Serial.printf("[mYBUS] ❌ Value too large: %u (max %u)\n",
                      static_cast<unsigned>(valueLen),
                      static_cast<unsigned>(MYBUS_MAX_PAYLOAD_SIZE - 2));
        return false;
    }

    if (valueLen > 0 && regValue != nullptr) {
        Serial.printf("[mYBUS] 📝 Value (hex): ");
        for (size_t i = 0; i < valueLen && i < 16; i++) {
            Serial.printf("%02x", regValue[i]);
        }
        if (valueLen > 16) Serial.print("...");
        Serial.println();
    }
    Serial.println("[mYBUS] 📝 ========================================");

    // ✅ Payload: [Address Low][Address High][Value]
    uint8_t payload[2 + valueLen];
    size_t payloadLen = 2 + valueLen;

    payload[0] = regAddr & 0xFF;
    payload[1] = (regAddr >> 8) & 0xFF;

    if (regValue != nullptr && valueLen > 0) {
        memcpy(&payload[2], regValue, valueLen);
    }

    Serial.printf("[mYBUS] 📝 Payload built: %u bytes\n", payloadLen);
    Serial.printf("[mYBUS] 📝 Payload (hex): ");
    for (size_t i = 0; i < payloadLen; i++) {
        Serial.printf("%02x", payload[i]);
    }
    Serial.println();

    uint8_t flags = MYBUS_FLAG_REQUEST;
    flags |= (1 << MYBUS_FLAG_SCU_BIT);

    bool ok = sendMybusBinaryFrame(
        0,
        MYBUS_INTERFACE_WIFI,
        MYBUS_ZONE_DEFAULT,
        busDeviceId,
        nextRequestNumber(),
        MYBUS_QOS_DEFAULT,
        MYBUS_OPTIONS_DEFAULT,
        flags,
        MYBUS_SECURITY_ENCRYPTED,
        MYBUS_COMPRESSION_NONE,
        MYBUS_COMMAND_REGISTRY,
        payload,
        payloadLen,
        outResponse
    );

    // ✅ چون فقط اینجا regAddr در دسترس است، دیکد نهایی مقدار پاسخ همین‌جا انجام می‌شود
    if (ok && outResponse != nullptr) {
        decodeRegistryResponseValue(*outResponse, regAddr);
    }

    return ok;
}

// ============================================================
// ✅ sendMybusData - لاگ درست + اعتبارسنجی پارس + busDeviceId اختیاری + outResponse
// ============================================================

bool CloudManager::sendMybusData(JsonDocument& data, JsonDocument* outResponse) {
    Serial.println("[mYBUS] 📝 ========================================");
    Serial.println("[mYBUS] 📝 sendMybusData Called (Binary mode)");

    if (!sessionKeyValid) {
        Serial.println("[mYBUS] ❌ No secure session");
        return false;
    }

    uint16_t regAddr = data["RegAdd"] | 0;
    String regVal = data["RegVal"] | "";
    uint8_t busDeviceId = data["DeviceId"] | 1;

    Serial.printf("[mYBUS] 📝 RegAdd: 0x%04X\n", regAddr);
    Serial.printf("[mYBUS] 📝 RegVal: %s\n", regVal.c_str());
    Serial.printf("[mYBUS] 📝 Bus Device ID: %u\n", busDeviceId);

    uint8_t value[32];
    size_t valueLen = 0;

    MyBusDataType dataType = static_cast<MyBusDataType>((regAddr >> 8) & 0x0F);
    Serial.printf("[mYBUS] 📝 DataType: %d\n", dataType);

    if (!regVal.isEmpty()) {
        switch (dataType) {
            case DT_FLOAT: {
                char* endPtr = nullptr;
                float f = strtod(regVal.c_str(), &endPtr);
                if (endPtr == regVal.c_str() || (endPtr && *endPtr != '\0')) {
                    Serial.printf("[mYBUS] ⚠️ Invalid float value: '%s' (sent as 0)\n",
                                  regVal.c_str());
                }
                memcpy(value, &f, sizeof(float));
                valueLen = sizeof(float);
                Serial.printf("[mYBUS] 📝 Value as float: %f (size: %u)\n", f, valueLen);
                break;
            }

            case DT_INT32: {
                char* endPtr = nullptr;
                long num = strtol(regVal.c_str(), &endPtr, 10);
                if (endPtr == regVal.c_str() || (endPtr && *endPtr != '\0')) {
                    Serial.printf("[mYBUS] ⚠️ Invalid int32 value: '%s' (sent as 0)\n",
                                  regVal.c_str());
                }
                int32_t num32 = static_cast<int32_t>(num);
                memcpy(value, &num32, sizeof(int32_t));
                valueLen = sizeof(int32_t);
                Serial.printf("[mYBUS] 📝 Value as int32: %d (size: %u)\n", num32, valueLen);
                break;
            }

            case DT_UINT16: {
                char* endPtr = nullptr;
                long num = strtol(regVal.c_str(), &endPtr, 10);
                if (endPtr == regVal.c_str() || (endPtr && *endPtr != '\0') ||
                    num < 0 || num > 0xFFFF) {
                    Serial.printf("[mYBUS] ⚠️ Invalid uint16 value: '%s'\n", regVal.c_str());
                }
                uint16_t num16 = (uint16_t)num;
                memcpy(value, &num16, sizeof(uint16_t));
                valueLen = sizeof(uint16_t);
                Serial.printf("[mYBUS] 📝 Value as uint16: %u (size: %u)\n", num16, valueLen);
                break;
            }

            case DT_BIT: {
                bool isTrue = regVal.equalsIgnoreCase("true") || regVal == "1";
                bool isFalse = regVal.equalsIgnoreCase("false") || regVal == "0";
                if (!isTrue && !isFalse) {
                    Serial.printf("[mYBUS] ⚠️ Ambiguous bool value: '%s' (treated as false)\n",
                                  regVal.c_str());
                }
                value[0] = isTrue ? 1 : 0;
                valueLen = 1;
                Serial.printf("[mYBUS] 📝 Value as bool: %u (size: %u)\n", value[0], valueLen);
                break;
            }

            case DT_STRING:
            default: {
                valueLen = min(regVal.length(), sizeof(value) - 1);
                memcpy(value, regVal.c_str(), valueLen);
                Serial.printf("[mYBUS] 📝 Value as string: %s (size: %u)\n", regVal.c_str(), valueLen);
                break;
            }
        }
    }

    Serial.println("[mYBUS] 📝 ========================================");

    return sendRegistryFrame(regAddr, value, valueLen, true, busDeviceId, outResponse);
}

// ============================================================
// ✅ WRITE_REGISTRY via WebSocket
// (تنها و یگانه نسخه‌ی این تابع - نسخه‌ی تکراری/ناقص باید حذف شود)
// ============================================================

void CloudManager::handleWriteRegistry(JsonDocument& doc)
{
    Serial.println("[WS] 📝 WRITE_REGISTRY called");

    uint16_t regAddr = doc["RegAdd"] | 0;
    String regVal = doc["RegVal"] | "";
    uint8_t busDeviceId = doc["DeviceId"] | 1;

    Serial.printf("[WS] 📝 RegAdd: 0x%04X (%d)\n", regAddr, regAddr);
    Serial.printf("[WS] 📝 RegVal: %s\n", regVal.c_str());
    Serial.printf("[WS] 📝 DeviceId: %u\n", busDeviceId);

    // ✅ NEW: اطلاع فوری به هندلر محلی سخت‌افزار (main.cpp -> onCommandReceived)
    // این خط جدا از منطق ارسال به بک‌اند است و صرفاً یک اعلان محلی/فوری است
    if (commandCallback) {
        JsonDocument localCmd;
        localCmd["action"] = "SET_REGISTRY";
        localCmd["RegAdd"] = regAddr;
        localCmd["RegVal"] = regVal;
        localCmd["DeviceId"] = busDeviceId;
        commandCallback(localCmd);
    }

    if (!sessionKeyValid) {
        JsonDocument response;
        response["type"] = "error";
        response["message"] = "No secure session";
        sendRealtimeData(response);
        return;
    }

    JsonDocument req;
    req["RegAdd"] = regAddr;
    req["RegVal"] = regVal;
    req["DeviceId"] = busDeviceId;

    JsonDocument response;
    bool sent = sendMybusData(req, &response);

    bool registrySuccess = sent && (response["success"] | false);

    if (registrySuccess) {
        Serial.println("[WS] ✅ WRITE_REGISTRY successful");

        JsonDocument wsMsg;
        wsMsg["type"] = "registry_write_response";
        wsMsg["RegAdd"] = regAddr;
        wsMsg["status"] = "success";
        wsMsg["command"] = response["command"] | 0;
        wsMsg["flags"] = response["flags"] | 0;

        sendRealtimeData(wsMsg);
    } else {
        Serial.println("[WS] ❌ WRITE_REGISTRY failed");

        JsonDocument errorMsg;
        errorMsg["type"] = "error";
        errorMsg["message"] = "Failed to write registry";
        errorMsg["RegAdd"] = regAddr;

        if (!sent) {
            errorMsg["reason"] = "transport_error";
        } else if (response["errorCode"].is<int>()) {
            errorMsg["reason"] = "backend_error";
            errorMsg["errorCode"] = response["errorCode"];
        } else {
            errorMsg["reason"] = "unknown";
        }

        sendRealtimeData(errorMsg);
    }
}

// ============================================================
// onCommand
// ============================================================

void CloudManager::onCommand(CommandCallback cb)
{
    commandCallback = cb;
    Serial.println("[CLOUD] Command callback registered");
}

// ============================================================
// API URL
// ============================================================

void CloudManager::setApiBaseUrl(const String& url)
{
    if (url.isEmpty()) return;
    apiBaseUrl = url;
    while (apiBaseUrl.endsWith("/")) {
        apiBaseUrl.remove(apiBaseUrl.length() - 1);
    }
    Serial.print("[HTTP] API Base URL: ");
    Serial.println(apiBaseUrl);
}

String CloudManager::getApiBaseUrl() const
{
    return apiBaseUrl;
}

// ============================================================
// Device ID setter
// ============================================================

void CloudManager::setDeviceId(const String& id)
{
    if (id.isEmpty()) return;
    deviceId = id;
    preferences.putString("deviceId", deviceId);
}

String CloudManager::getDeviceId() const
{
    return deviceId;
}

String CloudManager::getJwtToken() const
{
    return jwtToken;
}

// ============================================================
// WebSocket Server
// ============================================================

void CloudManager::startWebSocketServer()
{
    if (server != nullptr) return;

    server = new AsyncWebServer(80);
    ws = new AsyncWebSocket("/ws");

    ws->onEvent([this](AsyncWebSocket* server, AsyncWebSocketClient* client,
                        AwsEventType type, void* arg, uint8_t* data, size_t len) {
        this->onWebSocketEvent(server, client, type, arg, data, len);
    });

    server->addHandler(ws);
    server->begin();

    Serial.println("[WS] WebSocket server started");
    Serial.println("[WS] Path: /ws");
}

void CloudManager::loopWebSocketServer()
{
    if (ws != nullptr) {
        ws->cleanupClients();
    }
}

bool CloudManager::isWebSocketConnected() const
{
    return wsConnected && wsClient != nullptr;
}

// ============================================================
// WebSocket event
// ============================================================

void CloudManager::onWebSocketEvent(AsyncWebSocket* server, AsyncWebSocketClient* client,
                                     AwsEventType type, void* arg, uint8_t* data, size_t len)
{
    switch (type) {
        case WS_EVT_CONNECT: {
            Serial.printf("[WS] Client connected: %u\n", client->id());
            wsClient = client;
            wsConnected = true;

            JsonDocument welcome;
            welcome["type"] = "connection_ack";
            welcome["message"] = "Connected to ESP32 device";
            welcome["deviceId"] = deviceId;
            welcome["uptime"] = millis() / 1000;
            welcome["freeHeap"] = ESP.getFreeHeap();
            welcome["wifiRSSI"] = WiFi.RSSI();
            sendRealtimeData(welcome);
            requestUsersList();
            break;
        }

        case WS_EVT_DISCONNECT: {
            Serial.printf("[WS] Client disconnected: %u\n", client->id());
            if (wsClient == client) {
                wsClient = nullptr;
                wsConnected = false;
            }
            break;
        }

        case WS_EVT_DATA: {
            handleWebSocketMessage(arg, data, len);
            break;
        }

        case WS_EVT_PONG:
        case WS_EVT_ERROR:
        default:
            break;
    }
}

// ============================================================
// WebSocket message
// ============================================================

void CloudManager::handleWebSocketMessage(void* arg, uint8_t* data, size_t len)
{
    AwsFrameInfo* info = reinterpret_cast<AwsFrameInfo*>(arg);
    if (info == nullptr || data == nullptr) return;

    if (!info->final || info->index != 0 || info->len != len || info->opcode != WS_TEXT) {
        return;
    }

    String message;
    message.reserve(len + 1);
    for (size_t i = 0; i < len; ++i) {
        message += static_cast<char>(data[i]);
    }

    Serial.printf("[WS] Message received: %s\n", message.c_str());

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);
    if (error) {
        Serial.printf("[WS] JSON parse error: %s\n", error.c_str());
        return;
    }

    if (!doc["action"].is<const char*>()) return;

    String action = doc["action"].as<String>();

    // GET_STATUS
    if (action == "GET_STATUS") {
        JsonDocument response;
        response["type"] = "status";
        response["deviceId"] = deviceId;
        response["status"] = "online";
        response["siteId"] = siteId;
        response["uptime"] = millis() / 1000;
        response["freeHeap"] = ESP.getFreeHeap();
        response["wifiRSSI"] = WiFi.RSSI();
        response["secure"] = sessionKeyValid;
        sendRealtimeData(response);
        return;
    }

    // GET_USERS
    if (action == "GET_USERS") {
        std::vector<UserInfo> users;
        if (loadUsers(users)) {
            JsonDocument response;
            response["type"] = "users_list";
            response["count"] = users.size();
            JsonArray usersArray = response["users"].to<JsonArray>();
            for (const auto& user : users) {
                JsonObject obj = usersArray.add<JsonObject>();
                obj["username"] = user.username;
                obj["role"] = user.role;
                obj["lastLogin"] = user.lastLogin;
            }
            sendRealtimeData(response);
        } else {
            JsonDocument response;
            response["type"] = "error";
            response["message"] = "No users found";
            sendRealtimeData(response);
        }
        return;
    }

    // GET_SITE_INFO
    if (action == "GET_SITE_INFO") {
        SiteInfo info;
        if (loadSiteInfo(info)) {
            JsonDocument response;
            response["type"] = "site_info_response";
            response["siteId"] = info.siteId;
            response["siteName"] = info.siteName;
            response["licenseKey"] = info.licenseKey;
            response["expiryDate"] = info.expiryDate;
            response["maxUsers"] = info.maxUsers;
            sendRealtimeData(response);
        } else {
            JsonDocument response;
            response["type"] = "error";
            response["message"] = "No site info found";
            sendRealtimeData(response);
        }
        return;
    }

    // ✅ GET_REGISTRY - Read
    if (action == "GET_REGISTRY") {
        uint16_t regAddr = doc["RegAdd"] | 0;
        Serial.printf("[WS] 📥 GET_REGISTRY: 0x%04X (%d)\n", regAddr, regAddr);

        if (!sessionKeyValid) {
            JsonDocument response;
            response["type"] = "error";
            response["message"] = "No secure session";
            sendRealtimeData(response);
            return;
        }

        JsonDocument req;
        req["RegAdd"] = regAddr;
        req["RegVal"] = "";

        JsonDocument response;
        bool sent = sendMybusData(req, &response);
        bool readSuccess = sent && (response["success"] | false);

        if (readSuccess) {
            Serial.println("[WS] ✅ Registry response received");

            JsonDocument wsMsg;
            wsMsg["type"] = "registry_response";
            wsMsg["RegAdd"] = regAddr;

            if (!response["value"].isNull()) {
                wsMsg["value"] = response["value"];
            }
            wsMsg["payloadHex"] = response["payloadHex"] | "";
            wsMsg["command"] = response["command"] | 0;
            wsMsg["flags"] = response["flags"] | 0;

            sendRealtimeData(wsMsg);
        } else {
            JsonDocument errorMsg;
            errorMsg["type"] = "error";
            errorMsg["message"] = "Failed to read registry";
            errorMsg["RegAdd"] = regAddr;

            if (!sent) {
                errorMsg["reason"] = "transport_error";
            } else if (response["errorCode"].is<int>()) {
                errorMsg["reason"] = "backend_error";
                errorMsg["errorCode"] = response["errorCode"];
            } else {
                errorMsg["reason"] = "unknown";
            }

            sendRealtimeData(errorMsg);
        }
        return;
    }

    // ✅ WRITE_REGISTRY - Write
    if (action == "WRITE_REGISTRY") {
        handleWriteRegistry(doc);
        return;
    }

    // COMMAND (legacy)
    if (action == "COMMAND") {
        if (!doc["data"].is<JsonObject>()) {
            Serial.println("[WS] COMMAND missing data object");
            return;
        }

        JsonObject dataObj = doc["data"].as<JsonObject>();
        String encryptedData = dataObj["encryptedData"] | "";
        String iv = dataObj["iv"] | "";
        String authTag = dataObj["authTag"] | "";

        if (encryptedData.isEmpty() || iv.isEmpty() || authTag.isEmpty()) {
            JsonDocument response;
            response["type"] = "error";
            response["message"] = "Missing encrypted data";
            sendRealtimeData(response);
            return;
        }

        String plaintext;
        if (decryptPayload(encryptedData, iv, authTag, plaintext)) {
            JsonDocument commandDoc;
            DeserializationError cmdErr = deserializeJson(commandDoc, plaintext);
            if (!cmdErr) {
                if (commandCallback) {
                    commandCallback(commandDoc);
                }
                JsonDocument response;
                response["type"] = "command_ack";
                response["status"] = "success";
                response["message"] = "Command executed";
                sendRealtimeData(response);
            } else {
                JsonDocument response;
                response["type"] = "error";
                response["message"] = "Invalid command JSON";
                sendRealtimeData(response);
            }
        } else {
            JsonDocument response;
            response["type"] = "error";
            response["message"] = "Decryption failed";
            sendRealtimeData(response);
        }
        return;
    }

    // HANDSHAKE
    if (action == "HANDSHAKE") {
        JsonDocument response;
        response["type"] = "handshake_response";
        response["status"] = "success";
        response["message"] = "Use HTTP /devices/handshake for mYBUS v2";
        sendRealtimeData(response);
        return;
    }

    // UNKNOWN
    JsonDocument response;
    response["type"] = "error";
    response["message"] = String("Unknown action: ") + action;
    sendRealtimeData(response);
    Serial.printf("[WS] Unknown action: %s\n", action.c_str());
}

// ============================================================
// Send realtime data
// ============================================================

bool CloudManager::sendRealtimeData(JsonDocument& data)
{
    if (!wsConnected || wsClient == nullptr) {
        Serial.println("[WS] Cannot send: not connected");
        return false;
    }

    String response;
    serializeJson(data, response);

    bool result = wsClient->text(response);
    if (!result) {
        Serial.println("[WS] Failed to send message");
        return false;
    }
    return true;
}

// ============================================================
// Save token
// ============================================================

void CloudManager::saveToken(const String& token)
{
    if (token.isEmpty()) {
        Serial.println("[AUTH] Cannot save empty token");
        return;
    }
    preferences.putString("jwtToken", token);
    Serial.println("[AUTH] Token saved");
}

String CloudManager::loadToken()
{
    jwtToken = preferences.getString("jwtToken", "");
    refreshTokenValue = preferences.getString("refreshToken", "");

    String storedDeviceId = preferences.getString("deviceId", "");
    if (!storedDeviceId.isEmpty()) {
        deviceId = storedDeviceId;
    }

    if (jwtToken.isEmpty()) {
        isAuthenticated = false;
        Serial.println("[AUTH] No token found");
        return "";
    }

    isAuthenticated = true;
    Serial.println("[AUTH] Token loaded from Preferences");
    if (!refreshTokenValue.isEmpty()) {
        Serial.println("[AUTH] Refresh token loaded");
    }
    return jwtToken;
}