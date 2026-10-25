# Lilka Boombox

Music player firmware for the **Lilka v2** handheld console (ESP32-S3, v0.3):
SD card music with switchable visualizers, internet radio, and **AirPlay 2
speaker mode** (stream from iPhone/Mac) — with full Cyrillic text support,
battery/WiFi indicators and a screen-off mode.

## Features

- **SD music** — browse `/music` (subfolders allowed), play/pause,
  **hold LEFT/RIGHT to rewind/forward in 5 s steps** (tap = prev/next track),
  volume, and **shuffle** (press again to restore the original order).
- **AirPlay 2** — the box shows up as "Lilka" in the AirPlay picker and
  plays ALAC from iPhone/Mac (44.1 kHz output, no resampling). The screen
  keeps a static status page (no UI in this mode, v1); audio, volume and
  track switching work from the hardware buttons. Quit = hold B (reboot back
  to the menu).
- **Internet radio** — user station list from the SD card, or a built-in
  fallback list. Stream titles shown when the server sends them (ICY).
- **Switchable visualizers** (SELECT cycles, all sharing the same view
  frame): 15-band **spectrum bars**, **waveform**, **orbit** — plus an
  **off** mode — and a stepped color volume meter.
- **Cyrillic / UTF-8 rendering** via Arduino_GFX's native U8g2 font support
  (`u8g2_font_6x13_t_cyrillic`, `u8g2_font_10x20_t_cyrillic`).
- **Battery indicator** (LiPo on GPIO3 through a 33 k/100 k divider) and a
  **WiFi signal icon** in the header.
- **Screen off** — button D powers the display down; any key wakes it.
  (Note: on the current hardware revision GPIO46 also powers the audio DAC,
  so playback stops while the screen is off — a hardware fix is planned.)
- **Power saving** — WiFi is brought up only while in radio/AirPlay mode and
  switched fully off (`WIFI_OFF`) when you leave it.

## Supported formats

`mp3`, `m4a`, `aac`, `wma`, `flac`, `wav`, `ogg`, `mid`

AirPlay streams are ALAC (the codec the sender chooses; 44.1 kHz stereo in
practice).

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
| Menu | A | select (MUSIC / WEB RADIO / AIRPLAY / ABOUT) |
| Browser | UP / DOWN | move |
| Browser | A | open folder / play file |
| Browser | B | parent folder (at `/music` → menu) |
| Play | A | play / pause |
| Play | LEFT / RIGHT | tap: previous / next track · **hold: rewind / forward (5 s steps)** |
| Play | B | stop (back to browser) |
| Play | C | shuffle on / off (restore order) |
| Play | SELECT | cycle visualizer (bars → wave → orbit → off) |
| Play | UP / DOWN | volume |
| Radio | A | play / pause stream |
| Radio | LEFT / RIGHT | previous / next station |
| Radio | B | exit to menu (WiFi powered down) |
| Radio | UP / DOWN | volume |
| **AirPlay** | A | play / pause |
| **AirPlay** | UP / DOWN | volume (auto-repeat while held) |
| **AirPlay** | LEFT / RIGHT | previous / next track |
| **AirPlay** | **B (hold 2 s)** | **quit — reboot to the menu** |
| Any (except AirPlay) | **D** | **screen off** (any key returns) |

## WiFi credentials

The player reads (never writes) the network saved by **Keira OS** from NVS:
namespace `network`, key `last_ssid` plus `<hash(ssid)>_pw` (djb2 hash, lower
32 bits as 8 hex chars). If no credentials are present, radio mode shows a
hint and returns to the menu.

AirPlay mode uses the same credentials: on first run they are bridged into
the AirPlay stack's own NVS namespace; without them the box falls back to a
captive setup AP (join "LilkaBoombox" / "lilkalilka", then
`http://192.168.4.1` to store credentials).

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

| File / dir | Role |
|---|---|
| `src/hw.h` | pin map, display creation (ST7789 280×240, HSPI 80 MHz, mode 3), button polling, SD init |
| `src/player.cpp` | `MusicPlayer` — SD browsing, ESP32-audioI2S playback, spectrum, radio + WiFi, I2S release for AirPlay |
| `src/ui.cpp` | flicker-free screens (double-buffered `Arduino_Canvas`): menu, browser, play, about |
| `src/viz.cpp` | visualizer modes (bars / wave / orbit / off) |
| `src/font.cpp` | thin wrapper over Arduino_GFX U8g2 fonts (UTF-8, 2 sizes) |
| `src/battery.cpp` | LiPo level (median of 32 ADC samples, 5 s cache) |
| `src/airplay_mode.cpp` | in-place handoff to the AirPlay stack: stop player → release I2S/WiFi → run the vendored RTSP/HAP/decoder tasks → poll the Lilka keypad; quit reboots |
| `src/main.cpp` | mode machine: MENU → BROWSER → PLAY / RADIO / ABOUT / AIRPLAY / OFF |
| `lib/airplay-esp32/` | vendored [rbouteiller/airplay-esp32](https://github.com/rbouteiller/airplay-esp32) (RTSP/HAP, ALAC+AAC glue, DACP, buttons, web UI) — display/LVGL parts removed |
| `lib/esp_audio_codec/` | Espressif codec component: headers + **prebuilt esp32s3 archives** (the ALAC/AAC decoders) |

Design decisions and the full development log: [`PROGRESS.md`](PROGRESS.md).

## Credits

- [schreibfaul1/ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S) — audio engine
- [rbouteiller/airplay-esp32](https://github.com/rbouteiller/airplay-esp32) — AirPlay 2 stack
- [Espressif esp_audio_codec](https://github.com/espressif) — ALAC/AAC decoders
- [Arduino_GFX](https://github.com/moononournation/Arduino-GFX) — rendering (display + native U8g2 font support)
- [olikraus/U8g2](https://github.com/olikraus/U8g2_Arduino) — Cyrillic font data
- [Lilka-dev](https://github.com/Lilka-dev) community — hardware, SDK, Keira OS
- Arduino / ESP32 cores

**By github/vaddieg & qween**
