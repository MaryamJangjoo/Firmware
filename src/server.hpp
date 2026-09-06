#ifndef SERVER_HPP
#define SERVER_HPP

#include <ESPAsyncWebServer.h>
#include "session.hpp"

// Global server objects
extern AsyncWebServer server;
extern AsyncWebSocket ws;
extern SessionManager sessionMgr;

// Initialize server and websocket
void serverBegin();

// WebSocket event handler
void onWsEvent(AsyncWebSocket* server,
               AsyncWebSocketClient* client,
               AwsEventType type,
               void* arg,
               uint8_t* data,
               size_t len);

#endif // SERVER_HPP
