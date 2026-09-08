#include "CloudManager.h"

#include <Arduino.h>
#include <esp_system.h>
#include <vector>

#include "mybus_protocol_constants.h"

namespace {


String generateDeviceId()
{
    const uint64_t mac = ESP.getEfuseMac();

    char buffer[40];

    snprintf(
        buffer,
        sizeof(buffer),
        "ESP32_ECOSMART_%012llX",
        static_cast<unsigned long long>(mac)
    );

    return String(buffer);
}

} 

CloudManager::CloudManager()
    : httpTransport_(
          apiBaseUrl_,
          jwtToken_
      ),
      auth_(
          httpTransport_,
          preferences_,
          deviceId_,
          jwtToken_
      ),
      mybusSession_(
          httpTransport_,
          deviceId_,
          mybus_proto::INTERFACE_WIFI,
          0
      ),
      mybusTransport_(
          httpTransport_,
          mybusSession_
      ),
      wsServer_(
          mybusTransport_,
          storage_,
          deviceId_,
          siteId_
      )
{
  

    if (!preferences_.begin("cloud", false)) {
        Serial.println(
            "[CLOUD] ❌ Preferences begin failed"
        );
    }



    deviceId_ = generateDeviceId();

    const String storedDeviceId =
        preferences_.getString(
            "deviceId",
            ""
        );

    if (!storedDeviceId.isEmpty()) {
        deviceId_ = storedDeviceId;
    }

    mybusSession_.setDeviceId(deviceId_); // ✅ sync اول

    // --------------------------------------------------------
    // mYBUS numeric address
    //
    // IMPORTANT:
    // These values must match:
    //
    // device.mybusDeviceId
    // device.mybusZoneId
    //
    // on backend.
    // --------------------------------------------------------

    mybusDeviceId_ =
        preferences_.getUChar(
            "mybusDev",
            0
        );

    mybusZoneId_ =
        preferences_.getUChar(
            "mybusZone",
            0
        );

    // --------------------------------------------------------
    // Configure session
    // --------------------------------------------------------

    mybusSession_.setInterfaceId(
        mybus_proto::INTERFACE_WIFI
    );

    mybusSession_.setZone(
        mybusZoneId_
    );

    // --------------------------------------------------------
    // JWT
    //
    // ⚠️ CloudAuth::loadToken() می‌تواند deviceId_ را (چون رفرنس است)
    // از preferences دوباره بازنویسی کند. برای اطمینان، بعد از این
    // فراخوانی هم mybusSession_ را دوباره sync می‌کنیم.
    // --------------------------------------------------------

    jwtToken_ =
        auth_.loadToken();

    mybusSession_.setDeviceId(deviceId_); // ✅ sync دوباره بعد از loadToken

    // --------------------------------------------------------
    // Device ECDH keypair
    // --------------------------------------------------------

    if (!mybusSession_.initializeDeviceKeypair()) {
        Serial.println(
            "[CLOUD] ❌ Device keypair initialization failed"
        );
    }

    // --------------------------------------------------------
    // Filesystem / Users
    //
    // ✅ site_info.json دیگر لازم نیست: فقط users.json (برای لاگین
    // آفلاین) ساخته/خوانده می‌شود.
    // --------------------------------------------------------

    if (!storage_.init()) {

        Serial.println(
            "[FS] ❌ Filesystem mount failed"
        );

    } else {

        storage_.createDefaultUsersFile();
    }

    // --------------------------------------------------------
    // Startup log
    // --------------------------------------------------------

    Serial.println();
    Serial.println(
        "========================================"
    );

    Serial.println(
        "[CLOUD] CloudManager initialized"
    );

    Serial.println(
        "========================================"
    );

    Serial.print(
        "[CLOUD] Device ID: "
    );

    Serial.println(
        deviceId_
    );

    Serial.print(
        "[CLOUD] mYBUS Device ID: "
    );

    Serial.println(
        mybusDeviceId_
    );

    Serial.print(
        "[CLOUD] mYBUS Zone: "
    );

    Serial.println(
        mybusZoneId_
    );

    Serial.print(
        "[CLOUD] Interface: "
    );

    Serial.println(
        mybus_proto::INTERFACE_WIFI
    );

    Serial.print(
        "[CLOUD] API URL: "
    );

    Serial.println(
        apiBaseUrl_
    );

    Serial.print(
        "[CLOUD] Authenticated: "
    );

    Serial.println(
        auth_.isLoggedIn()
            ? "YES"
            : "NO"
    );

    Serial.print(
        "[CLOUD] Secure session: "
    );

    Serial.println(
        mybusSession_.isEstablished()
            ? "✅ ACTIVE"
            : "❌ NOT ACTIVE"
    );

    if (mybusDeviceId_ == 0) {
        Serial.println(
            "[CLOUD] ⚠️ mYBUS Device ID is NOT configured"
        );
    }

    if (mybusZoneId_ == 0) {
        Serial.println(
            "[CLOUD] ⚠️ mYBUS Zone is NOT configured"
        );
    }
}

