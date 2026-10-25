// airplay_mode.h — AirPlay 2 speaker mode (vendored airplay-esp32).
// See PROGRESS.md "AirPlay 2 mode" for the architecture:
//   menu -> startAirPlayMode() parks the Arduino loop and runs their stack;
//   quit  -> eraseAirplayBootFlag() + esp_restart() (v1).
#pragma once
#include <Arduino.h>

// (display handoff helpers live in airplay_mode.cpp only)

/// Full handoff: stop our player/I2S/WiFi, spawn the AirPlay task, park loop.
/// Never returns (the loop task sleeps forever).
void startAirPlayMode();

/// Set the NVS flag so the next boot goes straight into AirPlay mode.
/// (Two-stage: this flag triggers a clean reboot; the internal "enter" flag,
/// set automatically, performs the actual takeover on the following boot.)
void setAirplayBootFlag(bool on);
bool airplayBootFlag();

// internal, used by the two-stage boot handoff
void setAirplayEnterFlag(bool on);
bool airplayEnterFlag();

/// Called from the AirPlay side on "quit" (B held): erase flag + reboot.
void quitAirPlayAndReboot();
