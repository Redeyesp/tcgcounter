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

// The same bars running the other way: horizontal on a standing (portrait) screen.
void menuIconUpright(int cx, int cy, uint16_t color) {
  auto& g = gfx();
  for (int k = -1; k <= 1; ++k) g.fillRect(cx - 1 + 6 * k, cy - 8, 3, 17, color);
}

// A gold coin with a rim.
void coinIcon(int cx, int cy, uint16_t rim) {
  auto& g = gfx();
  g.fillCircle(cx, cy, 10, theme::ACCENT);
  g.drawCircle(cx, cy, 7, rim);
  g.drawCircle(cx, cy, 6, rim);
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

// A die showing three pips.
void diceIcon(int cx, int cy, uint16_t color, uint16_t pip) {
  auto& g = gfx();
  g.fillRoundRect(cx - 9, cy - 9, 19, 19, 4, color);
  g.fillCircle(cx - 4, cy - 4, 2, pip);
  g.fillCircle(cx, cy, 2, pip);
  g.fillCircle(cx + 4, cy + 4, 2, pip);
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
    case HubIcon::Dice:    diceIcon(x, y, ink, fill); break;
    case HubIcon::MenuUpright: menuIconUpright(x, y, ink); break;
    case HubIcon::Coin:    coinIcon(x, y, pressed ? theme::TEXT_ON_ACCENT : rgb565(150, 110, 0)); break;
  }
}

namespace {

struct DieColors { uint16_t fill, edge, facet, text; };

DieColors dieColors(DieState state, uint16_t edgeColor) {
  switch (state) {
    case DieState::Winner:  // landed / won: gold
      return {theme::ACCENT, mix565(theme::ACCENT, theme::BG, 110), mix565(theme::ACCENT, theme::BG, 70),
              theme::TEXT_ON_ACCENT};
    case DieState::Tied:
      return {theme::BUTTON, theme::ACCENT, mix565(theme::ACCENT, theme::BUTTON, 150), theme::ACCENT};
    case DieState::Out:
      return {theme::PANEL, theme::PANEL_EDGE, mix565(theme::PANEL_EDGE, theme::PANEL, 110), theme::TEXT_DIM};
    default:  // Rolling
      return {theme::BUTTON, edgeColor, mix565(edgeColor, theme::BUTTON, 150), theme::TEXT};
  }
}

int px(float v) { return (int)lroundf(v); }

// Corner k of a regular polygon: `n` corners, the first straight up.
void corner(int cx, int cy, float r, int n, int k, float startDeg, int& x, int& y) {
  const float a = (startDeg + 360.0f * k / n) * 3.14159265f / 180.0f;
  x = cx + px(r * cosf(a));
  y = cy - px(r * sinf(a));
}

void fillPolygon(lgfx::LovyanGFX& c, int cx, int cy, const int* xs, const int* ys, int n, uint16_t color) {
  for (int k = 0; k < n; ++k) c.fillTriangle(cx, cy, xs[k], ys[k], xs[(k + 1) % n], ys[(k + 1) % n], color);
}

void outlinePolygon(lgfx::LovyanGFX& c, const int* xs, const int* ys, int n, int thick, uint16_t color) {
  for (int k = 0; k < n; ++k) uiThickLine(c, xs[k], ys[k], xs[(k + 1) % n], ys[(k + 1) % n], thick, color);
}

void dieNumber(lgfx::LovyanGFX& c, int cx, int cy, int r, uint8_t value, uint16_t color) {
  if (value == 0) return;
  char num[4];
  snprintf(num, sizeof(num), "%u", (unsigned)value);
  c.setFont(r >= 42 ? theme::fontHuge() : r >= 30 ? theme::fontTitle() : theme::fontButton());
  c.setTextDatum(lgfx::textdatum_t::middle_center);
  c.setTextColor(color);
  c.drawString(num, cx, cy);
  c.drawString(num, cx + 1, cy);
}

}  // namespace

