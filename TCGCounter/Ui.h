#pragma once
/* ============================================================================
 *  Ui — small shared drawing helpers and hit-testing. No state, no logic.
 * ==========================================================================*/
#include <stdint.h>
#include "Gfx.h"

struct Rect {
  int16_t x, y, w, h;
  bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
  int16_t cx() const { return (int16_t)(x + w / 2); }
  int16_t cy() const { return (int16_t)(y + h / 2); }
};

// Every helper draws into canvas `c`: the screen (gfx()) or an off-screen
// sprite (Commander cards are drawn off-screen, then pushed — rotated if needed).

// Rounded frame of thickness t drawn without touching the interior.
void uiRoundFrame(lgfx::LovyanGFX& c, int x, int y, int w, int h, int r, int t, uint16_t color);

// Filled rounded button with a centred text label.
void uiTextButton(lgfx::LovyanGFX& c, const Rect& r, const char* label, const lgfx::IFont* font,
                  uint16_t fill, uint16_t textColor, int radius = 10);

// Pixel-drawn icons (crisp, independent of fonts).
void uiMinus(lgfx::LovyanGFX& c, int cx, int cy, int len, int thick, uint16_t color);
void uiPlus(lgfx::LovyanGFX& c, int cx, int cy, int len, int thick, uint16_t color);
void uiHomeIcon(lgfx::LovyanGFX& c, int cx, int cy, uint16_t color, uint16_t cutoutColor);
void uiChevron(lgfx::LovyanGFX& c, int cx, int cy, int size, int thick, bool pointRight, uint16_t color);
