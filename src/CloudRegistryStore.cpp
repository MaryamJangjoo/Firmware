#include "CloudRegistryStore.h"

#include "Logging.h"

static const char* TAG = "CLOUD-REG-STORE";

namespace {
constexpr const char* NVS_NAMESPACE = "cloud-reg";

constexpr const char* KEY_FQDN  = "fqdn";
constexpr const char* KEY_IP    = "ip";
constexpr const char* KEY_PORT  = "port";
constexpr const char* KEY_USER  = "user";
constexpr const char* KEY_PASS  = "pass";
constexpr const char* KEY_DEVID = "devid";
} 

CloudRegistryStore::CloudRegistryStore()
{
}

String CloudRegistryStore::truncate(const String& value, size_t maxLen)
{
    if (value.length() <= maxLen) {
        return value;
    }
    return value.substring(0, maxLen);
}

bool CloudRegistryStore::begin()
{
    if (opened_) {
        return true;
    }

    if (!prefs_.begin(NVS_NAMESPACE, false)) {
        ECOSMART_LOGE(TAG, "NVS open failed - cloud config will not persist");
        return false;
    }

    opened_ = true;
    loadFromNvs();

    ECOSMART_LOGI(TAG, "Loaded from NVS:");
    ECOSMART_LOGI(TAG, "  FQDN     : %s",
                  serverFqdn_.isEmpty() ? "(unset)" : serverFqdn_.c_str());
    ECOSMART_LOGI(TAG, "  IP       : %s",
                  serverIp_.isEmpty() ? "(unset)" : serverIp_.c_str());
    ECOSMART_LOGI(TAG, "  Port     : %u",
                  static_cast<unsigned>(serverPort_));
    ECOSMART_LOGI(TAG, "  Username : %s",
                  username_.isEmpty() ? "(unset)" : username_.c_str());
    ECOSMART_LOGI(TAG, "  Password : %s",
                  password_.isEmpty() ? "(empty)" : "****");
    ECOSMART_LOGI(TAG, "  DeviceId : %s",
                  deviceId_.isEmpty() ? "(unset)" : deviceId_.c_str());
    ECOSMART_LOGI(TAG, "  Configured: %s", isConfigured() ? "YES" : "NO");

    return true;
}

void CloudRegistryStore::loadFromNvs()
{
    if (!opened_) return;

    serverFqdn_ = prefs_.isKey(KEY_FQDN)  ? prefs_.getString(KEY_FQDN)  : "";
    serverIp_   = prefs_.isKey(KEY_IP)    ? prefs_.getString(KEY_IP)    : "";
    serverPort_ = prefs_.getUShort(KEY_PORT, DEFAULT_PORT);
    username_   = prefs_.isKey(KEY_USER)  ? prefs_.getString(KEY_USER)  : "";
    password_   = prefs_.isKey(KEY_PASS)  ? prefs_.getString(KEY_PASS)  : "";
    deviceId_   = prefs_.isKey(KEY_DEVID) ? prefs_.getString(KEY_DEVID) : "";

    if (serverPort_ == 0) {
        ECOSMART_LOGW(TAG, "Stored port was 0, falling back to %u",
                      static_cast<unsigned>(DEFAULT_PORT));
        serverPort_ = DEFAULT_PORT;
    }
}

bool CloudRegistryStore::persistString(const char* key, const String& value)
{
    if (!opened_) {
        ECOSMART_LOGW(TAG, "NVS not open, '%s' not persisted", key);
        return false;
    }

    const size_t written = prefs_.putString(key, value);
    if (written == 0 && !value.isEmpty()) {
        ECOSMART_LOGE(TAG, "NVS write failed for '%s'", key);
        return false;
    }
    return true;
}

bool CloudRegistryStore::setServerFqdn(const String& v)
{
    const String next = truncate(v, MAX_STRING_LEN);

    if (!persistString(KEY_FQDN, next)) {
        return false;
    }

    serverFqdn_ = next;
    ECOSMART_LOGI(TAG, "ServerFQDN -> '%s'", serverFqdn_.c_str());
    return true;
}

bool CloudRegistryStore::setServerIp(const String& v)
{
    const String next = truncate(v, MAX_STRING_LEN);

    if (!persistString(KEY_IP, next)) {
        return false;
    }

    serverIp_ = next;
    ECOSMART_LOGI(TAG, "ServerIP -> '%s'", serverIp_.c_str());
    return true;
}

