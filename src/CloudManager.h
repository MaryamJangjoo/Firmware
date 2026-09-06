#ifndef CLOUD_MANAGER_H
#define CLOUD_MANAGER_H

#include <Arduino.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <functional>
#include <vector>
#include <FS.h>

#ifdef USE_LittleFS
#include <LittleFS.h>
#define FILESYSTEM LittleFS
#else
#include <SPIFFS.h>
#define FILESYSTEM SPIFFS
#endif

#include "crypto.hpp"
#include "mybus_frame.h"

struct UserInfo {
    String username;
    String passwordHash;
    String publicKey;
    String role;
    uint32_t lastLogin = 0;
};

struct SiteInfo {
    String siteId;
    String siteName;
    String licenseKey;
    uint32_t expiryDate = 0;
    uint32_t maxUsers = 10;
};

class CloudManager {
public:
    using CommandCallback = std::function<void(const JsonDocument&)>;

    CloudManager();
    ~CloudManager();

    // ========================================================
    // Authentication
    // ========================================================

    bool loginUser(
        const String& username,
        const String& password,
        const String& requestedDeviceId
    );

    bool refreshToken();

    bool isLoggedIn() const;
    bool isTokenValid() const;

    // ========================================================
    // mYBUS v2
    // ========================================================

    bool performHandshake();
    bool isSecureSessionEstablished() const;
    void clearSecureSession();

    // ========================================================
    // mYBUS v2 Binary Frame
    // ========================================================

    bool sendMybusBinaryFrame(
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
        JsonDocument* outResponse = nullptr   // ✅ اگر داده شود، پاسخ رمزگشایی/parse‌شده اینجا پر می‌شود
    );

    bool sendRawBinaryToBackend(
        const uint8_t* data,
        size_t len,
        JsonDocument* outResponse = nullptr   // ✅ همون منطق بالا
    );

    bool sendRegistryFrame(
        uint16_t regAddr,
        const uint8_t* regValue,
        size_t valueLen,
        bool isWrite,
        uint8_t busDeviceId = 1,
        JsonDocument* outResponse = nullptr   // ✅
    );

    // ========================================================
    // WebSocket Server
    // ========================================================

    void startWebSocketServer();
    void loopWebSocketServer();

    bool isWebSocketConnected() const;

    bool sendRealtimeData(JsonDocument& data);
    bool sendMybusData(JsonDocument& data, JsonDocument* outResponse = nullptr);

    void onCommand(CommandCallback cb);

    // ========================================================
    // Server Requests
    // ========================================================

    void requestSiteInfo();
    void requestUsersList();

    // ========================================================
    // Local Filesystem
    // ========================================================

    bool loadUsers(std::vector<UserInfo>& users);
    bool saveUsers(const std::vector<UserInfo>& users);

    bool loadSiteInfo(SiteInfo& info);
    bool saveSiteInfo(const SiteInfo& info);

    bool isFilesystemMounted() const;

    bool readFile(const String& path, String& content);
    bool writeFile(const String& path, const String& content);

    void listFiles();
    void printFileContent(const String& path);

    bool createDefaultUsersFile();
    bool createDefaultSiteInfoFile();

    // ========================================================
    // Offline Authentication
    // ========================================================

    bool loginOffline(
        const String& username,
        const String& password
    );

    bool verifyUserPassword(
        const String& username,
        const String& password
    );

    // ========================================================
    // Configuration
    // ========================================================

    void setApiBaseUrl(const String& url);
    String getApiBaseUrl() const;

    void setDeviceId(const String& id);
    String getDeviceId() const;

    String getJwtToken() const;

private:

    // ========================================================
    // Constants
    // ========================================================

    static constexpr size_t MYBUS_MAX_PAYLOAD_SIZE = 512;

