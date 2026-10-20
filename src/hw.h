// Thin hardware layer for the Lilka v2 console (ESP32-S3).
//
// Pin map copied from the official Lilka SDK (config.h, LILKA_VERSION 2).
// Display and SD card share one SPI bus (default SPI object, pins via GPIO matrix).

#pragma once

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <Arduino_GFX_Library.h>

namespace hw {

// Buttons (active-low, use INPUT_PULLUP)
constexpr int8_t PIN_UP = 38, PIN_DOWN = 41, PIN_LEFT = 39, PIN_RIGHT = 40;
constexpr int8_t PIN_A = 5, PIN_B = 6, PIN_C = 10, PIN_D = 9;
constexpr int8_t PIN_SELECT = 0, PIN_START = 4;

// SPI bus shared by display and SD card
constexpr int8_t PIN_SPI_SCK = 18, PIN_SPI_MOSI = 17, PIN_SPI_MISO = 8;

// Display: ST7789, 240x280 active window on a 240x320 panel
constexpr int8_t PIN_DISP_DC = 15, PIN_DISP_CS = 7;
// Display backlight/power (TFT_BL, SDK "LILKA_SLEEP"): on/off line, LOW blanks
// the display (no smooth dimming on this hardware).
constexpr int8_t PIN_DISP_BL = 46;
constexpr uint32_t DISP_SPI_HZ = 80000000;
constexpr uint8_t DISP_ROTATION = 3;
constexpr uint16_t DISP_PANEL_W = 240, DISP_PANEL_H = 280;
constexpr uint16_t DISP_OFFSET_Y = 20; // 20px black bands top/bottom of the 320px panel

// SD card
constexpr int8_t PIN_SD_CS = 16;
constexpr uint32_t SD_SPI_HZ = 20000000;

// I2S out (PCM5102A DAC)
constexpr int8_t PIN_I2S_BCLK = 42, PIN_I2S_LRCK = 1, PIN_I2S_DOUT = 2;

// Battery: LiPo through a 33k/100k divider (ADC1, 11 dB atten, 12-bit)
constexpr int8_t PIN_BATT = 3;

} // namespace hw

/// Simple polling button controller with 10ms debounce (same behavior as the Lilka SDK).
class Buttons {
public:
    enum Key : uint8_t {
        UP = 0, DOWN, LEFT, RIGHT,
        A, B, C, D,
        SELECT, START,
        COUNT
    };

    struct State {
        bool pressed = false;
        bool justPressed = false;
        bool justReleased = false;
    };

    void begin() {
        for (uint8_t i = 0; i < COUNT; i++) pinMode(pins_[i], INPUT_PULLUP);
    }

    /// Call from the main loop (~every 5-10ms).
    void poll() {
        uint32_t now = millis();
        for (uint8_t i = 0; i < COUNT; i++) {
            if (now - lastChange_[i] < 10) continue;
            bool pressed = !digitalRead(pins_[i]);
            if (pressed != st_[i].pressed) {
                st_[i].pressed = pressed;
                st_[i].justPressed = pressed;
                st_[i].justReleased = !pressed;
                lastChange_[i] = now;
            }
        }
    }

    /// Get state of a key and clear its justPressed/justReleased flags.
    State get(Key k) {
        State s = st_[k];
        st_[k].justPressed = false;
        st_[k].justReleased = false;
        return s;
    }

    bool pressed(Key k) { return st_[k].pressed; }

private:
    const uint8_t pins_[COUNT] = {
        hw::PIN_UP, hw::PIN_DOWN, hw::PIN_LEFT, hw::PIN_RIGHT,
        hw::PIN_A, hw::PIN_B, hw::PIN_C, hw::PIN_D,
        hw::PIN_SELECT, hw::PIN_START,
    };
    State st_[COUNT];
    uint32_t lastChange_[COUNT] = {0};
};

// This panel needs SPI_MODE3 (the official Lilka SDK forces it the same way).
class ST7789Lilka : public Arduino_ST7789 {
public:
    using Arduino_ST7789::Arduino_ST7789;
    void setSPIMode(uint8_t mode) { _override_datamode = mode; }
};

/// Create the ST7789 display (80MHz, mode 3, rotation 3).
///
/// Same topology as the Lilka SDK:
///   1. spi_begin():  SPI.begin(sck, miso, mosi)   — start the shared bus with the pins
///   2. display bus:  Arduino_HWSPI(..., &SPI, true) — high-level beginTransaction() API
///   3. SD:           SD.begin(cs, SPI, freq)       — same SPIClass, same driver API
///
/// Do NOT use Arduino_ESP32SPI here: it drives the SPI registers directly (raw HAL),
/// which corrupts the IDF SPI driver state — the core SD library's beginTransaction()
/// then hangs forever waiting for an interrupt the raw setup never enables.
inline Arduino_GFX* createDisplay() {
    SPI.begin(hw::PIN_SPI_SCK, hw::PIN_SPI_MISO, hw::PIN_SPI_MOSI);
    auto bus = new Arduino_HWSPI(hw::PIN_DISP_DC, hw::PIN_DISP_CS, hw::PIN_SPI_SCK,
                                 hw::PIN_SPI_MOSI, hw::PIN_SPI_MISO, &SPI, true);
    auto disp = new ST7789Lilka(bus, -1, hw::DISP_ROTATION, true,
                                hw::DISP_PANEL_W, hw::DISP_PANEL_H,
                                0, hw::DISP_OFFSET_Y, 0, hw::DISP_OFFSET_Y);
    disp->setSPIMode(SPI_MODE3);
    disp->begin(hw::DISP_SPI_HZ);
    return disp;
}

/// Initialize the SD card on the shared SPI bus (FatFS via the core SD library).
/// NOTE: call after createDisplay() if the display was created first (same bus).
inline bool initSD() {
    return SD.begin(hw::PIN_SD_CS, SPI, hw::SD_SPI_HZ);
}
