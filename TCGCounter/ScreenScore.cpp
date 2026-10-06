/* ============================================================================
 *  ScreenScore — two-player score race: RIFTBOUND (first to 8 points) and
 *  LORCANA (first to 20 lore). One module serves both; which game is shown
 *  follows g_state.screen.
 *
 *  ┌──────────────────────────────────────┐
 *  │ ┌──┐                            ┌──┐ │  P1, upside down for the player
 *  │ │+ │          3 /8              │− │ │  across the table
 *  │ └──┘        PLAYER 1            └──┘ │
 *  ├──────────(⌂)───(H)───(↻)─────────────┤  ⌂ = Home  H = high roll  ↻ = Restart
 *  │ ┌──┐        PLAYER 2            ┌──┐ │
 *  │ │− │          5 /8              │+ │ │  P2
 *  │ └──┘                            └──┘ │
 *  └──────────────────────────────────────┘
 *
 *  Touch:   tap/hold − / + = score (hold repeats) · tap a card = select it
 *           ⌂ = Home · ↻ = Restart (asks first; both back to 0)
 *           H = high roll: both cards show a D20 that spins and lands, the
 *           higher roll turns gold (a tie rolls again); tap to go back
 *  Encoder: turn = selected player's score · press = other player
 *           long-press = Restart (asks first)
 *  Reaching the target turns the card gold: WINNER!  − still works, so a
 *  mis-tap can be taken back.
 * ==========================================================================*/
#include <Arduino.h>
#include "Screens.h"
#include "App.h"
#include "GameState.h"
#include "CommanderLayout.h"
#include "TableDraw.h"
#include "HighRoll.h"
#include "Theme.h"
#include "Ui.h"
#include "UiConfirm.h"
#include "Config.h"
#include <stdio.h>

