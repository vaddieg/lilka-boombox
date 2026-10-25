# Lilka Music Player — Progress Log

## Plan

| Stage | Goal | Status |
|-------|------|--------|
| 1 | Project skeleton + button/display test | ✅ done (HW verified, flicker fixed) |
| 2 | SD card music mode (browse `/sd/music`, play/pause/prev/next, volume) | ✅ implemented — needs HW test |
| 3 | Internet radio mode (WiFi + streaming stations) | ✅ implemented — needs HW test |
| 4 | Main menu, polish (progress bar, battery, visualizers) | ✅ done |
| 5 | AirPlay 2 speaker mode (vendored airplay-esp32 in the same firmware) | 🔄 handoff v1 done — needs HW test |

## Design decisions

- **Toolchain**: pioarduino platform `55.03.312-1` (arduino-esp32 3.3.12 + ESP-IDF 5.5,
  C++20 support). Works in VSCode pioarduino and plain PlatformIO (platform URL in
  `platformio.ini`).
- **Board**: `lilka_v2` — board JSON in `./boards/` (ESP32-S3, 16MB flash, PSRAM).
- **No Lilka SDK**: the official SDK targets the arduino-esp32 2.x core (pinned
  Arduino_GFX 1.6.0, legacy `I2S.h`, `esp32/clk.h`) and does not compile on the 3.x core.
  Thin in-house hardware layer (`src/hw.h`) with pin map from the SDK's `config.h`:
  - display: ST7789 240x280 (20px y-offset on 240x320 panel), HSPI, 80MHz, mode 3, rotation 3
  - SD card: shared HSPI bus, CS=16, 20MHz, core SD library (FatFS)
  - I2S: BCLK=42, LRCK=1, DOUT=2 (PCM5102A)
  - buttons: active-low, INPUT_PULLUP, 10ms debounce
- **UI**: `moononournation/GFX Library for Arduino` 1.6.9 (compiles on 3.x core).
  `Arduino_ESP32SPI` + `Arduino_ST7789` (subclass `ST7789Lilka` forces SPI_MODE3 via
  `_override_datamode`, same trick as the SDK) + `Arduino_Canvas` double buffer.
  Built-in GLCD font 8x8, `setTextSize()`.
  - Canvas gotcha: framebuffer is allocated in `canvas.begin()` — mandatory, use
    `begin(GFX_SKIP_OUTPUT_BEGIN)`; otherwise all draw/flush are silent no-ops → black screen.
  - `canvas.flush()` blits the whole buffer in one 16-bit bitmap write → no flicker.
- **Rendering policy**: draw into canvas, `flush()` only on change. UI screens (stage 2)
  follow this: `BrowserScreen` redraws on cursor change; `PlayScreen` redraws on action
  or every 500 ms (progress bar).
- **SD**: core `SD` (3.x API `SD.begin(cs, SPI, freq)`). Display + SD share HSPI pins.
  - **Shared-bus gotcha (fixed, 2024-06)**: GFX 1.6.9 `Arduino_ESP32SPI` drives the SPI
    registers directly (raw HAL, never calls `SPIClass::begin()`). That left the IDF SPI
    driver in a state where the core SD library's `beginTransaction()` HANGS forever
    (waits for an interrupt the raw setup never enables). Symptoms: "NO SD CARD" at first
    (second spi_bus_initialize failed), then hang in `SD.begin()` after adding
    `SPI.begin()` first.
    **Fix** = use the SDK's exact topology with `Arduino_HWSPI` (driver-level
    `beginTransaction`, present in 1.6.9 too):
    1. `SPI.begin(sck, miso, mosi)` — like SDK `spi_begin()`
    2. `Arduino_HWSPI(dc, cs, sck, mosi, miso, &SPI, true)` — display bus
    3. `SD.begin(cs, SPI, freq)` — same SPIClass, same driver API → they coexist.
    `_override_datamode` (SPI_MODE3) still works: `Arduino_TFT::begin()` passes it to
    `bus->begin(speed, mode)`.
  - Audio engine (`MusicPlayer::m_audioOn`) was disabled while debugging the SPI hang;
    re-enabled after SD/UI verified — first real HW test of the 4.0.0 audio init
    (`setPinout` + audio task) happens with this build.
- **Audio**: `schreibfaul1/ESP32-audioI2S` pinned at tag **4.0.0** — a complete rewrite,
  classic API (setSource/setPath/startup) is gone:
  - `Audio audio; audio.setPinout(BCLK, LRC, DOUT);`
  - `audio.connecttoFS(SD, "/music/file.mp3");` — plays local FS file, codec auto-detected
  - `audio.loop();` must run every main-loop iteration (with a ~1-2 ms delay)
  - volume: 21 steps by default (`getVolumeSteps()`), `setVolume(step)`; we map app 0..100
  - track end: `Audio::audio_info_callback` → event `evt_eof`
  - position: `getAudioCurrentTime()` / `getAudioFileDuration()` in **seconds**
  - `pauseResume()` toggles pause; `stopSong()` stops
  - needs C++20 (`<span>`) → pioarduino platform; `CORE_DEBUG_LEVEL=0` in build_flags.
- **Guest OTA mode**: `verifyRollbackLater()` returns `true` in `src/main.cpp` — keeps the
  app's OTA slot valid so ESP-IDF auto-rollback never reverts it.

## Stage 1 — skeleton + button test ✅

- Build ✅. HW test: orientation OK, all 10 buttons OK, flicker fixed (canvas double
  buffer + flush-on-change). Serial prints free heap + PSRAM.

