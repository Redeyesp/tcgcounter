#pragma once
/* ============================================================================
 *  Theme.h — colours and fonts. Purely visual; no logic, no pins.
 *  Colours are RGB565 (uint16_t), which LovyanGFX takes directly.
 * ==========================================================================*/
#include <stdint.h>
#include "Gfx.h"

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

namespace theme {
// ---- Base palette (dark, high contrast) ----
constexpr uint16_t BG          = rgb565(0, 0, 0);
constexpr uint16_t PANEL       = rgb565(22, 24, 30);
constexpr uint16_t PANEL_EDGE  = rgb565(58, 63, 76);
constexpr uint16_t TEXT        = rgb565(245, 245, 245);
constexpr uint16_t TEXT_DIM    = rgb565(135, 141, 156);
constexpr uint16_t TEXT_ON_ACCENT = rgb565(10, 10, 12);
constexpr uint16_t BUTTON      = rgb565(44, 48, 59);
constexpr uint16_t BUTTON_DOWN = rgb565(92, 99, 118);
constexpr uint16_t ACCENT      = rgb565(255, 196, 0);   // focus / pressed highlight

// ---- One identifying colour per player (P1..P4) ----
constexpr uint16_t PLAYER[4] = {
  rgb565(255, 92, 92),    // P1 red
  rgb565(70, 160, 255),   // P2 blue
  rgb565(70, 214, 120),   // P3 green
  rgb565(196, 120, 255),  // P4 purple
};

// ---- Fonts (all built into LovyanGFX) ----
inline const lgfx::IFont* fontLife()   { return &lgfx::fonts::Font6; }               // 48 px digits
constexpr bool LIFE_FAUX_BOLD = true;  // draw digits twice, 1 px apart: thicker strokes, still crisp
inline const lgfx::IFont* fontLabel()  { return &lgfx::fonts::FreeSansBold9pt7b; }
inline const lgfx::IFont* fontButton() { return &lgfx::fonts::FreeSansBold12pt7b; }
inline const lgfx::IFont* fontTitle()  { return &lgfx::fonts::FreeSansBold18pt7b; }
inline const lgfx::IFont* fontBody()   { return &lgfx::fonts::FreeSans12pt7b; }
inline const lgfx::IFont* fontSmall()  { return &lgfx::fonts::FreeSans9pt7b; }
}  // namespace theme
