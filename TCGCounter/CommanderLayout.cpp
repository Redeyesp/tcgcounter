#include "CommanderLayout.h"
#include "InputEvents.h"
#include "Config.h"

namespace {

// With COMMANDER_FACE_SEATS 0 every card is drawn for someone sitting at the
// bottom edge (handy when the device stands in front of one person).
constexpr Side face(Side s) { return COMMANDER_FACE_SEATS ? s : Side::Bottom; }

constexpr Side TOP = face(Side::Top), BOTTOM = face(Side::Bottom), RIGHT = face(Side::Right);

// 2 px gaps between cards. Index = number of players - 2.
const TableLayout LAYOUTS[] = {
  // 2 players: one wide card each, facing each other
  {2, {{{0, 0, 320, 119}, TOP}, {{0, 121, 320, 119}, BOTTOM}},
   2, {{136, 120, HubKind::Menu}, {184, 120, HubKind::Dice}}},
  // 3 players: 2x2 grid, bottom-right seat left empty
  {3, {{{0, 0, 159, 119}, TOP}, {{161, 0, 159, 119}, TOP}, {{0, 121, 159, 119}, BOTTOM}},
   2, {{160, 120, HubKind::Menu}, {240, 180, HubKind::Dice}}},  // 🎲 in the empty seat
  // 4 players: 2x2 grid
  {4, {{{0, 0, 159, 119}, TOP}, {{161, 0, 159, 119}, TOP},
       {{0, 121, 159, 119}, BOTTOM}, {{161, 121, 159, 119}, BOTTOM}},
   2, {{160, 120, HubKind::Menu}, {160, 180, HubKind::Dice}}},
  // 5 players: 2x2 grid on the left, head of the table on the right
  {5, {{{0, 0, 119, 119}, TOP}, {{121, 0, 119, 119}, TOP},
       {{0, 121, 119, 119}, BOTTOM}, {{121, 121, 119, 119}, BOTTOM},
       {{242, 0, 78, 240}, RIGHT}},
   2, {{120, 120, HubKind::Menu}, {120, 180, HubKind::Dice}}},
  // 6 players: three on each long side; ≡ and 🎲 on the two column joints
  {6, {{{0, 0, 105, 119}, TOP}, {{107, 0, 106, 119}, TOP}, {{215, 0, 105, 119}, TOP},
       {{0, 121, 105, 119}, BOTTOM}, {{107, 121, 106, 119}, BOTTOM}, {{215, 121, 105, 119}, BOTTOM}},
   2, {{106, 120, HubKind::Menu}, {214, 120, HubKind::Dice}}},
};

constexpr int GAP_SLACK = 2;  // a touch this close to a card (in a gap) still counts for it

int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

int distToRect(const Rect& r, int x, int y) {
  const int dx = x < r.x ? r.x - x : (x >= r.x + r.w ? x - (r.x + r.w - 1) : 0);
  const int dy = y < r.y ? r.y - y : (y >= r.y + r.h ? y - (r.y + r.h - 1) : 0);
  return dx + dy;
}

Zone zoneAt(const CardGeom& g, int lx, int ly) {
  if (!g.wide) {
    if (ly >= g.zoneY) return lx < g.w / 2 ? Zone::Minus : Zone::Plus;
    return Zone::Area;
  }
  if (ly < g.zoneTop) return Zone::Area;
  if (lx < g.minusEnd) return Zone::Minus;
  if (lx >= g.plusStart) return Zone::Plus;
  return Zone::Area;
}

int16_t i16(int v) { return (int16_t)v; }
Rect rect(int x, int y, int w, int h) { return Rect{i16(x), i16(y), i16(w), i16(h)}; }

}  // namespace

const TableLayout& tableLayout(uint8_t players) {
  if (players < COMMANDER_MIN_PLAYERS || players > COMMANDER_MAX_PLAYERS)
    players = COMMANDER_DEFAULT_PLAYERS;
  return LAYOUTS[players - COMMANDER_MIN_PLAYERS];
}

static bool sideways(Side s) { return s == Side::Left || s == Side::Right; }

int16_t seatLocalW(const Seat& s) { return sideways(s.side) ? s.r.h : s.r.w; }
int16_t seatLocalH(const Seat& s) { return sideways(s.side) ? s.r.w : s.r.h; }

void seatToLocal(const Seat& s, int sx, int sy, int& lx, int& ly) {
  const int w = s.r.w, h = s.r.h;
  const int dx = clampInt(sx - s.r.x, 0, w - 1), dy = clampInt(sy - s.r.y, 0, h - 1);
  switch (s.side) {
    case Side::Bottom: lx = dx;         ly = dy;         break;
    case Side::Top:    lx = w - 1 - dx; ly = h - 1 - dy; break;
    case Side::Left:   lx = dy;         ly = w - 1 - dx; break;  // player at the left edge
    case Side::Right:  lx = h - 1 - dy; ly = dx;         break;  // player at the right edge
  }
}

void localToScreen(const Seat& s, int lx, int ly, int& sx, int& sy) {
  const int w = s.r.w, h = s.r.h;
  int dx = 0, dy = 0;
  switch (s.side) {
    case Side::Bottom: dx = lx;         dy = ly;         break;
    case Side::Top:    dx = w - 1 - lx; dy = h - 1 - ly; break;
    case Side::Left:   dx = w - 1 - ly; dy = lx;         break;
    case Side::Right:  dx = ly;         dy = h - 1 - lx; break;
  }
  sx = s.r.x + dx;
  sy = s.r.y + dy;
}

