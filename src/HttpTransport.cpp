#include "HttpTransport.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "Logging.h"

static const char* TAG = "HTTP";

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
        ECOSMART_LOGE(TAG, "WiFi disconnected");
        return "";
    }

    HTTPClient http;
    String url = apiBaseUrl_ + endpoint;

    ECOSMART_LOGI(TAG, "%s %s", method.c_str(), url.c_str());

    if (!http.begin(url)) {
        ECOSMART_LOGE(TAG, "begin() failed");
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
        ECOSMART_LOGE(TAG, "Unsupported method: %s", method.c_str());
        http.end();
        return "";
    }

    String response = http.getString();
    http.end();

    if (isHttpSuccess(httpCode)) {
        ECOSMART_LOGI(TAG, "Success: %d", httpCode);
        return response;
    }

    ECOSMART_LOGE(TAG, "Request failed: %d", httpCode);
    if (!response.isEmpty()) {
        ECOSMART_LOGI(TAG, "Response:");
        ECOSMART_LOGI(TAG, "%s", response.c_str());
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
        ECOSMART_LOGE(TAG, "WiFi disconnected");
        return false;
    }

    HTTPClient http;
    String url = apiBaseUrl_ + "/devices/data";

    ECOSMART_LOGI(TAG, "POST %s (%u bytes)", url.c_str(), static_cast<unsigned>(len));

    if (!http.begin(url)) {
        ECOSMART_LOGE(TAG, "begin() failed");
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
        ECOSMART_LOGE(TAG, "Failed: %d", httpCode);
        String response = http.getString();
        if (!response.isEmpty()) {
            ECOSMART_LOGI(TAG, "Response: %s", response.c_str());
        }
        http.end();
        return false;
    }

    ECOSMART_LOGI(TAG, "Success: %d", httpCode);

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
            ECOSMART_LOGW(TAG, "Response read total timeout exceeded");
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

    ECOSMART_LOGI(TAG, "Response body: %u bytes%s",
                  static_cast<unsigned>(responseBuf.size()),
                  totalTimeoutHit ? " (truncated by total timeout)" : "");

    if (responseBuf.empty()) {
        ECOSMART_LOGE(TAG, "Result: FAILED (empty body)");
        return false;
    }

    if (responseBuf[0] == 0x7B /* '{' */) {
        String jsonStr(reinterpret_cast<const char*>(responseBuf.data()), responseBuf.size());

        JsonDocument jsonDoc;
        if (deserializeJson(jsonDoc, jsonStr) != DeserializationError::Ok) {
            ECOSMART_LOGE(TAG, "JSON response failed to parse");
            return false;
        }

        if (jsonDoc["type"] == "Buffer" && jsonDoc["data"].is<JsonArray>()) {
            JsonArray dataArray = jsonDoc["data"].as<JsonArray>();
            outResponseBytes.reserve(dataArray.size());
            for (JsonVariant v : dataArray) {
                outResponseBytes.push_back(v.as<uint8_t>());
            }
            ECOSMART_LOGI(TAG, "Extracted %u bytes from JSON Buffer",
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