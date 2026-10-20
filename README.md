# Lilka Boombox

A music player firmware for the **Lilka v2** handheld console (ESP32-S3):
SD card music with a live spectrum, plus internet radio — with full Cyrillic
text support, battery/WiFi indicators and a screen-off mode.

## Features

- **SD music** — browse `/music` (subfolders allowed), play/pause/prev/next,
  volume, and **shuffle** (press again to restore the original order).
- **Internet radio** — user station list from the SD card, or a built-in
  fallback list. Stream titles shown when the server sends them (ICY).
- **15-band live spectrum** and a stepped color volume meter.
- **Cyrillic / UTF-8 rendering** via Arduino_GFX's native U8g2 font support
  (`u8g2_font_6x13_t_cyrillic`, `u8g2_font_10x20_t_cyrillic`).
- **Battery indicator** (LiPo on GPIO3 through a 33 k/100 k divider) and a
  **WiFi signal icon** in the header.
- **Screen off** — button D powers the display down; any key wakes it.
  (Note: on the current hardware revision GPIO46 also powers the audio DAC,
  so playback stops while the screen is off — a hardware fix is planned.)
- **Power saving** — WiFi is brought up only while in radio mode and switched
  fully off (`WIFI_OFF`) when you leave it.

## Supported formats

`mp3`, `m4a`, `aac`, `wma`, `flac`, `wav`, `ogg`, `mid`

## SD card layout

```
/music/            songs (any subfolder structure)
/webradio/         station files, one per station:
   <Name>.url      first line = stream URL, filename = station name
```

Example station file `Хіт FM.url`:

```
http://online.hitfm.ua/HitFM
```

If `/webradio` is missing or empty, a small built-in station list is used.

## Controls

| Screen | Button | Action |
|---|---|---|
| Menu | UP / DOWN | move |
| Menu | A | select (SD MUSIC / INTERNET RADIO / ABOUT) |
| Browser | UP / DOWN | move |
| Browser | A | open folder / play file |
| Browser | B | parent folder (at `/music` → menu) |
| Play | A | play / pause |
| Play | LEFT / RIGHT | previous / next track |
| Play | B | stop (back to browser) |
| Play | C | shuffle on / off (restore order) |
| Play | UP / DOWN | volume |
| Radio | A | play / pause stream |
| Radio | LEFT / RIGHT | previous / next station |
| Radio | B | exit to menu (WiFi powered down) |
| Radio | UP / DOWN | volume |
| Any | **D** | **screen off** (any key returns) |

## WiFi credentials

The player reads (never writes) the network saved by **Keira OS** from NVS:
namespace `network`, key `last_ssid` plus `<hash(ssid)>_pw` (djb2 hash, lower
32 bits as 8 hex chars). If no credentials are present, radio mode shows a
hint and returns to the menu.

## Building & flashing

Toolchain: PlatformIO with the [pioarduino](https://github.com/pioarduino/platform-espressif32)
platform (arduino-esp32 3.3.12 + ESP-IDF 5.5), board `lilka_v2`
(ESP32-S3, 16 MB flash, PSRAM; board definition in `boards/`).

```sh
pio run -e v2            # build
pio run -e v2 -t upload  # flash (USB)
pio device monitor       # serial log @ 115200
```

The firmware runs in "guest" OTA mode: it marks itself valid so the ESP-IDF
auto-rollback mechanism never reverts the boot slot (it is not the primary/OS
firmware).

## Architecture (short version)

| File | Role |
|---|---|
| `src/hw.h` | pin map, display creation (ST7789 280×240, HSPI 80 MHz, mode 3), button polling, SD init |
| `src/player.cpp` | `MusicPlayer` — SD browsing, ESP32-audioI2S playback, spectrum, radio + WiFi |
| `src/ui.cpp` | flicker-free screens (double-buffered `Arduino_Canvas`): menu, browser, play, about |
| `src/font.cpp` | thin wrapper over Arduino_GFX U8g2 fonts (UTF-8, 2 sizes) |
| `src/battery.cpp` | LiPo level (median of 32 ADC samples, 5 s cache) |
| `src/main.cpp` | mode machine: MENU → BROWSER → PLAY / RADIO / ABOUT / OFF |

Design decisions and the full development log: [`PROGRESS.md`](PROGRESS.md).

## Credits

- [schreibfaul1/ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S) — audio engine
- [Arduino_GFX](https://github.com/moononournation/Arduino-GFX) — rendering (display + native U8g2 font support)
- [olikraus/U8g2](https://github.com/olikraus/U8g2_Arduino) — Cyrillic font data
- [Lilka-dev](https://github.com/Lilka-dev) community — hardware, SDK, Keira OS
- Arduino / ESP32 cores

**By github/vaddieg & qween**
