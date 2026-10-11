#pragma once
/* ============================================================================
 *  TapHalves — counter cards without − / + buttons: each card is split into
 *  an invisible left half (−1) and right half (+1), as its player sees it.
 *  The half under the finger turns red (−) or green (+); a short tap counts
 *  once when the finger lifts (so a swipe that starts on a card counts
 *  nothing), holding repeats like the old buttons did, and the counted half
 *  stays lit for TAP_FLASH_MS. Faint − / + marks show which side is which.
 *
 *  Used by the Commander / Standard life cards, the Riftbound / Lorcana
 *  score cards and the Pokemon damage area.
 * ==========================================================================*/
#include <stdint.h>
#include "Gfx.h"
#include "Theme.h"

constexpr uint16_t TAP_FLASH_PLUS  = rgb565(28, 124, 62);   // green
constexpr uint16_t TAP_FLASH_MINUS = rgb565(156, 34, 46);   // red
constexpr uint16_t TAP_HINT        = rgb565(88, 95, 114);   // the faint − / + marks

// One finger on one counter. `card` is whatever the screen numbers its
// counters by (player, side, ...); `dir` is -1 (left half) or +1 (right half).
struct TapCounter {
  int8_t   card = -1;  // under the finger (-1: none)
  int8_t   dir = 0;
  bool     repeated = false;
  int8_t   flashCard = -1;
  int8_t   flashDir = 0;
  uint32_t flashUntil = 0;

  void down(int8_t c, int8_t d) { card = c; dir = d; repeated = false; }
  void cancel() { card = -1; dir = 0; repeated = false; }
  // TouchRepeat while held: the direction to count now (0: none).
  int8_t repeat(uint32_t now);
  // TouchUp: the direction to count (0: none — not a tap, or it already repeated).
  int8_t up(bool tap, uint32_t now);
  // -1 / 0 / +1: which half of `c` to light up now.
  int8_t shown(int8_t c, uint32_t now) const;
  void flash(int8_t c, int8_t d, uint32_t now);
};

// Lights the left (dir -1) or right (+1) half of a w x h card at (x, y) with
// corner radius r. Draw it right after the card's background `bg`.
void drawTapFlash(lgfx::LovyanGFX& c, int x, int y, int w, int h, int r, int8_t dir, uint16_t bg);

// The faint − at (mx, my) and + at (px, py); the side being lit (`lit` -1 / +1)
// is drawn bright.
void drawTapHints(lgfx::LovyanGFX& c, int mx, int my, int px, int py, int len, int thick, int8_t lit);
