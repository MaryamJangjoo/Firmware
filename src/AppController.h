#ifndef APP_CONTROLLER_H
#define APP_CONTROLLER_H

#include <Arduino.h>
#include <Wire.h>
#include <tas5805m.hpp>
#include <btAudio.h>
#include <FastLED.h>
#include <ArduinoJson.h>

#include "CloudManager.h"
#include "LocalRegisterMap.h"
#include "ecosmart_registries.h"

class AppController {
public:
    AppController();

    void begin();
    void handle();

private:
    LocalRegisterMap localRegisters_;

    // ---- Audio register state (8 Registers) ----
    uint8_t  audioModeRaw_       = 0;      // 0x8101
    uint8_t  audioControlRaw_    = 0;      // 0x8102
    uint16_t audioSleepTimerRaw_ = 0;      // 0x8221
    uint16_t audioStationRaw_    = 0;      // 0x8222
    uint8_t  audioVolumeRaw_     = 70;     // 0x8103
    uint8_t  audioBassRaw_       = 0;      // 0x8104
    String   audioTitle_;                  // 0x0800 (اصلاح‌شده از 0x0700)
    String   audioArtist_;                 // 0x0801 (اصلاح‌شده از 0x0701)


    unsigned long lastSleepTimerTickMs_ = 0;

    void registerLocalRegisters();
    void registerAudioRegisters();
    void tickAudioSleepTimer();

    // ---- Serial Commands ----
    void handleSerialCommands();
    void testAllAudioRegisters();
    void printHelp();

    // ---- Pin Definitions ----
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

    // ---- WiFi Credentials ----
    static constexpr const char* WIFI_SSID = "megafaYakand8202";
    static constexpr const char* WIFI_PASSWORD = "megafaY@kand*@)@";

    // ---- mYBUS Configuration ----
    static constexpr uint8_t MYBUS_DEVICE_ID = 1;
    static constexpr uint8_t MYBUS_ZONE_ID   = 1;

    // ---- Hardware ----
    tas5805m amp;
    btAudio  bta;
    CRGB     leds[NUM_LEDS];

    bool ledState     = false;
    bool lastLedState = false;

    int32_t  dynamicMax = 0;
    uint32_t lastUpdate = 0;

    CloudManager* cloudManager = nullptr;

    // ---- Private Methods ----
    bool connectToWiFi();
    void initCloudManager();
    void initAudioHardware();
    void configureMybusAddress();

    void handleWiFiReconnect();
    void handleLedState();

    enum class WifiReconnectState { IDLE, RECONNECTING };

    WifiReconnectState wifiReconnectState_ = WifiReconnectState::IDLE;
    unsigned long wifiReconnectStartMs_ = 0;
    unsigned long wifiLastAttemptMs_ = 0;

    static constexpr unsigned long WIFI_RECONNECT_ATTEMPT_INTERVAL_MS = 500;
    static constexpr unsigned long WIFI_RECONNECT_TIMEOUT_MS = 10000;

    void setCurtainOn();
    void setCurtainOff();

    void visualizeAudio(const uint8_t* data, uint32_t len);
    void onBtData(const uint8_t* data, uint32_t len);
    static void btDataTrampoline(const uint8_t* data, uint32_t len);

    void onCommandReceived(const JsonDocument& command);

    static AppController* s_instance;
};

#endif