#ifndef CLOUD_MANAGER_H
#define CLOUD_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>

#include "HttpTransport.h"
#include "CloudAuth.h"
#include "CloudStorage.h"
#include "MybusSession.h"
#include "MybusTransport.h"
#include "CloudWebSocketServer.h"

class CloudManager {
public:

    CloudManager();
    ~CloudManager();

    bool loginUser(const String& u, const String& p, const String& deviceId);
    bool refreshToken();
    bool isLoggedIn() const;
    bool loginOffline(const String& u, const String& p);
    bool performHandshake();
    bool isSecureSessionEstablished() const;

    bool sendMybusData(JsonDocument& data, JsonDocument* outResponse = nullptr);

    bool sendRegistryFrame(
        uint16_t regAddr,
        const uint8_t* val,
        size_t len,
        bool isWrite,
        uint8_t busDeviceId = 0,
        JsonDocument* outResponse = nullptr
    );

    void setMybusDeviceId(uint8_t deviceId);
    void setMybusZoneId(uint8_t zone);
    uint8_t getMybusDeviceId() const;
    uint8_t getMybusZoneId() const;

    void startWebSocketServer();
    void loopWebSocketServer();
    bool isWebSocketConnected() const;

    // ✅ callback جدید: باینری
    void onBinaryFrame(CloudWebSocketServer::BinaryFrameCallback cb);

    void onLocalRegistryRead(CloudWebSocketServer::LocalRegistryReadCallback cb);
    void onShouldSkipMybusWrite(CloudWebSocketServer::ShouldSkipMybusWriteCallback cb);

    void setApiBaseUrl(const String& url);
    String getApiBaseUrl() const;
    void setDeviceId(const String& id);
    String getDeviceId() const;
    String getJwtToken() const;

private:

    String apiBaseUrl_ = "http://192.168.88.184:3000";
    String deviceId_;
    String jwtToken_;
    String siteId_;
    uint8_t mybusDeviceId_ = 0;
    uint8_t mybusZoneId_ = 0;

    Preferences preferences_;
    HttpTransport httpTransport_;
    CloudAuth auth_;
    CloudStorage storage_;
    MybusSession mybusSession_;
    MybusTransport mybusTransport_;
    CloudWebSocketServer wsServer_;

    uint32_t requestNumber_ = 0;
    uint32_t nextRequestNumber();
};

#endif