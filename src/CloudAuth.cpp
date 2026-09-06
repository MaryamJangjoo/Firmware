#include "CloudAuth.h"

#include <Arduino.h>
#include <ArduinoJson.h>

CloudAuth::CloudAuth(HttpTransport& transport, Preferences& prefs,
                      String& deviceId, String& jwtToken)
    : transport_(transport), preferences_(prefs),
      deviceId_(deviceId), jwtToken_(jwtToken)
{
}

bool CloudAuth::loginUser(const String& username, const String& password,
                          const String& requestedDeviceId)
{
    Serial.println("[AUTH] PHASE 0: Device Login Started");

    if (username.isEmpty() || password.isEmpty() || requestedDeviceId.isEmpty()) {
        Serial.println("[AUTH] ❌ Invalid login parameters");
        return false;
    }

    deviceId_ = requestedDeviceId;

    JsonDocument doc;
    doc["username"] = username;
    doc["password"] = password;
    doc["deviceId"] = deviceId_;

    String body;
    serializeJson(doc, body);

    String response = transport_.sendRequest("/devices/login", "POST", body, false);
    if (response.isEmpty()) {
        Serial.println("[AUTH] ❌ Login failed - empty response");
        return false;
    }

    JsonDocument responseDoc;
    if (deserializeJson(responseDoc, response) != DeserializationError::Ok) {
        Serial.println("[AUTH] ❌ JSON parse error");
        return false;
    }

    const char* accessToken = responseDoc["accessToken"];
    if (accessToken == nullptr || strlen(accessToken) == 0) {
        Serial.println("[AUTH] ❌ Login response has no accessToken");
        return false;
    }

    jwtToken_ = String(accessToken);
    saveToken(jwtToken_);

    const char* refreshToken = responseDoc["refreshToken"];
    if (refreshToken != nullptr) {
        refreshTokenValue_ = String(refreshToken);
        preferences_.putString("refreshToken", refreshTokenValue_);
    }

    const char* responseDeviceId = responseDoc["deviceId"];
    if (responseDeviceId != nullptr) {
        deviceId_ = String(responseDeviceId);
    }
    preferences_.putString("deviceId", deviceId_);

    const char* responseSiteId = responseDoc["siteId"];
    if (responseSiteId != nullptr) {
        siteId_ = String(responseSiteId);
    }

    isAuthenticated_ = true;
    Serial.println("[AUTH] ✅ Device login successful");
    return true;
}

bool CloudAuth::refreshToken()
{
    if (refreshTokenValue_.isEmpty()) {
        Serial.println("[AUTH] No refresh token");
        return false;
    }

    // ✅ رفع باگ: قبلاً بادی درخواست کاملاً خالی ساخته می‌شد و
    // refreshTokenValue_ هیچ‌وقت به بدنه اضافه نمی‌شد، پس بک‌اند
    // نمی‌توانست بفهمد کدام توکن باید رفرش شود.
    JsonDocument doc;
    doc["refreshToken"] = refreshTokenValue_;

    String body;
    serializeJson(doc, body);

    String response = transport_.sendRequest("/auth/refresh", "POST", body, true);
    if (response.isEmpty()) {
        Serial.println("[AUTH] Refresh failed");
        return false;
    }

    JsonDocument responseDoc;
    if (deserializeJson(responseDoc, response) != DeserializationError::Ok) {
        Serial.println("[AUTH] Refresh JSON error");
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

    jwtToken_ = newToken;
    saveToken(jwtToken_);

    const char* newRefresh = responseDoc["refreshToken"];
    if (newRefresh != nullptr) {
        refreshTokenValue_ = String(newRefresh);
        preferences_.putString("refreshToken", refreshTokenValue_);
    }

    isAuthenticated_ = true;
    Serial.println("[AUTH] Access token refreshed");
    return true;
}

bool CloudAuth::isLoggedIn() const
{
    return isAuthenticated_ && !jwtToken_.isEmpty();
}

bool CloudAuth::isTokenValid() const
{
    return !jwtToken_.isEmpty();
}

// ⚠️ توجه: loginOffline/verifyUserPassword از این فایل حذف شدند.
// منطق واقعی در CloudStorage::loginOffline / CloudStorage::verifyUserPassword
// پیاده‌سازی شده و CloudManager مستقیماً از آن استفاده می‌کند
// (نگاه کنید به CloudManager::loginOffline).

String CloudAuth::loadToken()
{
    jwtToken_ = preferences_.getString("jwtToken", "");
    refreshTokenValue_ = preferences_.getString("refreshToken", "");

    String storedDeviceId = preferences_.getString("deviceId", "");
    if (!storedDeviceId.isEmpty()) {
        deviceId_ = storedDeviceId;
    }

    if (jwtToken_.isEmpty()) {
        isAuthenticated_ = false;
        Serial.println("[AUTH] No token found");
        return "";
    }

    isAuthenticated_ = true;
    Serial.println("[AUTH] Token loaded from Preferences");
    return jwtToken_;
}

void CloudAuth::saveToken(const String& token)
{
    if (token.isEmpty()) {
        Serial.println("[AUTH] Cannot save empty token");
        return;
    }
    preferences_.putString("jwtToken", token);
    Serial.println("[AUTH] Token saved");
}