#include "ScoreLayout.h"

namespace {

int16_t i16(int v) { return (int16_t)v; }
Rect rect(int x, int y, int w, int h) { return Rect{i16(x), i16(y), i16(w), i16(h)}; }

// Seats: the 2- and 4-player Commander tables. Round buttons:
//   2 cards: ≡ 🎲 ↻ in a row on the line between the cards; the card rows
//            next to that line (above the digits) stay empty.
//   4 cards: ≡ in the middle, ↻ between the top cards, 🎲 between the bottom
//            cards (the middle of their inner edges, beside the score).
TableLayout makeTable(uint8_t players) {
  TableLayout L = tableLayout(players);
  L.hubCount = 3;
  if (players == 4) {
    L.hubs[0] = {160, 120, HubKind::Menu};
    L.hubs[1] = {160, 180, HubKind::Dice};
    L.hubs[2] = {160, 60, HubKind::Restart};
  } else {
    L.hubs[0] = {100, 120, HubKind::Menu};
    L.hubs[1] = {160, 120, HubKind::Dice};
    L.hubs[2] = {220, 120, HubKind::Restart};
  }
  return L;
}

}  // namespace

const TableLayout& scoreTable(uint8_t players) {
  static const TableLayout TWO = makeTable(2), FOUR = makeTable(4);
  return players == 4 ? FOUR : TWO;
}

ScoreGeom scoreGeom(int16_t w, int16_t h, bool bonus) {
  ScoreGeom g{};
  g.w = w;
  g.h = h;
  g.wide = w >= 2 * h;
  if (g.wide) {
    // 320 x 119: tall − / + at the sides, "5 /8" in the middle (digit rows
    // 23..92), the name under it. Rows 0..22 stay empty: the round buttons
    // reach into the cards there.
    const int bw = 60;
    g.minus = rect(10, 10, bw, h - 20);
    g.plus  = rect(w - 10 - bw, 10, bw, h - 20);
    g.minusEnd = i16(10 + bw + 10);         // 80
    g.plusStart = i16(w - 10 - bw - 10);    // 240
    g.numCx = i16(w / 2);
    g.numCy = 58;
    g.numMaxW = i16(g.plusStart - g.minusEnd - 4);
    g.numMaxH = 70;
    g.capY = i16(h - 14);                   // 105: rows ~99..111
    g.capX = i16(w / 2);
    if (bonus) {
      // +1 in the top right corner of the score area, like an exponent:
      // the digits stay left of it and the "/8" sits lower, on their baseline
      g.bonus = rect(g.plusStart - 42, 26, 40, 24);
      g.bonusZone = rect(g.plusStart - 48, 22, 48, 62);
    }
    g.dieCx = i16(w / 2);
    g.dieCy = 58;
    g.dieR = 34;
    g.zoneY = h;
    return g;
  }
  // compact (159 x 119): the Commander card — name pill on top, the score in
  // the middle, − / + at the bottom. The round buttons sit on the inner top
  // corner and the middle of the inner edge, so the score keeps 22 px from
  // both sides.
  const int pillW = w - 36 < 105 ? w - 36 : 105;
  g.label = rect((w - pillW) / 2, 8, pillW, 20);
  const int gap = 11;
  const int bw = (w - 27) / 2 < 52 ? (w - 27) / 2 : 52;
  const int bx = (w - (2 * bw + gap)) / 2;
  const int by = h - 37;
  g.minus = rect(bx, by, bw, 30);
  g.plus  = rect(bx + bw + gap, by, bw, 30);
  g.zoneY = i16(by - 6);
  g.minusEnd = g.plusStart = i16(w / 2);
  g.numCy = i16(46 + (h - 119) / 2);
  g.numMaxH = 38;
  const int side = 22;
  if (bonus) {
    // +1 at the right of the score; it keeps clear of a round button on the
    // right edge (centre 22 px away incl. its dark ring)
    g.bonus = rect(w - side - 37, g.numCy - 13, 36, 26);
    g.bonusZone = rect(g.bonus.x - 4, g.label.y + g.label.h, w - (g.bonus.x - 4),
                       g.zoneY - (g.label.y + g.label.h));
    g.numCx = i16((side + g.bonus.x - 4) / 2);
    g.numMaxW = i16(g.bonus.x - 4 - side);
  } else {
    g.numCx = i16(w / 2);
    g.numMaxW = i16(w - 2 * side);
  }
  g.capX = g.numCx;
  g.capY = i16(by - 8);
  g.dieR = 36;
  g.dieCx = i16(w / 2);
  g.dieCy = i16(32 + (h - 40) / 2);
  return g;
}

ScoreHit scoreHitTest(uint8_t players, bool bonus, int x, int y) {
  const TableLayout& L = scoreTable(players);
  const int8_t hub = tableHubAt(L, x, y);
  if (hub >= 0) return {ScoreZone::Hub, (uint8_t)hub};
  const int8_t p = tableSeatAt(L, x, y);
  if (p < 0) return SCORE_NO_HIT;
  const Seat& s = L.seats[p];
  const ScoreGeom g = scoreGeom(s, bonus);
  int lx, ly;
  seatToLocal(s, x, y, lx, ly);
  const uint8_t i = (uint8_t)p;
  if (g.bonusZone.w > 0 && g.bonusZone.contains(lx, ly)) return {ScoreZone::Bonus, i};
  if (g.wide) {
    if (lx < g.minusEnd) return {ScoreZone::Minus, i};
    if (lx >= g.plusStart) return {ScoreZone::Plus, i};
    return {ScoreZone::Area, i};
  }
  if (ly >= g.zoneY) return {lx < g.w / 2 ? ScoreZone::Minus : ScoreZone::Plus, i};
  return {ScoreZone::Area, i};
}
