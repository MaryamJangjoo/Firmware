#ifndef CLOUD_WEBSOCKET_SERVER_H
#define CLOUD_WEBSOCKET_SERVER_H

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <functional>
#include "MybusTransport.h"
#include "CloudStorage.h"

class CloudWebSocketServer {
public:
    using CommandCallback = std::function<void(const JsonDocument&)>;

    CloudWebSocketServer(MybusTransport& mybus, CloudStorage& storage,
                          const String& deviceId, const String& siteId);
    ~CloudWebSocketServer();

    void start();
    void loop();
    bool isConnected() const;
    bool sendRealtimeData(JsonDocument& data);

    void onCommand(CommandCallback cb) { commandCallback_ = cb; }

    void requestSiteInfo();
    void requestUsersList();

private:
    void onEvent(AsyncWebSocket*, AsyncWebSocketClient*, AwsEventType,
                 void*, uint8_t*, size_t);
    void handleMessage(void* arg, uint8_t* data, size_t len);
    void handleWriteRegistry(JsonDocument& doc);

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

    CommandCallback commandCallback_;
};

#endif