CardGeom cardGeom(int16_t w, int16_t h) {
  CardGeom g{};
  g.w = w;
  g.h = h;
  g.wide = w >= 2 * h;
  if (!g.wide) {
    // Label pill stays >= 18 px from the card's sides: clear of the centre
    // button that sits on the cards' corners. 159 px card -> 105 px pill.
    const int pillW = w - 36 < 105 ? w - 36 : 105;
    g.pill = rect((w - pillW) / 2, 8, pillW, 20);
    const int gap = 11;
    const int bw = (w - 27) / 2 < 52 ? (w - 27) / 2 : 52;
    const int bx = (w - (2 * bw + gap)) / 2;
    const int by = h - 37;
    g.minus = rect(bx, by, bw, 30);
    g.plus  = rect(bx + bw + gap, by, bw, 30);
    g.numCx = i16(w / 2);
    g.numCy = i16(46 + (h - 119) / 2);  // digits rows 28..63 on a 119 px card
    g.numMaxW = i16(w - 12);
    g.numMaxH = 38;
    g.capX = i16(w / 2);
    g.capY = i16(by - 8);
    g.zoneY = i16(by - 6);
    g.zoneTop = 0;
    g.minusEnd = g.plusStart = i16(w / 2);
    // D20 between the label and the card's bottom edge
    g.dieR = 36;
    g.dieCx = i16(w / 2);
    g.dieCy = i16(32 + (h - 40) / 2);
  } else {
    const int pillW = w / 2 - 16 < 105 ? w / 2 - 16 : 105;
    g.pill = rect(10, 8, pillW, 20);
    const int bw = w / 5 < 60 ? w / 5 : 60;
    const int top = 34, bh = h - 42;
    g.minus = rect(10, top, bw, bh);
    g.plus  = rect(w - 10 - bw, top, bw, bh);
    g.numCx = i16(w / 2);
    g.numCy = i16((top + h - 8) / 2);
    g.numMaxW = i16(w - 2 * (10 + bw) - 12);
    g.numMaxH = i16(bh);
    g.capX = i16(w - 14);
    g.capY = 18;
    g.zoneY = h;
    g.zoneTop = 30;
    g.minusEnd = i16(10 + bw + 8);
    g.plusStart = i16(w - 10 - bw - 8);
    // D20 centred; moved right when it would reach the label pill
    g.dieR = i16((h - 12) / 2 < 40 ? (h - 12) / 2 : 40);
    g.dieCx = i16(w / 2);
    g.dieCy = i16(h / 2);
    const int halfW = g.dieR * 7 / 8 + 4;  // hexagon half-width (~0.866 R) + gap
    if (g.dieCy - g.dieR < g.pill.y + g.pill.h && g.dieCx - halfW < g.pill.x + g.pill.w)
      g.dieCx = i16(g.pill.x + g.pill.w + halfW);
  }
  return g;
}

bool clearOfHubs(const TableLayout& L, const Seat& s, const Rect& local) {
  int ax, ay, bx, by;
  localToScreen(s, local.x, local.y, ax, ay);
  localToScreen(s, local.x + local.w - 1, local.y + local.h - 1, bx, by);
  const Rect r = rect(ax < bx ? ax : bx, ay < by ? ay : by,
                      (ax < bx ? bx - ax : ax - bx) + 1, (ay < by ? by - ay : ay - by) + 1);
  const int keep = HUB_R + HUB_MOAT + 1;
  for (uint8_t k = 0; k < L.hubCount; ++k) {
    const int hx = L.hubs[k].x, hy = L.hubs[k].y;
    const int nx = hx < r.x ? r.x : (hx >= r.x + r.w ? r.x + r.w - 1 : hx);
    const int ny = hy < r.y ? r.y : (hy >= r.y + r.h ? r.y + r.h - 1 : hy);
    const int dx = hx - nx, dy = hy - ny;
    if (dx * dx + dy * dy < keep * keep) return false;
  }
  return true;
}

Hit commanderHitTest(uint8_t players, int x, int y) {
  const TableLayout& L = tableLayout(players);
  for (uint8_t k = 0; k < L.hubCount; ++k) {
    const int dx = x - L.hubs[k].x, dy = y - L.hubs[k].y;
    if (dx * dx + dy * dy <= HUB_HIT_R * HUB_HIT_R) return {Zone::Hub, k};
  }
  int best = -1, bestD = GAP_SLACK + 1;
  for (uint8_t i = 0; i < L.players; ++i) {
    const int d = distToRect(L.seats[i].r, x, y);
    if (d < bestD) { bestD = d; best = i; }
  }
  if (best < 0) return NO_HIT;  // empty seat / outside every card
  const Seat& s = L.seats[best];
  int lx, ly;
  seatToLocal(s, x, y, lx, ly);
  return {zoneAt(cardGeom(s), lx, ly), (uint8_t)best};
}

bool isSidewaysSwipe(const Seat& s, int16_t swipeDir) {
  if (sideways(s.side)) return swipeDir == SWIPE_UP || swipeDir == SWIPE_DOWN;
  return swipeDir == SWIPE_LEFT || swipeDir == SWIPE_RIGHT;
}
