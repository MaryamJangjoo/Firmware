#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <tas5805m.hpp>
#include <btAudio.h>
#include <FastLED.h>
#include "server.hpp"
#include "crypto.hpp"

// LED Strip pins
#define PIN_LED_1 33
#define PIN_LED_2 32
#define NUM_LEDS 30

// I2C pins
#define PIN_I2C_SDA 21
#define PIN_I2C_SCL 22

// Shift register pins
#define PIN_SR_LATCH 4
#define PIN_SR_CLOCK 18
#define PIN_SR_DATA 23

// I2S pins for TAS5805M
#define PIN_I2S_SCK 5    // BCLK
#define PIN_I2S_WS 25    // LRCK
#define PIN_I2S_SDOUT 26 // DIN to TAS5805M
#define PIN_I2S_SDIN 35  // Not used
#define PIN_I2S_FAULT 34 // Optional: fault monitor
#define PIN_I2S_PDN 27   // Power-down control

// ---------- Config ----------
const char *WIFI_SSID = "megafaYakand8202";
const char *WIFI_PASS = "megafaY@kand*@)@";
const uint16_t WS_PORT = 80;
const char *WS_PATH = "/ws";
// After HMAC verify OK, set authenticated true
bool g_authenticated = true;

tas5805m amp(&Wire);
btAudio bta = btAudio("mYSpeaker");
CRGB leds[NUM_LEDS];

bool ledState = 0;

// Visualization state
static int32_t dynamicMax = 0; // Initial guess
static uint32_t lastUpdate = 0;
// static uint8_t smoothedLevel = 0;

void visualizeAudio(const uint8_t *data, uint32_t len)
{
    // 2. Visualize audio
    int16_t *samples = (int16_t *)data;
    int peak = 0;
    for (uint32_t i = 0; i < len / 2; i++)
    {
        int16_t val = abs(samples[i]);
        if (val > peak)
            peak = val;
    }
    if (peak > dynamicMax)
        dynamicMax = peak; // Expand range if needed

    // Decay over time to adapt to quieter music
    if (millis() - lastUpdate > 50)
    {
        dynamicMax = max(peak, dynamicMax - 5); // Decay slowly
        lastUpdate = millis();
    }
    uint8_t level = map(peak, 0, 32767, 0, NUM_LEDS);
    for (int i = 0; i < NUM_LEDS; i++)
    {
        leds[i] = CHSV(160 - i * 10, 255, (i < level) ? 255 : 0);
    }
    FastLED.show();
}
void bt_data_cb(const uint8_t *data, uint32_t len)
{
    // Send audio to I2S
    size_t written;
    i2s_write(I2S_NUM_0, data, len, &written, portMAX_DELAY);

    // Visualize audio
    visualizeAudio(data, len);
}
void set_on()
{
    // ground latchPin and hold low for as long as you are transmitting
    digitalWrite(PIN_SR_LATCH, LOW);
    shiftOut(PIN_SR_DATA, PIN_SR_CLOCK, LSBFIRST, 0xff);
    shiftOut(PIN_SR_DATA, PIN_SR_CLOCK, LSBFIRST, 0xff);
    // return the latch pin high to signal chip that it
    // no longer needs to listen for information
    digitalWrite(PIN_SR_LATCH, HIGH);
}
void set_off()
{
    // ground latchPin and hold low for as long as you are transmitting
    digitalWrite(PIN_SR_LATCH, LOW);
    shiftOut(PIN_SR_DATA, PIN_SR_CLOCK, LSBFIRST, 0);
    shiftOut(PIN_SR_DATA, PIN_SR_CLOCK, LSBFIRST, 0);
    // return the latch pin high to signal chip that it
    // no longer needs to listen for information
    digitalWrite(PIN_SR_LATCH, HIGH);
}


void setup()
{
    pinMode(PIN_LED_1, OUTPUT);
    pinMode(PIN_LED_1, OUTPUT);
    pinMode(PIN_SR_CLOCK, OUTPUT);
    pinMode(PIN_SR_DATA, OUTPUT);
    pinMode(PIN_SR_LATCH, OUTPUT);

    Serial.begin(115200);
    delay(200);
    Serial.println("System Starting ....");

    pinMode(PIN_I2S_PDN, OUTPUT);
    digitalWrite(PIN_I2S_PDN, LOW); // Power down TAS5805M
    Serial.println("PDN pin set HIGH (TAS5805M active)");

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("\r\nConnect to WiFi ..");
    while (WiFi.status() != WL_CONNECTED)
    {
        Serial.print('.');
        delay(500);
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.printf("WiFi connected: %s\n", WiFi.localIP().toString().c_str());
    }
    else
    {
        Serial.println("WiFi connect failed; continuing anyway");
    }

    cryptoInit();
    serverBegin();

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    if (amp.init() != ESP_OK)
    {
        Serial.println("Failed to initialize TAS5805M");
    }
    else
    {
        uint8_t volume = 60; // Volume level (0-124)
        esp_err_t ret = tas5805m_set_volume_pct(volume);
        if (ret != ESP_OK)
        {
            ESP_LOGE("TAS5805M", "Failed to set volume");
        }
        ret = tas5805m_get_volume_pct(&volume);
        if (ret != ESP_OK)
        {
            ESP_LOGE("TAS5805M", "Failed to get volume");
        }
        else
        {
            ESP_LOGI("TAS5805M", "Current volume: %d", volume);
        }
    }

    bta.begin();
    bta.reconnect();
    bta.I2S(PIN_I2S_SCK, PIN_I2S_SDOUT, PIN_I2S_WS);
    bta.volume(1.0);
    bta.setSinkCallback(bt_data_cb);

    // audio.setPinout(PIN_I2S_SCK, PIN_I2S_WS, PIN_I2S_SDOUT);
    // audio.setVolume(21);

    //    audio.connecttospeech("Hi, This is megaafaa Yaakand Eco Smart System.", "en");
    //    audio.connecttoFS(SD, "/320k_test.mp3");
    //    audio.connecttohost("http://www.wdr.de/wdrlive/media/einslive.m3u");
    //    audio.connecttohost("https://stream.srg-ssr.ch/rsp/aacp_48.asx"); // SWISS POP
    //    audio.connecttohost("http://mp3.ffh.de/radioffh/hqlivestream.aac"); //  128k aac
    // audio.connecttohost("http://mp3.ffh.de/radioffh/hqlivestream.mp3"); //  128k mp3
    // audio.connecttohost("https://portal.yakand.com/03.mp3"); //  128k mp3
    //    audio.connecttohost("https://github.com/schreibfaul1/ESP32-audioI2S/raw/master/additional_info/Testfiles/sample1.m4a"); // m4a
    //    audio.connecttohost("https://github.com/schreibfaul1/ESP32-audioI2S/raw/master/additional_info/Testfiles/test_16bit_stereo.wav"); // wav
    //    audio.connecttospeech("Wenn die Hunde schlafen, kann der Wolf gut Schafe stehlen.", "de");
    // setup_i2s();

    // FastLED.addLeds<WS2811, PIN_LED_1, RGB>(leds, NUM_LEDS);
}

unsigned long loop500ms = 0;
uint8_t loopColor = 0;
bool lastState = ledState;
void loop()
{
    if (lastState != ledState)
    {
        lastState = ledState;
        ledState == true ? set_on() : set_off();
    }

    ws.cleanupClients();
}