## Stage 3 — main menu + internet radio (implemented, awaiting HW test)

- `main.cpp`: mode machine MENU ↔ (BROWSER ↔ PLAY) / RADIO.
- `ui.cpp`: `MenuScreen` (SD MUSIC / INTERNET RADIO). `PlayScreen` is reused for
  radio (`setRadio(true)`): header "RADIO", station/stream title instead of file
  name, no progress bar, L/R = switch station, B = exit.
- `player.cpp` radio section:
  - **User stations**: `MusicPlayer::loadStations()` (called when entering radio
    mode, so new files need no reboot) reads `*.url` text files from `/webradio`
    on the SD root — station name = filename without extension, URL = trimmed
    first line of the file. List is sorted by name. If the folder is missing or
    has no valid `.url` files, falls back to 4 built-in stations (URLs from the
    ESP32-audioI2S examples): 3FM NL, Jazz ofr.fm, WDR2 DE (https), Antenne
    Oldies DE.
  - **Keira OS WiFi credentials** (from `keira/src/services/network/network.cpp`):
    NVS namespace `"network"`, key `last_ssid` = last connected SSID, key
    `<hash(ssid)>_pw` = password. Hash = djb2 (`h = (h<<5) - h + c`), lower 32
    bits printed as `%08x`. We only read — never write.
  - **Power saving**: WiFi stack is not touched at boot. Entering radio:
    `WiFi.mode(WIFI_STA)` + `WiFi.begin`, 20 s connect timeout (blocking, screen
    shows "connecting to <ssid>"). Leaving radio (B, or stream dropped →
    `evt_eof`): `stopSong()` + `WiFi.disconnect(true, true)` + `WiFi.mode(WIFI_OFF)`
    — same procedure Keira's NetworkService uses to disable WiFi.
  - Stream title: `evt_streamtitle` / `evt_name` → `streamTitle()`. Station name
    is held for 2 s after (re)start so the icy title doesn't flash over it;
    after that the server title takes over. Spectrum + volume stripes work
    unchanged
    (library FFT runs on stream samples too).
- **Human test checklist (Stage 3):**
  1. Boot → menu; UP/DOWN + A work.
  2. SD MUSIC behaves as before (stage 2 checklist).
  3. INTERNET RADIO: "connecting to <ssid>", then audio from 3FM; title shows
     the icy-name/streamtitle when the server sends it.
  4. L/R switches stations (title updates, audio changes); A pause/resume.
  5. B → back to menu; verify WiFi is off (serial: "[radio] wifi off"), and that
     the device idles without WiFi activity.
  6. Volume stripes + spectrum animate in radio mode.
  7. Browser: B goes up a folder; at /music, B returns to the main menu.
  8. After leaving radio, SD player shows the file name + progress bar again.
  9. Create /webradio/mystation.url (one line with a stream URL) on the SD root
     → radio mode lists it (name = filename); delete all .url files → built-in
     stations come back.

## Stage 2 — SD music mode (implemented, awaiting HW test)

Files:
- `src/player.h/.cpp` — `MusicPlayer`: non-recursive folder listing (folders first, then
  audio files: mp3/flac/wav/ogg/opus/m4a/aac/wma/mid), play/stop/pause/next/prev inside
  the current folder, track-end event, volume 0..100.
- `src/ui.h/.cpp` — `BrowserScreen` (folder list, ".." row below /music, auto-repeat
  scrolling) and `PlayScreen` (title, progress bar, time, PAUSED, volume bar, hints).
- `src/main.cpp` — state machine BROWSER ↔ PLAY.

Controls (change in `src/ui.cpp` if you prefer a different mapping):

| Screen | Button | Action |
|--------|--------|--------|
| browser | UP / DOWN | move (hold to auto-repeat) |
| browser | A | open folder / start file |
| browser | B | parent folder (also on ".." row) |
| player | A | play / pause |
| player | LEFT / RIGHT | prev / next track |
| player | B | stop → back to browser |
| player | UP / DOWN | volume +5 / −5 |

Behavior notes:
- starts in `/sd/music`; missing card or folder → on-screen error message.
- next/prev wrap around within the current folder (subfolders not traversed).
- track end → auto next; end of folder → back to browser.
- main loop: `player.loop()` + UI + `vTaskDelay(2 ms)` (audio library needs this cadence).

Build: SUCCESS — RAM 13.9% (45KB/320KB), Flash 24.3% (1.59MB/6.5MB).

### Spectrum bars + layout v2 (added)

- Play screen: volume meter in the left margin = 20 thin (1 px) left-aligned
  stripes, 1 px spacing, narrow at bottom (4 px) widening to top (28 px),
  39 px tall total, 2x narrow (width 2–14 px); lit stripes green (vol 5–65,
  ~2/3) / yellow (70–80) / red (85–100), unlit neutral gray 0x8428 (25% white). 15 spectrum bars to the right, shared
  baseline y=252, tops at y≈134 (clears time row and PAUSED indicator).

- 15-band FFT spectrum on the play screen (library-smoothed bars + peak hold),
  fed by `Audio::evt_spectrum` (callback → mutex-protected copy, polled by UI).
- **Gotcha 1**: `aud.settings.SPECTRUM` defaults to `false` in 4.0.0 — the FFT never
  ran. Enabled in `MusicPlayer::begin()`.
