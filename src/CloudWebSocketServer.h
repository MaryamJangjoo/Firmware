#ifndef CLOUD_WEBSOCKET_SERVER_H
#define CLOUD_WEBSOCKET_SERVER_H

#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <functional>
#include <vector>

#include "MybusTransport.h"
#include "CloudStorage.h"
#include "RegisterRawValue.h"
#include "mybus_frame.h"

class CloudWebSocketServer {
public:

    using BinaryFrameCallback =
        std::function<void(
            const MyBusHeader& hdr,
            const std::vector<uint8_t>& payload
        )>;

    using LocalRegistryReadCallback =
        std::function<bool(
            uint16_t addr,
            RegisterRawValue& outValue
        )>;

    using ShouldSkipMybusWriteCallback =
        std::function<bool(uint16_t addr)>;

    void onBinaryFrame(BinaryFrameCallback cb)
    {
        binaryFrameCallback_ = cb;
    }

    void onLocalRegistryRead(LocalRegistryReadCallback cb)
    {
        localReadCallback_ = cb;
    }

    void onShouldSkipMybusWrite(ShouldSkipMybusWriteCallback cb)
    {
        shouldSkipMybusWriteCallback_ = cb;
    }

    void start();
    void loop();

    bool isConnected() const;

    void requestSiteInfo();
    void requestUsersList();

    CloudWebSocketServer(
        MybusTransport& mybus,
        CloudStorage& storage,
        const String& deviceId,
        const String& siteId
    );

    ~CloudWebSocketServer();

private:

    BinaryFrameCallback binaryFrameCallback_;   
    LocalRegistryReadCallback localReadCallback_;
    ShouldSkipMybusWriteCallback shouldSkipMybusWriteCallback_;

    void onEvent(
        AsyncWebSocket* server,
        AsyncWebSocketClient* client,
        AwsEventType type,
        void* arg,
        uint8_t* data,
        size_t len
    );

    void handleBinaryMessage(void* arg, uint8_t* data, size_t len);

    void handleReadRegistry(const MyBusHeader& hdr, const std::vector<uint8_t>& payload);
    void handleWriteRegistryFrame(const MyBusHeader& hdr, const std::vector<uint8_t>& payload);
    void handleGetStatus(uint16_t requestNumber);
    void handleGetUsers(uint16_t requestNumber);
    void handleGetSiteInfo(uint16_t requestNumber);

    void sendRegistryReadResponse(
        uint16_t requestNumber,
        uint16_t regAddr,
        const uint8_t* value,
        size_t valueLen
    );

    void sendError(
        uint8_t originalCommand,
        uint16_t requestNumber,
        uint8_t reason,
        uint16_t regAddr = 0
    );

    bool sendControlFrame(
        uint8_t command,
        uint8_t flags,
        uint16_t requestNumber,
        const uint8_t* payload,
        size_t payloadLen
    );

    uint32_t requestNumber_ = 0;
    uint32_t nextRequestNumber();

    AsyncWebServer* server_ = nullptr;
    AsyncWebSocket* ws_ = nullptr;
    AsyncWebSocketClient* client_ = nullptr;
    bool connected_ = false;

    MybusTransport& mybus_;
    CloudStorage& storage_;
    const String& deviceId_;
    const String& siteId_;
};

#endif