/* ============================================================================
 *  ScreenDigimon — Digimon TCG memory gauge for two players, laid out like
 *  the gauge board: the device lies flat between the players, player 1 at
 *  the bottom edge (upright), player 2 at the top edge (upside down for
 *  player 1, upright for them). The middle row reads 5 4 3 2 1 0 1 2 3 4 5:
 *  player 1's 1..5 on the left, player 2's on the right. Each player's 6..10
 *  folds back on their own side, so the gauge runs 5 -> 6 at the edge:
 *
 *  ┌────────────────────────────────────────────┐
 *  │ [PASS]                        [YOUR TURN]  │  player 2 (upside down)
 *  │ (≡)  MEMORY 2      10  9  8  7  6          │  player 2's 6..10
 *  │  5  4  3  2  1  (0)  1  2  3  4  5         │  the middle row
 *  │  6  7  8  9 10      MEMORY -2   (≡)        │  player 1's 6..10
 *  │ [WAITING]                       [PASS]     │  player 1
 *  └────────────────────────────────────────────┘
 *  Every number faces the player whose side it is on. The counter is the
 *  gold circle; the circles it has gone past on that side are tinted.
 *
 *  Touch:   tap a circle = put the counter there. When it lands 1+ on the
 *           waiting player's side, the turn passes to them.
 *           PASS (only the player whose turn it is) = opponent gets 3 memory
 *           and the turn.  ≡ (either one) = Digimon menu.
 *  Encoder: turn = memory of the player whose turn it is, +-1 per click
 *           (counter-clockwise = spend) · long-press = PASS
 * ==========================================================================*/
#include <Arduino.h>
#include "Screens.h"
#include "App.h"
#include "GameState.h"
#include "CommanderLayout.h"
#include "TableDraw.h"
#include "Theme.h"
#include "Ui.h"
#include "Config.h"
#include <stdio.h>

