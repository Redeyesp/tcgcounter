/* ============================================================================
 *  ScreenScore — two-player score race: RIFTBOUND (first to 8 points) and
 *  LORCANA (first to 20 lore). One module serves both; which game is shown
 *  follows g_state.screen.
 *
 *  ┌──────────────────────────────────────┐
 *  │ ┌──┐                            ┌──┐ │  P1, upside down for the player
 *  │ │+ │          3 /8              │− │ │  across the table
 *  │ └──┘        PLAYER 1            └──┘ │
 *  ├────────────(≡)───(🎲)───(↻)───────────┤  ≡ menu · 🎲 dice · ↻ Restart
 *  │ ┌──┐        PLAYER 2            ┌──┐ │
 *  │ │− │          5 /8              │+ │ │  P2
 *  │ └──┘                            └──┘ │
 *  └──────────────────────────────────────┘
 *
 *  Touch:   tap/hold − / + = score (hold repeats) · tap a card = select it
 *           ≡ = menu: CONTINUE · HIGH ROLL (both cards show a D20 that spins
 *               and lands; the higher roll turns gold, a tie rolls again;
 *               tap to go back) · HOME
 *           🎲 = Dice page: D4 / D6 / D8 / D12 / D20, REROLL, BACK to the game
 *           ↻ = Restart (asks first; both back to 0)
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
#include "UiDice.h"
#include "Config.h"
#include <stdio.h>

namespace {

// ---------------------------------------------------------------- which game
struct ScoreMode {
  Screen      screen;
  uint8_t     target;
  ScoreGame& (*game)();
  const char* name;
};
ScoreGame& riftbound() { return g_state.riftbound; }
ScoreGame& lorcana()   { return g_state.lorcana; }
const ScoreMode MODES[] = {
  {SCREEN_RIFTBOUND, RIFTBOUND_TARGET, riftbound, "RIFTBOUND"},
  {SCREEN_LORCANA,   LORCANA_TARGET,   lorcana,   "LORCANA"},
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
constexpr int MENU_X = 100, DICE_X = 160, RESTART_X = 220;
constexpr int DIE_CY = 58, DIE_R = 34;   // high roll: D20 rows 24..92, clear of the buttons and the caption

// ---------------------------------------------------------------- hit test
enum class Zone : uint8_t { None, Area, Minus, Plus, Menu, Dice, Restart };
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
  if (inHub(x, y, MENU_X)) return {Zone::Menu, 0};
  if (inHub(x, y, DICE_X)) return {Zone::Dice, 0};
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
DiceOverlay   s_dice;

// ≡ menu page: CONTINUE / HIGH ROLL / HOME (same look as the home menu)
constexpr int8_t MENU_CONTINUE = 0, MENU_ROLL = 1, MENU_HOME = 2, MENU_ITEMS = 3;
bool   s_menuOpen = false;
int8_t s_menuFocus = MENU_CONTINUE, s_menuPressed = -1;
int8_t s_drawnMenuFocus = -1, s_drawnMenuPressed = -1;

Rect menuRect(int8_t i) { return Rect{16, (int16_t)(44 + i * 64), 288, 56}; }

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
int8_t   s_drawnPage = 0;  // full-screen page on screen: 0 table, 1 question, 2 dice, 3 menu

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
  drawHubButton(MENU_X, HUB_Y, HubIcon::Menu, pressed == Zone::Menu);
  drawHubButton(DICE_X, HUB_Y, HubIcon::Dice, pressed == Zone::Dice);
  drawHubButton(RESTART_X, HUB_Y, HubIcon::Restart, pressed == Zone::Restart);
}

// ---- ≡ menu page
void drawMenuItem(int8_t i) {
  auto& g = gfx();
  const Rect r = menuRect(i);
  const uint16_t fill = (i == s_menuPressed) ? theme::BUTTON_DOWN : theme::BUTTON;
  g.fillRect(r.x, r.y, r.w, r.h, theme::BG);
  g.fillRoundRect(r.x, r.y, r.w, r.h, 12, fill);
  if (i == s_menuFocus) uiRoundFrame(g, r.x, r.y, r.w, r.h, 12, 3, theme::ACCENT);
  static const char* const LABELS[MENU_ITEMS] = {"CONTINUE", "HIGH ROLL", "HOME"};
  g.setFont(theme::fontTitle());
  g.setTextDatum(lgfx::textdatum_t::middle_left);
  g.setTextColor(theme::TEXT);
  g.drawString(LABELS[i], r.x + 18, r.cy() + 1);
  uiChevron(g, r.x + r.w - 20, r.cy(), 18, 4, true, theme::TEXT_DIM);
}

void drawMenuPage() {
  auto& g = gfx();
  g.fillScreen(theme::BG);
  g.setFont(theme::fontButton());
  g.setTextDatum(lgfx::textdatum_t::middle_left);
  g.setTextColor(theme::TEXT_DIM);
  g.drawString(mode().name, 20, 22);
  for (int8_t i = 0; i < MENU_ITEMS; ++i) drawMenuItem(i);
}

void startHighRoll() { highRollStart(s_roll, SCORE_PLAYERS, millis(), rollD20); }

void chooseMenu(int8_t i) {
  s_menuOpen = false;
  if (i == MENU_ROLL) startHighRoll();
  else if (i == MENU_HOME) goToScreen(SCREEN_HOME);
}

void handleMenuInput(const InputEvent& e) {
  switch (e.type) {
    case InputType::EncoderTurn: {
      int f = (s_menuFocus + e.delta) % MENU_ITEMS;
      if (f < 0) f += MENU_ITEMS;
      s_menuFocus = (int8_t)f;
      break;
    }
    case InputType::EncoderClick:     chooseMenu(s_menuFocus); break;
    case InputType::EncoderLongPress: s_menuOpen = false; break;  // = CONTINUE
    case InputType::TouchDown:
      s_menuPressed = -1;
      for (int8_t i = 0; i < MENU_ITEMS; ++i)
        if (menuRect(i).contains(e.x, e.y)) s_menuPressed = i;
      if (s_menuPressed >= 0) s_menuFocus = s_menuPressed;
      break;
    case InputType::TouchUp: {
      const int8_t released = s_menuPressed;
      s_menuPressed = -1;
      if (released >= 0 && isTap(e, TOUCH_TAP_MAX_MS)) chooseMenu(released);
      break;
    }
    default: break;
  }
}

void openMenu() {
  s_menuOpen = true;
  s_menuFocus = MENU_CONTINUE;
  s_menuPressed = -1;
}

// While a high roll is on screen: nothing works until the dice land; then
// the first touch or encoder action only goes back to the scores.
void handleRollInput(const InputEvent& e) {
  if (highRollBusy(s_roll)) { s_press = NO_HIT; return; }
  switch (e.type) {
    case InputType::TouchDown:
      s_press = NO_HIT;
      highRollStop(s_roll);  // this touch is used up: it only closes the result
      break;
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
  s_dice.close();
  s_menuOpen = false;
  highRollStop(s_roll);
}

void handleInput(const InputEvent& e) {
  if (s_menuOpen) { handleMenuInput(e); return; }
  if (s_dice.isOpen()) { s_dice.handleInput(e); return; }  // BACK closes it: the table repaints
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
      if (released.zone == Zone::Menu) openMenu();
      else if (released.zone == Zone::Dice) s_dice.open();
      else if (released.zone == Zone::Restart) askRestart();
      break;
    }
    default:
      break;
  }
}

void render(bool full) {
  const int8_t page = s_menuOpen ? 3 : s_dice.isOpen() ? 2 : s_restart.isOpen() ? 1 : 0;
  const bool pageChanged = page != s_drawnPage;
  s_drawnPage = page;
  if (page == 1) { s_restart.render(full || pageChanged); return; }
  if (page == 2) { s_dice.render(full || pageChanged); return; }
  if (page == 3) {
    if (full || pageChanged) {
      gfx().startWrite();
      drawMenuPage();
      gfx().endWrite();
    } else if (s_menuFocus != s_drawnMenuFocus || s_menuPressed != s_drawnMenuPressed) {
      gfx().startWrite();
      for (int8_t i = 0; i < MENU_ITEMS; ++i) {
        const bool was = (i == s_drawnMenuFocus || i == s_drawnMenuPressed);
        const bool is  = (i == s_menuFocus || i == s_menuPressed);
        if (was || is) drawMenuItem(i);
      }
      gfx().endWrite();
    }
    s_drawnMenuFocus = s_menuFocus;
    s_drawnMenuPressed = s_menuPressed;
    return;
  }
  full = full || pageChanged;  // back from a page: repaint the table

  auto& g = gfx();
  const bool onHub = s_press.zone == Zone::Menu || s_press.zone == Zone::Dice ||
                     s_press.zone == Zone::Restart;
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
  s_dice.tick(now);
}

}  // namespace

const ScreenModule RiftboundScreen = {"Riftbound", onEnter, handleInput, render, tick};
const ScreenModule LorcanaScreen   = {"Lorcana", onEnter, handleInput, render, tick};
