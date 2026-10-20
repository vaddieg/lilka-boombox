#include "ui.h"
#include "battery.h"

#include <WiFi.h>

// ---------------------------------------------------------------- helpers

// The screen has rounded corners — keep header/footer text clear of the edges.
constexpr int16_t kPadX = 20;

String fmtTime(uint32_t sec) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60));
    return String(buf);
}

// ---------------------------------------------------------------- Screen

void Screen::header(const String& text) {
    m_buf->fillRect(0, 0, W(), 18, C_ACCENT);
    // right-side icons: WiFi signal (15px) + battery (24px); text fits around
    const int16_t bx = W() - kPadX - 24, by = 3;
    const int lvl = battery::level();
    m_buf->drawRect(bx, by, 22, 12, C_BG);
    m_buf->fillRect(bx + 22, by + 3, 2, 6, C_BG); // tip
    if (lvl > 0) {
        int w = (int)((long)lvl * 20 / 100);
        if (w < 1) w = 1;
        m_buf->fillRect(bx + 1, by + 1, w, 10, lvl <= 20 ? C_RED : C_BG);
    }
    // WiFi: 0-4 horizontal bars (bottom = shortest), filled bottom-up,
    // only while connected. The top bar is reserved for a really good signal.
    if (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED) {
        int rssi = WiFi.RSSI();
        int bars = rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -78 ? 2 : rssi >= -88 ? 1 : 0;
        const int16_t wx = bx - 4 - 15;
        static const int16_t kBarW[4] = {2, 7, 12, 15};  // bottom -> top
        // draw only the lit bars (dim unlit ones were too close to the lit
        // color to tell apart)
        for (int i = 0; i < bars; i++) {
            m_buf->fillRect(wx + (15 - kBarW[i]) / 2, 12 - i * 3, kBarW[i], 2, C_BG);
        }
    }
    font::text(m_buf, kPadX, 5, font::fit(text.c_str(), W() - 2 * kPadX - 45), C_BG);
}

void Screen::footer(const String& text) {
    font::text(m_buf, kPadX, H() - 14, font::fit(text.c_str(), W() - 2 * kPadX), C_DIM);
}

void Screen::bar(int16_t x, int16_t y, int16_t w, int16_t h, float frac, uint16_t color) {
    m_buf->fillRect(x, y, w, h, 0x162B);
    int16_t fw = (frac > 1.0f) ? w : (int16_t)(w * frac);
    if (fw > 0) m_buf->fillRect(x, y, fw, h, color);
}

// ---------------------------------------------------------------- Browser

void BrowserScreen::enterFolder(MusicPlayer& pl) {
    m_cursor = 0;
    m_dirty = true;
    (void)pl;
}

void BrowserScreen::draw(MusicPlayer& pl, bool canBack) {
    const int16_t top = 18, rowH = 24, footH = 14;  // rows use the 20 px font
    const int rows = (H() - top - footH) / rowH;

    m_buf->fillScreen(C_BG);
    header("SD " + pl.folder());

    size_t total = pl.entryCount() + (canBack ? 1 : 0);

    // keep cursor visible
    if (m_cursor >= total) m_cursor = total ? total - 1 : 0;
    if (m_cursor < m_scroll) m_scroll = m_cursor;
    if (m_cursor >= m_scroll + (size_t)rows) m_scroll = m_cursor - rows + 1;

    for (int r = 0; r < rows; r++) {
        size_t i = m_scroll + (size_t)r;
        int16_t y = top + r * rowH;

        String name;
        if (i < total) {
            if (canBack && i == 0) {
                name = "..";
            } else {
                const auto& e = pl.entry(i - (canBack ? 1 : 0));
                name = e.isDir ? e.name + "/" : e.name;
            }
        }

        if (i == m_cursor) m_buf->fillRect(0, y, W(), rowH, C_SEL);
        font::text(m_buf, 6, y + 2, font::fit(name.c_str(), W() - 12),
                   i == m_cursor ? C_SEL_TEXT : C_FG, 2);
    }

    footer("A:open  B:up  UP/DOWN:move");
}

Action BrowserScreen::update(MusicPlayer& pl) {
    bool canBack = pl.folder() != m_root;
    size_t total = pl.entryCount() + (canBack ? 1 : 0);
    if (total == 0) total = 1; // empty folder: keep one (blank) row for stable layout
    if (m_cursor >= total) m_cursor = total - 1;

    Buttons::State up = m_btns->get(Buttons::UP);
    Buttons::State down = m_btns->get(Buttons::DOWN);
    Buttons::State a = m_btns->get(Buttons::A);
    Buttons::State b = m_btns->get(Buttons::B);

    // move with auto-repeat while held
    if (up.pressed && (up.justPressed || millis() - m_lastMove > 180)) {
        if (m_cursor > 0) { m_cursor--; m_dirty = true; }
        m_lastMove = millis();
    }
    if (down.pressed && (down.justPressed || millis() - m_lastMove > 180)) {
        if (m_cursor + 1 < total) { m_cursor++; m_dirty = true; }
        m_lastMove = millis();
    }
    if (!up.pressed && !down.pressed) m_lastMove = 0;

    Action action = Action::NONE;
    if (a.justPressed) {
        if (canBack && m_cursor == 0) {
            action = Action::BACK;
        } else {
            action = Action::ENTER;
            m_selected = canBack ? m_cursor - 1 : m_cursor;
        }
    } else if (b.justPressed) {
        // parent folder, or exit to the main menu when at the root folder
        action = Action::BACK;
    }

    if (m_dirty || action != Action::NONE) {
        draw(pl, canBack);
        m_buf->flush();
        m_dirty = false;
    }
    return action;
}

