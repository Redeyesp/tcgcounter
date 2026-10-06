#include "TableDraw.h"
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "Theme.h"
#include "Ui.h"

namespace {

constexpr uint32_t CARD_BUF_PIXELS = 20000;  // 40 KB: a whole 159x119 card

struct CardBuffer { uint16_t* px; uint32_t pixels; };

const CardBuffer& cardBuffer() {
  static CardBuffer b = [] {
    CardBuffer r = {nullptr, 0};
    // Low on memory? Smaller buffers just mean more bands.
    for (uint32_t n = CARD_BUF_PIXELS; n >= 320 && !r.px; n /= 2) {
      r.px = static_cast<uint16_t*>(malloc(n * sizeof(uint16_t)));
      if (r.px) r.pixels = n;
    }
    return r;
  }();
  return b;
}

void menuIcon(int cx, int cy, uint16_t color) {
  auto& g = gfx();
  for (int k = -1; k <= 1; ++k) g.fillRect(cx - 8, cy - 1 + 6 * k, 17, 3, color);
}

void closeIcon(int cx, int cy, uint16_t color) {
  auto& g = gfx();
  for (int t = -1; t <= 1; ++t) {  // 3 px thick X
    g.drawLine(cx - 7 + t, cy - 7, cx + 7 + t, cy + 7, color);
    g.drawLine(cx - 7 + t, cy + 7, cx + 7 + t, cy - 7, color);
  }
}

// "H" for high roll.
void highRollIcon(int cx, int cy, uint16_t color) {
  auto& g = gfx();
  g.setFont(theme::fontButton());
  g.setTextDatum(lgfx::textdatum_t::middle_center);
  g.setTextColor(color);
  g.drawString("H", cx, cy + 1);
  g.drawString("H", cx + 1, cy + 1);  // a little bolder
}

// RGB565 blend: t = 0 -> a, 255 -> b
uint16_t mix565(uint16_t a, uint16_t b, uint8_t t) {
  const int ar = a >> 11, ag = (a >> 5) & 63, ab = a & 31;
  const int br = b >> 11, bg = (b >> 5) & 63, bb = b & 31;
  const int r = ar + (br - ar) * t / 255, gg = ag + (bg - ag) * t / 255, bl = ab + (bb - ab) * t / 255;
  return (uint16_t)((r << 11) | (gg << 5) | bl);
}

// Circular arrow: a 3 px ring with a gap at the top right, arrow head at the
// gap pointing clockwise. Readable from any side of the table.
void restartIcon(int cx, int cy, uint16_t color) {
  auto& g = gfx();
  const int r0 = 6, r1 = 9;
  g.fillArc(cx, cy, r0, r1, 0, 290, color);  // 0 deg = 3 o'clock, clockwise
  const float a = 290.0f * 3.14159265f / 180.0f;
  const float ux = cosf(a), uy = sinf(a);  // radial direction at the arc's end
  const float tx = -uy, ty = ux;           // clockwise tangent
  const float mx = cx + 7.5f * ux, my = cy + 7.5f * uy;  // middle of the ring
  const int tipX = (int)lroundf(mx + 6.0f * tx), tipY = (int)lroundf(my + 6.0f * ty);
  const int b1x = (int)lroundf(mx + 5.5f * ux), b1y = (int)lroundf(my + 5.5f * uy);
  const int b2x = (int)lroundf(mx - 5.5f * ux), b2y = (int)lroundf(my - 5.5f * uy);
  g.fillTriangle(tipX, tipY, b1x, b1y, b2x, b2y, color);
}

}  // namespace

void drawSeatCard(const Seat& s, CardPainter paint, const void* ctx) {
  const CardBuffer& buf = cardBuffer();
  if (!buf.px) return;  // no memory at all: nothing sensible to draw

  static lgfx::LGFX_Sprite spr(&gfx());
  const int w = s.r.w, h = s.r.h;
  const int maxRows = (int)(buf.pixels / (uint32_t)w);
  const int bands = (h + maxRows - 1) / maxRows;
  const int bandH = (h + bands - 1) / bands;

  // pushSprite() would send the buffer by DMA in the background, and drawing
  // the next band/card into it while that runs mixes cards up on the screen.
  // So: nothing may still be in flight, and every push is a blocking transfer.
  gfx().waitDMA();
  for (int b0 = 0; b0 < h; b0 += bandH) {
    const int bh = (h - b0 < bandH) ? h - b0 : bandH;
    spr.setBuffer(buf.px, w, bh, 16);
    spr.setRotation((uint8_t)s.side);  // turn the drawing to face the seat
    // Card position (as its player sees it) of this band's first pixel:
    int ax, ay, bx, by;
    seatToLocal(s, s.r.x, s.r.y + b0, ax, ay);
    seatToLocal(s, s.r.x + w - 1, s.r.y + b0 + bh - 1, bx, by);
    paint(spr, -(ax < bx ? ax : bx), -(ay < by ? ay : by), ctx);
    gfx().pushImage(s.r.x, s.r.y + b0, w, bh, static_cast<const lgfx::swap565_t*>((void*)buf.px));
  }
}