namespace {

// ---------------------------------------------------------------- which game
struct ScoreMode {
  Screen  screen;
  uint8_t target;
  ScoreGame& (*game)();
};
ScoreGame& riftbound() { return g_state.riftbound; }
ScoreGame& lorcana()   { return g_state.lorcana; }
const ScoreMode MODES[] = {
  {SCREEN_RIFTBOUND, RIFTBOUND_TARGET, riftbound},
  {SCREEN_LORCANA,   LORCANA_TARGET,   lorcana},
};
const ScoreMode& mode() { return g_state.screen == SCREEN_LORCANA ? MODES[1] : MODES[0]; }

// ---------------------------------------------------------------- geometry
// Same two seats as a 2-player Commander table: P1 top (facing the far
// side), P2 bottom. Card coordinates as the player sees it: 320 x 119.
const Seat& seat(uint8_t p) { return tableLayout(2).seats[p]; }

constexpr int CARD_W = 320, CARD_H = 119;
constexpr int FRAME_R = 10, FRAME_THICK = 4;
constexpr Rect BTN_MINUS = {10, 10, 60, 99};   // tall buttons at the card's sides
constexpr Rect BTN_PLUS  = {250, 10, 60, 99};
constexpr int  MINUS_ZONE_END = 80, PLUS_ZONE_START = 240;  // touch zones (card columns)
constexpr int  NUM_CX = 160, NUM_CY = 58;       // centre of the digits (rows 23..92)
constexpr int  CAPTION_Y = 105;  // rows ~99..111, below the digits
constexpr int  NUM_MAX_W = PLUS_ZONE_START - MINUS_ZONE_END - 4;

// Centre buttons on the line between the cards. The card rows above the
// digits (0..22) stay empty there, so the buttons never cover a number.
constexpr int HUB_Y = 120;
constexpr int HOME_X = 100, ROLL_X = 160, RESTART_X = 220;
constexpr int DIE_CY = 58, DIE_R = 34;   // high roll: D20 rows 24..92, clear of the buttons and the caption

// ---------------------------------------------------------------- hit test
enum class Zone : uint8_t { None, Area, Minus, Plus, Home, Roll, Restart };
struct Hit {
  Zone    zone;
  uint8_t player;
};
constexpr Hit NO_HIT = {Zone::None, 0};

bool inHub(int x, int y, int hx) {
  const int dx = x - hx, dy = y - HUB_Y;
  return dx * dx + dy * dy <= HUB_HIT_R * HUB_HIT_R;
}

Hit hitTest(int x, int y) {
  if (inHub(x, y, HOME_X)) return {Zone::Home, 0};
  if (inHub(x, y, ROLL_X)) return {Zone::Roll, 0};
  if (inHub(x, y, RESTART_X)) return {Zone::Restart, 0};
  const uint8_t p = y >= HUB_Y ? 1 : 0;  // the 2 px gap belongs to the nearer card
  int lx, ly;
  seatToLocal(seat(p), x, y, lx, ly);
  if (lx < MINUS_ZONE_END) return {Zone::Minus, p};
  if (lx >= PLUS_ZONE_START) return {Zone::Plus, p};
  return {Zone::Area, p};
}

// ---------------------------------------------------------------- UI-only state
Hit           s_press = NO_HIT;
ConfirmDialog s_restart;
HighRoll      s_roll;

uint8_t rollD20() { return (uint8_t)random(1, HIGHROLL_SIDES + 1); }  // hardware RNG on the ESP32

struct CardView {
  uint8_t score;
  bool    won;
  bool    highlight;
  Zone    pressed;  // Minus / Plus / None
  DieState die;     // high roll running: how this player's D20 looks
  uint8_t  face;
  bool operator==(const CardView& o) const {
    return score == o.score && won == o.won && highlight == o.highlight && pressed == o.pressed &&
           die == o.die && face == o.face;
  }
  bool operator!=(const CardView& o) const { return !(*this == o); }
};

CardView s_drawn[SCORE_PLAYERS];
Zone     s_drawnHubPressed = Zone::None;
bool     s_drawnDialog = false;

CardView viewOf(uint8_t p) {
  const ScoreGame& g = mode().game();
  CardView v;
  v.score = g.score[p];
  v.won = scoreHasWon(g, p, mode().target);
  v.highlight = (g.selected == p);
  const bool onButton = s_press.zone == Zone::Minus || s_press.zone == Zone::Plus;
  v.pressed = (onButton && s_press.player == p) ? s_press.zone : Zone::None;
  v.die = highRollDie(s_roll, p);
  v.face = v.die != DieState::None ? s_roll.value[p] : 0;
  if (v.die != DieState::None) v.pressed = Zone::None;
  return v;
}

// ---------------------------------------------------------------- actions
void adjust(uint8_t p, int delta) { scoreAdjust(mode().game(), p, delta, mode().target); }

void askRestart() {
  ScoreGame& g = mode().game();
  if (scoreIsFresh(g)) {  // already 0 : 0 — nothing to lose, nothing to ask
    scoreNewGame(g);
    return;
  }
  char body[40];
  snprintf(body, sizeof(body), "Both players back to 0 / %u", (unsigned)mode().target);
  s_restart.open("RESTART?", body, "The current game will be lost.", "RESTART");
}

// ---------------------------------------------------------------- drawing
struct CardJob { uint8_t p; const CardView* v; };

void drawButton(lgfx::LovyanGFX& c, int ox, int oy, uint8_t p, Zone which, bool pressed) {
  const Rect& r = which == Zone::Minus ? BTN_MINUS : BTN_PLUS;
  const uint16_t fill = pressed ? theme::PLAYER[p] : theme::BUTTON;
  const uint16_t sym  = pressed ? theme::TEXT_ON_ACCENT : theme::TEXT;
  c.fillRoundRect(ox + r.x, oy + r.y, r.w, r.h, 8, fill);
  if (which == Zone::Minus) uiMinus(c, ox + r.cx(), oy + r.cy(), 26, 5, sym);
  else                      uiPlus(c, ox + r.cx(), oy + r.cy(), 26, 5, sym);
}

// High roll: the D20 replaces the score and the − / + buttons.
void paintRoll(lgfx::LovyanGFX& c, int ox, int oy, uint8_t p, const CardView& v) {
  const bool won = v.die == DieState::Winner;
  c.fillRect(ox, oy, CARD_W, CARD_H, theme::BG);
  c.fillRoundRect(ox, oy, CARD_W, CARD_H, FRAME_R, won ? theme::WIN_PANEL : theme::PANEL);
  if (won) uiRoundFrame(c, ox, oy, CARD_W, CARD_H, FRAME_R, FRAME_THICK, theme::ACCENT);
  else     uiRoundFrame(c, ox, oy, CARD_W, CARD_H, FRAME_R, 1, theme::PANEL_EDGE);
  drawD20(c, ox + NUM_CX, oy + DIE_CY, DIE_R, v.die, theme::PLAYER[p], v.face);
  char cap[12];
  c.setTextDatum(lgfx::textdatum_t::middle_center);
  c.setFont(theme::fontLabel());
  if (won) {
    snprintf(cap, sizeof(cap), "HIGH ROLL!");
    c.setTextColor(theme::ACCENT);
  } else if (v.die == DieState::Tied) {
    snprintf(cap, sizeof(cap), "TIE!");
    c.setTextColor(theme::ACCENT);
  } else {
    snprintf(cap, sizeof(cap), "PLAYER %u", (unsigned)(p + 1));
    c.setTextColor(theme::PLAYER[p]);
  }
  c.drawString(cap, ox + NUM_CX, oy + CAPTION_Y);
}

void paintCard(lgfx::LovyanGFX& c, int ox, int oy, const void* ctx) {
  const CardJob& j = *static_cast<const CardJob*>(ctx);
  const CardView& v = *j.v;
  const uint8_t p = j.p;
  if (v.die != DieState::None) { paintRoll(c, ox, oy, p, v); return; }

  c.fillRect(ox, oy, CARD_W, CARD_H, theme::BG);
  c.fillRoundRect(ox, oy, CARD_W, CARD_H, FRAME_R, v.won ? theme::WIN_PANEL : theme::PANEL);
  if (v.highlight) uiRoundFrame(c, ox, oy, CARD_W, CARD_H, FRAME_R, FRAME_THICK, theme::PLAYER[p]);
  else             uiRoundFrame(c, ox, oy, CARD_W, CARD_H, FRAME_R, 1, theme::PANEL_EDGE);
  drawButton(c, ox, oy, p, Zone::Minus, v.pressed == Zone::Minus);
  drawButton(c, ox, oy, p, Zone::Plus,  v.pressed == Zone::Plus);

  // "5 /8": big digits, then the target sitting on their baseline
  char num[4], target[6];
  snprintf(num, sizeof(num), "%u", (unsigned)v.score);
  snprintf(target, sizeof(target), "/%u", (unsigned)mode().target);
  uint8_t fontCount = 0;
  const theme::NumberFont& f = theme::numberFonts(fontCount)[0];  // the big one (Font8)
  c.setFont(f.font);
  const int numW = c.textWidth(num) + (theme::LIFE_FAUX_BOLD ? 1 : 0);
  const lgfx::IFont* tf = theme::fontButton();
  c.setFont(tf);
  if (numW + 6 + c.textWidth(target) > NUM_MAX_W) { tf = theme::fontSmall(); c.setFont(tf); }
  const int left = NUM_CX - (numW + 6 + c.textWidth(target)) / 2;
  const uint16_t numCol = v.won ? theme::ACCENT : theme::TEXT;
  const int y = NUM_CY - f.top - f.height / 2;
  c.setFont(f.font);
  c.setTextDatum(lgfx::textdatum_t::top_left);
  c.setTextColor(numCol);
  c.drawString(num, ox + left, oy + y);
  if (theme::LIFE_FAUX_BOLD) c.drawString(num, ox + left + 1, oy + y);
  c.setFont(tf);
  c.setTextDatum(lgfx::textdatum_t::baseline_left);
  c.setTextColor(v.won ? theme::ACCENT : theme::TEXT_DIM);
  c.drawString(target, ox + left + numW + 6, oy + NUM_CY + f.height / 2);

  // caption: who this is, or that they won
  char cap[12];
  c.setTextDatum(lgfx::textdatum_t::middle_center);
  c.setFont(theme::fontLabel());
  if (v.won) {
    snprintf(cap, sizeof(cap), "WINNER!");
    c.setTextColor(theme::ACCENT);
  } else {
    snprintf(cap, sizeof(cap), "PLAYER %u", (unsigned)(p + 1));
    c.setTextColor(theme::PLAYER[p]);
  }
  c.drawString(cap, ox + NUM_CX, oy + CAPTION_Y);
}

void drawCard(uint8_t p, const CardView& v) {
  const CardJob job = {p, &v};
  drawSeatCard(seat(p), paintCard, &job);
}

void drawHubs(Zone pressed) {
  drawHubButton(HOME_X, HUB_Y, HubIcon::Home, pressed == Zone::Home);
  drawHubButton(ROLL_X, HUB_Y, HubIcon::HighRoll, pressed == Zone::Roll);
  drawHubButton(RESTART_X, HUB_Y, HubIcon::Restart, pressed == Zone::Restart);
}

void startHighRoll() { highRollStart(s_roll, SCORE_PLAYERS, millis(), rollD20); }

// While a high roll is on screen: nothing works until the dice land; then
// any touch or encoder action goes back to the scores, and H rolls again.
void handleRollInput(const InputEvent& e) {
  if (highRollBusy(s_roll)) { s_press = NO_HIT; return; }
  switch (e.type) {
    case InputType::TouchDown: {
      const Hit h = hitTest(e.x, e.y);
      s_press = h.zone == Zone::Roll ? h : NO_HIT;
      if (h.zone != Zone::Roll) highRollStop(s_roll);  // this touch only closes the result
      break;
    }
    case InputType::TouchUp: {
      const Hit released = s_press;
      s_press = NO_HIT;
      if (released.zone == Zone::Roll && isTap(e, TOUCH_TAP_MAX_MS)) startHighRoll();
      break;
    }
    case InputType::EncoderTurn:
    case InputType::EncoderClick:
    case InputType::EncoderLongPress:
      highRollStop(s_roll);
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------- module functions
void onEnter() {
  s_press = NO_HIT;
  s_restart.close();
  highRollStop(s_roll);
}

void handleInput(const InputEvent& e) {
  if (highRollActive(s_roll)) { handleRollInput(e); return; }
  if (s_restart.isOpen()) {
    if (s_restart.handleInput(e) == ConfirmResult::Confirm) scoreNewGame(mode().game());
    return;
  }
  ScoreGame& g = mode().game();
  switch (e.type) {
    case InputType::EncoderTurn:
      adjust(g.selected, e.delta);
      break;
    case InputType::EncoderClick:
      scoreSelectNext(g);
      break;
    case InputType::EncoderLongPress:
      askRestart();
      break;
    case InputType::TouchDown:
      s_press = hitTest(e.x, e.y);
      if (s_press.zone == Zone::Area || s_press.zone == Zone::Minus || s_press.zone == Zone::Plus)
        scoreSelect(g, s_press.player);
      if (s_press.zone == Zone::Minus) adjust(s_press.player, -1);
      if (s_press.zone == Zone::Plus)  adjust(s_press.player, +1);
      break;
    case InputType::TouchRepeat:  // hold-to-repeat on − / +
      if (s_press.zone == Zone::Minus) adjust(s_press.player, -1);
      if (s_press.zone == Zone::Plus)  adjust(s_press.player, +1);
      break;
    case InputType::TouchUp: {
      const Hit released = s_press;
      s_press = NO_HIT;
      if (!isTap(e, TOUCH_TAP_MAX_MS)) break;
      if (released.zone == Zone::Home) goToScreen(SCREEN_HOME);
      else if (released.zone == Zone::Roll) startHighRoll();
      else if (released.zone == Zone::Restart) askRestart();
      break;
    }
    default:
      break;
  }
}

void render(bool full) {
  const bool dialog = s_restart.isOpen();
  const bool pageChanged = dialog != s_drawnDialog;
  s_drawnDialog = dialog;
  if (dialog) {
    s_restart.render(full || pageChanged);
    return;
  }
  full = full || pageChanged;  // back from the question: repaint the table

  auto& g = gfx();
  const bool onHub = s_press.zone == Zone::Home || s_press.zone == Zone::Roll || s_press.zone == Zone::Restart;
  const Zone hubPressed = onHub ? s_press.zone : Zone::None;
  bool hubDirty = full || hubPressed != s_drawnHubPressed;
  bool started = false;
  if (full) {
    g.startWrite();
    started = true;
    g.fillScreen(theme::BG);
  }
  for (uint8_t p = 0; p < SCORE_PLAYERS; ++p) {
    const CardView v = viewOf(p);
    if (!full && v == s_drawn[p]) continue;
    if (!started) { g.startWrite(); started = true; }
    drawCard(p, v);
    s_drawn[p] = v;
    hubDirty = true;  // the card edges lie under the centre buttons
  }
  if (hubDirty) {
    if (!started) { g.startWrite(); started = true; }
    drawHubs(hubPressed);
    s_drawnHubPressed = hubPressed;
  }
  if (started) g.endWrite();
}

void tick(uint32_t now) {
  if (highRollActive(s_roll)) highRollUpdate(s_roll, now, rollD20);
}

}  // namespace

const ScreenModule RiftboundScreen = {"Riftbound", onEnter, handleInput, render, tick};
const ScreenModule LorcanaScreen   = {"Lorcana", onEnter, handleInput, render, tick};
