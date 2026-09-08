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

    // ========================================================
    // Auth
    // ========================================================

    bool loginUser(
        const String& u,
        const String& p,
        const String& deviceId
    );

    bool refreshToken();

    bool isLoggedIn() const;

    bool loginOffline(
        const String& u,
        const String& p
    );

    // ========================================================
    // mYBUS
    // ========================================================

    bool performHandshake();

    bool isSecureSessionEstablished() const;

    bool sendMybusData(
        JsonDocument& data,
        JsonDocument* outResponse = nullptr
    );

    bool sendRegistryFrame(
        uint16_t regAddr,
        const uint8_t* val,
        size_t len,
        bool isWrite,
        uint8_t busDeviceId = 0,
        JsonDocument* outResponse = nullptr
    );

    // ========================================================
    // mYBUS Address Configuration
    // ========================================================

    void setMybusDeviceId(
        uint8_t deviceId
    );

    void setMybusZoneId(
        uint8_t zone
    );

    uint8_t getMybusDeviceId() const;

    uint8_t getMybusZoneId() const;

    // ========================================================
    // WebSocket
    // ========================================================

    void startWebSocketServer();

    void loopWebSocketServer();

    bool isWebSocketConnected() const;

    bool sendRealtimeData(
        JsonDocument& data
    );

    // General command callback.
    void onCommand(
        CloudWebSocketServer::CommandCallback cb
    );

    // Local registry read callback.
    void onLocalRegistryRead(
        CloudWebSocketServer::LocalRegistryReadCallback cb
    );

    // Local register -> physical mYBUS forwarding decision.
    void onShouldSkipMybusWrite(
        CloudWebSocketServer::ShouldSkipMybusWriteCallback cb
    );

    // ========================================================
    // Config
    // ========================================================

    void setApiBaseUrl(
        const String& url
    );

    String getApiBaseUrl() const;

    void setDeviceId(
        const String& id
    );

    String getDeviceId() const;

    String getJwtToken() const;

private:

    // ========================================================
    // Backend API
    // ========================================================

    String apiBaseUrl_ =
        "http://192.168.88.98:3000";

    // JWT / backend identity
    String deviceId_;

    String jwtToken_;

    String siteId_;

    // ========================================================
    // mYBUS Address
    //
    // These MUST match backend:
    //
    // device.mybusDeviceId
    // device.mybusZoneId
    // ========================================================

    uint8_t mybusDeviceId_ = 0;

    uint8_t mybusZoneId_ = 0;

    // ========================================================
    // Services
    // ========================================================

    Preferences preferences_;

    HttpTransport httpTransport_;

    CloudAuth auth_;

    CloudStorage storage_;

    MybusSession mybusSession_;

    MybusTransport mybusTransport_;

    CloudWebSocketServer wsServer_;

    // ========================================================
    // mYBUS Request Number
    // ========================================================

    uint32_t requestNumber_ = 0;

    uint32_t nextRequestNumber();
};

#endif