// ---------------------------------------------------------------- Play

void PlayScreen::draw(MusicPlayer& pl) {
    // title: file name (SD) or station/stream title (radio); 2x font, wrapped
    // onto two lines by pixel width
    String name = m_radio ? pl.streamTitle() : pl.entryName();
    const int titleW = W() - 12;
    const unsigned char* p = (const unsigned char*)name.c_str();
    char l1[96];
    size_t n = 0;
    l1[0] = 0;
    while (*p) {
        size_t len = (*p < 0x80) ? 1 : ((*p < 0xE0) ? 2 : 3);
        if (n + len >= sizeof(l1)) break;
        memcpy(l1 + n, p, len);
        n += len;
        p += len;
        l1[n] = 0;
        if (font::width(l1, 2) > titleW) {
            n -= len;
            p -= len;
            l1[n] = 0;
            break;
        }
    }

    m_buf->fillScreen(C_BG);
    // radio: the station name lives in the header; the big title below is the
    // station name too, replaced by the song title as soon as the server sends one
    header(m_radio ? "RADIO - " + pl.stationName(pl.currentStation()) : "NOW PLAYING");
    if (n) font::text(m_buf, 6, 28, l1, C_FG, 2);
    if (*p) font::text(m_buf, 6, n ? 52 : 28, font::fit((const char*)p, titleW, 2), C_FG, 2);

    // progress (local files only; streams have no known length)
    if (!m_radio) {
        uint32_t pos = pl.positionSec(), dur = pl.durationSec();
        bar(8, 80, W() - 16, 4, dur ? (float)pos / dur : 0.0f, C_ACCENT);
        String posS = fmtTime(pos), durS = fmtTime(dur);
        font::text(m_buf, 8, 92, posS.c_str(), C_DIM);
        font::text(m_buf, W() - 8 - font::width(durS.c_str()), 92, durS.c_str(), C_DIM);
    }

    if (pl.isPaused()) {
        font::text(m_buf, (W() - font::width("PAUSED", 2)) / 2, 110, "PAUSED", C_WARN, 2);
    } else if (!m_radio && pl.isShuffled()) {
        font::text(m_buf, (W() - font::width("SHUFFLE", 2)) / 2, 110, "SHUFFLE", C_ACCENT, 2);
    }

    // volume: 20 thin left-aligned stripes (volume changes in steps of 5, so one
    // strip per UP press), narrow at the bottom widening to the top. Lit strips:
    // green up to ~2/3, then yellow, then red.
    const int16_t baseY = H() - 28, volX = 6;
    const int16_t minW = 2, maxW = 10, stH = 1, stGap = 3;
    const int steps = pl.volume100() / 5; // 0..20 lit strips
    for (int i = 0; i < 20; i++) {
        int16_t w = (int16_t)((uint32_t)(minW + (maxW - minW) * i / 19));
        int16_t y = baseY - i * (stH + stGap);
        uint16_t c = C_VOL_OFF;
        if (i < steps) c = (i < 13) ? C_GREEN : (i < 16 ? C_YELLOW : C_RED);
        m_buf->fillRect(volX, y, w, stH, c);
    }

    // spectrum: 15 bars to the right of the volume stripes, sharing their baseline
    const int16_t maxH = 110, barW = 10, gap = 3, x0 = 39;
    uint8_t bands[MusicPlayer::SPECTRUM_BANDS] = {};
    uint8_t peaks[MusicPlayer::SPECTRUM_BANDS] = {};
    if (pl.isPlaying() && !pl.isPaused()) pl.getSpectrum(bands, peaks);
    for (size_t b = 0; b < MusicPlayer::SPECTRUM_BANDS; b++) {
        int16_t x = x0 + (int16_t)b * (barW + gap);
        int16_t h = (int16_t)((uint32_t)bands[b] * maxH / 255);
        if (h > 0) {
            m_buf->fillRect(x, baseY - h, barW, h, C_ACCENT);
        } else {
            m_buf->fillRect(x, baseY - 1, barW, 1, 0x162B);
        }
        int16_t ph = (int16_t)((uint32_t)peaks[b] * maxH / 255);
        if (ph > 0) m_buf->fillRect(x, baseY - ph, barW, 2, C_FG);
    }

    footer(m_radio ? "A:pause < >:station B:exit" : "A:pause  < >:track  B:back C:shuffle");
}

