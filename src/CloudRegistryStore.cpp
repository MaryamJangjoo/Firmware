#include "CloudRegistryStore.h"

namespace {
constexpr const char* NVS_NAMESPACE = "cloud-reg";

constexpr const char* KEY_FQDN   = "fqdn";
constexpr const char* KEY_IP     = "ip";
constexpr const char* KEY_PORT   = "port";
constexpr const char* KEY_USER   = "user";
constexpr const char* KEY_PASS   = "pass";
constexpr const char* KEY_DEVID  = "devid";

String truncate(const String& value, size_t maxLen)
{
    if (value.length() <= maxLen) {
        return value;
    }
    return value.substring(0, maxLen);
}
} // namespace

CloudRegistryStore::CloudRegistryStore()
{
}

bool CloudRegistryStore::begin()
{
    if (opened_) {
        return true;
    }

    if (!prefs_.begin(NVS_NAMESPACE, false)) {
        Serial.println("[CLOUD-REG] NVS open failed");
        return false;
    }

    opened_ = true;
    loadFromNvs();

    Serial.println("[CLOUD-REG] Loaded from NVS:");
    Serial.printf("  FQDN     : %s\n", serverFqdn_.c_str());
    Serial.printf("  IP       : %s\n", serverIp_.c_str());
    Serial.printf("  Port     : %u\n", static_cast<unsigned>(serverPort_));
    Serial.printf("  Username : %s\n", username_.c_str());
    Serial.printf("  Password : %s\n", password_.isEmpty() ? "(empty)" : "****");
    Serial.printf("  DeviceId : %s\n", deviceId_.c_str());

    return true;
}

void CloudRegistryStore::loadFromNvs()
{
    if (!opened_) return;

    serverFqdn_ = prefs_.getString(KEY_FQDN, "");
    serverIp_   = prefs_.getString(KEY_IP, "");
    serverPort_ = prefs_.getUShort(KEY_PORT, DEFAULT_PORT);
    username_   = prefs_.getString(KEY_USER, "");
    password_   = prefs_.getString(KEY_PASS, "");
    deviceId_   = prefs_.getString(KEY_DEVID, "");
}

void CloudRegistryStore::persistString(const char* key, const String& value)
{
    if (!opened_) return;

    const String truncated = truncate(value, MAX_STRING_LEN);
    prefs_.putString(key, truncated);
}

void CloudRegistryStore::setServerFqdn(const String& v)
{
    serverFqdn_ = truncate(v, MAX_STRING_LEN);
    persistString(KEY_FQDN, serverFqdn_);

    Serial.printf("[CLOUD-REG] ServerFQDN -> '%s'\n", serverFqdn_.c_str());
}

void CloudRegistryStore::setServerIp(const String& v)
{
    serverIp_ = truncate(v, MAX_STRING_LEN);
    persistString(KEY_IP, serverIp_);

    Serial.printf("[CLOUD-REG] ServerIP -> '%s'\n", serverIp_.c_str());
}

void CloudRegistryStore::setServerPort(uint16_t v)
{
    if (v == 0) {
        Serial.println("[CLOUD-REG] ServerPort 0 is invalid, ignored");
        return;
    }

    serverPort_ = v;

    if (opened_) {
        prefs_.putUShort(KEY_PORT, serverPort_);
    }

    Serial.printf("[CLOUD-REG] ServerPort -> %u\n",
                  static_cast<unsigned>(serverPort_));
}

void CloudRegistryStore::setUsername(const String& v)
{
    username_ = truncate(v, MAX_STRING_LEN);
    persistString(KEY_USER, username_);

    Serial.printf("[CLOUD-REG] Username -> '%s'\n", username_.c_str());
}

void CloudRegistryStore::setPassword(const String& v)
{
    password_ = truncate(v, MAX_STRING_LEN);
    persistString(KEY_PASS, password_);

    Serial.println("[CLOUD-REG] Password updated");
}

void CloudRegistryStore::setDeviceIdFromCloudManager(const String& v)
{
    deviceId_ = truncate(v, MAX_STRING_LEN);
    persistString(KEY_DEVID, deviceId_);

    Serial.printf("[CLOUD-REG] DeviceId -> '%s'\n", deviceId_.c_str());
}

String CloudRegistryStore::buildApiBaseUrl() const
{
    if (serverIp_.isEmpty()) {
        return "";
    }

    String url = "http://";
    url += serverIp_;
    url += ":";
    url += String(serverPort_);
    return url;
}

bool CloudRegistryStore::isConfigured() const
{
    return !serverIp_.isEmpty() && serverPort_ != 0;
}