// ============================================================
// Destructor
// ============================================================

CloudManager::~CloudManager()
{
    preferences_.end();
}

// ============================================================
// Auth
// ============================================================

bool CloudManager::loginUser(
    const String& u,
    const String& p,
    const String& deviceId)
{
    const bool ok =
        auth_.loginUser(
            u,
            p,
            deviceId
        );

    // ✅ لاگین می‌تواند deviceId_ را از پاسخ بک‌اند (responseDeviceId)
    // تغییر دهد؛ چون CloudAuth::deviceId_ رفرنس است، خود
    // CloudManager::deviceId_ هم عوض می‌شود. باید mybusSession_ را
    // دوباره sync کنیم تا هندشیک بعدی از deviceId درست استفاده کند.
    if (ok) {
        mybusSession_.setDeviceId(deviceId_);
    }

    return ok;
}

bool CloudManager::refreshToken()
{
    return auth_.refreshToken();
}

bool CloudManager::isLoggedIn() const
{
    return auth_.isLoggedIn();
}

// ============================================================
// Offline login
// ============================================================

bool CloudManager::loginOffline(
    const String& username,
    const String& password)
{
    if (username.isEmpty() ||
        password.isEmpty()) {

        return false;
    }

    return storage_.loginOffline(
        username,
        password
    );
}

// ============================================================
// mYBUS
// ============================================================

bool CloudManager::performHandshake()
{
    // --------------------------------------------------------
    // Handshake requires a valid configured address
    // --------------------------------------------------------

    if (mybusDeviceId_ == 0 ||
        mybusDeviceId_ == 255) {

        Serial.println(
            "[mYBUS] ❌ Cannot handshake: invalid Device ID"
        );

        return false;
    }

    if (mybusZoneId_ == 0 ||
        mybusZoneId_ == 255) {

        Serial.println(
            "[mYBUS] ❌ Cannot handshake: invalid Zone"
        );

        return false;
    }

    // ✅ sync نهایی، درست قبل از هندشیک، به‌عنوان شبکه‌ی ایمنی آخر
    // (ارزان است و تضمین می‌کند مقدار همیشه به‌روز باشد، حتی اگر
    // مسیر دیگری deviceId_ را تغییر داده باشد).
    mybusSession_.setDeviceId(
        deviceId_
    );

    // Keep session configuration synchronized
    mybusSession_.setInterfaceId(
        mybus_proto::INTERFACE_WIFI
    );

    mybusSession_.setZone(
        mybusZoneId_
    );

    return mybusSession_.performHandshake(
        nextRequestNumber()
    );
}

bool CloudManager::isSecureSessionEstablished() const
{
    return mybusSession_.isEstablished();
}

bool CloudManager::sendMybusData(
    JsonDocument& data,
    JsonDocument* outResponse)
{
    if (!mybusSession_.isEstablished()) {

        Serial.println(
            "[mYBUS] ❌ Secure session not established"
        );

        return false;
    }

    if (mybusDeviceId_ == 0 ||
        mybusDeviceId_ == 255) {

        Serial.println(
            "[mYBUS] ❌ Invalid local mYBUS Device ID"
        );

        return false;
    }

    if (mybusZoneId_ == 0 ||
        mybusZoneId_ == 255) {

        Serial.println(
            "[mYBUS] ❌ Invalid local mYBUS Zone"
        );

        return false;
    }

    return mybusTransport_.sendMybusData(
        data,
        nextRequestNumber(),
        outResponse
    );
}

bool CloudManager::sendRegistryFrame(
    uint16_t regAddr,
    const uint8_t* val,
    size_t len,
    bool isWrite,
    uint8_t busDeviceId,
    JsonDocument* outResponse)
{
    if (!mybusSession_.isEstablished()) {

        Serial.println(
            "[mYBUS] ❌ Secure session not established"
        );

        return false;
    }

    if (busDeviceId == 0) {
        busDeviceId =
            mybusDeviceId_;
    }

    if (busDeviceId == 0 ||
        busDeviceId == 255) {

        Serial.println(
            "[mYBUS] ❌ Invalid mYBUS Device ID"
        );

        return false;
    }

    if (mybusZoneId_ == 0 ||
        mybusZoneId_ == 255) {

        Serial.println(
            "[mYBUS] ❌ Invalid mYBUS Zone"
        );

        return false;
    }

    mybusSession_.setInterfaceId(
        mybus_proto::INTERFACE_WIFI
    );

    mybusSession_.setZone(
        mybusZoneId_
    );

    return mybusTransport_.sendRegistryFrame(
        regAddr,
        val,
        len,
        isWrite,
        busDeviceId,
        nextRequestNumber(),
        outResponse
    );
}

