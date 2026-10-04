#include "Ui.h"
#include <math.h>

void uiRoundFrame(lgfx::LovyanGFX& g, int x, int y, int w, int h, int r, int t, uint16_t color) {
  // Straight edges
  g.fillRect(x + r, y, w - 2 * r, t, color);
  g.fillRect(x + r, y + h - t, w - 2 * r, t, color);
  g.fillRect(x, y + r, t, h - 2 * r, color);
  g.fillRect(x + w - t, y + r, t, h - 2 * r, color);
  // Corners: solid quarter rings (no pixel gaps, unlike nested drawRoundRect)
  const int ri = (r - t + 1 > 0) ? r - t + 1 : 0;
  g.fillArc(x + r,         y + r,         ri, r, 180, 270, color);
  g.fillArc(x + w - r - 1, y + r,         ri, r, 270, 360, color);
  g.fillArc(x + w - r - 1, y + h - r - 1, ri, r,   0,  90, color);
  g.fillArc(x + r,         y + h - r - 1, ri, r,  90, 180, color);
}

void uiTextButton(lgfx::LovyanGFX& g, const Rect& r, const char* label, const lgfx::IFont* font,
                  uint16_t fill, uint16_t textColor, int radius) {
  g.fillRoundRect(r.x, r.y, r.w, r.h, radius, fill);
  g.setFont(font);
  g.setTextDatum(lgfx::textdatum_t::middle_center);
  g.setTextColor(textColor);
  g.drawString(label, r.cx(), r.cy());
}

void uiMinus(lgfx::LovyanGFX& g, int cx, int cy, int len, int thick, uint16_t color) {
  g.fillRect(cx - len / 2, cy - thick / 2, len, thick, color);
}

void uiPlus(lgfx::LovyanGFX& g, int cx, int cy, int len, int thick, uint16_t color) {
  g.fillRect(cx - len / 2, cy - thick / 2, len, thick, color);
  g.fillRect(cx - thick / 2, cy - len / 2, thick, len, color);
}

void uiHomeIcon(lgfx::LovyanGFX& g, int cx, int cy, uint16_t color, uint16_t cutoutColor) {
  g.fillTriangle(cx - 11, cy - 1, cx + 11, cy - 1, cx, cy - 11, color);  // roof
  g.fillRect(cx - 7, cy - 2, 15, 12, color);                             // walls
  g.fillRect(cx - 2, cy + 3, 5, 7, cutoutColor);                         // door
}

// Thick line as a filled quad (two triangles), no anti-aliasing / no reads.
static void thickLine(lgfx::LovyanGFX& g, int x0, int y0, int x1, int y1, int thick, uint16_t color) {
  const float dx = (float)(x1 - x0), dy = (float)(y1 - y0);
  const float len = sqrtf(dx * dx + dy * dy);
  if (len < 0.5f) return;
  const float nx = -dy / len * thick * 0.5f, ny = dx / len * thick * 0.5f;
  const int ax = (int)lroundf(x0 + nx), ay = (int)lroundf(y0 + ny);
  const int bx = (int)lroundf(x0 - nx), by = (int)lroundf(y0 - ny);
  const int cx = (int)lroundf(x1 - nx), cy = (int)lroundf(y1 - ny);
  const int ex = (int)lroundf(x1 + nx), ey = (int)lroundf(y1 + ny);
  g.fillTriangle(ax, ay, bx, by, cx, cy, color);
  g.fillTriangle(ax, ay, cx, cy, ex, ey, color);
}

void uiChevron(lgfx::LovyanGFX& g, int cx, int cy, int size, int thick, bool pointRight, uint16_t color) {
  const int h = size / 2;
  const int dir = pointRight ? 1 : -1;
  const int tipX = cx + dir * h / 2, backX = cx - dir * h / 2;
  thickLine(g, backX, cy - h, tipX, cy, thick, color);
  thickLine(g, tipX, cy, backX, cy + h, thick, color);
  g.fillCircle(tipX, cy, thick / 2, color);  // round the joint
}
