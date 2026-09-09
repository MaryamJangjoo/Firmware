#ifndef APP_CONTROLLER_H
#define APP_CONTROLLER_H

#include <Arduino.h>
#include <Wire.h>
#include <tas5805m.hpp>
#include <btAudio.h>
#include <FastLED.h>
#include <ArduinoJson.h>

#include "CloudManager.h"
#include "ecosmart_registeries.h"

class AppController {
public:
    AppController();

    void begin();
    void handle();
    
private:
    static constexpr int PIN_LED_1 = 33;
    static constexpr int PIN_LED_2 = 32;
    static constexpr int NUM_LEDS  = 30;

    static constexpr int PIN_I2C_SDA = 21;
    static constexpr int PIN_I2C_SCL = 22;

    static constexpr int PIN_SR_LATCH = 4;
    static constexpr int PIN_SR_CLOCK = 18;
    static constexpr int PIN_SR_DATA  = 23;

    static constexpr int PIN_I2S_SCK   = 5;
    static constexpr int PIN_I2S_WS    = 25;
    static constexpr int PIN_I2S_SDOUT = 26;
    static constexpr int PIN_I2S_SDIN  = 35;
    static constexpr int PIN_I2S_FAULT = 34;
    static constexpr int PIN_I2S_PDN   = 27;

    const char* WIFI_SSID     = "megafaYakand8202";
    const char* WIFI_PASSWORD = "megafaY@kand*@)@";

    static constexpr uint8_t MYBUS_DEVICE_ID = 1;
    static constexpr uint8_t MYBUS_ZONE_ID   = 1;

    tas5805m amp;
    btAudio  bta;
    CRGB     leds[NUM_LEDS];

    bool ledState     = false;
    bool lastLedState = false;

    int32_t  dynamicMax = 0;
    uint32_t lastUpdate = 0;

    CloudManager* cloudManager = nullptr;

    enum class WifiReconnectState { IDLE, RECONNECTING };

    WifiReconnectState wifiReconnectState_ = WifiReconnectState::IDLE;
    unsigned long wifiReconnectStartMs_ = 0;
    unsigned long wifiLastAttemptMs_ = 0;

    static constexpr unsigned long WIFI_RECONNECT_ATTEMPT_INTERVAL_MS = 500;
    static constexpr unsigned long WIFI_RECONNECT_TIMEOUT_MS = 10000;

    bool connectToWiFi();
    void initCloudManager();
    void initAudioHardware();
    void configureMybusAddress();

    void handleWiFiReconnect();
    void handleLedState();

    void setCurtainOn();
    void setCurtainOff();

    void visualizeAudio(const uint8_t* data, uint32_t len);
    void onBtData(const uint8_t* data, uint32_t len);
    static void btDataTrampoline(const uint8_t* data, uint32_t len);

    // ---- Audio registry (Registery_t-based, reg_module_audio) ----
    Registery_t* findAudioRegistryEntry(uint16_t regAddr);
    bool readAudioRegistry(uint16_t regAddr, JsonDocument& outValue);
    bool writeAudioRegistry(uint16_t regAddr, const String& regVal);

    bool handleCurtainRegistryWrite(uint16_t regAddr, const String& regVal);
    String getRegistryValue(uint16_t regAddr);  
    bool readLocalRegistry(uint16_t regAddr, JsonDocument& outValue);
    bool writeLocalRegistry(uint16_t regAddr, const String& regVal);   
    void applyOutputsToHardware();                                 

    void onCommandReceived(const JsonDocument& command);

    static AppController* s_instance;
};

#endif