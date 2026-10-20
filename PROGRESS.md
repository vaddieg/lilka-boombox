# Lilka Music Player — Progress Log

## Plan

| Stage | Goal | Status |
|-------|------|--------|
| 1 | Project skeleton + button/display test | ✅ done (HW verified, flicker fixed) |
| 2 | SD card music mode (browse `/sd/music`, play/pause/prev/next, volume) | ✅ implemented — needs HW test |
| 3 | Internet radio mode (WiFi + streaming stations) | ✅ implemented — needs HW test |
| 4 | Main menu, polish (progress bar, battery, seek) | 🔄 menu done |

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
