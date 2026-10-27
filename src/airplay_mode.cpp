// airplay_mode.cpp — AirPlay 2 handoff (see airplay_mode.h / PROGRESS.md).
//
// The vendored airplay-esp32 code is pure FreeRTOS/IDF; we run it in its own
// task after parking the Arduino loop task. On quit we erase the boot flag
// and reboot (v1 — no in-place teardown).

#include "airplay_mode.h"

#include "hw.h"   // Arduino_GFX type
#include <SPI.h>

#include "player.h"

#include <Preferences.h>
#include <WiFi.h>

// vendored airplay-esp32 APIs
#include "settings.h"
#include "hap.h"
#include "playback_control.h"
#include "audio_receiver.h"
#include "audio_output.h"
#include "rtsp_server.h"
#include "mdns_airplay.h"
#include "airplay_wifi.h"
#include "web_server.h"
#include "led.h"
#include "esp_heap_caps.h"
#include "esp_netif.h"
#include <netinet/in.h>
#include "freertos/task.h"

// g_player lives in main.cpp's anonymous namespace — use the accessor.
static inline MusicPlayer& player() { return playerRef(); }

namespace {

Preferences prefs;
const char kFlagKey[] = "boot_airplay";
const char kEnterKey[] = "ap_enter";

/// B (GPIO 6) is our "quit AirPlay" key: hold for ~2 s.
constexpr int kQuitGpio = 6;
constexpr uint32_t kQuitHoldMs = 2000;

extern "C" {
esp_err_t mdns_service_exists(const char* service, const char* proto);
}

void airplay_task(void*) {
    Serial.println("[airplay] task: starting services");

    // AirPlay does heavy decode + I2S DMA + WiFi; run at full speed.
    // (The Arduino side downclocks to 160 MHz for battery life — at 160 the
    // audio pipeline cracks.)
    setCpuFrequencyMhz(240);

    // Settings (their NVS namespace). NVS flash itself is already up — our
    // setup() initialized it; their settings_init() just opens a handle.
    if (settings_init() != ESP_OK) {
        Serial.println("[airplay] settings_init failed");
    }
    ESP_ERROR_CHECK(playback_control_init());
    // Upstream main.c does this at boot — without it every
    // playback_control_* call (buttons, DACP) hits the
    // PLAYBACK_SOURCE_NONE branch and silently does nothing.
    playback_control_set_source(PLAYBACK_SOURCE_AIRPLAY);
    led_init(); // no-op on our board (LED GPIOs disabled in config)

    // Bridge our saved WiFi creds into their NVS (namespace "airplay",
    // plain wifi_ssid/wifi_pass strings) so first-run doesn't need the
    // captive-AP setup flow. Only if they have nothing yet.
    if (!settings_has_wifi_credentials()) {
        Preferences p;
        p.begin("network", true);
        String ssid = p.getString("last_ssid", "");
        String pass;
        if (ssid.length()) {
            uint64_t h = 0; // keiraSsidHash (djb2, lower 32 bits)
            for (unsigned i = 0; i < ssid.length(); i++) h = (h << 5) - h + (unsigned char)ssid[i];
            char hb[9];
            snprintf(hb, sizeof(hb), "%08x", (unsigned int)h);
            String key = String(hb) + "_pw";
            pass = p.getString(key.c_str(), "");
        }
        p.end();
        if (ssid.length() && pass.length()) {
            settings_set_wifi_credentials(ssid.c_str(), pass.c_str());
            Serial.println("[airplay] bridged wifi creds from 'network' NVS");
        }
    }

    // Display: NOT touched (v1). Their esp_lcd/LVGL stack fights the
    // Arduino-HAL-owned SPI2 bus (prebuilt SPI driver quirks: lazy-init
    // leaves pins unmuxed, out-of-range polling transfers crash). The
    // screen keeps the Arduino-drawn AirPlay status page we painted before
    // parking the loop. See PROGRESS.md "AirPlay display — deferred".

    // Network: their WiFi manager (STA + captive AP fallback with the web
    // server for first-run credentials). Arduino's WiFi stack is already
    // released by startAirPlayMode() (WiFi.disconnect(true)).
    wifi_init_apsta(NULL, NULL);
    bool haveCreds = settings_has_wifi_credentials();
    if (haveCreds && !wifi_wait_connected(20000)) {
        Serial.println("[airplay] wifi connect timeout — check AP");
    }
    if (!haveCreds) {
        Serial.println("[airplay] no creds: join the setup AP, then http://192.168.4.1");
    }

    // Web UI (config + OTA). Kept for v1 testing.
    web_server_start(80);

    if (wifi_is_connected()) {
        // Core AirPlay services (no PTP in v1 — no multi-room sync).
        // Print return codes (no ESP_ERROR_CHECK) so one failure can't hide
        // the rest during debugging.
        esp_err_t e;
        if ((e = hap_init()) != ESP_OK) Serial.printf("[airplay] hap_init: %s\n", esp_err_to_name(e));
        if ((e = audio_receiver_init()) != ESP_OK) Serial.printf("[airplay] audio_receiver_init: %s\n", esp_err_to_name(e));
        if ((e = audio_output_init()) != ESP_OK) Serial.printf("[airplay] audio_output_init: %s\n", esp_err_to_name(e));
        mdns_airplay_init(); // void — (was accidentally dropped in the
                             // instrumentation rewrite: no mDNS = invisible)
        audio_output_start(); // void — errors surface via ESP_LOG only
        if ((e = rtsp_server_start()) != ESP_OK) Serial.printf("[airplay] rtsp_server_start: %s\n", esp_err_to_name(e));
        Serial.println("[airplay] services up — 'LilkaBoombox' should appear in AirPlay");

        // One-shot sanity checks (mDNS can't announce without a LAN IP).
        // The Arduino WiFi object was deinit'd, so query esp_netif directly. (mDNS can't
        // announce without one.) Query esp_netif directly — the Arduino
        // WiFi object was deinit'd and its netif table is stale.
        vTaskDelay(pdMS_TO_TICKS(3000));
        bool ipFound = false;
        for (esp_netif_t* nif = esp_netif_next_unsafe(nullptr); nif; nif = esp_netif_next_unsafe(nif)) {
            const char* key = esp_netif_get_ifkey(nif);
            if (key && strncmp(key, "WIFI_STA", 8) == 0) {
                esp_netif_ip_info_t info = {};
                if (esp_netif_get_ip_info(nif, &info) == ESP_OK) {
                    Serial.printf("[airplay] STA %s ip=" IPSTR "\n", key, IP2STR(&info.ip));
                    ipFound = true;
                }
            }
        }
        if (!ipFound) Serial.println("[airplay] STA has no IP — mDNS cannot announce");

        // mDNS self-check (0 = ESP_OK = present).
        Serial.printf("[airplay] mdns: airplay=%d raop=%d (0=ok)\n",
                      (int)mdns_service_exists("_airplay", "_tcp"),
                      (int)mdns_service_exists("_raop", "_tcp"));
    } else {
        Serial.println("[airplay] wifi not up — will stay on setup AP");
    }

    // Lilka keypad, polled (50 ms tick). Their interrupt-driven buttons.c
    // is not used — after the in-place handoff its GPIO ISR path never
    // fired (only our polled B key worked). Mapping:
    //   A = play/pause   UP = vol+   DOWN = vol-   (vol auto-repeats)
    //   RIGHT = next     LEFT = prev  B held 2 s = quit
    // next/prev do DACP (mDNS + HTTP) and may block briefly — acceptable
    // for a 50 ms poller (quit detection uses millis(), so it survives).
    pinMode(hw::PIN_A, INPUT_PULLUP);
    pinMode(hw::PIN_UP, INPUT_PULLUP);
    pinMode(hw::PIN_DOWN, INPUT_PULLUP);
    pinMode(hw::PIN_RIGHT, INPUT_PULLUP);
    pinMode(hw::PIN_LEFT, INPUT_PULLUP);
    Serial.println("[airplay] keys: A=play/pause UP/DN=vol R/L=next/prev B2s=quit");

    struct Key {
        int gpio;
        void (*fire)();
        bool repeat;      // auto-repeat while held (volume)
        int seenLow;      // consecutive LOW ticks (bounce filter)
        bool pressed;
        uint32_t lastFire;
    };
    Key keys[] = {
        {hw::PIN_A,     playback_control_play_pause,  false, 0, false, 0},
        {hw::PIN_UP,    playback_control_volume_up,   true,  0, false, 0},
        {hw::PIN_DOWN,  playback_control_volume_down, true,  0, false, 0},
        {hw::PIN_RIGHT, playback_control_next,        false, 0, false, 0},
        {hw::PIN_LEFT,  playback_control_prev,        false, 0, false, 0},
    };

    uint32_t quitAt = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(50));

