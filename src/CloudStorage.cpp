#include "CloudStorage.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#ifdef USE_LittleFS
#include <LittleFS.h>
#else
#include <SPIFFS.h>
#endif

bool CloudStorage::init()
{
    if (mounted_) return true;

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

    mounted_ = true;
    Serial.println("[FS] Filesystem mounted successfully");
    listFiles();
    return true;
}

bool CloudStorage::isMounted() const
{
    return mounted_;
}

bool CloudStorage::readFile(const String& path, String& content)
{
    content = "";
    if (!mounted_ && !init()) return false;

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

bool CloudStorage::writeFile(const String& path, const String& content)
{
    if (!mounted_ && !init()) return false;

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

    Serial.printf("[FS] Written: %s (%u bytes)\n", path.c_str(),
                  static_cast<unsigned>(written));
    return true;
}

void CloudStorage::listFiles()
{
    if (!mounted_ && !init()) return;

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
        Serial.printf("  - %s (%u bytes)\n", file.name(),
                      static_cast<unsigned>(file.size()));
        file = root.openNextFile();
    }
    root.close();
}

void CloudStorage::printFileContent(const String& path)
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

bool CloudStorage::createDefaultUsersFile()
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

bool CloudStorage::createDefaultSiteInfoFile()
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

bool CloudStorage::loadUsers(std::vector<UserInfo>& users)
{
    users.clear();
    String content;
    if (!readFile("/users.json", content)) {
        Serial.println("[FS] users.json not found");
        return false;
    }

    JsonDocument doc;
    if (deserializeJson(doc, content) != DeserializationError::Ok) {
        Serial.println("[FS] users.json parse error");
        return false;
    }

    JsonVariant usersVariant = doc["users"];
    if (!usersVariant.is<JsonArray>()) {
        Serial.println("[FS] Invalid users.json format");
        return false;
    }

    for (JsonObject obj : usersVariant.as<JsonArray>()) {
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

    Serial.printf("[FS] Loaded %u users\n", static_cast<unsigned>(users.size()));
    return true;
}

bool CloudStorage::saveUsers(const std::vector<UserInfo>& users)
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

bool CloudStorage::loadSiteInfo(SiteInfo& info)
{
    String content;
    if (!readFile("/site_info.json", content)) {
        Serial.println("[FS] site_info.json not found");
        return false;
    }

    JsonDocument doc;
    if (deserializeJson(doc, content) != DeserializationError::Ok) {
        Serial.println("[FS] site_info.json parse error");
        return false;
    }

    info.siteId = doc["siteId"] | "";
    info.siteName = doc["siteName"] | "";
    info.licenseKey = doc["licenseKey"] | "";
    info.expiryDate = doc["expiryDate"] | 0;
    info.maxUsers = doc["maxUsers"] | 10;

    return true;
}

bool CloudStorage::saveSiteInfo(const SiteInfo& info)
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

// ⚠️ همچنان مقایسه‌ی متن‌ساده (نه هش واقعی) — یادداشت امنیتی قبلی
// همچنان صادق است؛ برای production باید با bcrypt/PBKDF2 جایگزین شود.
bool CloudStorage::verifyUserPassword(const String& username, const String& password)
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

bool CloudStorage::loginOffline(const String& username, const String& password)
{
    if (username.isEmpty() || password.isEmpty()) return false;
    return verifyUserPassword(username, password);
}