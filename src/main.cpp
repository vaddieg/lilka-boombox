// Lilka music player.
//
// Stage 3: main menu + SD card music + internet radio.
//   - menu:    UP/DOWN move, A select (SD MUSIC / INTERNET RADIO)
//   - browser: UP/DOWN move, A open (folder or play file), B parent folder
//   - player:  A play/pause, LEFT/RIGHT prev/next track, B stop (back to browser),
//              UP/DOWN volume
//   - radio:   A play/pause, LEFT/RIGHT switch station, B stop (back to menu),
//              UP/DOWN volume. WiFi is powered up only while in radio mode and
//              fully off (WIFI_OFF) on the way back to the menu.
//   - non-recursive listing; next/prev stay inside the current folder

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <SD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>

#include "hw.h"
#include "player.h"
#include "ui.h"
#include "font.h"
#include "battery.h"

// "Guest" OTA mode: mark the app as valid so the ESP-IDF auto-rollback mechanism
// never reverts the boot slot (we are not the primary/OS firmware).
extern "C" bool verifyRollbackLater() { return true; }

namespace {

constexpr const char* kRoot = "/music";

Arduino_GFX* g_disp = nullptr;
Arduino_Canvas* g_buf = nullptr;
Buttons g_btns;
MusicPlayer g_player;
MenuScreen* g_menu = nullptr;
BrowserScreen* g_browser = nullptr;
PlayScreen* g_play = nullptr;
AboutScreen* g_about = nullptr;

enum class Mode { MENU, BROWSER, PLAY, RADIO, ABOUT, OFF };
Mode g_mode = Mode::MENU;
Mode g_offPrev = Mode::MENU; // mode to return to when the screen wakes

/// D: screen off — display power line (BL) LOW, all redraws skipped.
void screenOff() {
    g_offPrev = g_mode;
    g_mode = Mode::OFF;
    digitalWrite(hw::PIN_DISP_BL, LOW);
    Serial.println("[ui] screen off");
}

/// Any key: screen on — back to the previous mode with a fresh redraw.
void screenOn() {
    digitalWrite(hw::PIN_DISP_BL, HIGH);
    g_mode = g_offPrev;
    g_menu->invalidate();
    g_browser->invalidate();
    g_play->invalidate();
    g_about->invalidate();
    Serial.println("[ui] screen on");
}

void message(const String& title, const String& text, uint32_t holdMs = 2500) {
    g_btns.poll(); // swallow button state so it doesn't trigger right after
    g_buf->fillScreen(C_BG);
    font::text(g_buf, 8, 40, font::fit(title.c_str(), g_disp->width() - 16, 2), C_WARN, 2);
    font::text(g_buf, 8, 90, font::fit(text.c_str(), g_disp->width() - 16), C_FG);
    g_buf->flush();
    if (holdMs) delay(holdMs);
}

String parentPath(const String& p) {
    int i = p.lastIndexOf('/');
    if (i <= 0) return String(kRoot);
    String parent = p.substring(0, i);
    return parent.length() ? parent : String("/");
}

void toBrowser() {
    g_mode = Mode::BROWSER;
    g_browser->enterFolder(g_player);
}

void toPlayer() {
    g_mode = Mode::PLAY;
    g_play->setRadio(false); // clear the flag left over from radio mode
    g_play->reset();
}

void toMenu() {
    g_mode = Mode::MENU;
    g_menu->reset();
}

void toAbout() {
    g_mode = Mode::ABOUT;
    g_about->invalidate();
}

void enterRadio() {
    String ssid, pass;
    if (!g_player.wifiCreds(ssid, pass)) {
        message("NO WIFI CREDENTIALS", "set up WiFi in Keira OS first");
        toMenu();
        return;
    }
    message("RADIO", "connecting to " + ssid, 0); // stays on screen while connecting
    if (!g_player.connectWiFi(ssid, pass, 20000)) {
        g_player.wifiOff();
        message("WIFI FAILED", "could not connect to " + ssid);
        toMenu();
        return;
    }
    g_player.loadStations(); // /webradio/*.url if present, else built-in
    if (!g_player.playStation(0)) {
        g_player.wifiOff();
        message("STREAM FAILED", g_player.stationName(0));
        toMenu();
        return;
    }
    g_mode = Mode::RADIO;
    g_play->setRadio(true);
    g_play->reset();
}

void exitRadio() {
    g_player.stopRadio(); // stops the stream and powers WiFi down
    toMenu();
}

} // namespace