bool CloudRegistryStore::setServerPort(uint16_t v)
{
    if (v == 0) {
        ECOSMART_LOGW(TAG, "ServerPort 0 is invalid, ignored");
        return false;
    }

    if (!opened_) {
        ECOSMART_LOGW(TAG, "NVS not open, port not persisted");
        return false;
    }

    if (prefs_.putUShort(KEY_PORT, v) == 0) {
        ECOSMART_LOGE(TAG, "NVS write failed for port");
        return false;
    }

    serverPort_ = v;
    ECOSMART_LOGI(TAG, "ServerPort -> %u",
                  static_cast<unsigned>(serverPort_));
    return true;
}

bool CloudRegistryStore::setUsername(const String& v)
{
    const String next = truncate(v, MAX_STRING_LEN);

    if (!persistString(KEY_USER, next)) {
        return false;
    }

    username_ = next;
    ECOSMART_LOGI(TAG, "Username -> '%s'", username_.c_str());
    return true;
}

bool CloudRegistryStore::setPassword(const String& v)
{
    const String next = truncate(v, MAX_STRING_LEN);

    if (!persistString(KEY_PASS, next)) {
        return false;
    }

    password_ = next;
    ECOSMART_LOGI(TAG, "Password updated (%u chars)",
                  static_cast<unsigned>(password_.length()));
    return true;
}

bool CloudRegistryStore::setDeviceIdFromCloudManager(const String& v)
{
    const String next = truncate(v, MAX_STRING_LEN);

    // Avoid a pointless NVS write on every boot.
    if (next == deviceId_) {
        return true;
    }

    if (!persistString(KEY_DEVID, next)) {
        return false;
    }

    deviceId_ = next;
    ECOSMART_LOGI(TAG, "DeviceId -> '%s'", deviceId_.c_str());
    return true;
}

String CloudRegistryStore::buildApiBaseUrl() const
{
    // FQDN wins when present; IP is the fallback.
    const String host = serverFqdn_.isEmpty() ? serverIp_ : serverFqdn_;

    if (host.isEmpty()) {
        return "";
    }

    String url = "http://";
    url += host;
    url += ":";
    url += String(serverPort_);
    return url;
}

bool CloudRegistryStore::isConfigured() const
{
    const bool hasHost = !serverIp_.isEmpty() || !serverFqdn_.isEmpty();
    return hasHost && serverPort_ != 0;
}

bool CloudRegistryStore::clearAll()
{
    if (!opened_) {
        ECOSMART_LOGW(TAG, "NVS not open, clearAll ignored");
        return false;
    }

    const bool ok = prefs_.clear();

    serverFqdn_ = "";
    serverIp_   = "";
    serverPort_ = DEFAULT_PORT;
    username_   = "";
    password_   = "";
    deviceId_   = "";

    ECOSMART_LOGW(TAG, "All cloud registry values cleared (ok=%d)",
                  static_cast<int>(ok));
    return ok;
}

// ============================================================
// seedDefaults
//
// Called once at startup. If the store has no ServerIP,
// Username or Password yet (first boot after a factory reset,
// or after NVS erase), the compile-time defaults are written
// to NVS so the device has a valid config on the next login
// attempt.
//
// If a field is already set, it is left untouched. This makes
// the function idempotent: user-configured values always win
// and are never overwritten.
// ============================================================
bool CloudRegistryStore::seedDefaults(
    const String& defaultIp,
    const String& defaultUsername,
    const String& defaultPassword)
{
    if (!opened_) {
        ECOSMART_LOGW(TAG, "NVS not open, cannot seed defaults");
        return false;
    }

    bool anySeeded = false;

    if (serverIp_.isEmpty() && !defaultIp.isEmpty()) {
        ECOSMART_LOGI(TAG, "Seeding ServerIP from compile-time macro");
        if (setServerIp(defaultIp)) {
            anySeeded = true;
        }
    } else if (!serverIp_.isEmpty()) {
        ECOSMART_LOGI(TAG, "ServerIP already set to '%s', not seeding",
                      serverIp_.c_str());
    }

    if (username_.isEmpty() && !defaultUsername.isEmpty()) {
        ECOSMART_LOGI(TAG, "Seeding Username from compile-time macro");
        if (setUsername(defaultUsername)) {
            anySeeded = true;
        }
    } else if (!username_.isEmpty()) {
        ECOSMART_LOGI(TAG, "Username already set, not seeding");
    }

    if (password_.isEmpty() && !defaultPassword.isEmpty()) {
        ECOSMART_LOGI(TAG, "Seeding Password from compile-time macro");
        if (setPassword(defaultPassword)) {
            anySeeded = true;
        }
    } else if (!password_.isEmpty()) {
        ECOSMART_LOGI(TAG, "Password already set, not seeding");
    }

    if (!anySeeded) {
        ECOSMART_LOGI(TAG, "All defaults already present, no seeding needed");
    }

    return true;
}