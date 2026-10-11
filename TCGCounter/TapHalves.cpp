#include "TapHalves.h"
#include "Ui.h"
#include "Config.h"

int8_t TapCounter::repeat(uint32_t now) {
  if (card < 0 || dir == 0) return 0;
  repeated = true;
  flash(card, dir, now);
  return dir;
}

int8_t TapCounter::up(bool tap, uint32_t now) {
  const int8_t d = (card >= 0 && tap && !repeated) ? dir : 0;
  if (d) flash(card, d, now);
  cancel();
  return d;
}

int8_t TapCounter::shown(int8_t c, uint32_t now) const {
  if (card >= 0 && card == c) return dir;
  if (flashCard == c && (int32_t)(flashUntil - now) > 0) return flashDir;
  return 0;
}

void TapCounter::flash(int8_t c, int8_t d, uint32_t now) {
  flashCard = c;
  flashDir = d;
  flashUntil = now + TAP_FLASH_MS;
}

void drawTapFlash(lgfx::LovyanGFX& c, int x, int y, int w, int h, int r, int8_t dir, uint16_t bg) {
  if (dir == 0) return;
  const int mid = w / 2;
  if (dir > 0) {  // right half: rounded on the outside, straight down the middle
    c.fillRoundRect(x + mid - r, y, w - mid + r, h, r, TAP_FLASH_PLUS);
    c.fillRect(x + mid - r, y, r, h, bg);
  } else {
    c.fillRoundRect(x, y, mid + r, h, r, TAP_FLASH_MINUS);
    c.fillRect(x + mid, y, r, h, bg);
  }
}

void drawTapHints(lgfx::LovyanGFX& c, int mx, int my, int px, int py, int len, int thick, int8_t lit) {
  uiMinus(c, mx, my, len, thick, lit < 0 ? theme::TEXT : TAP_HINT);
  uiPlus(c, px, py, len, thick, lit > 0 ? theme::TEXT : TAP_HINT);
}
