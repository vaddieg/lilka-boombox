// Music visualizers fed by the 15-band FFT spectrum.
//
// Every visualizer draws into the exact same view frame as the classic
// spectrum bars (x=39, baseline y=H()-38, 192x100 px), so switching between
// them never shifts the layout.

#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "player.h"

namespace viz {

struct Frame {
    int16_t x, y, w, h;
};

// Palette (all modes): vivid hue sweep red -> yellow -> green -> cyan ->
// blue (0..240 deg, HSL s=0.99 l=0.5; see rainbow() in viz.cpp).
enum class Mode : uint8_t {
    BARS,   // classic spectrum bars, rainbow across the bands
    WAVE,   // mirrored filled wave, rainbow along the wave
    ORBIT,  // rotating rainbow spokes around a bass-pulsing white core
    OFF,    // visualizer off (dim baseline only)
    COUNT
};

/// The shared view frame, sized for a screen of width/height w/h.
Frame frameFor(int16_t w, int16_t h);

const char* name(Mode m);
Mode next(Mode m);

/// Draw the visualizer into the canvas.
/// bands/peaks: SPECTRUM_BANDS values 0..255 (zeros when idle/paused).
/// t: millis(), used for time-based animation.
void draw(Arduino_GFX* buf, Mode m, const uint8_t* bands, const uint8_t* peaks,
          Frame f, uint32_t t);

} // namespace viz
