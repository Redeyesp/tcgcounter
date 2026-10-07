#include "KingdomsArt.h"
#include "GameState.h"
#include "Theme.h"
#include <math.h>

namespace {

// Shapes are laid out in a 100 x 100 box centred on (0, 0) and scaled.
struct Pen {
  lgfx::LovyanGFX& c;
  int cx, cy;
  float k;
  int X(float u) const { return cx + (int)lroundf(u * k); }
  int Y(float v) const { return cy + (int)lroundf(v * k); }
  int L(float d) const { const int v = (int)lroundf(d * k); return v < 1 ? 1 : v; }
  void tri(float x0, float y0, float x1, float y1, float x2, float y2, uint16_t col) const {
    c.fillTriangle(X(x0), Y(y0), X(x1), Y(y1), X(x2), Y(y2), col);
  }
  void box(float x, float y, float w, float h, float r, uint16_t col) const {
    c.fillRoundRect(X(x), Y(y), L(w), L(h), (int)lroundf(r * k), col);
  }
  void dot(float x, float y, float r, uint16_t col) const { c.fillCircle(X(x), Y(y), L(r), col); }
  void oval(float x, float y, float rx, float ry, uint16_t col) const { c.fillEllipse(X(x), Y(y), L(rx), L(ry), col); }
  void line(float x0, float y0, float x1, float y1, float w, uint16_t col) const {
    c.drawWideLine(X(x0), Y(y0), X(x1), Y(y1), w * k / 2, col);
  }
};

constexpr uint16_t GOLD      = rgb565(255, 196, 0);
constexpr uint16_t GOLD_DARK = rgb565(170, 112, 0);
constexpr uint16_t STEEL     = rgb565(214, 218, 228);
constexpr uint16_t STEEL_DARK= rgb565(120, 126, 140);
constexpr uint16_t LEATHER   = rgb565(122, 80, 44);
constexpr uint16_t BRASS     = rgb565(196, 150, 60);
constexpr uint16_t RUBY      = rgb565(232, 40, 64);
constexpr uint16_t SAPPHIRE  = rgb565(60, 120, 255);
constexpr uint16_t EMERALD   = rgb565(40, 200, 110);

struct RoleInfo { const char* name; const char* goal; uint16_t color; uint16_t tint; };
const RoleInfo ROLES[KINGDOMS_ROLE_COUNT] = {
  {"KING", "Reveal yourself when all eyes open. You start at 50 life and go first. "
           "Win: outlast everyone but your Knight.",
   GOLD, rgb565(54, 40, 4)},
  {"KNIGHT", "Keep it secret. Protect the King. Win: you and the King are the last ones left.",
   rgb565(70, 214, 120), rgb565(12, 44, 24)},
  {"BANDIT", "Keep it secret. Win: the King is out. All Bandits win together.",
   rgb565(255, 92, 92), rgb565(58, 16, 18)},
  {"TRAITOR", "Keep it secret. Trust no one. Win: be the last player standing.",
   rgb565(196, 120, 255), rgb565(38, 20, 62)},
  {"USURPER", "Keep it secret. Finish off the King yourself: you become King at 50 life, "
              "the old King takes your role at 1 life.",
   rgb565(70, 160, 255), rgb565(10, 30, 62)},
};
const RoleInfo& info(uint8_t role) { return ROLES[role < KINGDOMS_ROLE_COUNT ? role : 0]; }

// A crown: three spikes with balls, a band with three gems.
void crown(const Pen& p, uint16_t body, uint16_t dark, uint16_t gemMid) {
  p.box(-36, -6, 72, 18, 0, body);
  p.tri(-40, -4, -12, -4, -38, -32, body);
  p.tri(-14, -4, 14, -4, 0, -40, body);
  p.tri(12, -4, 40, -4, 38, -32, body);
  p.dot(-38, -34, 5, body);
  p.dot(0, -42, 5, body);
  p.dot(38, -34, 5, body);
  p.box(-42, 10, 84, 20, 4, body);
  p.line(-40, 10, 40, 10, 2, dark);
  p.dot(0, 20, 6, gemMid);
  p.dot(-24, 20, 4, SAPPHIRE);
  p.dot(24, 20, 4, EMERALD);
}

// A dagger from the pommel end (x0, y0) toward the tip (x1, y1); the guard
// sits `grip` along the way.
void dagger(const Pen& p, float x0, float y0, float x1, float y1, float grip, float w) {
  const float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
  const float ux = dx / len, uy = dy / len;
  const float gx = x0 + ux * grip, gy = y0 + uy * grip;
  p.line(gx, gy, x1, y1, w, STEEL);                                 // blade
  p.line(gx + ux * 2, gy + uy * 2, x1 - ux * 3, y1 - uy * 3, w / 3, STEEL_DARK);  // fuller
  p.line(x0, y0, gx, gy, w * 0.8f, LEATHER);                        // grip
  p.line(gx - uy * w * 1.3f, gy + ux * w * 1.3f, gx + uy * w * 1.3f, gy - ux * w * 1.3f, w * 0.8f, BRASS);  // guard
  p.dot(x0, y0, w * 0.6f, BRASS);                                   // pommel
}

void king(const Pen& p) {
  p.oval(0, 40, 44, 6, rgb565(90, 64, 8));  // shadow
  crown(p, GOLD, GOLD_DARK, RUBY);
  p.tri(-6, -4, 6, -4, 0, -30, rgb565(255, 228, 120));  // shine on the middle spike
}

void knight(const Pen& p) {
  const uint16_t green = rgb565(60, 190, 104), dark = rgb565(20, 96, 52), light = rgb565(170, 245, 190);
  // the sword behind the shield: hilt bottom left, tip top right
  p.line(-24, 24, 40, -40, 8, STEEL);
  p.line(-22, 22, 36, -36, 2, STEEL_DARK);
  p.line(-24, 24, -40, 40, 6, LEATHER);
  p.line(-34, 14, -14, 34, 6, BRASS);
  p.dot(-42, 42, 4, BRASS);
  // the shield
  p.box(-30, -38, 60, 42, 8, dark);
  p.tri(-30, 2, 30, 2, 0, 46, dark);
  p.box(-26, -34, 52, 38, 6, green);
  p.tri(-26, 2, 26, 2, 0, 40, green);
  p.box(-4, -30, 8, 62, 2, light);   // cross
  p.box(-22, -16, 44, 8, 2, light);
}

void bandit(const Pen& p) {
  const uint16_t skin = rgb565(84, 86, 100), red = rgb565(220, 50, 56), redDark = rgb565(140, 24, 32);
  p.dot(0, -6, 32, skin);                       // head
  p.tri(18, -2, 46, -12, 40, 12, redDark);      // bandana knot
  p.box(-31, -2, 62, 12, 0, red);               // bandana over the lower face
  p.tri(-31, 8, 31, 8, 0, 34, red);
  p.line(-24, 14, 0, 30, 2, redDark);           // folds
  p.line(24, 14, 0, 30, 2, redDark);
  p.box(-32, -24, 64, 16, 8, rgb565(16, 16, 22));  // eye mask
  p.oval(-12, -16, 6, 4, theme::TEXT);
  p.oval(12, -16, 6, 4, theme::TEXT);
  p.dot(-11, -16, 2, rgb565(16, 16, 22));
  p.dot(13, -16, 2, rgb565(16, 16, 22));
  dagger(p, 26, 48, 48, 12, 10, 6);
}

void traitor(const Pen& p) {
  const uint16_t cloak = rgb565(84, 44, 134), hood = rgb565(140, 84, 206);
  p.tri(-48, 48, 48, 48, 0, -12, cloak);        // shoulders
  p.tri(-18, -32, 18, -32, 4, -50, hood);       // hood peak
  p.dot(0, -12, 30, hood);
  p.oval(0, -6, 18, 22, rgb565(12, 6, 18));     // face in the dark
  p.dot(-7, -9, 3, rgb565(236, 200, 255));      // eyes
  p.dot(7, -9, 3, rgb565(236, 200, 255));
  dagger(p, 4, 46, 30, 10, 12, 6);
}

void usurper(const Pen& p, uint16_t tint) {
  const uint16_t blue = rgb565(70, 160, 255), dark = rgb565(26, 80, 160);
  p.oval(0, 40, 44, 6, rgb565(14, 40, 80));
  crown(p, blue, dark, rgb565(255, 255, 255));
  // the crack
  p.line(-4, -40, 4, -24, 3, tint);
  p.line(4, -24, -4, -10, 3, tint);
  p.line(-4, -10, 6, 6, 3, tint);
  p.line(6, 6, -2, 30, 3, tint);
  dagger(p, -40, 44, 34, -36, 16, 7);           // through the crown
}

}  // namespace

