#include "CloudManager.h"

#include <Arduino.h>
#include <esp_system.h>
#include <vector>

namespace {

// ⚠️ فرض شده — نسخه‌ی قدیمی این ثابت‌ها را به‌عنوان static const عضو کلاس
// نگه می‌داشت با مقادیر نامشخص برای ما. اگر بک‌اند/سند mYBUS مقدار دیگری
// برای Interface/Zone انتظار دارد، همین‌جا عوضشان کنید.
constexpr uint8_t kMybusInterfaceWifi = 1;
constexpr uint8_t kMybusZoneDefault   = 0;

String generateDeviceId()
{
    uint64_t mac = ESP.getEfuseMac();
    char buffer[40];
    snprintf(buffer, sizeof(buffer), "ESP32_ECOSMART_%012llX",
              static_cast<unsigned long long>(mac));
    return String(buffer);
}

} // namespace

// ============================================================
// Constructor / Destructor
// ============================================================

CloudManager::CloudManager()
    : httpTransport_(apiBaseUrl_, jwtToken_),
      auth_(httpTransport_, preferences_, deviceId_, jwtToken_),
      mybusSession_(httpTransport_, deviceId_, kMybusInterfaceWifi, kMybusZoneDefault),
      mybusTransport_(httpTransport_, mybusSession_),
      wsServer_(mybusTransport_, storage_, deviceId_, siteId_)
{
    if (!preferences_.begin("cloud", false)) {
        Serial.println("[CLOUD] Preferences begin failed");
    }

    // ---- Device ID: پیش‌فرض بر اساس MAC، مگر این‌که قبلاً در NVS ذخیره شده باشد ----
    deviceId_ = generateDeviceId();
    String storedDeviceId = preferences_.getString("deviceId", "");
    if (!storedDeviceId.isEmpty()) {
        deviceId_ = storedDeviceId;
    }

    // ---- توکن ذخیره‌شده را بارگذاری کن ----
    jwtToken_ = auth_.loadToken();

    // ---- Device keypair برای هندشیک mYBUS ----
    if (!mybusSession_.initializeDeviceKeypair()) {
        Serial.println("[CLOUD] Device keypair initialization failed");
    }

    // ---- فایل‌سیستم / کاربران / اطلاعات سایت ----
    if (!storage_.init()) {
        Serial.println("[FS] Filesystem mount failed");
    } else {
        storage_.createDefaultUsersFile();
        storage_.createDefaultSiteInfoFile();

        SiteInfo info;
        if (storage_.loadSiteInfo(info)) {
            siteId_ = info.siteId;
        }
    }

    Serial.println();
    Serial.println("========================================");
    Serial.println("[CLOUD] CloudManager initialized");
    Serial.println("========================================");
    Serial.print("[CLOUD] Device ID: ");
    Serial.println(deviceId_);
    Serial.print("[CLOUD] API URL: ");
    Serial.println(apiBaseUrl_);
    Serial.print("[CLOUD] Authenticated: ");
    Serial.println(auth_.isLoggedIn() ? "YES" : "NO");
    Serial.print("[CLOUD] Secure session: ");
    Serial.println(mybusSession_.isEstablished() ? "✅ ACTIVE" : "❌ NOT ACTIVE");
}

CloudManager::~CloudManager()
{
    preferences_.end();
}

// ============================================================
// Auth
// ============================================================

bool CloudManager::loginUser(const String& u, const String& p, const String& deviceId)
{
    return auth_.loginUser(u, p, deviceId);
}

bool CloudManager::refreshToken()
{
    return auth_.refreshToken();
}

bool CloudManager::isLoggedIn() const
{
    return auth_.isLoggedIn();
}

// ⚠️ عمداً از auth_.loginOffline() استفاده نمی‌کند: CloudAuth در سازنده‌ی
// فعلی‌اش هیچ ارجاعی به CloudStorage ندارد (نگاه کنید به کامنت داخل
// CloudAuth.h)، پس منطق offline-login مستقیماً همین‌جا با storage_ پیاده
// شده. اگر بعداً CloudAuth را با CloudStorage& سیم‌کشی کردید، این تابع را
// می‌توانید به auth_.loginOffline(u, p) ساده کنید.
bool CloudManager::loginOffline(const String& username, const String& password)
{
    if (username.isEmpty() || password.isEmpty()) return false;

    std::vector<UserInfo> users;
    if (!storage_.loadUsers(users)) {
        Serial.println("[AUTH] No users available for offline login");
        return false;
    }

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

    Serial.printf("[AUTH] User '%s' not found\n", username.c_str());
    return false;
}

// ============================================================
// mYBUS
// ============================================================

bool CloudManager::performHandshake()
{
    return mybusSession_.performHandshake(nextRequestNumber());
}

bool CloudManager::isSecureSessionEstablished() const
{
    return mybusSession_.isEstablished();
}

bool CloudManager::sendMybusData(JsonDocument& data, JsonDocument* outResponse)
{
    return mybusTransport_.sendMybusData(data, nextRequestNumber(), outResponse);
}

bool CloudManager::sendRegistryFrame(uint16_t regAddr, const uint8_t* val, size_t len,
                                      bool isWrite, uint8_t busDeviceId,
                                      JsonDocument* outResponse)
{
    return mybusTransport_.sendRegistryFrame(
        regAddr, val, len, isWrite, busDeviceId, nextRequestNumber(), outResponse);
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

bool CloudManager::sendRealtimeData(JsonDocument& data)
{
    return wsServer_.sendRealtimeData(data);
}

void CloudManager::onCommand(CloudWebSocketServer::CommandCallback cb)
{
    wsServer_.onCommand(cb);
}

// ============================================================
// Config
// ============================================================

void CloudManager::setApiBaseUrl(const String& url)
{
    if (url.isEmpty()) return;
    apiBaseUrl_ = url;
    while (apiBaseUrl_.endsWith("/")) {
        apiBaseUrl_.remove(apiBaseUrl_.length() - 1);
    }
    Serial.print("[HTTP] API Base URL: ");
    Serial.println(apiBaseUrl_);
}

String CloudManager::getApiBaseUrl() const
{
    return apiBaseUrl_;
}

void CloudManager::setDeviceId(const String& id)
{
    if (id.isEmpty()) return;
    deviceId_ = id;
    preferences_.putString("deviceId", deviceId_);
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
    if (requestNumber_ == 0) requestNumber_ = 1;
    return requestNumber_;
}