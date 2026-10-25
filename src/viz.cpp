#include "viz.h"

namespace viz {

// Shared view frame of the classic spectrum bars: 15 bars, 10 px wide,
// 3 px gap, starting at x=39 (right of the volume stripes), 100 px tall,
// baseline at y = H()-38.
Frame frameFor(int16_t w, int16_t h) {
    (void)w;
    const int16_t x = 39, barW = 10, gap = 3, n = MusicPlayer::SPECTRUM_BANDS;
    const int16_t fw = n * (barW + gap) - gap;
    const int16_t fh = 100;
    return {x, (int16_t)(h - 38 - fh), fw, fh};
}

const char* name(Mode m) {
    switch (m) {
        case Mode::BARS: return "BARS";
        case Mode::WAVE: return "WAVE";
        case Mode::ORBIT: return "ORBIT";
        default: return "OFF";
    }
}

Mode next(Mode m) {
    return (Mode)(((uint8_t)m + 1) % (uint8_t)Mode::COUNT);
}

namespace {

// Rainbow: t 0..1 -> hue 0..240 (red -> yellow -> green -> cyan -> blue);
// the last third of the wheel (blue -> violet -> magenta -> red) is dropped.
// HSL with 0.99 saturation and 0.5 lightness (vivid, colorFromHLS(hue, 0.99, 0.5)).
constexpr float kRainbowSat = 1.0f;
constexpr float kRainbowLit = 0.5f;
constexpr float kRainbowHueMax = 240.0f;

// float 0..1 -> 0..255, clamped (float32 can land at 254.99998 for 1.0)
static inline uint32_t r8(float v) {
    uint32_t c = (uint32_t)(v * 255.0f + 0.5f);
    return c > 255 ? 255u : c;
}

uint16_t rainbow(float t) {
    float h = fmodf(t * kRainbowHueMax, 360.0f);
    if (h < 0) h += 360.0f;
    const float c = (1.0f - fabsf(2.0f * kRainbowLit - 1.0f)) * kRainbowSat; // chroma
    const float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    const float m = kRainbowLit - c / 2.0f;             // lightness base
    float r1, g1, b1;
    if (h < 60)       { r1 = c; g1 = x; b1 = 0; }
    else if (h < 120) { r1 = x; g1 = c; b1 = 0; }
    else if (h < 180) { r1 = 0; g1 = c; b1 = x; }
    else if (h < 240) { r1 = 0; g1 = x; b1 = c; }
    else if (h < 300) { r1 = x; g1 = 0; b1 = c; }
    else              { r1 = c; g1 = 0; b1 = x; }
    // pack RGB565: red/blue are 5-bit fields (>>3), green is 6 bits (>>2)!
    return (uint16_t)(((r8(r1 + m) >> 3) << 11) | ((r8(g1 + m) >> 2) << 5) | (r8(b1 + m) >> 3));
}

// -- BARS: classic 15 spectrum bars, rainbow across the bands -------------
void drawBars(Arduino_GFX* buf, const uint8_t* bands, const uint8_t* peaks, Frame f) {
    const int16_t barW = 10, gap = 3, baseY = f.y + f.h;
    for (size_t b = 0; b < MusicPlayer::SPECTRUM_BANDS; b++) {
        int16_t x = f.x + (int16_t)b * (barW + gap);
        int16_t hgt = (int16_t)((uint32_t)bands[b] * f.h / 255);
        if (hgt > 0) // full rainbow across the bands
            buf->fillRect(x, baseY - hgt, barW, hgt, rainbow((float)b / MusicPlayer::SPECTRUM_BANDS));
        else
            buf->fillRect(x, baseY - 1, barW, 1, 0x162B);
        int16_t ph = (int16_t)((uint32_t)peaks[b] * f.h / 255);
        if (ph > 0) buf->fillRect(x, baseY - ph, barW, 2, 0xFFFF);
    }
}

// -- WAVE: mirrored filled wave; calm = blue, loud = red ------------------
void drawWave(Arduino_GFX* buf, const uint8_t* bands, Frame f, uint32_t t) {
    const int cy = f.y + f.h / 2;
    buf->fillRect(f.x, cy, f.w, 1, 0x162B); // dim center line
    const int maxAmp = f.h / 2 - 2;
    for (int px = 0; px < f.w; px++) {
        float bf = (float)px * MusicPlayer::SPECTRUM_BANDS / f.w;
        int i = (int)bf;
        if (i >= MusicPlayer::SPECTRUM_BANDS - 1) i = MusicPlayer::SPECTRUM_BANDS - 2;
        float fr = bf - i;
        float v = bands[i] * (1.0f - fr) + bands[i + 1] * fr;
        int amp = (int)(v / 255.0f * maxAmp);
        // gentle shimmer so a quiet track isn't dead flat
        if (amp > 0) amp += (int)(3.0f * sinf(px * 0.11f + t * 0.005f));
        if (amp < 0) amp = 0;
        if (amp == 0) continue;
        // rainbow along the wave
        buf->fillRect(f.x + px, cy - amp, 1, amp * 2 + 1, rainbow((float)px / f.w));
        buf->fillRect(f.x + px, cy - amp, 1, 1, 0xFFFF); // bright crests
    }
}

// -- ORBIT: rotating rainbow spokes around a bass-pulsing core ------------
void drawOrbit(Arduino_GFX* buf, const uint8_t* bands, Frame f, uint32_t t) {
    const int cx = f.x + f.w / 2, cy = f.y + f.h / 2;
    const uint32_t bass = (bands[0] + bands[1] + bands[2] + bands[3]) / 4;
    const int core = 5 + (int)((uint32_t)bass * 12 / 255);
    // outer ring color follows the bass level through the rainbow, white core
    buf->fillCircle(cx, cy, core + 2, rainbow((float)bass / 255.0f));
    buf->fillCircle(cx, cy, core, 0xFFFF);

    const int maxLen = f.h / 2 - core - 8;
    for (size_t b = 0; b < MusicPlayer::SPECTRUM_BANDS; b++) {
        float ang = (float)b * 2.0f * (float)M_PI / MusicPlayer::SPECTRUM_BANDS + t * 0.0004f;
        float ca = cosf(ang), sa = sinf(ang);
        int len = 4 + (int)((uint32_t)bands[b] * maxLen / 255);
        int x0 = cx + (int)(ca * (core + 2));
        int y0 = cy + (int)(sa * (core + 2));
        int x1 = cx + (int)(ca * (core + 2 + len));
        int y1 = cy + (int)(sa * (core + 2 + len));
        uint16_t c = rainbow((float)b / MusicPlayer::SPECTRUM_BANDS);
        buf->drawLine(x0, y0, x1, y1, c);
        buf->fillCircle(x1, y1, 1, c);
    }
}

// -- OFF -------------------------------------------------------------------
void drawOff(Arduino_GFX* buf, Frame f) {
    buf->fillRect(f.x, f.y + f.h - 1, f.w, 1, 0x162B);
}

} // namespace

void draw(Arduino_GFX* buf, Mode m, const uint8_t* bands, const uint8_t* peaks,
          Frame f, uint32_t t) {
    switch (m) {
        case Mode::BARS: drawBars(buf, bands, peaks, f); break;
        case Mode::WAVE: drawWave(buf, bands, f, t); break;
        case Mode::ORBIT: drawOrbit(buf, bands, f, t); break;
        default: drawOff(buf, f); break;
    }
}

} // namespace viz