void drawHubButton(int x, int y, HubIcon icon, bool pressed) {
  auto& g = gfx();
  const uint16_t fill = pressed ? theme::ACCENT : theme::BUTTON;
  const uint16_t ink  = pressed ? theme::TEXT_ON_ACCENT : theme::TEXT;
  g.fillCircle(x, y, HUB_R + HUB_MOAT, theme::BG);  // dark moat separates it from the cards
  g.fillCircle(x, y, HUB_R, fill);
  g.drawCircle(x, y, HUB_R, theme::PANEL_EDGE);
  switch (icon) {
    case HubIcon::Menu:    menuIcon(x, y, ink); break;
    case HubIcon::Close:   closeIcon(x, y, ink); break;
    case HubIcon::Home:    uiHomeIcon(g, x, y + 1, ink, fill); break;
    case HubIcon::Restart: restartIcon(x, y, ink); break;
    case HubIcon::HighRoll: highRollIcon(x, y, ink); break;
  }
}

void drawD20(lgfx::LovyanGFX& c, int cx, int cy, int r, DieState state, uint16_t playerColor, uint8_t value) {
  uint16_t fill, edge, facet, text;
  switch (state) {
    case DieState::Winner:
      fill = theme::ACCENT; edge = mix565(theme::ACCENT, theme::BG, 110);
      facet = mix565(theme::ACCENT, theme::BG, 70); text = theme::TEXT_ON_ACCENT;
      break;
    case DieState::Tied:
      fill = theme::BUTTON; edge = theme::ACCENT;
      facet = mix565(theme::ACCENT, theme::BUTTON, 150); text = theme::ACCENT;
      break;
    case DieState::Out:
      fill = theme::PANEL; edge = theme::PANEL_EDGE;
      facet = mix565(theme::PANEL_EDGE, theme::PANEL, 110); text = theme::TEXT_DIM;
      break;
    default:  // Rolling
      fill = theme::BUTTON; edge = playerColor;
      facet = mix565(playerColor, theme::BUTTON, 150); text = theme::TEXT;
      break;
  }
  // Pointy-top hexagon = the die's outline; a triangle in the middle = the
  // face towards you; lines from its corners to the outline = the side faces.
  int hx[6], hy[6], tx[3], ty[3];
  const float ri = r * 0.6f;
  for (int k = 0; k < 6; ++k) {
    const float a = (90.0f + 60.0f * k) * 3.14159265f / 180.0f;
    hx[k] = cx + (int)lroundf(r * cosf(a));
    hy[k] = cy - (int)lroundf(r * sinf(a));
    if (k % 2 == 0) {
      tx[k / 2] = cx + (int)lroundf(ri * cosf(a));
      ty[k / 2] = cy - (int)lroundf(ri * sinf(a));
    }
  }
  for (int k = 0; k < 6; ++k) c.fillTriangle(cx, cy, hx[k], hy[k], hx[(k + 1) % 6], hy[(k + 1) % 6], fill);
  for (int k = 0; k < 3; ++k) {
    const int v = 2 * k;  // hexagon corner straight out from this triangle corner
    uiThickLine(c, tx[k], ty[k], hx[v], hy[v], 2, facet);
    uiThickLine(c, tx[k], ty[k], hx[(v + 1) % 6], hy[(v + 1) % 6], 2, facet);
    uiThickLine(c, tx[k], ty[k], hx[(v + 5) % 6], hy[(v + 5) % 6], 2, facet);
    uiThickLine(c, tx[k], ty[k], tx[(k + 1) % 3], ty[(k + 1) % 3], 2, facet);
  }
  for (int k = 0; k < 6; ++k) uiThickLine(c, hx[k], hy[k], hx[(k + 1) % 6], hy[(k + 1) % 6], 3, edge);

  char num[4];
  snprintf(num, sizeof(num), "%u", (unsigned)value);
  c.setFont(r >= 30 ? theme::fontTitle() : theme::fontButton());
  c.setTextDatum(lgfx::textdatum_t::middle_center);
  c.setTextColor(text);
  const int ny = cy + r / 10;  // the face triangle is wider below its centre
  c.drawString(num, cx, ny);
  c.drawString(num, cx + 1, ny);
}
