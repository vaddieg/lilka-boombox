// UTF-8 text (incl. Cyrillic) on the GFX canvas.
//
// The Keira OS / Lilka SDK trick: our Arduino_GFX build has native U8g2 font
// support (U8G2_FONT_SUPPORT, enabled automatically because U8g2lib.h is on
// the include path). The olikraus/U8g2 fork ships cyrillic bitmap fonts; we
// pass the font data straight to the canvas: setFont(u8g2_font_*) +
// setUTF8Print(true) + print() — rendered in any 565 color. No U8G2 object,
// no null display, no blit.
//
// Sizes (the `scale` parameter selects):
//   1 -> u8g2_font_6x13_t_cyrillic  (13 px, regular UI text)
//   2 -> u8g2_font_10x20_t_cyrillic (20 px, titles / menu — the SDK default)
#pragma once

#include <Arduino_GFX_Library.h>

namespace font {
/// One-time init: remember the canvas used for width()/fit() queries.
void begin(Arduino_GFX* gfx);

/// Draw UTF-8 text, top-left at (x, y). scale: 1 = 13 px, 2 = 20 px.
void text(Arduino_GFX* gfx, int16_t x, int16_t y, const char* utf8, uint16_t color,
          uint8_t scale = 1);

/// Width of the text in px (variable-width font).
int16_t width(const char* utf8, uint8_t scale = 1);

/// UTF-8-aware truncation to maxW px, appending '~' when truncated.
/// Returns a static buffer — use the result immediately.
const char* fit(const char* utf8, int16_t maxW, uint8_t scale = 1);

/// Returns the byte length (1..4) of the UTF-8 codepoint starting at p.
/// Validates continuation bytes to prevent buffer over-reads.
inline size_t charLen(const unsigned char* p) {
    if (!p || !*p) return 0;
    unsigned char c = *p;
    size_t len = 1;
    if (c < 0x80) len = 1;
    else if ((c & 0xE0) == 0xC0) len = 2;
    else if ((c & 0xF0) == 0xE0) len = 3;
    else if ((c & 0xF8) == 0xF0) len = 4;
    else return 1;
    for (size_t i = 1; i < len; i++) {
        if ((p[i] & 0xC0) != 0x80) return 1;
    }
    return len;
}

inline size_t charLen(const char* p) {
    return charLen((const unsigned char*)p);
}
}
