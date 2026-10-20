#include "battery.h"
#include "hw.h"

#include <algorithm>

namespace {
constexpr float kDivider = 100.0f / (33.0f + 100.0f);      // 33k/100k divider
constexpr float kMaxV = 3.1f / kDivider;                   // 4.123 V
constexpr float kEmptyV = 3.2f, kFullV = 4.2f;             // LiPo range
int s_level = -1;
uint32_t s_lastMs = 0;
bool s_have = false; // a measurement has been made (the 5-s cache must not
                     // block the very first reading)
}

namespace battery {
void begin() {
    // INPUT_PULLDOWN so an unconnected battery reads 0 (SDK does the same).
    // NOTE: no adc1_config_* here — the modern arduino-esp32 core uses the new
    // ADC driver and its defaults already match the SDK's setup (11 dB atten,
    // 12-bit); the legacy driver calls panic with a driver conflict.
    pinMode(hw::PIN_BATT, INPUT_PULLDOWN);
    // Prime the freshly initialized ADC — the first batch of samples right
    // after unit init can read 0, which would show an empty battery at boot.
    for (int i = 0; i < 16; i++) analogRead(hw::PIN_BATT);
    s_lastMs = 0;
}

int level() {
    uint32_t now = millis();
    if (s_have && now - s_lastMs < 5000) return s_level;
    s_have = true;
    s_lastMs = now;

    // median of 32 samples (SDK approach)
    uint16_t v[32];
    for (auto& x : v) x = analogRead(hw::PIN_BATT);
    std::sort(v, v + 32);
    float volts = (float)v[16] / 4095.0f * kMaxV;
    if (volts < 0.5f) {
        s_level = -1; // no battery
    } else {
        float top = kFullV < kMaxV ? kFullV : kMaxV;
        s_level = (int)((volts - kEmptyV) * 100.0f / (top - kEmptyV) + 0.5f);
        if (s_level < 0) s_level = 0;
        if (s_level > 100) s_level = 100;
    }
    return s_level;
}
}
