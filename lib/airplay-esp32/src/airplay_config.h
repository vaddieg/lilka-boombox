// airplay_config.h — build-time configuration for the vendored airplay-esp32
// code (lib/airplay-esp32), replacing its IDF Kconfig.
//
// Upstream source: github.com/rbouteiller/airplay-esp32 (Non-Commercial
// License). Values mirror config/sdkconfig.defaults{,.esp32s3} + our hardware
// (see src/hw.h). Force-included on every TU via build_flags (-include).
//
// Note: this only covers C preprocessor macros. sdkconfig knobs that affect
// PREBUILT core libraries (lwIP socket limits, wifi buffers, mbedTLS allocator
// choices, PSRAM malloc policy) are NOT applied here — the core keeps its own
// defaults. If we ever hit socket/UDP limits during HW testing, those are the
// suspects.

#pragma once


#ifdef __cplusplus
extern "C" {
#endif
// --- PSRAM (we have 8 MB octal) -------------------------------------------
#define CONFIG_SPIRAM 1
#define CONFIG_SPIRAM_SPEED_80M 1
#define CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM 1
// >1 KB allocations go to PSRAM (upstream value; keeps internal DRAM free for
// DMA descriptors / semaphores)
#define CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL 1024

// --- CPU -------------------------------------------------------------------
#define CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ 240
#define CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240 1

// --- Output: 48 kHz (upstream default; 44.1 -> 48 sinc resampler) ----------
// LILKA EXPERIMENT: 44.1 k output — Mac ANNOUNCES 44.1k ALAC, so this
// removes the 44.1->48 sinc resampler stage entirely (compile-time stub
// in audio_resample.c) to test whether the rare Mac cracks come from the
// resampler. If cracks remain, they're Wi-Fi/UDP loss, not rate-related.
// (Trade-off: a hypothetical 48k AirPlay stream would play at wrong pitch
// — macOS AirPlay is 44.1k in practice.)
#define CONFIG_OUTPUT_SAMPLE_RATE_44100 1
#define CONFIG_OUTPUT_SAMPLE_RATE_HZ 44100
#define CONFIG_RESAMPLER_TAPS 28

// --- I2S pins: our PCM5102A (MCLK unused — DAC runs from internal crystal) -
#define CONFIG_I2S_BCK_IO 42
#define CONFIG_I2S_WS_IO 1
#define CONFIG_I2S_DO_IO 2
#define CONFIG_I2S_SCK_IO -1      // MCLK not connected on lilka_v2
#define CONFIG_I2S_GND_IO -1
#define CONFIG_I2S_VCC_IO -1

// --- Battery ADC (their display shows a battery icon) ----------------------
#define CONFIG_BAT_CHANNEL 3

// --- Buttons: our keypads (active-low, internal pull-up in their code) -----
// A = play/pause, C/D = volume, UP/DOWN = track select, B = quit AirPlay
#define CONFIG_BTN_PLAY_PAUSE_GPIO 5
#define CONFIG_BTN_PLAY_PAUSE_DOUBLE_CLICK 0
#define CONFIG_BTN_VOLUME_UP_GPIO 10
#define CONFIG_BTN_VOLUME_DOWN_GPIO 9
#define CONFIG_BTN_NEXT_GPIO 40
#define CONFIG_BTN_PREV_GPIO 39

// --- Display: our 240x280 ST7789 (20px y-offset panel) via SPI -------------
#define CONFIG_DISPLAY_ENABLED 1
#define CONFIG_DISPLAY_DRIVER_ST7789 1
#define CONFIG_DISPLAY_BUS_SPI 1
#define CONFIG_DISPLAY_ST7789_WIDTH 240
#define CONFIG_DISPLAY_ST7789_HEIGHT 280
// Our panel's visible window (from the working Arduino_GFX config):
// CASET 20..259, RASET 0..279, MADCTL MY|MV|RGB. esp_lcd maps x->CASET,
// y->RASET, so the gap is X=20, Y=0 (NOT Y=20 as on the upstream board).
#define CONFIG_DISPLAY_ST7789_GAP_X 20
#define CONFIG_DISPLAY_ST7789_GAP_Y 0
#define CONFIG_DISPLAY_SPI_CLK 18
#define CONFIG_DISPLAY_SPI_MOSI 17
#define CONFIG_DISPLAY_SPI_CS 7
#define CONFIG_DISPLAY_SPI_DC 15
#define CONFIG_DISPLAY_SPI_RST -1
#define CONFIG_DISPLAY_BL_GPIO 46
#define CONFIG_DISPLAY_UPDATE_MS 200
#define CONFIG_DISPLAY_FLIP 0
// Display: REMOVED in v1 (LVGL/esp_lvgl_port/display driver deleted —
// AirPlay mode has no UI; the screen keeps the static Arduino-drawn page).
// CONFIG_DISPLAY_* pins below are kept as panel-wiring documentation.

// --- AirPlay tuning ---------------------------------------------------------
#define CONFIG_AIRPLAY_FORCE_V1 0
#define CONFIG_ENABLE_AIRPLAY_ARTWORK 1
#define CONFIG_AIRPLAY_TIMING_THRESHOLD_MS 25
#define CONFIG_AIRPLAY_RT_TIMING_THRESHOLD_MS 50

// --- Wifi -------------------------------------------------------------------
#define CONFIG_WIFI_PREFER_5GHZ 0
#define CONFIG_WIFI_PREFER_5GHZ_MIN_RSSI 0
// captive-AP fallback shown when the box has no stored wifi credentials
#define CONFIG_DEFAULT_AP_SSID "LilkaBoombox"
#define CONFIG_DEFAULT_AP_PASSWORD "lilkalilka"

// --- lwIP (runtime values used by their code; the prebuilt core keeps its
// own compile-time limits) ----------------------------------------------------
#define CONFIG_LWIP_MAX_SOCKETS 16
#define CONFIG_LWIP_TCP_WND_DEFAULT 16384

// --- Misc guards ------------------------------------------------------------
#define CONFIG_FREERTOS_UNICORE 0   // S3 is dual core
#define CONFIG_BT_ENABLED 0
#define CONFIG_BT_PIN_CODE "0000"
#define CONFIG_DISPLAY_HEIGHT_32 0
#define CONFIG_DISPLAY_I2C_ADDR 0
#define CONFIG_DISPLAY_I2C_SCL -1
#define CONFIG_DISPLAY_I2C_SDA -1

// --- Disabled hardware (not on lilka_v2) ------------------------------------
#define CONFIG_LED_STATUS_GPIO -1
#define CONFIG_LED_ERROR_GPIO -1
#define CONFIG_LED_RGB_GPIO -1
#define CONFIG_LED_STATUS_BRIGHTNESS 0
#define CONFIG_SPDIF_DO_IO -1
#define CONFIG_UAC_SPEAKER_CHANNEL_NUM 2
#define CONFIG_UAC_MIC_CHANNEL_NUM 2
#define CONFIG_UAC_SAMPLE_RATE 48000

// NOTE: guards in the vendored .c/.h files use #if (not #ifdef) so that
// 0 = disabled; keep it that way when editing.
#define CONFIG_DAC_TAS57XX 0
#define CONFIG_DAC_TAS58XX 0
#define CONFIG_DAC_TAS58XX_ESP32C5 0
#define CONFIG_DAC_ES8311 0
#define CONFIG_DAC_CONTROLS_VOLUME 0
#define CONFIG_BT_A2DP_ENABLE 0
#define CONFIG_BT_SSP_ENABLED 0
#define CONFIG_ETH_W5500_ENABLED 0
#define CONFIG_LED_STATUS_INVERT 1
#define CONFIG_LED_ERROR_INVERT 1
#define CONFIG_LED_RGB_COLOR_STANDBY 0
#define CONFIG_LED_RGB_COLOR_PAUSED 0
#define CONFIG_LED_STATUS_STANDBY_OFF 1
#define CONFIG_LED_STATUS_STANDBY_BLINK_MEDIUM 0
#define CONFIG_LED_STATUS_PLAYING_VU 0
#define CONFIG_LED_STATUS_PLAYING_BLINK_FAST 1
#define CONFIG_LED_STATUS_PAUSED_STEADY 1
#define CONFIG_AUDIO_OUTPUT_SPDIF 0
#define CONFIG_AUDIO_OUTPUT_USB 0
#define CONFIG_DISPLAY_DRIVER_SH1106 0
#define CONFIG_DISPLAY_DRIVER_SH1107 0
#define CONFIG_DISPLAY_DRIVER_SSD1309 0

#ifdef __cplusplus
}
#endif // extern C
