#include "CloudManager.h"

#include <Arduino.h>
#include <esp_system.h>
#include <vector>

#include "mybus_protocol_constants.h"
#include "Logging.h"

static const char* TAG = "CLOUD";

namespace {

String generateDeviceId()
{
    const uint64_t mac = ESP.getEfuseMac();

    char buffer[40];

    snprintf(
        buffer,
        sizeof(buffer),
        "ECOSMART_%012llX",
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
        ECOSMART_LOGE(TAG, "Preferences begin failed");
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

    mybusSession_.setDeviceId(deviceId_);

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

    mybusSession_.setInterfaceId(
        mybus_proto::INTERFACE_WIFI
    );

    mybusSession_.setZone(
        mybusZoneId_
    );

    jwtToken_ =
        auth_.loadToken();

    mybusSession_.setDeviceId(deviceId_);

    if (!mybusSession_.initializeDeviceKeypair()) {
        ECOSMART_LOGE(TAG, "Device keypair initialization failed");
    }

    if (!storage_.init()) {
        ECOSMART_LOGE(TAG, "Filesystem mount failed");
    } else {
        storage_.createDefaultUsersFile();
        storage_.createDefaultSiteInfoFile();
    }

    ECOSMART_LOGI(TAG, "========================================");
    ECOSMART_LOGI(TAG, "CloudManager initialized");
    ECOSMART_LOGI(TAG, "========================================");
    ECOSMART_LOGI(TAG, "Device ID: %s", deviceId_.c_str());
    ECOSMART_LOGI(TAG, "mYBUS Device ID: %u", static_cast<unsigned>(mybusDeviceId_));
    ECOSMART_LOGI(TAG, "mYBUS Zone: %u", static_cast<unsigned>(mybusZoneId_));
    ECOSMART_LOGI(TAG, "Interface: %u", static_cast<unsigned>(mybus_proto::INTERFACE_WIFI));
    ECOSMART_LOGI(TAG, "API URL: %s", apiBaseUrl_.c_str());
    ECOSMART_LOGI(TAG, "Authenticated: %s", auth_.isLoggedIn() ? "YES" : "NO");
    ECOSMART_LOGI(TAG, "Secure session: %s",
        mybusSession_.isEstablished() ? "ACTIVE" : "NOT ACTIVE");

    if (mybusDeviceId_ == 0) {
        ECOSMART_LOGW(TAG, "mYBUS Device ID is NOT configured");
    }

    if (mybusZoneId_ == 0) {
        ECOSMART_LOGW(TAG, "mYBUS Zone is NOT configured");
    }
}

CloudManager::~CloudManager()
{
    preferences_.end();
}

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

bool CloudManager::performHandshake()
{
    if (mybusDeviceId_ == 0 ||
        mybusDeviceId_ == 255) {

        ECOSMART_LOGE(TAG, "Cannot handshake: invalid Device ID");
        return false;
    }

    if (mybusZoneId_ == 0 ||
        mybusZoneId_ == 255) {

        ECOSMART_LOGE(TAG, "Cannot handshake: invalid Zone");
        return false;
    }

    mybusSession_.setDeviceId(
        deviceId_
    );

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
        ECOSMART_LOGE(TAG, "Secure session not established");
        return false;
    }

    if (mybusDeviceId_ == 0 ||
        mybusDeviceId_ == 255) {

        ECOSMART_LOGE(TAG, "Invalid local mYBUS Device ID");
        return false;
    }

    if (mybusZoneId_ == 0 ||
        mybusZoneId_ == 255) {

        ECOSMART_LOGE(TAG, "Invalid local mYBUS Zone");
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
        ECOSMART_LOGE(TAG, "Secure session not established");
        return false;
    }

    if (busDeviceId == 0) {
        busDeviceId =
            mybusDeviceId_;
    }

    if (busDeviceId == 0 ||
        busDeviceId == 255) {

        ECOSMART_LOGE(TAG, "Invalid mYBUS Device ID");
        return false;
    }

    if (mybusZoneId_ == 0 ||
        mybusZoneId_ == 255) {

        ECOSMART_LOGE(TAG, "Invalid mYBUS Zone");
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

void CloudManager::setMybusDeviceId(
    uint8_t deviceId)
{
    if (deviceId == 0 ||
        deviceId == 255) {

        ECOSMART_LOGE(TAG, "Invalid Device ID: %u", static_cast<unsigned>(deviceId));
        return;
    }

    mybusDeviceId_ =
        deviceId;

    preferences_.putUChar(
        "mybusDev",
        mybusDeviceId_
    );

    ECOSMART_LOGI(TAG, "Device ID set to %u", static_cast<unsigned>(mybusDeviceId_));
}

void CloudManager::setMybusZoneId(
    uint8_t zone)
{
    if (zone == 0 ||
        zone == 255) {

        ECOSMART_LOGE(TAG, "Invalid Zone: %u", static_cast<unsigned>(zone));
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

    ECOSMART_LOGI(TAG, "Zone set to %u", static_cast<unsigned>(mybusZoneId_));
}

uint8_t CloudManager::getMybusDeviceId() const
{
    return mybusDeviceId_;
}

uint8_t CloudManager::getMybusZoneId() const
{
    return mybusZoneId_;
}

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

void CloudManager::onBinaryFrame(CloudWebSocketServer::BinaryFrameCallback cb)
{
    wsServer_.onBinaryFrame(cb);
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

void CloudManager::onLocalRegistryWrite(
    CloudWebSocketServer::LocalRegistryWriteCallback cb)
{
    wsServer_.setLocalRegistryWrite(cb);
}

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

    ECOSMART_LOGI(TAG, "API Base URL: %s", apiBaseUrl_.c_str());
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

    mybusSession_.setDeviceId(
        deviceId_
    );

    ECOSMART_LOGI(TAG, "Device ID changed: %s", deviceId_.c_str());
}

String CloudManager::getDeviceId() const
{
    return deviceId_;
}

String CloudManager::getJwtToken() const
{
    return jwtToken_;
}

uint32_t CloudManager::nextRequestNumber()
{
    ++requestNumber_;

    if (requestNumber_ == 0) {
        requestNumber_ = 1;
    }

    return requestNumber_;
}