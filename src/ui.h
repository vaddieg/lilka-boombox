// Flicker-free UI screens: everything is drawn into the shared Arduino_Canvas and
// flushed only on change.

#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "hw.h"
#include "player.h"
#include "font.h"
#include "viz.h"

// Colors (565)
constexpr uint16_t C_BG       = 0x0000;
constexpr uint16_t C_FG       = 0xFFFF;
constexpr uint16_t C_ACCENT   = 0x07FF; // cyan (0x07E0 is green!)
constexpr uint16_t C_DIM      = 0x7BEF; // gray
constexpr uint16_t C_SEL      = 0x07FF; // selection bar
constexpr uint16_t C_SEL_TEXT = 0x0000;
constexpr uint16_t C_WARN     = 0xFD20; // yellow-orange
constexpr uint16_t C_GREEN    = 0x07E0;
constexpr uint16_t C_YELLOW   = 0xFD20;
constexpr uint16_t C_RED      = 0xF800;
constexpr uint16_t C_VOL_OFF  = 0x7BEF; // neutral gray (25% white), unlit volume strip

/// Seconds as "m:ss"
String fmtTime(uint32_t sec);

enum class Action {
    NONE = 0,
    ENTER,      // browser: open folder / start file
    BACK,       // browser: parent folder
    PLAY_PAUSE,
    NEXT,
    PREV,
    STOP,
    VOL_UP,
    VOL_DOWN,
    SHUFFLE,    // player: C toggles shuffle (reorder / restore order)
};

/// Base for a screen: draws into the shared canvas, owns no memory of its own.
class Screen {
public:
    Screen(Arduino_Canvas* buf, Arduino_GFX* disp, Buttons* btns)
        : m_buf(buf), m_disp(disp), m_btns(btns) {}

    /// Force a redraw on the next update() (e.g. periodic battery refresh).
    void invalidate() { m_dirty = true; }

protected:
    void header(const String& text);
    void footer(const String& text);
    void bar(int16_t x, int16_t y, int16_t w, int16_t h, float frac, uint16_t color);
    int16_t W() { return m_disp->width(); }
    int16_t H() { return m_disp->height(); }
    Arduino_Canvas* m_buf;
    Arduino_GFX* m_disp;
    Buttons* m_btns;
    bool m_dirty = true;
};

/// Folder browser (non-recursive). Shows ".." when below the root folder.
class BrowserScreen : public Screen {
public:
    using Screen::Screen;
    /// Root folder (default "/music"): ".." is hidden here.
    void setRoot(const String& root) { m_root = root; }

    void enterFolder(MusicPlayer& pl); // reset cursor, mark dirty
    Action update(MusicPlayer& pl);
    /// Entry index under the cursor (valid after an ENTER action).
    size_t selectedEntry() const { return m_selected; }

private:
    void draw(MusicPlayer& pl, bool canBack);
    String m_root = "/music";
    size_t m_cursor = 0;
    size_t m_scroll = 0;
    size_t m_selected = 0; // entry index of the cursor row
    uint32_t m_lastMove = 0;
};

/// Now-playing screen: file name, progress bar, volume bar, hints.
/// Reused for internet radio (setRadio): station/stream title instead of the
/// file name, no progress bar, L/R switches stations.
class PlayScreen : public Screen {
public:
    using Screen::Screen;
    void reset() { m_dirty = true; m_lastDrawMs = 0; }
    void setRadio(bool r) { m_radio = r; m_dirty = true; }
    Action update(MusicPlayer& pl);

private:
    void draw(MusicPlayer& pl);
    uint32_t m_lastDrawMs = 0;
    bool m_radio = false;
    viz::Mode m_viz = viz::Mode::BARS; // SELECT cycles; kept across tracks
    uint32_t m_vizFlashUntil = 0;      // name shown briefly after a switch

    // L/R: tap = prev/next (fired on release if never held long enough);
    // hold = seek in 5 s steps (music mode only).
    struct SeekKey { uint32_t downAt = 0; bool seeking = false; uint32_t lastStepMs = 0; };
    SeekKey m_left, m_right;
};

/// Main menu: "SD MUSIC" / "INTERNET RADIO" / "ABOUT".
class MenuScreen : public Screen {
public:
    using Screen::Screen;
    static constexpr int kItemCount = 4; // MUSIC / WEB RADIO / AIRPLAY / ABOUT
    void reset() { m_cursor = 0; m_selected = 0; m_dirty = true; }
    /// Index of the item under the cursor (valid after an ENTER action).
    int selected() const { return m_selected; }
    Action update();

private:
    void draw();
    int m_cursor = 0;
    int m_selected = 0;
    uint32_t m_lastMove = 0;
};

/// Static "About" info screen. A or B returns to the menu.
class AboutScreen : public Screen {
public:
    using Screen::Screen;
    Action update();

private:
    void draw();
};