- **Gotcha 2**: the library pushes `fft_items_t::NUM_BANDS` = **16** elements into the
  spectrum vectors (band 15 = 20 kHz, unused by Nyquist). Our handler required
  `vec1.size() == 15` and silently dropped every `evt_spectrum` event → bars stuck at 0.
  Fixed: accept `>= 15`, take the first 15 bands.

**Human test checklist (Stage 2):**
1. Put a few files in `/music` on the SD card (mp3 + flac + a wav if you have them),
   optionally in a subfolder.
2. Flash; Serial should show free heap + "Stage 2: SD music mode".
3. Browser: list appears, folders with trailing "/", A enters, B goes up.
4. Play a file: sound out the speakers, progress bar moves, time updates.
5. A pauses (PAUSED shown), A resumes. LEFT/RIGHT switch tracks, B returns to browser.
6. UP/DOWN changes volume (bar + audible).
7. Let a track end: should auto-advance to the next file.

### UTF-8 / Cyrillic text (added — final approach)

- The GLCD font is ASCII-only. The REAL Keira/Lilka SDK trick (found in
  `lilka-sdk/lib/lilka/src/lilka/display.cpp`): **Arduino_GFX has native U8g2
  font support** (`U8G2_FONT_SUPPORT`, auto-enabled because `U8g2lib.h` is on
  the include path). The SDK calls `display.setFont(u8g2_font_10x20_t_cyrillic)`
  + `setUTF8Print(true)` + `print()` directly on `Arduino_ST7789`/
  `Arduino_Canvas` — no U8G2 object, no blit. (SDK comments cite Arduino_GFX
  PR #460 and read ascent/descent from `font[13]`/`font[14]`.)
- `src/font.h/.cpp` is now a thin wrapper over the canvas:
  `setFont(u8g2_font_*)` (olikraus fork), `setUTF8Print(true)`,
  `setTextColor(c, c)` (bg==fg -> transparent), `setCursor(x, y + ascent)`
  (ascent from `font[13]`, y = text top -> baseline), `print(s)`. Width via
  `getTextBounds`. API: `begin(gfx)`, `text()`, `width()`, `fit()`.
- Font sizes — 5x8 confirmed too tiny, DO NOT USE: `scale 1` =
  `u8g2_font_6x13_t_cyrillic` (regular UI text), `scale 2` =
  `u8g2_font_10x20_t_cyrillic` (titles/menu — the SDK's default font).
  Layout re-tuned for the taller glyphs: browser rowH 16, play-screen
  title lines y=28/52, progress bar y=80, times y=92, PAUSED y=110, menu
  item text y+4, footer y=H()-14.
- Screen has rounded corners: header/footer text use `kPadX = 16` px
  horizontal padding. Browser rows use the 20 px font (rowH 24, ~8 rows).
- `webradio/*.url` station files renamed to Cyrillic display names (Хіт FM,
  Громадське, Кіс FM, Мелодія FM, МФМ Харків, Наше Радіо, NRJ Україна,
  Одеське Радіо, Радіо НВ, Радіо Релакс, Радіо Рокс, Українське Радіо).
  Re-copy the webradio/ folder to the SD card.
- **Dead end (for the record)**: using U8G2 as a software renderer (null
  display driver + 1bpp buffer + manual blit) produced an empty buffer on
  device. The U8g2 clib draw API also misbehaves in a plain host harness.
  Do not go back to that path.
- All UI text (headers, browser rows, now-playing title, menu, messages) goes
  through `font::*`. Cyrillic now works everywhere, incl. stream titles and
  station names; `/webradio/*.url` filenames may contain Cyrillic.
- 8-s boot font test screen — removed after the sizes were confirmed on
  device.
- Radio title behavior (final): the header reads `RADIO - <station name>`
  (always visible), and the big title shows the station name too, replaced by
  the icy song title **as soon as the server sends it** (the 2-s hold was
  dropped; empty titles are ignored so the name is never blanked).

Build: SUCCESS. (font sizes confirmed on device)

### Screen off (added)

- GPIO46 (`hw::PIN_DISP_BL`, SDK "LILKA_SLEEP") is the display backlight/
  power (TFT_BL) line — acts as on/off, no smooth dimming. Driven HIGH at
  setup.
- Button D in any mode: `Mode::OFF`, BL LOW, all screen updates skipped
  (loop returns early; audio keeps playing, radio stream continues).
- Any key wakes: BL HIGH, return to the previous mode (`g_offPrev`), all
  screens invalidated for a fresh redraw; the waking press is consumed so it
  doesn't trigger an action on the restored screen.
- **HW caveat (confirmed on device)**: GPIO46 currently powers the whole panel
  including the audio DAC — screen off kills playback (playhead keeps
  advancing in software). No software workaround (an auto-pause/resume hack
  was tried and reverted); the plan is a hardware fix separating the audio
  power rail from the display one.

### Shuffle (added)

- Button X (physical C, SDK key C) in SD play mode toggles shuffle:
  first press = Fisher-Yates random reorder of the folder's audio files
  (folders keep their places, original order saved in `m_origEntries`);
  second press restores the original sorted order. Current track stays
  selected (reindexed by name) in both directions. `MusicPlayer::
  toggleShuffle()/isShuffled()`; `stop()` and `openFolder()` reset to the
  original order (browser always shows the normal listing).
- UI: `Action::SHUFFLE`; "SHUFFLE" indicator (C_ACCENT, 2x font) at y=110
  while shuffled (PAUSED takes that line when paused); footer shows
  `C:shuffle` (user's wording: `X:shuffle`). Volume hint dropped from the
  footer to make room.
- Indicator placement (v2): the original 2x-font "SHUFFLE" at y=110 was too
  big and collided with the spectrum bar peaks (~y=134). Now scale 1, centered
  at y=92 — the time row just under the progress bar; it's narrow enough to
  fit between the position/duration times at the edges. PAUSED keeps its old
  spot (y=110, 2x) and still suppresses the SHUFFLE indicator while paused.
- **Title bug (fixed)**: enabling shuffle changed the displayed title to an
  unrelated track while audio kept playing. Cause: the "reindex current track"
  step read the name via `m_entries[m_current]` AFTER the in-place reorder, so
  `m_current` already pointed at a different file. Fix: capture the current
  name before shuffling, reindex to it after. (`restoreOrder()` was already
  safe — it reads the name before restoring the order.)

### About screen (added)

- Third main-menu item "ABOUT" -> static info screen (`AboutScreen` in ui.h/
  ui.cpp, `Mode::ABOUT` in main.cpp). A/B back to menu.
- Content (text hand-tuned by the user): sections MUSIC (`/sd/music` +
  formats), WEB RADIO (`/sd/webradio/*.url`, name=filename, url=1st line),
  CREDITS (schreibfaul1/ESP32-audioI2S, olikraus/U8g2, Lilka-dev community,
  Arduino, "By github/vaddieg & qween"). Section titles C_ACCENT, details
  C_FG/C_DIM. Note: the on-device paths are shown as `/sd/...` even though the
  app mounts the card at `/` (kRoot="/music").

### Battery indicator (added)

- `src/battery.{h,cpp}` — same scheme as the official Lilka SDK (`lilka-sdk/.../battery.cpp`):
  LiPo on GPIO3 (ADC1) through a 33k/100k divider, `ADC_ATTEN_DB_11` (0..3.1 V),
  12-bit, median of 32 samples. volts = raw/4095 * 3.1/(100/133) = raw/4095*4.123;
  < 0.5 V -> no battery (-1); else 3.2 V -> 0%, min(4.2, 4.123) V -> 100%.
  Cached, re-measured every 5 s.
- NOTE 1: the SDK configures `ADC1_CHANNEL_2`, but on the ESP32-S3 GPIO3 is
  channel 3 — v1 carry-over in the SDK; harmless because analogRead configures
  the pin itself.
- NOTE 2 (crash fix): do NOT call the legacy `adc1_config_*` functions — the
  modern arduino-esp32 core (3.3.x) uses the new ADC driver and the legacy
  calls panic: `ADC: CONFLICT! driver_ng is not allowed to be used with the
  legacy driver`. The core's defaults already match the SDK (11 dB, 12-bit),
  so only `pinMode(PIN_BATT, INPUT_PULLDOWN)` is needed.
- Header icon: 24x12 vector battery (1 px outline + tip + proportional fill) at
  the right edge of the accent header, red fill at <= 20%, outline only when no
  battery. Header text is fit-truncated to clear it.
- Refresh fixes: (1) the first ADC batch after unit init can read 0 -> 16
  priming samples in `begin()`; (2) the 5-s cache must not block the very first
  measurement (`s_have` flag); (3) the main loop invalidates all screens every
  5 s (`Screen::invalidate()`, `m_dirty` moved to the base class) so the icon
  updates without user input.
- Rounded-corner padding: kPadX 16 -> **20 px** (16 was not enough).
- WiFi signal icon (left of battery in the header): 4 horizontal bars, 2 px
  tall, widths 2/7/12/15 (bottom shortest), centered in the 15 px footprint,
  filled bottom-up, lit = C_BG. Only the lit bars are drawn — dim unlit bars
  (50% blend) were indistinguishable from lit on the cyan header.
  RSSI -> bars (original ladder, per user): >=-55:4, >=-67:3, >=-78:2, >=-88:1,
  else 0. Drawn only while STA-connected (WiFi is off in menu/SD mode — power
  saving, which reads as "0 bars" there); header text reserves the icon space.
  Connection log prints the measured RSSI: `[radio] wifi: ... rssi N dBm`.

### Hold-to-seek in music mode (added)
Tap L/R = prev/next track (now fired on RELEASE, not press); hold L/R ≥400 ms
= seek in 5 s steps every 400 ms while held (`MusicPlayer::seekBy` →
`aud.setTimeOffset(±5)`, which clamps to 0..duration and stops the song at
either end). Radio mode keeps pure station switching (no seek). Progress bar
redraws while seeking (`m_dirty` in PlayScreen::update).

### Music visualizers (added, HW verified — colors correct)

- `src/viz.h/.cpp` — visualizer framework. SELECT cycles modes in the play
  screen (SD + radio; the FFT feeds both). The mode name flashes 1.5 s at the
  top-left of the frame after a switch; the selection persists across tracks
  and into radio mode. Footer hint: `S:viz`.
- **All visualizers share one view frame** — the classic spectrum-bar rect
  (x=39, 192x100, baseline y=H()-38), so switching never shifts the layout.
- Modes:
  - **BARS** — the classic 15 spectrum bars, colored per band (rainbow),
    white peak-hold ticks.
  - **WAVE** — mirrored filled wave across the frame, hue swept along x,
    white crests, gentle time shimmer so quiet tracks aren't flat.
  - **ORBIT** — slowly rotating rainbow spokes (length = band) around a
    bass-pulsing core (outer ring colored by bass level, white center).
  - **OFF** — dim 1 px baseline only.
- **Update rate: 20 fps with real data.** The audio library's FFT stage emits
  `evt_spectrum` every 50 ms (`timer_ms = 50` in Audio.cpp 4.0.0) and the
  display values are internally smoothed per tick (attack 100 / release 20).
  The UI was just sampling at 10 Hz — fix was only the play-screen redraw
  period 100 ms -> 50 ms. A synthetic "halved intermediate frame" trick was
  tried and REVERTED (felt weird); do not bring it back.
- SELECT/START key hygiene (`main.cpp`): screens only consume the keys they
  read; the main loop swallows START everywhere and SELECT outside play/radio,
  so a stale justPressed can't fire an action after a mode switch.
- **RGB565 packing gotcha (the big one, fixed):** our HSV/HSL-to-565
  conversion packed green with `>> 3` (the 5-bit rule) — but green's 565
  field is **6 bits** and needs `>> 2`. Result: every green came out at half
  strength (03E0 = g125 instead of 07E0 = g255), yellow rendered as
  amber/khaki ("no yellow at all"), green looked undersaturated, while
  red/blue (true 5-bit fields) were fine. Correct packing:
  `((r>>3)<<11) | ((g>>2)<<5) | (b>>3)`. Host-verified against reference
  values: red F800, yellow FFE0, green 07E0, cyan 07FF, blue 001F.
- Color choice: full rainbow sweep, hue 0..240 deg only (red -> yellow ->
  green -> cyan -> blue; violet/magenta third dropped — and it puts band hues
  every 16 deg), HSL s=1.0 l=0.5, packed via `rainbow(t)` in viz.cpp. The
  `r8()` clamp guards float32 landing at 254.99998 for full channels.
- **Display color-mode safety (added while debugging colors):** Arduino_GFX
  ST7789 init tables (v1.6.0 through v1.6.9, upstream included) write
  `COLMOD = 0x55` — an INVALID value (16-bit = 0x05, 18-bit = 0x06) — so the
  panel keeps its factory-default mode after SWRESET. `ST7789Lilka::
  force16bitColor()` now sends `COLMOD = 0x05` explicitly in
  `createDisplay()`; no-op if already 16-bit, corrective if not. (Had the
  panel been left in 18-bit mode, 16-bit writes would misread byte-wise:
  yellow -> near-white, red -> magenta, blue -> dark green.)
- **C_ACCENT is green, not cyan** (0x07E0 = (0,255,0); true cyan = 0x07FF).
  The old "cyan" comment was wrong — the display always rendered it
  correctly. Comment fixed in ui.h.

### AirPlay 2 mode (stage 5 — spike done, HW test + handoff pending)

**Decision: AirPlay, not Bluetooth.** Our chip is ESP32-S3-WROOM-1-**N16R8**
(standard S3, BLE 5.0 only, no BR/EDR radio): classic A2DP impossible,
LE Audio impossible (needs BLE 5.2/CIS). AirPlay 2 over Wi-Fi is the only
OS-level "phone music → box" path. Chosen implementation: **rbouteiller/
airplay-esp32** (AirPlay 2, ALAC+AAC, LVGL display, ~610★) — Non-Commercial
license, which is fine: Lilka is a non-commercial/educational product.

**Architecture (agreed with user):** one firmware, two worlds.
- Menu → **AIRPLAY**: stop player, `i2s_del_channel` (needs a 1-line vendor
  patch to expose `Audio::m_i2s_tx_handle` — `I2Sstop()` only disables the
  channel, port stays allocated), `WiFi.disconnect(true)`, spawn
  `airplay_task` (their RTSP/HAP/I2S/LVGL/buttons), then **park the Arduino
  loop**: `loopTask` blocks on `xSemaphoreTake(g_loopResume, portMAX_DELAY)`.
  "Quitting Arduino" = parking a priority-1 FreeRTOS task; the whole IDF
  world (heap/wifi driver/NVS/SPI2) keeps running.
- **Quit AirPlay → v1: `esp_restart()`** (erase an NVS flag first → boots
  straight into AirPlay mode). In-place teardown (their code has no symmetric
  stop paths) is a later improvement.
- I2S pins: BCLK=42, WS=1, DOUT=2, **MCLK unused** (our DAC runs from its
  internal crystal; matches their `CONFIG_I2S_SCK_IO=-1` default).
- Display: their `display_st7789.c` (esp_lcd + LVGL 9) — NOT our Arduino_GFX
  UI. Panel config: 240×280, GAP_Y=20, rotation TBD, SPI CLK=12/MOSI=15/
  CS=10/DC=9/RST=13/BL=14. Their layout constants assume landscape —
  portrait layout is a TODO.
- Buttons: their `buttons.c` → A=play/pause, C/D=vol, UP/DOWN=track, B=quit.

**Vendoring layout** (all local libs, no registry deps — LDF/registry can't
find the Espressif components): 
- `lib/airplay-esp32/src/` — their main/{rtsp,hap,plist,audio,network,
  playback_control,settings,led,dacp_client,bt_coex,buttons,alac_magic_cookie}
  + board_utils (weak defaults) + display/display_st7789.c + audio-resampler
  + dac driver headers. `airplay_config.h` = Kconfig replacement (force
  included per-TU via `#include` as first line of every vendored .c — per-lib
  `-include` flags in library.json leak into the LINKER command and break
  linking). `lv_conf.h` = LVGL config, found via `-DLV_CONF_INCLUDE_SIMPLE`.
- `lib/esp_audio_codec/` — registry component 2.5.0: headers +
  **prebuilt esp32s3 .a** (libesp_audio_codec.a + libesp_audio_simple_dec.a).
  The .a files are the asset: the Espressif registry ships *source only* and
  pioarduino has no IDF component build system, so the prebuilt archives
  must stay vendored. The 3 registration .c wrappers were dropped (dead:
  airplay code calls esp_alac_dec/esp_aac_dec directly, and their
  CONFIG_AUDIO_DECODER_* guards aren't defined here).
  + include tree (6 include roots — see platformio.ini -I list).
- LVGL stack REMOVED (display deferred): `lib/esp_lvgl_port/`, `lib/lvgl/`,
  `display/display_st7789.c`, `display_inc/airplay_display.h`, `lv_conf.h`
  all deleted; `-DLV_CONF_INCLUDE_SIMPLE` + port -I flags dropped from
  platformio.ini. NOTE: `lib/lvgl` was a broken stub (srcDir pointed at a
  nonexistent dir) — LDF silently fell back to the REGISTRY and downloaded
  a 98 MB full lvgl checkout into `.pio/libdeps/v2/`. Removing the lib_deps
  refs stops that. Zero `lv_*` symbols in firmware.elf now. To revive the
  display: re-vendor esp_lvgl_port 2.3.3 + a real lvgl 9.4 (registry name
  collision is fine once the local lib is valid) + the upstream
  display_st7789.c, and give AirPlay its own SPI bus at boot (see
  "AirPlay display — deferred").

**Gotchas hit (so they are never re-hit):**
- **LDF scans `lib/` only ONE level deep** — `lib/ext/foo` is invisible;
  libs must sit at `lib/foo`.
- LDF chain mode: project libs must be listed in `lib_deps` by name.
- `library.json` `build.includeDir` must be a STRING (a list crashes
  piolib.py with a bare traceback); extra include roots go into global
  `build_flags -I`.
- **Kconfig vs preprocessor semantics (the big one)**: upstream guards use
  `#ifdef`/`defined()`, where *absent macro = disabled*. Defining an option
  as `0` makes `#ifdef` TRUE. All `#ifdef/#if defined(CONFIG_X)` guards in
  the vendored code were converted to `#if CONFIG_X` (undefined → 0 in `#if`),
  and airplay_config.h defines every option (0/1). GPIO pins use -1 for
  "not connected" (`#if PIN >= 0` checks).
- LVGL 9.4: `LV_FONT_MONTSERRAT_x` macros expand to *font object names* —
  never use them as `LV_FONT_DEFAULT`; `LV_CONF_SKIP` makes LVGL ignore the
  conf file entirely.
- Their `audio-resampler` component and `dac_*` driver headers are separate
  components upstream — copied flat into airplay-esp32/src.
- `a2dp_sink.c` / `audio_output_{usb,spdif}.c` are `#if CONFIG_*`-guarded to
  empty (Bluedroid/UAC headers don't exist in the S3 core).
- Core prebuilts already contain: mbedtls/mbedcrypto (AES-CTR, RSA, bignum,
  drbg — verified with nm), libsodium (SRP), mdns, esp_http_server, esp_lcd
  (incl. esp_lcd_new_panel_st7789). NOT contained: bluedroid A2DP, UAC.
- lwIP/wifi/mbedTLS *sdkconfig knobs* (socket count, UDP recvmbox, PSRAM
  malloc policy, default RSA allocator) are baked into the prebuilt core
  libs and CANNOT be changed — if we hit socket/UDP limits in HW testing
  (their defaults want LWIP_MAX_SOCKETS=16, UDP_RECVMBOX=64), that's the
  suspect; fallbacks: drop the web server / PTP, tune audio_buffer sizes.

**State**: full build green (spike28). The vendored objects are not yet in
firmware.elf (linker drops unreferenced library members) — they pull in
when the handoff code references them.

**Implemented (handoff v1, builds green)**:
- Menu item **AIRPLAY** (4th, 40 px pitch) → `startAirPlayMode()`:
  stop player → `i2sRelease()` (vendor-patched `Audio::I2SstopPublic()` +
  `i2s_del_channel(aud.i2sTxHandle())` — 2-line patch in
  `.pio/libdeps/v2/ESP32-audioI2S/src/Audio.h`) → `wifiOff()` → set boot
  flag → spawn `airplay_task` (prio 3, core 0, 8 KB) → loop parks on
  `vTaskDelay(portMAX_DELAY)`.
- `airplay_task` init order (from their main.c): settings →
  playback_control → led (no-op) → display (their ST7789/LVGL, own SPI2
  bus — SD idle in this mode) → their wifi (STA + captive AP + web UI on
  :80) → on connect: hap → audio_receiver → audio_output → mdns →
  rtsp_server → buttons. **No PTP/NTP in v1** (no multi-room; PTP clock
  init failure would disable AirPlay, so it's skipped, not failed).
- Keys in AirPlay: A=play/pause, C/D=vol, RIGHT/LEFT=next/prev (their
  buttons.c). **B (GPIO 6) held 2 s = quit** → erase flag + `esp_restart()`.
- Boot-into-AirPlay is two-stage NVS (`boot_airplay` + `ap_enter`): stage 1
  clean-reboots (Arduino_GFX would otherwise still own SPI2), stage 2
  takes over before anything initializes. Quit erases the flag → menu.
- Headers: all vendored headers got `extern "C"` guards (they only ever
  compiled as C upstream); `wifi.h`/`display.h` renamed to
  `airplay_wifi.h`/`airplay_display.h` (name collision with the Arduino
  core's wifi.h via the flat -I list).
- Link: LDF puts lib objects BEFORE src/*.o on the link line →
  `-Wl,--start-group`/`--end-group` in build_flags; prebuilt decoder .a
  linked via `-L ... -Wl,--whole-archive -lesp_audio_codec` (library.json
  `build.lib` only takes lib *names*, not file paths).

**Buttons + volume in AirPlay mode (HW-verified).** Two gotchas:
1. **Their interrupt-driven `buttons.c` never fired after the in-place
   handoff** (only our polled B key worked; cause not chased — Arduino
   core / prebuilt-core GPIO ISR service state). Solution: poll the Lilka
   keypad ourselves in `airplay_task` (50 ms tick, 2-tick debounce, 200 ms
   volume auto-repeat). Mapping: **A=play/pause, UP=vol+, DOWN=vol-,
   RIGHT=next, LEFT=prev, B(hold 2s)=quit**.
2. **`playback_control_set_source(PLAYBACK_SOURCE_AIRPLAY)` is mandatory**
   (upstream `main.c:78` does it at boot). Without it every
   `playback_control_*` call (volume/next/prev/play-pause) hits the
   `PLAYBACK_SOURCE_NONE` branch and silently no-ops — buttons register
   fine but do nothing. Added to `airplay_task` after
   `playback_control_init()`.

**STATUS: AirPlay audio WORKS (HW-verified: iPhone smooth, Mac plays with
rare cracks).** Root cause of the long "connects but silent" saga:

**The stale-start-island skipper in `audio_timing.c` starved playout
forever on lossy Wi-Fi.** Before playout starts, `audio_timing_read()`
silently *consumed* (take+return, no counter, only a suppressed ESP_LOGI)
any frame sitting >100 ms below the contiguous run ending at the newest
buffered frame — meant to skip a ~100 ms stale "island" left at stream
start. But with a 2 s pre-buffer on a lossy link, every UDP loss
re-creates an island below the run, and the skipper ate frames (and the
held pending frame) faster than the buffer head could reach the run —
audio never started. Symptom fingerprint: `dec` climbs, `buf` stays in
the teens, `started=0`, `playing=1`, `out_underrun` flat, no sound.
Aggravating factor: the Mac re-sends SETRATEANCHORTIME every few seconds;
in the no-PTP fallback `audio_timing_set_anchor()` pins
`anchor_local_time_ns = now` and resets `consecutive_early_frames`, so the
stuck-anchor safety valve (needs 50 consecutive early frames) could never
fire. Fix: cap cumulative island skips per start episode at 500 ms
(`lilka_island_skip_total`, reset in `audio_timing_reset()`) — keeps the
anti-blip protection (observed island: 14 frames ≈ 112 ms), guarantees
playout starts. Verified: disabling the skipper entirely → audio plays;
capped version restored.

Debugging tools added (lilka): `adx_debug.{h,c}` per-branch drop counters;
`[tng]`/`[adx]`/`[airplay] audio:` diagnostic loop in `airplay_mode.cpp`
(5×5 s: rx/dec/drop, playing, has_data, timing state + anchor);
`audio_receiver_timing_state()`/`audio_receiver_buffered_frames()`
accessors. NOTE: the prebuilt log library swallows the vendored code's
ESP_LOG output (runtime `esp_log_level_set` has no effect) — rely on the
Serial diagnostics, not their logs.

**HW test round 8 — "Lilka not in AirPlay list" root cause: the
`mdns_airplay_init()` call had been accidentally deleted during the
instrumentation rewrite (it's declared `void`, so it vanished with its
"error check"). No mDNS → no advertisement. Restored + verified by
disassembling airplay_task in firmware.elf. (The IP check
`[airplay] STA WIFI_STA_DEF ip=...` confirmed LAN/DHCP were fine all
along; the `mdns_service_exists` probe crashed on the never-created
service mutex, which is what exposed it.)

**HW test rounds 5-7 — display pursuit abandoned (v1). Decision: AirPlay mode
does NOT touch the display; the screen shows a static Arduino-drawn page
("AIRPLAY / Lilka is an AirPlay speaker / hold B to quit" — message() with
holdMs=0 before parking). Why the display fight lost:
- The SPI2 host is owned by the Arduino HAL (raw registers, esp32-hal-spi.c);
  the IDF spi_master driver only half-exists for that host.
- Prebuilt libesp_driver_spi.a quirks: lazy-init (no spi_bus_initialize)
  leaves SCK/MOSI unmuxed (panel gets nothing); out-of-range polling
  transfers (>64 B FIFO) crash the driver instead of erroring (LoadProhibited
  in xQueueReceive — ret_queue garbage); a 4096 B DMA transfer in the probe
  crashed identically (suspect: private DMA bounce handling).
- One DMA-stack bug of OURS was found and fixed along the way: probe sent
  tx_buffer pointing at a stack param — SPI DMAs asynchronously, address
  dangles. (Their esp_lcd code is safe: params are copied into its static
  trans pool.)
- Display facts gathered (for a future attempt, probably via a dedicated
  display partition/driver swap, not bus sharing): panel is 280x240 logical
  (rotation 3); working window CASET 20..299 / RASET 0..239; MADCTL 0xA0
  (MY|MV, ST7789V bits MY=0x80/MX=0x40/MV=0x20; RGB bit 0 — BGR data order);
  SPI mode 3 required; SWRESET via command works (screen went black).
- Audio: the state that WORKED was round 3 (cracking at 160 MHz). Round 5
  (240 MHz + spi_bus_initialize + probe) had NO audio — unexplained; the
  display stripping also removes those two variables. Current build =
  round-3 conditions + 240 MHz + clean i2sRelease (stop=OK del=OK).

**HW test round 4 — AirPlay connects (no more cracking at 240 MHz); panel shows
the LAST ARDUINO FRAME — i.e. zero IDF-side transactions reach the panel
(even SWRESET didn't latch).**
Root cause: Arduino's SPIClass drives SPI2 via RAW HAL (esp32-hal-spi.c) — no
IDF spi_master bus context ever exists for the host. The IDF driver's
lazy-init (from esp_lcd's spi_bus_add_device) leaves the bus with NO pin
config → SCK/MOSI never muxed to the peripheral → transactions go to the
void. Fix: `g_dispBusHandoff()` now explicitly calls
`spi_bus_initialize(SPI2_HOST, {mosi=17, sclk=18, miso=-1, max_transfer_sz=32K},
SPI_DMA_CH_AUTO)` before display_init (Arduino side is parked; quit reboots).
Geometry (MADCTL 0xA0 = MY|MV per the library's ST7789V bit layout RGB=0x00,
280×240 logical, window CASET 20..299 / RASET 0..239) is set but NOT yet
verified — defer geometry tweaking until pixels actually flow.

**HW test round 3 — AirPlay connects (audio works, cracking); still no UI.**
- Cracking → fixed: AirPlay task now runs at 240 MHz (Arduino side downclocks to
  160 for battery; decode+I2S DMA+WiFi needs the full clock).
- No UI → geometry fix: esp_lcd maps x→CASET, y→RASET. The working
  Arduino_GFX config (rotation 3, offsets 0/20/0/20) yields window
  **CASET 20..259, RASET 0..279, MADCTL MY|MV|RGB** → so `set_gap(20, 0)`,
  not the upstream board's `(0, 20)`. (The upstream Waveshare 240×240
  panel has its offset on the RASET axis; ours is on the CASET axis.)
- Note: our UI side uses INVOFF (Arduino_GFX default `_ips=false`); their
  init sends INVON — if the UI appears with inverted colors, flip
  `esp_lcd_panel_invert_color(panel_handle, false)`.
- SPI mode 3 kept (user questioned it, but the whole Arduino UI only works in
  mode 3 — Lilka SDK forces it too; upstream board's mode 0 is for a
  different panel).

**HW test round 2 — Mac SEES the AirPlay device (RTSP+mDNS+WiFi all work); no UI on panel.**
Also: user wants NO reboot handoff (firmware always boots Arduino; AirPlay =
in-place teardown). Both addressed:**
1. Handoff is now fully in-place: menu → stop player → i2sRelease (NULL-guarded,
   was crashing when never started) → wifiOff (core does esp_wifi_stop +
   esp_wifi_deinit + netif destroy, verified in WiFiGeneric.cpp) → spawn
   airplay_task → park loop. Boot flags removed (stale ones erased in setup).
   Quit = esp_restart() back into Arduino mode.
2. Display root cause candidates (patched, awaiting HW test):
   a. **SPI mode**: their esp_lcd io used `spi_mode = 0`; the Lilka panel
      requires SPI_MODE3 (Lilka SDK forces it) → init commands never latched.
   b. **MADCTL**: they set MX|MV|RGB (0x25); our working orientation is
      MY|MV|RGB (0x61, Arduino_GFX rotation 3) → now
      `swap_xy(true) + mirror(false, true)`.
   Gap (CASET 0, RASET 20) was already correct for the 240×320 glass.

**First HW test round 1 — "stuck on message screen, no device visible" — root causes found & fixed**:
1. Display pins in `airplay_config.h` were guessed (12/15/10/9/13) — the real
   panel is SCK=18, MOSI=17, CS=7, DC=15 (RST unused). Valid-but-wrong GPIOs
   fail no checks, so the driver "worked" into the void. Fixed.
2. SPI handoff: the prebuilt SPI driver has no `spi_bus_deinit` (stripped),
   and our display owns SPI2_HOST for the boot. Their `display_init(bus)`
   accepts a pre-initialized host as an opaque pointer → we pass
   `(void*)(intptr_t)SPI2_HOST`; they add their esp_lcd device on the same
   bus/pins (Arduino side is idle the whole time).
3. WiFi: first run had no creds in their NVS (namespace "airplay") → captive
   AP only → invisible to AirPlay. Now the handoff bridges our saved creds
   (NVS "network": last_ssid + <djb2(ssid)>_pw) via
   `settings_set_wifi_credentials()` when theirs are empty.

**TODO before/after first HW test**:
- Portrait layout pass over their LVGL screen (GAP_Y=20; rotation 3 —
  their driver has no rotation yet, check their `lvgl_port_set_orientation`
  or add esp_lcd_panel_swap_xy/mirror_x).
- HW test: pairing from iPhone, stream, dropouts; watch lwIP socket
  budget (web server + RTSP + mDNS on prebuilt lwIP defaults).
- If captive-AP first-run is annoying: bridge our saved wifi creds into
  their settings (their keys vs our "lilka" namespace).
- HW test: SELECT cycles BARS -> WAVE -> ORBIT -> OFF -> BARS with name
  flash; 20 fps animation smooth; colors match reference values on device.