    static constexpr uint8_t MYBUS_INTERFACE_WIFI = 1;
    static constexpr uint8_t MYBUS_ZONE_DEFAULT = 10;
    static constexpr uint8_t MYBUS_QOS_DEFAULT = 0;
    static constexpr uint8_t MYBUS_OPTIONS_DEFAULT = 0;
    static constexpr uint8_t MYBUS_COMPRESSION_NONE = 0;
    static constexpr uint8_t MYBUS_FLAG_REQUEST = 0;
    static constexpr uint8_t MYBUS_SECURITY_HANDSHAKE = 1;
    static constexpr uint8_t MYBUS_SECURITY_ENCRYPTED = 2;
    static constexpr uint8_t MYBUS_COMMAND_HANDSHAKE = 250;
    static constexpr uint8_t MYBUS_COMMAND_REGISTRY = 2;

    // ========================================================
    // Authentication
    // ========================================================

    String apiBaseUrl = "http://192.168.88.174:3000";

    String deviceId;
    String jwtToken;
    String refreshTokenValue;
    String siteId;

    bool isAuthenticated = false;

    // ========================================================
    // Filesystem
    // ========================================================

    bool _filesystemMounted = false;
    bool _hasRequestedData = false;

    // ========================================================
    // mYBUS Security
    // ========================================================

    mbedtls_ecp_keypair deviceKeypair;

    bool deviceKeypairInitialized = false;

    String devicePublicKeyPem;
    String devicePrivateKeyPem;

    String serverPublicKeyPem;

    uint8_t sessionKey[32];
    bool sessionKeyValid = false;

    String handshakeNonce;

    uint32_t requestNumber = 0;

    // ========================================================
    // Preferences
    // ========================================================

    Preferences preferences;

    // ========================================================
    // HTTP
    // ========================================================

    String sendRequest(
        const String& endpoint,
        const String& method,
        const String& body,
        bool useAuth = true
    );

    bool isHttpSuccess(int httpCode);

    // ========================================================
    // Device
    // ========================================================

    String generateDeviceId();

    // ========================================================
    // Key / Crypto
    // ========================================================

    bool initializeDeviceKeypair();

    bool computeSessionKey();

    bool generateHandshakeNonce();

    bool createChallengeHmac(String& hmacHex);

    bool authenticateHandshakeSession();

    bool encryptPayload(
        const String& plainText,
        String& encryptedHex,
        String& ivHex,
        String& authTagHex
    );

    bool decryptPayload(
        const String& encryptedHex,
        const String& ivHex,
        const String& authTagHex,
        String& plainTextOut
    );

    String createNonce();

    String hmacHex(
        const uint8_t* key,
        size_t keyLen,
        const uint8_t* message,
        size_t messageLen
    );

    bool decryptAndParseMybusResponse(
        const uint8_t* wireData,
        size_t wireLen,
        JsonDocument& outDoc
    );

    // ✅ جدید: مقدار پاسخ را بر اساس dataType استخراج‌شده از regAddر درخواست دیکد می‌کند
    // (چون پاسخ بک‌اند دیگر آدرس رجیستری را echo نمی‌کند)
    void decodeRegistryResponseValue(
        JsonDocument& doc,
        uint16_t regAddr
    );

    // ========================================================
    // Token
    // ========================================================

    void saveToken(const String& token);
    String loadToken();

    // ========================================================
    // mYBUS
    // ========================================================

    uint32_t nextRequestNumber();

    // ========================================================
    // WebSocket
    // ========================================================

    AsyncWebServer* server = nullptr;
    AsyncWebSocket* ws = nullptr;
    AsyncWebSocketClient* wsClient = nullptr;

    bool wsConnected = false;

    CommandCallback commandCallback = nullptr;

    void onWebSocketEvent(
        AsyncWebSocket* server,
        AsyncWebSocketClient* client,
        AwsEventType type,
        void* arg,
        uint8_t* data,
        size_t len
    );

    void handleWebSocketMessage(
        void* arg,
        uint8_t* data,
        size_t len
    );

    void handleWriteRegistry(JsonDocument& doc);

    // ========================================================
    // Filesystem
    // ========================================================

    bool initFilesystem();

    // ========================================================
    // Singleton
    // ========================================================

    static CloudManager* s_instance;
};

#endif