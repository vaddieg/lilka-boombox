#include "font.h"

#include <U8g2lib.h>  // u8g2 font data (olikraus fork: cyrillic fonts)
#include <cstring>

namespace {
// Regular UI text: 6x13. Titles/menu: 10x20 (the SDK's default font).
const uint8_t* fontData(uint8_t scale) {
    return (scale >= 2) ? u8g2_font_10x20_t_cyrillic : u8g2_font_6x13_t_cyrillic;
}

// u8g2 v1 font header (23 bytes): byte 13 = ascent (capital 'A' height).
// The SDK reads these the same way.
inline int16_t ascentOf(uint8_t scale) { return (int8_t)fontData(scale)[13]; }

Arduino_GFX* g_gfx = nullptr;

void prep(Arduino_GFX* g, uint8_t scale) {
    g->setFont(fontData(scale));
    g->setUTF8Print(true);
    g->setTextSize(1, 1);
    g->setTextWrap(false);  // we pre-truncate with fit(); no implicit wraps
}
}

namespace font {
void begin(Arduino_GFX* gfx) {
    g_gfx = gfx;
}

void text(Arduino_GFX* gfx, int16_t x, int16_t y, const char* s, uint16_t color,
          uint8_t scale) {
    prep(gfx, scale);
    gfx->setTextColor(color, color);  // bg == fg -> transparent glyph
    gfx->setCursor(x, y + ascentOf(scale));  // y = text top -> baseline
    gfx->print(s);
}

int16_t width(const char* s, uint8_t scale) {
    if (!g_gfx) return 0;
    prep(g_gfx, scale);
    int16_t x1, y1, w = 0, h;
    g_gfx->getTextBounds(s, 0, ascentOf(scale), &x1, &y1, (uint16_t*)&w, (uint16_t*)&h);
    return w;
}

const char* fit(const char* s, int16_t maxW, uint8_t scale) {
    static char buf[96];
    if (width(s, scale) <= maxW) return s;
    const unsigned char* p = (const unsigned char*)s;
    size_t n = 0;
    buf[0] = 0;
    while (*p) {
        size_t len = (*p < 0x80) ? 1 : ((*p < 0xE0) ? 2 : 3);
        if (n + len + 2 > sizeof(buf)) break;
        memcpy(buf + n, p, len);
        n += len;
        p += len;
        buf[n] = 0;
        if (width(buf, scale) + 10 > maxW) {  // keep room for '~'
            n -= len;
            p -= len;
            buf[n] = 0;
            break;
        }
    }
    buf[n++] = '~';
    buf[n] = 0;
    return buf;
}
}
