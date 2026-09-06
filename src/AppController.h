#ifndef APP_CONTROLLER_H
#define APP_CONTROLLER_H

#include <Arduino.h>
#include <Wire.h>
#include <tas5805m.hpp>
#include <btAudio.h>
#include <FastLED.h>
#include <ArduinoJson.h>

#include "CloudManager.h"

class AppController {
public:
    AppController();

    void begin();   
    void handle();  
private:
    // ---- Pins ----
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

    // ---- WiFi ----
    const char* WIFI_SSID     = "megafaYakand8202";
    const char* WIFI_PASSWORD = "megafaY@kand*@)@";

    // ---- Hardware objects ----
    tas5805m amp;
    btAudio  bta;
    CRGB     leds[NUM_LEDS];

    bool ledState     = false;
    bool lastLedState = false;

    int32_t  dynamicMax = 0;
    uint32_t lastUpdate = 0;

    CloudManager* cloudManager = nullptr;

    // ---- setup helpers ----
    bool connectToWiFi();
    void initCloudManager();
    void initAudioHardware();

    // ---- loop helpers ----
    void handleWiFiReconnect();
    void handleLedState();

    // ---- Curtain ----
    void setCurtainOn();
    void setCurtainOff();

    // ---- Audio ----
    void visualizeAudio(const uint8_t* data, uint32_t len);
    void onBtData(const uint8_t* data, uint32_t len);
    static void btDataTrampoline(const uint8_t* data, uint32_t len);

    bool handleAudioRegistryWrite(uint16_t regAddr, const String& regVal);

    // ---- Cloud command handling ----
    void onCommandReceived(const JsonDocument& command);

    static AppController* s_instance;
};

#endif