        for (auto& k : keys) {
            if (digitalRead(k.gpio) == LOW) {
                if (!k.pressed) {
                    if (++k.seenLow >= 2) {   // ~100 ms: confirmed press
                        k.pressed = true;
                        k.lastFire = millis();
                        k.fire();
                    }
                } else if (k.repeat && millis() - k.lastFire >= 200) {
                    k.lastFire = millis();
                    k.fire();
                }
            } else {
                k.seenLow = 0;
                k.pressed = false;
            }
        }

        if (digitalRead(kQuitGpio) == LOW) {
            if (!quitAt) quitAt = millis();
            else if (millis() - quitAt >= kQuitHoldMs) {
                Serial.println("[airplay] quit requested (B held) — rebooting to menu");
                quitAirPlayAndReboot(); // does not return
            }
        } else {
            quitAt = 0;
        }
    }
}

} // namespace

void setAirplayEnterFlag(bool on) {
    prefs.begin("lilka", false);
    if (on) prefs.putBool(kEnterKey, true);
    else prefs.remove(kEnterKey);
    prefs.end();
}

bool airplayEnterFlag() {
    Preferences p;
    p.begin("lilka", true);
    bool v = p.getBool(kEnterKey, false);
    p.end();
    return v;
}

void quitAirPlayAndReboot() {
    // v1 quit path: full reboot back into Arduino mode (the AirPlay stack
    // has no teardown API; the reboot also cleans up its wifi/LVGL state).
    setAirplayBootFlag(false); // legacy flags — keep NVS tidy
    setAirplayEnterFlag(false);
    esp_restart();
}

void setAirplayBootFlag(bool on) {
    prefs.begin("lilka", false);
    if (on) prefs.putBool(kFlagKey, true);
    else prefs.remove(kFlagKey);
    prefs.end();
}

bool airplayBootFlag() {
    Preferences p;
    p.begin("lilka", true);
    bool v = p.getBool(kFlagKey, false);
    p.end();
    return v;
}

void startAirPlayMode() {
    Serial.println("[airplay] handoff: stopping player, releasing I2S + wifi");
    player().stop();       // SD/radio
    player().i2sRelease(); // i2s_channel_disable + i2s_del_channel
    player().wifiOff();    // full wifi deinit (their wifi.c re-inits)
    vTaskDelay(pdMS_TO_TICKS(100));

    xTaskCreatePinnedToCore(airplay_task, "airplay", 8192, NULL, 3, NULL, 0);

    Serial.println("[airplay] arduino loop parked — AirPlay owns the box");
    // Park: sleep forever. Quit is esp_restart() (v1), so we never wake.
    vTaskDelay(portMAX_DELAY);
}