void drawDie(lgfx::LovyanGFX& c, int cx, int cy, int r, uint8_t sides, DieState state,
             uint16_t edgeColor, uint8_t value) {
  const DieColors k = dieColors(state, edgeColor);
  const int edgeT = r >= 30 ? 3 : 2;
  int xs[6], ys[6];
  switch (sides) {
    case 4: {  // triangle seen from above, three faces meeting in the middle
      const int R = r * 6 / 5;      // a triangle needs a bigger radius to look as big
      const int ccy = cy + R / 4;   // centroid lowered: the triangle sits centred in its box
      for (int i = 0; i < 3; ++i) corner(cx, ccy, (float)R, 3, i, 90.0f, xs[i], ys[i]);
      c.fillTriangle(xs[0], ys[0], xs[1], ys[1], xs[2], ys[2], k.fill);
      for (int i = 0; i < 3; ++i) {  // ridges from the corners, stopping short of the number
        int ix, iy;
        corner(cx, ccy, R * 0.45f, 3, i, 90.0f, ix, iy);
        uiThickLine(c, xs[i], ys[i], ix, iy, 2, k.facet);
      }
      outlinePolygon(c, xs, ys, 3, edgeT, k.edge);
      dieNumber(c, cx, ccy + R / 10, r, value, k.text);
      return;
    }
    case 6: {  // cube face
      const int half = r * 4 / 5, rad = r / 5;
      c.fillRoundRect(cx - half, cy - half, 2 * half, 2 * half, rad, k.fill);
      if (r >= 30) c.drawRoundRect(cx - half + 6, cy - half + 6, 2 * half - 12, 2 * half - 12, rad, k.facet);
      uiRoundFrame(c, cx - half, cy - half, 2 * half, 2 * half, rad, edgeT, k.edge);
      dieNumber(c, cx, cy + 1, r, value, k.text);
      return;
    }
    case 8: {  // diamond: two faces meeting at the equator
      const int w = r * 17 / 20;
      xs[0] = cx;     ys[0] = cy - r;
      xs[1] = cx + w; ys[1] = cy;
      xs[2] = cx;     ys[2] = cy + r;
      xs[3] = cx - w; ys[3] = cy;
      fillPolygon(c, cx, cy, xs, ys, 4, k.fill);
      const int gap = r / 2;  // equator line, broken where the number sits
      uiThickLine(c, cx - w, cy, cx - gap, cy, 2, k.facet);
      uiThickLine(c, cx + gap, cy, cx + w, cy, 2, k.facet);
      outlinePolygon(c, xs, ys, 4, edgeT, k.edge);
      dieNumber(c, cx, cy, r, value, k.text);
      return;
    }
    case 12: {  // pentagon outline, pentagon face in the middle
      int ix[5], iy[5];
      for (int i = 0; i < 5; ++i) {
        corner(cx, cy, r * 1.05f, 5, i, 90.0f, xs[i], ys[i]);
        corner(cx, cy, r * 0.62f, 5, i, 90.0f, ix[i], iy[i]);
      }
      fillPolygon(c, cx, cy, xs, ys, 5, k.fill);
      for (int i = 0; i < 5; ++i) uiThickLine(c, ix[i], iy[i], xs[i], ys[i], 2, k.facet);
      outlinePolygon(c, ix, iy, 5, 2, k.facet);
      outlinePolygon(c, xs, ys, 5, edgeT, k.edge);
      dieNumber(c, cx, cy + r / 12, r, value, k.text);
      return;
    }
    default:
      break;
  }
  // D20: pointy-top hexagon = the outline; a triangle in the middle = the face
  // towards you; lines from its corners to the outline = the side faces.
  int tx[3], ty[3];
  for (int i = 0; i < 6; ++i) {
    corner(cx, cy, (float)r, 6, i, 90.0f, xs[i], ys[i]);
    if (i % 2 == 0) corner(cx, cy, r * 0.6f, 6, i, 90.0f, tx[i / 2], ty[i / 2]);
  }
  fillPolygon(c, cx, cy, xs, ys, 6, k.fill);
  for (int i = 0; i < 3; ++i) {
    const int v = 2 * i;  // hexagon corner straight out from this triangle corner
    uiThickLine(c, tx[i], ty[i], xs[v], ys[v], 2, k.facet);
    uiThickLine(c, tx[i], ty[i], xs[(v + 1) % 6], ys[(v + 1) % 6], 2, k.facet);
    uiThickLine(c, tx[i], ty[i], xs[(v + 5) % 6], ys[(v + 5) % 6], 2, k.facet);
    uiThickLine(c, tx[i], ty[i], tx[(i + 1) % 3], ty[(i + 1) % 3], 2, k.facet);
  }
  outlinePolygon(c, xs, ys, 6, edgeT, k.edge);
  dieNumber(c, cx, cy + r / 10, r, value, k.text);  // the face is wider below its centre
}

void drawD20(lgfx::LovyanGFX& c, int cx, int cy, int r, DieState state, uint16_t playerColor, uint8_t value) {
  drawDie(c, cx, cy, r, 20, state, playerColor, value);
}
