// LiPo battery level via ADC — same scheme as the official Lilka SDK.
//
// The battery is wired to the ADC through a 33 kOhm / 100 kOhm voltage
// divider. The ADC reads 0..3.1 V (11 dB attenuation, 12-bit), so the max
// measurable battery voltage is 3.1 / (100/133) = 4.123 V. Anything above
// counts as 100%. Levels interpolate 3.2 V -> 0% ... 4.123 V -> 100%.
#pragma once

namespace battery {
void begin();
/// 0..100, or -1 when no battery is connected. Cached; re-measured every 5 s.
int level();
}