Action PlayScreen::update(MusicPlayer& pl) {
    Buttons::State a = m_btns->get(Buttons::A);
    Buttons::State b = m_btns->get(Buttons::B);
    Buttons::State left = m_btns->get(Buttons::LEFT);
    Buttons::State right = m_btns->get(Buttons::RIGHT);
    Buttons::State up = m_btns->get(Buttons::UP);
    Buttons::State down = m_btns->get(Buttons::DOWN);
    Buttons::State x = m_btns->get(Buttons::C); // physical "X" button (SDK key C)

    Action action = Action::NONE;
    if (a.justPressed) action = Action::PLAY_PAUSE;
    else if (b.justPressed) action = Action::STOP;
    else if (left.justPressed) action = Action::PREV;
    else if (right.justPressed) action = Action::NEXT;
    else if (up.justPressed) action = Action::VOL_UP;
    else if (down.justPressed) action = Action::VOL_DOWN;
    else if (!m_radio && x.justPressed) action = Action::SHUFFLE;

    // redraw on action, or periodically (fast while playing: spectrum + progress)
    bool periodic = (millis() - m_lastDrawMs) >= (pl.isPlaying() && !pl.isPaused() ? 100 : 500);
    if (m_dirty || action != Action::NONE || periodic) {
        draw(pl);
        m_buf->flush();
        m_dirty = false;
        m_lastDrawMs = millis();
    }
    return action;
}

// ---------------------------------------------------------------- Menu

void MenuScreen::draw() {
    static const char* const kItems[kItemCount] = {"MUSIC", "WEB RADIO", "ABOUT"};
    m_buf->fillScreen(C_BG);
    header("LILKA BOOMBOX v0.1");
    for (int i = 0; i < kItemCount; i++) {
        int16_t y = 64 + i * 48;
        if (i == m_cursor) m_buf->fillRect(0, y, W(), 28, C_SEL);
        font::text(m_buf, 10, y + 8, kItems[i], i == m_cursor ? C_SEL_TEXT : C_FG, 2);
    }
    footer("UP/DOWN:move  A:select");
}

Action MenuScreen::update() {
    Buttons::State up = m_btns->get(Buttons::UP);
    Buttons::State down = m_btns->get(Buttons::DOWN);
    Buttons::State a = m_btns->get(Buttons::A);

    if (up.pressed && (up.justPressed || millis() - m_lastMove > 180)) {
        if (m_cursor > 0) { m_cursor--; m_dirty = true; }
        m_lastMove = millis();
    }
    if (down.pressed && (down.justPressed || millis() - m_lastMove > 180)) {
        if (m_cursor < kItemCount - 1) { m_cursor++; m_dirty = true; }
        m_lastMove = millis();
    }
    if (!up.pressed && !down.pressed) m_lastMove = 0;

    Action action = Action::NONE;
    if (a.justPressed) {
        m_selected = m_cursor;
        action = Action::ENTER;
    }

    if (m_dirty || action != Action::NONE) {
        draw();
        m_buf->flush();
        m_dirty = false;
    }
    return action;
}

// ---------------------------------------------------------------- About

void AboutScreen::draw() {
    struct Row { int16_t y; const char* text; uint16_t color; };
    static const Row kRows[] = {
        {26, "MUSIC", C_ACCENT},
        {44, "/sd/music: songs, subfolders allowed", C_FG},
        {60, "mp3, m4a, aac, wma, flac, wav, ogg, mid", C_DIM},
        //{76, "M4A AAC WMA MID", C_DIM},
        {76, "WEB RADIO", C_ACCENT},
        {94, "/sd/webradio/*.url: stations", C_FG},
        {110, "name=filename, url=1st line", C_DIM},
        {130, "CREDITS", C_ACCENT},
        {148, "schreibfaul1/ESP32-audioI2S", C_DIM},
        {164, "Arduino_GFX, olikraus/U8g2 fonts", C_DIM},
        {180, "Lilka-dev community, Arduino", C_DIM},
        {198, "By github/vaddieg & qween", C_ACCENT},
    };
    m_buf->fillScreen(C_BG);
    header("ABOUT");
    for (const Row& r : kRows)
        font::text(m_buf, 10, r.y, r.text, r.color);
    footer("A/B: back");
}

Action AboutScreen::update() {
    Buttons::State a = m_btns->get(Buttons::A);
    Buttons::State b = m_btns->get(Buttons::B);
    Action action = Action::NONE;
    if (a.justPressed || b.justPressed) action = Action::BACK;

    if (m_dirty || action != Action::NONE) {
        draw();
        m_buf->flush();
        m_dirty = false;
    }
    return action;
}
