#ifndef HTTP_TRANSPORT_H
#define HTTP_TRANSPORT_H

#include <Arduino.h>
#include <vector>
#include <ArduinoJson.h>

class HttpTransport {
public:
    HttpTransport(String& apiBaseUrl, String& jwtToken);

    String sendRequest(const String& endpoint, const String& method,
                        const String& body, bool useAuth = true);

    bool sendRawBinaryToBackend(const uint8_t* data, size_t len,
                                 std::vector<uint8_t>& outResponseBytes,
                                 JsonDocument* outJsonError = nullptr);

private:
    bool isHttpSuccess(int httpCode);

    // reference به فیلدهای CloudManager (یا کپی محلی، بسته به تصمیم معماری)
    String& apiBaseUrl_;
    String& jwtToken_;

    static constexpr uint32_t HTTP_TIMEOUT_MS = 15000;
};

#endif