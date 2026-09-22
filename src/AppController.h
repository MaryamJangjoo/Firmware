#ifndef APP_CONTROLLER_H
#define APP_CONTROLLER_H

#include <Arduino.h>
#include <Wire.h>
#include <tas5805m.hpp>
#include <btAudio.h>
#include <FastLED.h>
#include <ArduinoJson.h>
#include <vector>
#include "Logging.h"

#include "audio.hpp"
#include "cloud.hpp"

#include "CloudManager.h"
#include "ecosmart_registeries.h"
#include "mybus_frame.h"
#include "RawRegisterValue.h"
#include "RegistryControllerBase.h"
#include "InputsRegistryController.h"
#include "OutputsRegistryController.h"
#include "AudioRegistryController.h"
#include "RgbRegistryController.h"
#include "CurtainRegistryController.h"
#include "CloudRegistryStore.h"
#include "CloudRegistryController.h"
#include "HvacRegistryController.h"

#ifndef WIFI_SSID
#define WIFI_SSID "CHANGE_ME_SSID"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "CHANGE_ME_PASSWORD"
#endif

#ifndef API_BASE_URL
#define API_BASE_URL "http://192.168.1.100:3000"
#endif

#ifndef OWNER_USERNAME
#define OWNER_USERNAME "CHANGE_ME_USER"
#endif

#ifndef OWNER_PASSWORD
#define OWNER_PASSWORD "CHANGE_ME_PASSWORD"
#endif


extern bool     g_rgbControlActive;
extern uint32_t g_lastRgbWriteMs;

class AppController {
public:
    AppController();

    void begin();
    void handle();

private:

    static constexpr int PIN_LED_1 = 33;
    static constexpr int PIN_LED_2 = 32;
    static constexpr int NUM_LEDS = 16;

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

    static constexpr int PIN_DHT11 = 14;

    static constexpr uint8_t MYBUS_DEVICE_ID = 1;
    static constexpr uint8_t MYBUS_ZONE_ID   = 1;

    static constexpr uint32_t META_POLL_INTERVAL_MS = 5000;

    tas5805m amp;
    btAudio  bta;
    CRGB     leds[NUM_LEDS];

    InputsRegistryController  inputsController_;
    OutputsRegistryController outputsController_;
    AudioRegistryController   audioController_;
    RgbRegistryController     rgbController_;
    CurtainRegistryController curtainController_;

    CloudRegistryStore      cloudStore_;
    CloudRegistryController cloudController_;
    HvacRegistryController hvacController_;

    std::vector<RegistryControllerBase*> registryControllers_;

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

    uint32_t lastMetaPollMs_ = 0;

    bool connectToWiFi();
    void initCloudManager();
    void initAudioHardware();
    void configureMybusAddress();

    void handleWiFiReconnect();
    void handleLedState();

    void visualizeAudio(const uint8_t* data, uint32_t len);
    void onBtData(const uint8_t* data, uint32_t len);
    static void btDataTrampoline(const uint8_t* data, uint32_t len);

    void updateAudioMetadata();

    void onBinaryFrameReceived(
        const MyBusHeader& hdr,
        const std::vector<uint8_t>& payload
    );

    static String rawPayloadToRegValString(
        uint16_t regAddr,
        const uint8_t* data,
        size_t len
    );

    static AppController* s_instance;
};

#endif