const char* kingdomsRoleName(uint8_t role) { return info(role).name; }
const char* kingdomsRoleGoal(uint8_t role) { return info(role).goal; }
uint16_t kingdomsRoleColor(uint8_t role) { return info(role).color; }
uint16_t kingdomsRoleTint(uint8_t role) { return info(role).tint; }

void drawKingdomsArt(lgfx::LovyanGFX& c, int cx, int cy, int size, uint8_t role) {
  const Pen p = {c, cx, cy, size / 100.0f};
  switch (role) {
    case KINGDOMS_KING:    king(p); break;
    case KINGDOMS_KNIGHT:  knight(p); break;
    case KINGDOMS_BANDIT:  bandit(p); break;
    case KINGDOMS_TRAITOR: traitor(p); break;
    case KINGDOMS_USURPER: usurper(p, kingdomsRoleTint(role)); break;
    default: break;
  }
}

void drawKingdomsCardBack(lgfx::LovyanGFX& c, int x, int y, int w, int h, bool dim) {
  const uint16_t face  = dim ? rgb565(24, 26, 44) : rgb565(36, 40, 92);
  const uint16_t frame = dim ? rgb565(90, 72, 24) : rgb565(214, 160, 24);
  const uint16_t inner = dim ? rgb565(48, 52, 76) : rgb565(92, 100, 168);
  c.fillRoundRect(x, y, w, h, 6, frame);
  c.fillRoundRect(x + 2, y + 2, w - 4, h - 4, 5, face);
  c.drawRoundRect(x + 5, y + 5, w - 10, h - 10, 4, inner);
  const int cx = x + w / 2, cy = y + h / 2, d = w / 5;
  c.fillTriangle(cx, cy - d - 4, cx - d, cy, cx + d, cy, frame);  // diamond
  c.fillTriangle(cx, cy + d + 4, cx - d, cy, cx + d, cy, frame);
  c.fillCircle(cx, y + 12, 2, frame);
  c.fillCircle(cx, y + h - 13, 2, frame);
}
