#include "server.hpp"
#include "crypto.hpp"
#include "protocol.hpp"
#include "utils.hpp"

#include <WiFi.h>
#include <ESPAsyncHTTPUpdateServer.h>
#include <ArduinoJson.h>

AsyncWebServer server(80);
ESPAsyncHTTPUpdateServer updateServer;
AsyncWebSocket ws("/ws");
SessionManager sessionMgr;
static mbedtls_ecp_keypair g_deviceKp;
static bool g_deviceKpReady = false;

// --- WebSocket Event Handler ---
void onWsEvent(AsyncWebSocket* server,
               AsyncWebSocketClient* client,
               AwsEventType type,
               void* arg,
               uint8_t* data,
               size_t len) {
    int clientId = client->id();

    if (type == WS_EVT_CONNECT) {
        Serial.printf("Client %d connected\n", clientId);
        sessionMgr.add(clientId);
    }
    else if (type == WS_EVT_DISCONNECT) {
        Serial.printf("Client %d disconnected\n", clientId);
        sessionMgr.remove(clientId);
    }
    else if (type == WS_EVT_DATA) {
        AwsFrameInfo* info = (AwsFrameInfo*)arg;
        if (!(info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT))
            return;

        String payload = String((char*)data).substring(0, len);
        Serial.printf("Client %d sent: %s\n", clientId, payload.c_str());

        JsonDocument doc;
        if (deserializeJson(doc, payload)) {
            client->text("{\"type\":\"error\",\"msg\":\"json_parse_failed\"}");
            return;
        }
        std::string type = doc["type"] | "";

        // --- HELLO ---
        if (type == "hello") {
            // mbedtls_ecp_keypair deviceKp;
            // loadDeviceKeypair(deviceKp);

            std::vector<uint8_t> pub;
            exportUncompressedPub(g_deviceKp, pub);
            std::string pubB64 = base64Encode(pub.data(), pub.size());

            std::string out;
            buildDevicePubkey("P-256", pubB64, out);
            client->text(out.c_str());
        }

        // --- PWA PUBKEY ---
        else if (type == "pwa_pubkey") {
            std::string curve, pubRawB64, saltB64, infoB64;
            parsePwaPubkey(payload.c_str(), curve, pubRawB64, saltB64, infoB64);

            auto peerPub = base64Decode(pubRawB64);
            if (peerPub.size() != 65 || peerPub[0] != 0x04) {
                client->text("{\"type\":\"error\",\"msg\":\"bad_peer_pub_format\"}");
                return;
            }

            // Import peer pubkey
            // mbedtls_ecp_keypair deviceKp;
            // loadDeviceKeypair(deviceKp);
            mbedtls_ecp_point Qp;
            if (!importPeerUncompressedPub(peerPub, Qp, g_deviceKp.grp)) {
                client->text("{\"type\":\"error\",\"msg\":\"import_peer_pub_failed\"}");
                return;
            }

            // Compute shared secret
            uint8_t ikm[32];
            if (!ecdhShared(g_deviceKp.grp, g_deviceKp.d, Qp, ikm)) {
                client->text("{\"type\":\"error\",\"msg\":\"ecdh_failed\"}");
                return;
            }
            printHex("Shared secret (z, hex): ", ikm, sizeof(ikm));

            // Derive HMAC key
            auto salt = base64Decode(saltB64);
            auto info = base64Decode(infoB64);
            uint8_t hmacKey[32];
            if (!hkdfSha256(ikm, salt.data(), salt.size(),
                            info.data(), info.size(),
                            hmacKey)) {
                client->text("{\"type\":\"error\",\"msg\":\"hkdf_failed\"}");
                return;
            }

            Session* s = sessionMgr.get(clientId);
            if (s) {
                memcpy(s->hmacKey.data(), hmacKey, 32);
                s->authenticated = false;
            }

            std::string out;
            buildReadyForKdfAck(out);
            client->text(out.c_str());
        }

        // --- CHALLENGE ---
        else if (type == "challenge") {
            Session* s = sessionMgr.get(clientId);
            if (!s) return;

            std::string nonceB64 = doc["nonce"] | "";
            std::string hmacB64  = doc["hmac"]  | "";

            // Replay protection
            if (!sessionMgr.checkAndStoreNonce(clientId, nonceB64)) {
                client->text("{\"type\":\"error\",\"msg\":\"replay_detected\"}");
                return;
            }

            auto nonce = base64Decode(nonceB64);
            auto mac   = base64Decode(hmacB64);

            uint8_t calc[32];
            hmacSha256(s->hmacKey.data(), nonce.data(), nonce.size(), calc);

            if (memcmp(calc, mac.data(), 32) != 0) {
                client->text("{\"type\":\"error\",\"msg\":\"bad_hmac\"}");
                return;
            }

            // Respond with our own challenge
            uint8_t nonce2[16];
            randBytes(nonce2, sizeof(nonce2));
            uint8_t mac2[32];
            hmacSha256(s->hmacKey.data(), nonce2, sizeof(nonce2), mac2);

            std::string out;
            buildChallengeReply(base64Encode(nonce2, sizeof(nonce2)),
                                base64Encode(mac2, sizeof(mac2)),
                                out);
            client->text(out.c_str());

            // Mark session authenticated
            sessionMgr.setAuthenticated(clientId, true);
        }

        // --- SESSION OK ---
        else if (type == "session_ok") {
            Session* s = sessionMgr.get(clientId);
            if (!s || !s->authenticated) {
                client->text("{\"type\":\"error\",\"msg\":\"not_authenticated\"}");
                return;
            }
            client->text("{\"type\":\"session_ready\"}");
        }

        else {
            Serial.printf("Unknown message type: %s\n", type.c_str());
        }
    }
}

// --- Server Begin ---
void serverBegin() {
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    updateServer.setup(&server);
    // hook to update events if you need to
    updateServer.onUpdateBegin = [](const UpdateType type, int &result)
    {
        // you can force abort the update like this if you need to:
        // result = UpdateResult::UPDATE_ABORT;
        Serial.println("Update started : " + String(type));
    };
    updateServer.onUpdateEnd = [](const UpdateType type, int &result)
    {
        Serial.println("Update finished : " + String(type) + " result: " + String(result));
    };

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", "EcoSmart");
    });

    server.onNotFound([](AsyncWebServerRequest* request) {
        request->send(404, "text/plain", "Not found");
    });

    if (!g_deviceKpReady) {
        if (!loadDeviceKeypair(g_deviceKp)) {
            // As a fallback, generate and (optionally) save
            generateDeviceKeypair(g_deviceKp);
            saveDeviceKeypair(g_deviceKp);
        }
        g_deviceKpReady = true;
    }

    server.begin();
    Serial.println("Server started");
}
