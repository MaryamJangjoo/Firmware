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
//     it is not, so treat the device physically secure.
//   - All setters persist immediately. There is no explicit
//     flush() required by callers.
//   - String values are capped at MAX_STRING_LEN to bound the
//     amount of NVS storage used and to keep the WS payload
//     size predictable.
// ============================================================

class CloudRegistryStore {
public:
    static constexpr size_t MAX_STRING_LEN = 64;
    static constexpr uint16_t DEFAULT_PORT = 3000;

    CloudRegistryStore();

    // Must be called once during startup before any getter or
    // setter. Returns false if the NVS namespace could not be
    // opened (in which case all values stay at defaults and the
    // setters become no-ops).
    bool begin();

    // Getters
    String   getServerFqdn() const { return serverFqdn_; }
    String   getServerIp()   const { return serverIp_; }
    uint16_t getServerPort() const { return serverPort_; }
    String   getUsername()   const { return username_; }
    String   getPassword()   const { return password_; }
    String   getDeviceId()   const { return deviceId_; }

    // Setters - each one writes to NVS immediately
    void setServerFqdn(const String& v);
    void setServerIp(const String& v);
    void setServerPort(uint16_t v);
    void setUsername(const String& v);
    void setPassword(const String& v);

    // DeviceId is set from CloudManager during startup and is
    // not exposed for external writes through the registry.
    void setDeviceIdFromCloudManager(const String& v);

    // Rebuild apiBaseUrl_ from ServerIp + ServerPort
    String buildApiBaseUrl() const;

    // True if all required values are present and can be used
    // to talk to the backend.
    bool isConfigured() const;

private:
    void loadFromNvs();
    void persistString(const char* key, const String& value);

    Preferences prefs_;
    bool        opened_ = false;

    String   serverFqdn_;
    String   serverIp_;
    uint16_t serverPort_ = DEFAULT_PORT;
    String   username_;
    String   password_;
    String   deviceId_;
};

#endif // CLOUD_REGISTRY_STORE_H