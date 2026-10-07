/* ============================================================================
 *  ScreenDigimon — Digimon TCG memory gauge for two players. The device
 *  stands up (portrait) like Pokemon: player 1 at the top short edge (their
 *  half upside down), player 2 at the bottom. The gauge runs through the
 *  device: 0 is the round button in the middle, each player's 1..10 is on
 *  their own half, counting outward toward them.
 *
 *  ┌──────────────────────────┐   each half, as its player sees it
 *  │ [YOUR TURN]     [ PASS ] │   (top = middle of the device)
 *  │ MEMORY     5     LOCK 3  │
 *  │ [1 ][2 ][3 ][4 ][5 ]     │   the counter: the lit slot
 *  │ [6 ][7 ][8 ][9 ][10]     │
 *  └──────────────────────────┘
 *  Round buttons on the middle line: ≡ menu, 0 (memory to 0), memory lock.
 *
 *  Touch:   tap a slot = put the counter there (on that player's side).
 *           When it lands 1+ on the waiting player's side, the turn passes
 *           to them; with MEMORY LOCK on their turn then starts at exactly
 *           the lock value (default 3, set in the Digimon menu).
 *           PASS (the player whose turn it is) = opponent gets 3 memory
 *           (the lock value when locked) and the turn.
 *           0 = counter to 0 · lock = memory lock on/off · ≡ = Digimon menu
 *  Encoder: turn = memory of the player whose turn it is, +-1 per click
 *           (counter-clockwise = spend) · press = memory lock on/off ·
 *           long-press = PASS
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
// Player 1 faces the panel's left edge (top of the standing device), player
// 2 the right edge (bottom). Each half as its player sees it: 240 x 159.
const Seat SEATS[2] = {{{0, 0, 159, 240}, Side::Left}, {{161, 0, 159, 240}, Side::Right}};
constexpr int HALF_W = 240, HALF_H = 159;

// Round buttons on the middle line (panel coordinates).
constexpr int HUB_X = 160, LOCK_Y = 60, ZERO_Y = 120, MENU_Y = 180;

constexpr Rect TURN_PILL = {10, 28, 112, 24};
constexpr Rect PASS_BTN  = {130, 28, 100, 24};
constexpr int  NUM_CX = 130, NUM_CY = 75;  // right of the MEMORY caption, room for -10
constexpr int  GRID_X0 = 4, GRID_Y0 = 98, CELL_W = 44, CELL_H = 28, CELL_PITCH_X = 47, CELL_PITCH_Y = 31;
constexpr int  PAD = 4;

Rect cellRect(int k) {  // k 0..9 = memory 1..10
  return Rect{(int16_t)(GRID_X0 + (k % 5) * CELL_PITCH_X), (int16_t)(GRID_Y0 + (k / 5) * CELL_PITCH_Y),
              CELL_W, CELL_H};
}

// ---------------------------------------------------------------- hit test
enum class Hit : uint8_t { None, Area, Cell, Pass, Zero, Lock, Menu };
struct Touch {
  Hit     what;
  uint8_t side;
  uint8_t value;  // Cell: memory 1..10
};
constexpr Touch NO_TOUCH = {Hit::None, 0, 0};

bool inHub(int x, int y, int hy) {
  const int dx = x - HUB_X, dy = y - hy;
  return dx * dx + dy * dy <= HUB_HIT_R * HUB_HIT_R;
}

Touch hitTest(int x, int y) {
  if (inHub(x, y, LOCK_Y)) return {Hit::Lock, 0, 0};
  if (inHub(x, y, ZERO_Y)) return {Hit::Zero, 0, 0};
  if (inHub(x, y, MENU_Y)) return {Hit::Menu, 0, 0};
  const uint8_t side = x < 160 ? 0 : 1;
  int lx, ly;
  seatToLocal(SEATS[side], x, y, lx, ly);
  if (ly >= GRID_Y0 - 3) {  // the slots: gaps belong to the nearer slot
    int col = (lx - GRID_X0 + (CELL_PITCH_X - CELL_W) / 2) / CELL_PITCH_X;
    int row = (ly - GRID_Y0 + (CELL_PITCH_Y - CELL_H) / 2) / CELL_PITCH_Y;
    col = col < 0 ? 0 : (col > 4 ? 4 : col);
    row = row < 0 ? 0 : (row > 1 ? 1 : row);
    return {Hit::Cell, side, (uint8_t)(row * 5 + col + 1)};
  }
  const Rect& p = PASS_BTN;
  if (lx >= p.x - PAD && lx < p.x + p.w + PAD && ly >= p.y - PAD && ly < p.y + p.h + PAD)
    return {Hit::Pass, side, 0};
  return {Hit::Area, side, 0};
}

// ---------------------------------------------------------------- UI-only state
Touch s_press = NO_TOUCH;

DigimonGame& game() { return g_state.digimon; }

void tap(const Touch& t) {
  DigimonGame& g = game();
  switch (t.what) {
    case Hit::Cell: digimonSetMemory(g, t.side, t.value); break;
    case Hit::Pass: if (t.side == g.turn) digimonPass(g); break;  // only on your own turn
    case Hit::Zero: digimonSetMemory(g, 0, 0); break;
    case Hit::Lock: digimonSetLock(g, !g.lock); break;
    case Hit::Menu: goToScreen(SCREEN_DIGIMON_SETUP); break;
    default: break;
  }
}

// ---------------------------------------------------------------- drawing
struct HalfView {
  int8_t  memory;     // as this player sees it
  bool    myTurn;
  bool    lock;
  uint8_t lockAt;
  Hit     pressed;
  uint8_t pressedValue;
  bool operator==(const HalfView& o) const {
    return memory == o.memory && myTurn == o.myTurn && lock == o.lock && lockAt == o.lockAt &&
           pressed == o.pressed && pressedValue == o.pressedValue;
  }
  bool operator!=(const HalfView& o) const { return !(*this == o); }
};

HalfView viewOf(uint8_t side) {
  const DigimonGame& g = game();
  HalfView v;
  v.memory = digimonMemoryOf(g, side);
  v.myTurn = g.turn == side;
  v.lock = g.lock;
  v.lockAt = g.lockAt;
  const bool mine = s_press.side == side && (s_press.what == Hit::Cell || s_press.what == Hit::Pass);
  v.pressed = mine ? s_press.what : Hit::None;
  v.pressedValue = mine ? s_press.value : 0;
  return v;
}

struct HalfJob { uint8_t side; const HalfView* v; };

void label(lgfx::LovyanGFX& c, int x, int y, const char* t, const lgfx::IFont* f, uint16_t col,
           lgfx::textdatum_t datum = lgfx::textdatum_t::middle_center) {
  c.setFont(f);
  c.setTextDatum(datum);
  c.setTextColor(col);
  c.drawString(t, x, y);
}

void paintHalf(lgfx::LovyanGFX& c, int ox, int oy, const void* ctx) {
  const HalfJob& j = *static_cast<const HalfJob*>(ctx);
  const HalfView& v = *j.v;
  const uint16_t pc = theme::PLAYER[j.side];
  c.fillRect(ox, oy, HALF_W, HALF_H, theme::BG);
  c.fillRoundRect(ox, oy, HALF_W, HALF_H, 10, theme::PANEL);
  uiRoundFrame(c, ox, oy, HALF_W, HALF_H, 10, v.myTurn ? 4 : 1, v.myTurn ? pc : theme::PANEL_EDGE);

  // whose turn, and PASS for the player whose turn it is
  const Rect& t = TURN_PILL;
  if (v.myTurn) c.fillRoundRect(ox + t.x, oy + t.y, t.w, t.h, t.h / 2, pc);
  else          c.drawRoundRect(ox + t.x, oy + t.y, t.w, t.h, t.h / 2, theme::PANEL_EDGE);
  label(c, ox + t.cx(), oy + t.cy() + 1, v.myTurn ? "YOUR TURN" : "WAITING", theme::fontLabel(),
        v.myTurn ? theme::TEXT_ON_ACCENT : theme::TEXT_DIM);
  if (v.myTurn) {
    const Rect& p = PASS_BTN;
    c.fillRoundRect(ox + p.x, oy + p.y, p.w, p.h, 8, v.pressed == Hit::Pass ? theme::BUTTON_DOWN : theme::BUTTON);
    label(c, ox + p.cx(), oy + p.cy() + 1, "PASS", theme::fontLabel(), theme::TEXT);
  }

  // memory as this player sees it: on their side, or (negative) on the opponent's
  char num[6];
  snprintf(num, sizeof(num), "%d", (int)v.memory);
  uint8_t n = 0;
  const theme::NumberFont& f = theme::numberFonts(n)[1];  // 36 px digits
  c.setFont(f.font);
  c.setTextDatum(lgfx::textdatum_t::top_center);
  c.setTextColor(v.memory > 0 ? theme::TEXT : theme::TEXT_DIM);
  const int ny = NUM_CY - f.top - f.height / 2;
  c.drawString(num, ox + NUM_CX, oy + ny);
  if (theme::LIFE_FAUX_BOLD) c.drawString(num, ox + NUM_CX + 1, oy + ny);
  label(c, ox + 12, oy + NUM_CY, "MEMORY", theme::fontSmall(), theme::TEXT_DIM, lgfx::textdatum_t::middle_left);
  if (v.lock) {
    char lk[10];
    snprintf(lk, sizeof(lk), "LOCK %u", (unsigned)v.lockAt);
    label(c, ox + HALF_W - 12, oy + NUM_CY, lk, theme::fontLabel(), theme::ACCENT, lgfx::textdatum_t::middle_right);
  }

  // this player's side of the gauge, 1..10 outward from the middle
  for (int k = 0; k < 10; ++k) {
    const Rect r = cellRect(k);
    const int value = k + 1;
    const bool here = v.memory == value;
    const bool under = v.memory > value;  // the counter went past it
    const bool down = v.pressed == Hit::Cell && v.pressedValue == value;
    uint16_t fill = theme::BUTTON;
    if (under) fill = theme::BUTTON_DOWN;
    if (here) fill = pc;
    if (down) fill = theme::ACCENT;
    c.fillRoundRect(ox + r.x, oy + r.y, r.w, r.h, 7, fill);
    char b[4];
    snprintf(b, sizeof(b), "%d", value);
    label(c, ox + r.cx(), oy + r.cy() + 1, b, theme::fontButton(),
          (here || down) ? theme::TEXT_ON_ACCENT : (under ? theme::TEXT : theme::TEXT_DIM));
  }
}

HalfView s_drawn[2];
bool     s_drawnLock = false;
Hit      s_drawnHub = Hit::None;

void drawHubs(Hit pressed) {
  drawHubButton(HUB_X, LOCK_Y, HubIcon::LockUpright, pressed == Hit::Lock || game().lock);  // gold = locked
  drawHubButton(HUB_X, ZERO_Y, HubIcon::ZeroUpright, pressed == Hit::Zero);
  drawHubButton(HUB_X, MENU_Y, HubIcon::MenuUpright, pressed == Hit::Menu);
}

// ---------------------------------------------------------------- module functions
void onEnter() { s_press = NO_TOUCH; }

void handleInput(const InputEvent& e) {
  DigimonGame& g = game();
  switch (e.type) {
    case InputType::EncoderTurn:      digimonAdjust(g, g.turn, e.delta); break;
    case InputType::EncoderClick:     digimonSetLock(g, !g.lock); break;
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
  const Hit hub = (s_press.what == Hit::Lock || s_press.what == Hit::Zero || s_press.what == Hit::Menu)
                      ? s_press.what : Hit::None;
  bool hubDirty = full || hub != s_drawnHub || game().lock != s_drawnLock;
  bool started = false;
  if (full) {
    g.startWrite();
    started = true;
    g.fillScreen(theme::BG);
  }
  for (uint8_t i = 0; i < 2; ++i) {
    const HalfView v = viewOf(i);
    if (!full && v == s_drawn[i]) continue;
    if (!started) { g.startWrite(); started = true; }
    const HalfJob job = {i, &v};
    drawSeatCard(SEATS[i], paintHalf, &job);
    s_drawn[i] = v;
    hubDirty = true;  // the halves' edges lie under the round buttons
  }
  if (hubDirty) {
    if (!started) { g.startWrite(); started = true; }
    drawHubs(hub);
    s_drawnHub = hub;
    s_drawnLock = game().lock;
  }
  if (started) g.endWrite();
}

}  // namespace

const ScreenModule DigimonScreen = {"Digimon", onEnter, handleInput, render, nullptr};