void setup() {
    Serial.begin(115200);
    vTaskDelay(pdMS_TO_TICKS(500));

    // Downclock the CPU from the 240 MHz default to save power. WiFi and I2S
    // still work fine at 160 MHz (WiFi clock is derived from the CPU clock).
    setCpuFrequencyMhz(160);
    Serial.printf("[setup] cpu freq: %u MHz\n", (unsigned)ESP.getCpuFreqMHz());

    Serial.println("[setup] display...");
    g_disp = createDisplay();
    Serial.printf("[setup] display ok (%dx%d), canvas...\n", g_disp->width(), g_disp->height());
    g_buf = new Arduino_Canvas(g_disp->width(), g_disp->height(), g_disp, 0, 0, 0);
    if (!g_buf->begin(GFX_SKIP_OUTPUT_BEGIN)) {
        Serial.println("ERROR: canvas alloc failed");
        while (1) vTaskDelay(1000);
    }
    pinMode(hw::PIN_DISP_BL, OUTPUT);
    digitalWrite(hw::PIN_DISP_BL, HIGH);
    font::begin(g_buf);
    battery::begin();
    Serial.printf("[setup] battery: %d%%\n", battery::level());
    g_btns.begin();
    g_menu = new MenuScreen(g_buf, g_disp, &g_btns);
    g_browser = new BrowserScreen(g_buf, g_disp, &g_btns);
    g_play = new PlayScreen(g_buf, g_disp, &g_btns);
    g_about = new AboutScreen(g_buf, g_disp, &g_btns);

    Serial.printf("free heap: %lu B internal, %lu B PSRAM\n",
                  (unsigned long)ESP.getFreeHeap(),
                  (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    Serial.println("[setup] player...");
    if (!g_player.begin()) {
        message("I2S ERROR", "audio init failed");
        while (1) vTaskDelay(1000);
    }
    Serial.println("[setup] player ok");

    Serial.println("[setup] SD mount...");
    if (!initSD()) {
        Serial.println("SD mount failed");
        message("NO SD CARD", "insert card and reboot");
        while (1) vTaskDelay(1000);
    }
    Serial.printf("SD ok: %s, %llu bytes\n", SD.cardType() == 1 ? "SD" : (SD.cardType() == 2 ? "SDHC" : "SDXC"),
                  (unsigned long long)SD.totalBytes());

    Serial.println("[setup] open /music ...");
    if (!g_player.openFolder(kRoot)) {
        message("NO /MUSIC FOLDER", "create /music on the SD card");
        while (1) vTaskDelay(1000);
    }
    Serial.printf("[setup] %u entries in %s\n", (unsigned)g_player.entryCount(), g_player.folder().c_str());

    g_browser->setRoot(kRoot);
    g_browser->enterFolder(g_player);
    g_menu->reset();
}

void loop() {
    g_btns.poll();
    g_player.loop(); // audio keeps running while the screen is off

    if (g_mode == Mode::OFF) {
        // any key wakes the screen; consume the press so it doesn't act on the
        // restored screen
        bool any = false;
        for (uint8_t i = 0; i < Buttons::COUNT; i++)
            if (g_btns.get((Buttons::Key)i).justPressed) { any = true; break; }
        if (any) screenOn();
        return; // all screen updates skipped while off
    }
    if (g_btns.get(Buttons::D).justPressed) { // D works in every mode
        screenOff();
        return;
    }

    // the battery is re-measured every 5 s — force a redraw (menu/browser
    // otherwise only repaint on user input) so the header icon stays fresh
    static uint32_t lastBattMs = 0;
    if (millis() - lastBattMs >= 10000) {
        lastBattMs = millis();
        g_menu->invalidate();
        g_browser->invalidate();
        g_play->invalidate();
    }

    if (g_mode == Mode::MENU) {
        Action act = g_menu->update();
        if (act == Action::ENTER) {
            int sel = g_menu->selected();
            if (sel == 0) {
                Serial.println("[nav] SD music");
                toBrowser();
            } else if (sel == 1) {
                enterRadio();
            } else {
                Serial.println("[nav] about");
                toAbout();
            }
        }
    } else if (g_mode == Mode::ABOUT) {
        if (g_about->update() == Action::BACK) toMenu();
    } else if (g_mode == Mode::RADIO) {
        // stream dropped by the server -> back to menu (and WiFi off)
        if (g_player.pollEvent() == MusicPlayer::EV_TRACK_END) {
            Serial.println("[radio] stream ended");
            g_player.stopRadio();
            message("STREAM ENDED", "back to menu");
            toMenu();
        }

        Action act = g_play->update(g_player);
        size_t n = g_player.stationCount();
        switch (act) {
            case Action::PLAY_PAUSE:
                g_player.togglePause();
                Serial.println(g_player.isPaused() ? "[radio] paused" : "[radio] resumed");
                break;
            case Action::NEXT: Serial.println("[radio] next station"); g_player.playStation((g_player.currentStation() + 1) % n); break;
            case Action::PREV: Serial.println("[radio] prev station"); g_player.playStation((g_player.currentStation() + n - 1) % n); break;
            case Action::STOP: Serial.println("[radio] exit"); exitRadio(); break;
            case Action::VOL_UP: g_player.setVolume100(g_player.volume100() + 5); break;
            case Action::VOL_DOWN: g_player.setVolume100(g_player.volume100() - 5); break;
            default: break;
        }
    } else if (g_mode == Mode::PLAY) {
        // auto-advance on track end
        if (g_player.pollEvent() == MusicPlayer::EV_TRACK_END) {
            Serial.println("[play] track end");
            if (!g_player.nextTrack()) {
                g_player.stop();
                toBrowser();
            }
        }

        Action act = g_play->update(g_player);
        switch (act) {
            case Action::PLAY_PAUSE:
                g_player.togglePause();
                Serial.println(g_player.isPaused() ? "[play] paused" : "[play] resumed");
                break;
            case Action::NEXT: Serial.println("[play] next"); g_player.nextTrack(); break;
            case Action::PREV: Serial.println("[play] prev"); g_player.prevTrack(); break;
            case Action::STOP: Serial.println("[play] stop"); g_player.stop(); toBrowser(); break;
            case Action::VOL_UP: g_player.setVolume100(g_player.volume100() + 5); break;
            case Action::VOL_DOWN: g_player.setVolume100(g_player.volume100() - 5); break;
            case Action::SHUFFLE:
                g_player.toggleShuffle();
                Serial.printf("[play] shuffle %s\n", g_player.isShuffled() ? "on" : "off");
                g_play->invalidate();
                break;
            default: break;
        }
    } else {
        Action act = g_browser->update(g_player);
        if (act == Action::BACK) {
            if (g_player.folder() == kRoot) {
                Serial.println("[nav] back to menu");
                toMenu();
            } else {
                String p = parentPath(g_player.folder());
                Serial.println("[nav] up -> " + p);
                g_player.openFolder(p);
                g_browser->enterFolder(g_player);
            }
        } else if (act == Action::ENTER) {
            size_t idx = g_browser->selectedEntry();
            const auto& e = g_player.entry(idx);
            if (e.isDir) {
                Serial.println("[nav] enter dir " + e.name);
                if (g_player.openFolder(g_player.entryPath(idx)))
                    g_browser->enterFolder(g_player);
            } else {
                Serial.println("[nav] play " + e.name);
                if (g_player.playEntry(idx))
                    toPlayer();
            }
        }
    }

    vTaskDelay(pdMS_TO_TICKS(3));
}
