#ifndef CLOUD_WEBSOCKET_SERVER_H
#define CLOUD_WEBSOCKET_SERVER_H

#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <functional>

#include "MybusTransport.h"
#include "CloudStorage.h"

class CloudWebSocketServer {
public:

    // ============================================================
    // Callbacks
    // ============================================================

    // General command callback.
    using CommandCallback =
        std::function<void(const JsonDocument&)>;

    // Called by GET_REGISTRY.
    //
    // Return true:
    //     Register was local and outValue contains its value.
    //
    // Return false:
    //     Register is not local -> CloudWebSocketServer
    //     will use the real mYBUS path.
    using LocalRegistryReadCallback =
        std::function<bool(
            uint16_t addr,
            JsonDocument& outValue
        )>;

    // Called by WRITE_REGISTRY.
    //
    // Return true:
    //     This is a local register and must NOT be forwarded
    //     to physical mYBUS.
    //
    // Return false:
    //     Forward write to physical mYBUS.
    using ShouldSkipMybusWriteCallback =
        std::function<bool(uint16_t addr)>;

    // ============================================================
    // Callback registration
    // ============================================================

    void onCommand(
        CommandCallback cb
    )
    {
        commandCallback_ = cb;
    }

    void onLocalRegistryRead(
        LocalRegistryReadCallback cb
    )
    {
        localReadCallback_ = cb;
    }

    void onShouldSkipMybusWrite(
        ShouldSkipMybusWriteCallback cb
    )
    {
        shouldSkipMybusWriteCallback_ = cb;
    }

    // ============================================================
    // Lifecycle
    // ============================================================

    void start();
    void loop();

    // ============================================================
    // Connection
    // ============================================================

    bool isConnected() const;

    // ============================================================
    // Realtime communication
    // ============================================================

    bool sendRealtimeData(
        JsonDocument& data
    );

    // ============================================================
    // Requests
    // ============================================================

    void requestSiteInfo();
    void requestUsersList();

    // ============================================================
    // Constructor / Destructor
    // ============================================================

    CloudWebSocketServer(
        MybusTransport& mybus,
        CloudStorage& storage,
        const String& deviceId,
        const String& siteId
    );

    ~CloudWebSocketServer();

private:

    // ============================================================
    // Callbacks
    // ============================================================

    CommandCallback commandCallback_;

    LocalRegistryReadCallback
        localReadCallback_;

    ShouldSkipMybusWriteCallback
        shouldSkipMybusWriteCallback_;

    // ============================================================
    // WebSocket handlers
    // ============================================================

    void onEvent(
        AsyncWebSocket* server,
        AsyncWebSocketClient* client,
        AwsEventType type,
        void* arg,
        uint8_t* data,
        size_t len
    );

    void handleMessage(
        void* arg,
        uint8_t* data,
        size_t len
    );

    void handleWriteRegistry(
        JsonDocument& doc
    );

    // ============================================================
    // Request number
    // ============================================================

    uint32_t requestNumber_ = 0;

    uint32_t nextRequestNumber();

    // ============================================================
    // WebSocket state
    // ============================================================

    AsyncWebServer* server_ = nullptr;

    AsyncWebSocket* ws_ = nullptr;

    AsyncWebSocketClient* client_ = nullptr;

    bool connected_ = false;

    // ============================================================
    // Services
    // ============================================================

    MybusTransport& mybus_;

    CloudStorage& storage_;

    const String& deviceId_;

    const String& siteId_;
};

#endif