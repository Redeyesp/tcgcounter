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
constexpr uint16_t OUT_PANEL   = rgb565(96, 14, 22);    // card background of a player who is OUT
constexpr uint16_t DANGER      = rgb565(255, 80, 80);   // lethal values (21+ commander damage)
constexpr uint16_t CMD_PANEL   = rgb565(30, 32, 68);    // opponent cards in commander damage mode
constexpr uint16_t WIN_PANEL   = rgb565(62, 48, 6);     // card of a player who reached the target (Riftbound/Lorcana)
constexpr uint16_t DANGER_FILL = rgb565(176, 36, 48);   // button that throws something away (START / RESTART)
constexpr uint16_t DANGER_FILL_DOWN = rgb565(120, 24, 32);

// ---- One identifying colour per player (P1..P6) ----
constexpr uint16_t PLAYER[6] = {
  rgb565(255, 92, 92),    // P1 red
  rgb565(70, 160, 255),   // P2 blue
  rgb565(70, 214, 120),   // P3 green
  rgb565(196, 120, 255),  // P4 purple
  rgb565(255, 145, 40),   // P5 orange
  rgb565(240, 225, 70),   // P6 yellow
};

// ---- Fonts (all built into LovyanGFX) ----
constexpr bool LIFE_FAUX_BOLD = true;  // draw digits twice, 1 px apart: thicker strokes, still crisp

// Big numbers (life, commander damage): the largest of these that fits the
// card is used. `top`/`height` = rows the digits really cover below a
// top_center text datum, so they can be centred exactly.
struct NumberFont { const lgfx::IFont* font; int8_t top; uint8_t height; };
inline const NumberFont* numberFonts(uint8_t& count) {
  static const NumberFont FONTS[] = {
    {&lgfx::fonts::Font8, 4, 70},  // 2-player cards
    {&lgfx::fonts::Font6, 1, 36},  // standard cards
    {&lgfx::fonts::Font4, 2, 17},  // fallback for very narrow cards
  };
  count = sizeof(FONTS) / sizeof(FONTS[0]);
  return FONTS;
}
inline const lgfx::IFont* fontLabel()  { return &lgfx::fonts::FreeSansBold9pt7b; }
inline const lgfx::IFont* fontButton() { return &lgfx::fonts::FreeSansBold12pt7b; }
inline const lgfx::IFont* fontTitle()  { return &lgfx::fonts::FreeSansBold18pt7b; }
inline const lgfx::IFont* fontHuge()   { return &lgfx::fonts::FreeSansBold24pt7b; }
inline const lgfx::IFont* fontBody()   { return &lgfx::fonts::FreeSans12pt7b; }
inline const lgfx::IFont* fontSmall()  { return &lgfx::fonts::FreeSans9pt7b; }
}  // namespace theme