namespace {

// ---------------------------------------------------------------- geometry
// The gauge: 21 circles, index i = memory value + 10 (memory > 0 = player
// 1's side). Panel coordinates.
constexpr int GAUGE_N = 2 * DIGIMON_MEMORY_MAX + 1;
constexpr int CIRCLE_R = 13, CARD = 2 * CIRCLE_R + 2;  // each circle is its own 28 x 28 card
constexpr int PITCH = 29, MID_X = 160, MID_Y = 120, FOLD_DY = 36;
constexpr int CIRCLE_HIT_R = 16;

struct Point { int16_t x, y; };

// Where memory `m` (-10..10, player 1's point of view) sits on the board.
Point circleAt(int m) {
  const int v = m < 0 ? -m : m;
  const int dir = m > 0 ? -1 : 1;  // player 1's half runs left, player 2's right
  if (v <= 5) return {(int16_t)(MID_X + dir * PITCH * v), (int16_t)MID_Y};
  // 6..10 fold back: 6 next to 5 (at the edge), 10 next to 1
  return {(int16_t)(MID_X + dir * PITCH * (11 - v)), (int16_t)(m > 0 ? MID_Y + FOLD_DY : MID_Y - FOLD_DY)};
}

// The side a circle faces: player 1's numbers and 0 upright, player 2's turned.
Side sideOf(int m) { return m < 0 ? Side::Top : Side::Bottom; }

// Each player's strip along their edge, as they see it: 320 x 48.
const Seat BANDS[2] = {{{0, 192, 320, 48}, Side::Bottom}, {{0, 0, 320, 48}, Side::Top}};
constexpr int  BAND_W = 320, BAND_H = 48;
constexpr Rect TURN_PILL = {12, 11, 124, 28};
constexpr Rect PASS_BTN  = {200, 6, 108, 36};
constexpr int  PAD = 4;

// Memory as each player sees it, in the free corner on their side of the
// fold. Player 2's is player 1's turned half a circle around the middle.
const Seat READOUTS[2] = {{{158, 140, 112, 52}, Side::Bottom}, {{50, 48, 112, 52}, Side::Top}};
constexpr int READ_W = 112, READ_H = 52;

// The two ≡ buttons, one in each player's free corner.
constexpr Point MENU_HUB[2] = {{296, 162}, {24, 78}};

// ---------------------------------------------------------------- hit test
enum class Hit : uint8_t { None, Area, Circle, Pass, Menu };
struct Touch {
  Hit     what;
  uint8_t side;    // Pass / Menu: whose
  int8_t  memory;  // Circle: -10..10, player 1's point of view
};
constexpr Touch NO_TOUCH = {Hit::None, 0, 0};

int dist2(int x, int y, Point p) { return (x - p.x) * (x - p.x) + (y - p.y) * (y - p.y); }

Touch hitTest(int x, int y) {
  for (uint8_t side = 0; side < 2; ++side)
    if (dist2(x, y, MENU_HUB[side]) <= HUB_HIT_R * HUB_HIT_R) return {Hit::Menu, side, 0};
  for (uint8_t side = 0; side < 2; ++side) {
    if (!BANDS[side].r.contains(x, y)) continue;
    int lx, ly;
    seatToLocal(BANDS[side], x, y, lx, ly);
    const Rect& p = PASS_BTN;
    if (lx >= p.x - PAD && lx < p.x + p.w + PAD && ly >= p.y - PAD && ly < p.y + p.h + PAD)
      return {Hit::Pass, side, 0};
    return {Hit::Area, side, 0};
  }
  int best = -1, bestD = CIRCLE_HIT_R * CIRCLE_HIT_R + 1;
  for (int i = 0; i < GAUGE_N; ++i) {
    const int d = dist2(x, y, circleAt(i - DIGIMON_MEMORY_MAX));
    if (d < bestD) { bestD = d; best = i; }
  }
  if (best >= 0) return {Hit::Circle, 0, (int8_t)(best - DIGIMON_MEMORY_MAX)};
  return {Hit::Area, 0, 0};
}

// ---------------------------------------------------------------- UI-only state
Touch s_press = NO_TOUCH;

DigimonGame& game() { return g_state.digimon; }

void tap(const Touch& t) {
  DigimonGame& g = game();
  switch (t.what) {
    case Hit::Circle: digimonSetMemory(g, 0, t.memory); break;  // player 1's point of view
    case Hit::Pass:   if (t.side == g.turn) digimonPass(g); break;  // only on your own turn
    case Hit::Menu:   goToScreen(SCREEN_DIGIMON_SETUP); break;
    default: break;
  }
}

// ---------------------------------------------------------------- drawing
void label(lgfx::LovyanGFX& c, int x, int y, const char* t, const lgfx::IFont* f, uint16_t col,
           lgfx::textdatum_t datum = lgfx::textdatum_t::middle_center) {
  c.setFont(f);
  c.setTextDatum(datum);
  c.setTextColor(col);
  c.drawString(t, x, y);
}

// Circles the counter has gone past, tinted with the colour of that side.
constexpr uint16_t PASSED_FILL[2] = {rgb565(96, 34, 38), rgb565(26, 60, 98)};

enum class Dot : uint8_t { Plain, Passed, Counter, Pressed };

Dot dotState(int m) {
  const int mem = game().memory;
  if (s_press.what == Hit::Circle && s_press.memory == m) return Dot::Pressed;
  if (m == mem) return Dot::Counter;
  if (m != 0 && (m > 0) == (mem > 0) && (m > 0 ? m < mem : m > mem)) return Dot::Passed;
  return Dot::Plain;
}

struct DotJob { int8_t m; Dot state; };

void paintDot(lgfx::LovyanGFX& c, int ox, int oy, const void* ctx) {
  const DotJob& j = *static_cast<const DotJob*>(ctx);
  const int cx = ox + CARD / 2, cy = oy + CARD / 2;
  const uint16_t pc = j.m == 0 ? theme::ACCENT : theme::PLAYER[j.m > 0 ? 0 : 1];
  c.fillRect(ox, oy, CARD, CARD, theme::BG);
  uint16_t fill = theme::BUTTON, ink = theme::TEXT_DIM;
  switch (j.state) {
    case Dot::Passed:  fill = PASSED_FILL[j.m > 0 ? 0 : 1]; ink = theme::TEXT; break;
    case Dot::Counter: fill = theme::ACCENT; ink = theme::TEXT_ON_ACCENT; break;
    case Dot::Pressed: fill = theme::TEXT; ink = theme::TEXT_ON_ACCENT; break;
    default: if (j.m == 0) ink = theme::TEXT; break;
  }
  c.fillCircle(cx, cy, CIRCLE_R, fill);
  if (j.state == Dot::Plain) {  // the side's colour around the edge, like the board
    c.drawCircle(cx, cy, CIRCLE_R, pc);
    if (j.m == 0) c.drawCircle(cx, cy, CIRCLE_R - 1, pc);
  }
  const int v = j.m < 0 ? -j.m : j.m;
  char b[4];
  snprintf(b, sizeof(b), "%d", v);
  label(c, cx, cy + 1, b, v >= 10 ? theme::fontLabel() : theme::fontButton(), ink);
}

void drawDot(int m, Dot state) {
  const Point p = circleAt(m);
  const Seat seat = {{(int16_t)(p.x - CARD / 2), (int16_t)(p.y - CARD / 2), CARD, CARD}, sideOf(m)};
  const DotJob job = {(int8_t)m, state};
  drawSeatCard(seat, paintDot, &job);
}

// One player's edge strip: whose turn, and PASS for the player whose turn it is.
struct BandView {
  bool myTurn, passDown;
  bool operator==(const BandView& o) const { return myTurn == o.myTurn && passDown == o.passDown; }
  bool operator!=(const BandView& o) const { return !(*this == o); }
};
struct BandJob { uint8_t side; BandView v; };

BandView bandView(uint8_t side) {
  return {game().turn == side, s_press.what == Hit::Pass && s_press.side == side};
}

void paintBand(lgfx::LovyanGFX& c, int ox, int oy, const void* ctx) {
  const BandJob& j = *static_cast<const BandJob*>(ctx);
  const uint16_t pc = theme::PLAYER[j.side];
  c.fillRect(ox, oy, BAND_W, BAND_H, theme::BG);
  const Rect& t = TURN_PILL;
  if (j.v.myTurn) c.fillRoundRect(ox + t.x, oy + t.y, t.w, t.h, t.h / 2, pc);
  else            uiRoundFrame(c, ox + t.x, oy + t.y, t.w, t.h, t.h / 2, 2, pc);
  label(c, ox + t.cx(), oy + t.cy() + 1, j.v.myTurn ? "YOUR TURN" : "WAITING", theme::fontLabel(),
        j.v.myTurn ? theme::TEXT_ON_ACCENT : theme::TEXT_DIM);
  if (j.v.myTurn) {
    const Rect& p = PASS_BTN;
    c.fillRoundRect(ox + p.x, oy + p.y, p.w, p.h, 10, j.v.passDown ? theme::BUTTON_DOWN : theme::BUTTON);
    label(c, ox + p.cx(), oy + p.cy() + 1, "PASS", theme::fontButton(), theme::TEXT);
  }
}

// Memory as one player sees it: on their side, or (negative) on the opponent's.
struct ReadJob { uint8_t side; int8_t memory; };

void paintReadout(lgfx::LovyanGFX& c, int ox, int oy, const void* ctx) {
  const ReadJob& j = *static_cast<const ReadJob*>(ctx);
  c.fillRect(ox, oy, READ_W, READ_H, theme::BG);
  label(c, ox + READ_W / 2, oy + 7, "MEMORY", theme::fontSmall(), theme::TEXT_DIM);
  char num[6];
  snprintf(num, sizeof(num), "%d", (int)j.memory);
  uint8_t n = 0;
  const theme::NumberFont& f = theme::numberFonts(n)[1];  // 36 px digits
  c.setFont(f.font);
  c.setTextDatum(lgfx::textdatum_t::top_center);
  c.setTextColor(j.memory > 0 ? theme::PLAYER[j.side] : theme::TEXT_DIM);
  const int ny = 33 - f.top - f.height / 2;
  c.drawString(num, ox + READ_W / 2, oy + ny);
  if (theme::LIFE_FAUX_BOLD) c.drawString(num, ox + READ_W / 2 + 1, oy + ny);
}

// ---------------------------------------------------------------- what is on screen
Dot      s_dots[GAUGE_N];
BandView s_bands[2];
int8_t   s_readouts[2];
int8_t   s_menuDown = -1;  // which ≡ is pressed

// ---------------------------------------------------------------- module functions
void onEnter() { s_press = NO_TOUCH; }

void handleInput(const InputEvent& e) {
  DigimonGame& g = game();
  switch (e.type) {
    case InputType::EncoderTurn:      digimonAdjust(g, g.turn, e.delta); break;
    case InputType::EncoderLongPress: digimonPass(g); break;
    case InputType::TouchDown:        s_press = hitTest(e.x, e.y); break;
    case InputType::TouchUp: {
      const Touch t = s_press;
      s_press = NO_TOUCH;
      if (isTap(e, TOUCH_TAP_MAX_MS)) tap(t);
      break;
    }
    default: break;
  }
}

void render(bool full) {
  auto& g = gfx();
  bool started = false;
  auto begin = [&]() {
    if (!started) { g.startWrite(); started = true; }
  };
  if (full) {
    begin();
    g.fillScreen(theme::BG);
  }
  for (int i = 0; i < GAUGE_N; ++i) {
    const int m = i - DIGIMON_MEMORY_MAX;
    const Dot d = dotState(m);
    if (!full && d == s_dots[i]) continue;
    begin();
    drawDot(m, d);
    s_dots[i] = d;
  }
  for (uint8_t side = 0; side < 2; ++side) {
    const BandView b = bandView(side);
    if (full || b != s_bands[side]) {
      begin();
      const BandJob job = {side, b};
      drawSeatCard(BANDS[side], paintBand, &job);
      s_bands[side] = b;
    }
    const int8_t mem = digimonMemoryOf(game(), side);
    if (full || mem != s_readouts[side]) {
      begin();
      const ReadJob job = {side, mem};
      drawSeatCard(READOUTS[side], paintReadout, &job);
      s_readouts[side] = mem;
    }
  }
  const int8_t menuDown = s_press.what == Hit::Menu ? (int8_t)s_press.side : -1;
  if (full || menuDown != s_menuDown) {
    begin();
    for (uint8_t side = 0; side < 2; ++side)
      drawHubButton(MENU_HUB[side].x, MENU_HUB[side].y, HubIcon::Menu, menuDown == side);
    s_menuDown = menuDown;
  }
  if (started) g.endWrite();
}

}  // namespace

const ScreenModule DigimonScreen = {"Digimon", onEnter, handleInput, render, nullptr};