// ============================================================
// mYBUS address configuration
// ============================================================

void CloudManager::setMybusDeviceId(
    uint8_t deviceId)
{
    if (deviceId == 0 ||
        deviceId == 255) {

        Serial.printf(
            "[mYBUS] ❌ Invalid Device ID: %u\n",
            static_cast<unsigned>(deviceId)
        );

        return;
    }

    mybusDeviceId_ =
        deviceId;

    preferences_.putUChar(
        "mybusDev",
        mybusDeviceId_
    );

    Serial.printf(
        "[mYBUS] ✅ Device ID set to %u\n",
        static_cast<unsigned>(mybusDeviceId_)
    );
}

void CloudManager::setMybusZoneId(
    uint8_t zone)
{
    if (zone == 0 ||
        zone == 255) {

        Serial.printf(
            "[mYBUS] ❌ Invalid Zone: %u\n",
            static_cast<unsigned>(zone)
        );

        return;
    }

    mybusZoneId_ =
        zone;

    preferences_.putUChar(
        "mybusZone",
        mybusZoneId_
    );

    mybusSession_.setZone(
        mybusZoneId_
    );

    Serial.printf(
        "[mYBUS] ✅ Zone set to %u\n",
        static_cast<unsigned>(mybusZoneId_)
    );
}

uint8_t CloudManager::getMybusDeviceId() const
{
    return mybusDeviceId_;
}

uint8_t CloudManager::getMybusZoneId() const
{
    return mybusZoneId_;
}

// ============================================================
// WebSocket
// ============================================================

void CloudManager::startWebSocketServer()
{
    wsServer_.start();
}

void CloudManager::loopWebSocketServer()
{
    wsServer_.loop();
}

bool CloudManager::isWebSocketConnected() const
{
    return wsServer_.isConnected();
}

bool CloudManager::sendRealtimeData(
    JsonDocument& data)
{
    return wsServer_.sendRealtimeData(data);
}
// ============================================================
// WebSocket callbacks
// ============================================================

void CloudManager::onCommand(
    CloudWebSocketServer::CommandCallback cb
)
{
    wsServer_.onCommand(cb);
}

void CloudManager::onLocalRegistryRead(
    CloudWebSocketServer::LocalRegistryReadCallback cb
)
{
    wsServer_.onLocalRegistryRead(cb);
}

void CloudManager::onShouldSkipMybusWrite(
    CloudWebSocketServer::ShouldSkipMybusWriteCallback cb
)
{
    wsServer_.onShouldSkipMybusWrite(cb);
}

// ============================================================
// Config
// ============================================================

void CloudManager::setApiBaseUrl(
    const String& url)
{
    if (url.isEmpty()) {
        return;
    }

    apiBaseUrl_ = url;

    while (apiBaseUrl_.endsWith("/")) {
        apiBaseUrl_.remove(
            apiBaseUrl_.length() - 1
        );
    }

    Serial.print(
        "[HTTP] API Base URL: "
    );

    Serial.println(
        apiBaseUrl_
    );
}

String CloudManager::getApiBaseUrl() const
{
    return apiBaseUrl_;
}

void CloudManager::setDeviceId(
    const String& id)
{
    if (id.isEmpty()) {
        return;
    }

    deviceId_ = id;

    preferences_.putString(
        "deviceId",
        deviceId_
    );

    // ✅ هر جا از بیرون deviceId عوض شود (مثلاً برای اجبار به یک ID
    // قدیمی/خاص)، باید mybusSession_ هم بلافاصله sync شود.
    mybusSession_.setDeviceId(
        deviceId_
    );

    Serial.print(
        "[CLOUD] Device ID changed: "
    );

    Serial.println(
        deviceId_
    );
}

String CloudManager::getDeviceId() const
{
    return deviceId_;
}

String CloudManager::getJwtToken() const
{
    return jwtToken_;
}

// ============================================================
// Request number
// ============================================================

uint32_t CloudManager::nextRequestNumber()
{
    ++requestNumber_;

    if (requestNumber_ == 0) {
        requestNumber_ = 1;
    }

    return requestNumber_;
}