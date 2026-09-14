#include "HttpTransport.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>

HttpTransport::HttpTransport(String& apiBaseUrl, String& jwtToken)
    : apiBaseUrl_(apiBaseUrl), jwtToken_(jwtToken)
{
}

bool HttpTransport::isHttpSuccess(int httpCode)
{
    return httpCode >= 200 && httpCode < 300;
}

// ============================================================
// JSON request/response (auth, handshake)
// ============================================================

String HttpTransport::sendRequest(const String& endpoint, const String& method,
                                   const String& body, bool useAuth)
{
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[HTTP] WiFi disconnected");
        return "";
    }

    HTTPClient http;
    String url = apiBaseUrl_ + endpoint;

    Serial.printf("[HTTP] %s %s\n", method.c_str(), url.c_str());

    if (!http.begin(url)) {
        Serial.println("[HTTP] begin() failed");
        return "";
    }

    http.setTimeout(HTTP_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Accept", "application/json");

    if (useAuth && !jwtToken_.isEmpty()) {
        http.addHeader("Authorization", "Bearer " + jwtToken_);
    }

    int httpCode = -1;

    if (method.equalsIgnoreCase("POST")) {
        httpCode = http.POST(body);
    } else if (method.equalsIgnoreCase("GET")) {
        httpCode = http.GET();
    } else if (method.equalsIgnoreCase("PUT")) {
        httpCode = http.PUT(body);
    } else if (method.equalsIgnoreCase("DELETE")) {
        httpCode = http.sendRequest(
            "DELETE",
            reinterpret_cast<uint8_t*>(const_cast<char*>(body.c_str())),
            body.length());
    } else {
        Serial.printf("[HTTP] Unsupported method: %s\n", method.c_str());
        http.end();
        return "";
    }

    String response = http.getString();
    http.end();

    if (isHttpSuccess(httpCode)) {
        Serial.printf("[HTTP] Success: %d\n", httpCode);
        return response;
    }

    Serial.printf("[HTTP] Request failed: %d\n", httpCode);
    if (!response.isEmpty()) {
        Serial.println("[HTTP] Response:");
        Serial.println(response);
    }
    return "";
}

// ============================================================
// Raw binary (mYBUS registry frames)
// ============================================================

bool HttpTransport::sendRawBinaryToBackend(const uint8_t* data, size_t len,
                                            std::vector<uint8_t>& outResponseBytes,
                                            JsonDocument* outJsonError)
{
    outResponseBytes.clear();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[HTTP] WiFi disconnected");
        return false;
    }

    HTTPClient http;
    String url = apiBaseUrl_ + "/devices/data";

    Serial.printf("[HTTP] POST %s (%u bytes)\n", url.c_str(), static_cast<unsigned>(len));

    if (!http.begin(url)) {
        Serial.println("[HTTP] begin() failed");
        return false;
    }

    http.setTimeout(HTTP_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/octet-stream");
    http.addHeader("Accept", "application/octet-stream");

    if (!jwtToken_.isEmpty()) {
        http.addHeader("Authorization", "Bearer " + jwtToken_);
    }

    int httpCode = http.POST(const_cast<uint8_t*>(data), len);

    if (!isHttpSuccess(httpCode)) {
        Serial.printf("[HTTP] Failed: %d\n", httpCode);
        String response = http.getString();
        if (!response.isEmpty()) {
            Serial.println("[HTTP] Response: " + response);
        }
        http.end();
        return false;
    }

    Serial.printf("[HTTP] Success: %d\n", httpCode);

    WiFiClient& stream = http.getStream();
    std::vector<uint8_t> responseBuf;
    responseBuf.reserve(512);

    unsigned long lastByteMs = millis();
    unsigned long startMs = millis();
    const unsigned long IDLE_TIMEOUT_MS = 2000;
    const unsigned long TOTAL_TIMEOUT_MS = 20000;

    bool totalTimeoutHit = false;

    while (true) {
        // Enforce the absolute upper bound on the whole read operation.
        // This check must run before the availability check, otherwise a
        // server that keeps sending bytes without gaps (or sends a byte
        // exactly when the total timeout expires) could keep the loop
        // running indefinitely and starve the watchdog.
        if (millis() - startMs > TOTAL_TIMEOUT_MS) {
            Serial.println("[HTTP] Response read total timeout exceeded");
            totalTimeoutHit = true;
            break;
        }

        if (stream.available()) {
            responseBuf.push_back(static_cast<uint8_t>(stream.read()));
            lastByteMs = millis();
        } else if (millis() - lastByteMs > IDLE_TIMEOUT_MS) {
            break;
        } else {
            delay(1);
            yield();
        }
    }

    http.end();

    Serial.printf("[HTTP] Response body: %u bytes%s\n",
                  static_cast<unsigned>(responseBuf.size()),
                  totalTimeoutHit ? " (truncated by total timeout)" : "");

    if (responseBuf.empty()) {
        Serial.println("[mYBUS] Result: FAILED (empty body)");
        return false;
    }

    if (responseBuf[0] == 0x7B /* '{' */) {
        String jsonStr(reinterpret_cast<const char*>(responseBuf.data()), responseBuf.size());

        JsonDocument jsonDoc;
        if (deserializeJson(jsonDoc, jsonStr) != DeserializationError::Ok) {
            Serial.println("[mYBUS] JSON response failed to parse");
            return false;
        }

        if (jsonDoc["type"] == "Buffer" && jsonDoc["data"].is<JsonArray>()) {
            JsonArray dataArray = jsonDoc["data"].as<JsonArray>();
            outResponseBytes.reserve(dataArray.size());
            for (JsonVariant v : dataArray) {
                outResponseBytes.push_back(v.as<uint8_t>());
            }
            Serial.printf("[mYBUS] Extracted %u bytes from JSON Buffer\n",
                          static_cast<unsigned>(outResponseBytes.size()));
            return true;
        }

        if (outJsonError != nullptr) {
            outJsonError->clear();
            (*outJsonError)["error"] = "JSON response";
            (*outJsonError)["message"] = jsonDoc["message"] | "Unknown";
        }
        return false;
    }

    outResponseBytes = std::move(responseBuf);
    return true;
}