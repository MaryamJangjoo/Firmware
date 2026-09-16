#ifndef CLOUD_REGISTRY_STORE_H
#define CLOUD_REGISTRY_STORE_H

#include <Arduino.h>
#include <Preferences.h>

// ============================================================
// CloudRegistryStore
//
// Persistent storage for Cloud Connectivity registers
// (ServerFQDN, ServerIP, ServerPort, Username, Password,
// DeviceId). Values survive reboot by being written to NVS
// under the "cloud-reg" namespace.
//
// Design notes:
//   - DeviceId is set once (from CloudManager) and treated as
//     read-only by the registry layer.
//   - Password is stored as-is in NVS. NVS is encrypted on
//     ESP32 when flash encryption is enabled. On plain flash
//     it is not, so treat the device as physically sensitive.
//   - All setters persist immediately. There is no explicit
//     flush() required by callers.
//   - If begin() failed, setters do NOT mutate the in-RAM copy
//     either, so RAM and NVS can never diverge silently.
//   - String values are capped at MAX_STRING_LEN.
//   - buildApiBaseUrl() prefers FQDN over IP when both are set.
// ============================================================

class CloudRegistryStore {
public:
    static constexpr size_t   MAX_STRING_LEN = 64;
    static constexpr uint16_t DEFAULT_PORT   = 3000;

    CloudRegistryStore();

    bool begin();

    bool isOpen() const { return opened_; }


    String   getServerFqdn() const { return serverFqdn_; }
    String   getServerIp()   const { return serverIp_; }
    uint16_t getServerPort() const { return serverPort_; }
    String   getUsername()   const { return username_; }
    String   getPassword()   const { return password_; }
    String   getDeviceId()   const { return deviceId_; }


    bool setServerFqdn(const String& v);
    bool setServerIp(const String& v);
    bool setServerPort(uint16_t v);
    bool setUsername(const String& v);
    bool setPassword(const String& v);


    bool setDeviceIdFromCloudManager(const String& v);

    String buildApiBaseUrl() const;

    bool isConfigured() const;


    bool clearAll();


    bool seedDefaults(
        const String& defaultIp,
        const String& defaultUsername,
        const String& defaultPassword
    );

private:
    void loadFromNvs();
    bool persistString(const char* key, const String& value);

    static String truncate(const String& value, size_t maxLen);

    Preferences prefs_;
    bool        opened_ = false;

    String   serverFqdn_;
    String   serverIp_;
    uint16_t serverPort_ = DEFAULT_PORT;
    String   username_;
    String   password_;
    String   deviceId_;
};

#endif 