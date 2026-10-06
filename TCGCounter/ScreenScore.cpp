/* ============================================================================
 *  ScreenScore — score race: RIFTBOUND (1v1 or 4 players to 8, 2v2 teams to
 *  11) and LORCANA (2 or 4 players, 20 or 25 lore). One module serves both;
 *  which game is shown follows g_state.screen. The game's format is chosen
 *  in its menu (ScreenScoreSetup.cpp). Where the cards sit: ScoreLayout.h.
 *
 *  2 cards                                  4 cards
 *  ┌──────────────────────────────────────┐ ┌─────────────┬─────────────┐
 *  │ ┌──┐                            ┌──┐ │ │  PLAYER 2   ↻   PLAYER 1  │
 *  │ │+ │    (+1)  3 /8              │− │ │ │   3 /8      │    5 /8     │
 *  │ └──┘        PLAYER 1            └──┘ │ │  [−] [+]    │   [−] [+]   │
 *  ├────────────(≡)───(🎲)───(↻)───────────┤ ├─────────────≡─────────────┤
 *  │ ┌──┐        PLAYER 2            ┌──┐ │ │  PLAYER 3   │   PLAYER 4  │
 *  │ │− │          5 /8  (+1)        │+ │ │ │   0 /8      🎲    6 /8     │
 *  │ └──┘                            └──┘ │ │  [−] [+]    │   [−] [+]   │
 *  └──────────────────────────────────────┘ └─────────────┴─────────────┘
 *  Every card faces the player at its edge (top row upside down).
 *
 *  Touch:   tap/hold − / + = score (hold repeats) · tap a card = select it
 *           +1 (Riftbound) = plus life on / off: one extra point, at most
 *               one per player (team), so a score can end one past the
 *               target (9 /8)
 *           ≡ = the game's menu: CONTINUE · HIGH ROLL · new game · HOME
 *           🎲 = Dice page: D4 / D6 / D8 / D12 / D20, REROLL, BACK to the game
 *           ↻ = Restart (asks first; everyone back to 0)
 *  Encoder: turn = selected player's score · press = next player
 *           long-press = Restart (asks first)
 *  High roll (≡ -> HIGH ROLL): every card shows a D20 that spins and lands;
 *  the highest turns gold, a tie rolls again; tap to go back.
 *  Reaching the target turns the card gold: WINNER!  − still works, so a
 *  mis-tap can be taken back.
 * ==========================================================================*/
#include <Arduino.h>
#include "Screens.h"
#include "App.h"
#include "GameState.h"
#include "ScoreLayout.h"
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
  Screen      menu;   // ≡ goes here
  ScoreGame& (*game)();
  bool        bonus;  // +1 plus life buttons (Riftbound)
};
ScoreGame& riftbound() { return g_state.riftbound; }
ScoreGame& lorcana()   { return g_state.lorcana; }
const ScoreMode MODES[] = {
  {SCREEN_RIFTBOUND, SCREEN_RIFTBOUND_SETUP, riftbound, true},
  {SCREEN_LORCANA,   SCREEN_LORCANA_SETUP,   lorcana,   false},
};
const ScoreMode& mode() { return g_state.screen == SCREEN_LORCANA ? MODES[1] : MODES[0]; }
ScoreGame& game() { return mode().game(); }
const TableLayout& table() { return scoreTable(game().players); }

constexpr int FRAME_R = 10, FRAME_THICK = 4;

// ---------------------------------------------------------------- UI-only state
ScoreHit      s_press = SCORE_NO_HIT;
ConfirmDialog s_restart;
HighRoll      s_roll;
DiceOverlay   s_dice;
bool          s_rollOnEnter = false;  // the menu asked for a high roll

uint8_t rollD20() { return (uint8_t)random(1, HIGHROLL_SIDES + 1); }  // hardware RNG on the ESP32

struct CardView {
  uint8_t   total;      // score + plus life
  bool      bonus;      // plus life taken
  bool      won;
  bool      highlight;
  ScoreZone pressed;    // Minus / Plus / Bonus / None
  DieState  die;        // high roll running: how this player's D20 looks
  uint8_t   face;
  bool operator==(const CardView& o) const {
    return total == o.total && bonus == o.bonus && won == o.won && highlight == o.highlight &&
           pressed == o.pressed && die == o.die && face == o.face;
  }
  bool operator!=(const CardView& o) const { return !(*this == o); }
};

CardView s_drawn[SCORE_MAX_PLAYERS];
int8_t   s_drawnHubPressed = -1;
int8_t   s_drawnPage = 0;  // full-screen page on screen: 0 table, 1 question, 2 dice

CardView viewOf(uint8_t p) {
  const ScoreGame& g = game();
  CardView v;
  v.total = scoreTotal(g, p);
  v.bonus = g.bonus[p] != 0;
  v.won = scoreHasWon(g, p);
  v.highlight = (g.selected == p);
  const bool onCard = s_press.zone == ScoreZone::Minus || s_press.zone == ScoreZone::Plus ||
                      s_press.zone == ScoreZone::Bonus;
  v.pressed = (onCard && s_press.index == p) ? s_press.zone : ScoreZone::None;
  v.die = highRollDie(s_roll, p);
  v.face = v.die != DieState::None ? s_roll.value[p] : 0;
  if (v.die != DieState::None) v.pressed = ScoreZone::None;
  return v;
}

// ---------------------------------------------------------------- actions
void askRestart() {
  ScoreGame& g = game();
  if (scoreIsFresh(g)) {  // already all at 0 — nothing to lose, nothing to ask
    scoreRestart(g);
    return;
  }
  char body[40];
  const char* who = g.teams ? "Both teams" : (g.players == 2 ? "Both players" : "Everyone");
  snprintf(body, sizeof(body), "%s back to 0 / %u", who, (unsigned)g.target);
  s_restart.open("RESTART?", body, "The current game will be lost.", "RESTART");
}

void startHighRoll() { highRollStart(s_roll, game().players, millis(), rollD20); }

// ---------------------------------------------------------------- drawing a card
struct CardJob { const ScoreGeom* g; uint8_t p; const CardView* v; };

void nameText(uint8_t p, char* buf, size_t n) {
  snprintf(buf, n, game().teams ? "TEAM %u" : "PLAYER %u", (unsigned)(p + 1));
}

void drawPanel(lgfx::LovyanGFX& c, int ox, int oy, const ScoreGeom& g, uint16_t fill,
               bool thick, uint16_t frame) {
  c.fillRect(ox, oy, g.w, g.h, theme::BG);
  c.fillRoundRect(ox, oy, g.w, g.h, FRAME_R, fill);
  uiRoundFrame(c, ox, oy, g.w, g.h, FRAME_R, thick ? FRAME_THICK : 1, frame);
}

void drawButton(lgfx::LovyanGFX& c, int ox, int oy, const ScoreGeom& g, uint8_t p, ScoreZone which,
                bool pressed) {
  const Rect& r = which == ScoreZone::Minus ? g.minus : g.plus;
  const Rect b = {(int16_t)(ox + r.x), (int16_t)(oy + r.y), r.w, r.h};
  const uint16_t fill = pressed ? theme::PLAYER[p] : theme::BUTTON;
  const uint16_t sym  = pressed ? theme::TEXT_ON_ACCENT : theme::TEXT;
  const bool big = g.wide;
  c.fillRoundRect(b.x, b.y, b.w, b.h, 8, fill);
  if (which == ScoreZone::Minus) uiMinus(c, b.cx(), b.cy(), big ? 26 : 18, big ? 5 : 4, sym);
  else                           uiPlus(c, b.cx(), b.cy(), big ? 26 : 18, big ? 5 : 4, sym);
}

// +1 plus life: gold once taken.
void drawBonus(lgfx::LovyanGFX& c, int ox, int oy, const ScoreGeom& g, const CardView& v) {
  const Rect b = {(int16_t)(ox + g.bonus.x), (int16_t)(oy + g.bonus.y), g.bonus.w, g.bonus.h};
  const bool pressed = v.pressed == ScoreZone::Bonus;
  uint16_t fill = theme::BUTTON, text = theme::TEXT;
  if (v.bonus) { fill = theme::ACCENT; text = theme::TEXT_ON_ACCENT; }
  if (pressed) { fill = theme::BUTTON_DOWN; text = theme::TEXT; }
  c.fillRoundRect(b.x, b.y, b.w, b.h, b.h / 2, fill);
  // pixel-drawn "+" (the font's one sits too high and small), then "1"
  uiPlus(c, b.cx() - 6, b.cy(), 9, 3, text);
  c.setFont(theme::fontLabel());
  c.setTextDatum(lgfx::textdatum_t::middle_center);
  c.setTextColor(text);
  c.drawString("1", b.cx() + 6, b.cy() + 1);
}

// "5 /8": the score's digits, then the target sitting on their baseline.
void drawScore(lgfx::LovyanGFX& c, int ox, int oy, const ScoreGeom& g, const CardView& v) {
  char num[4], target[6];
  snprintf(num, sizeof(num), "%u", (unsigned)v.total);
  snprintf(target, sizeof(target), "/%u", (unsigned)game().target);
  uint8_t fontCount = 0;
  const theme::NumberFont* fonts = theme::numberFonts(fontCount);
  // biggest digits whose height fits, then the target in the bigger text font if it fits
  const theme::NumberFont* f = &fonts[fontCount - 1];
  for (uint8_t k = 0; k < fontCount; ++k)
    if (fonts[k].height <= g.numMaxH) { f = &fonts[k]; break; }
  int numW = 0;
  const lgfx::IFont* tf = theme::fontButton();
  for (uint8_t k = (uint8_t)(f - fonts); k < fontCount; ++k) {
    f = &fonts[k];
    c.setFont(f->font);
    numW = c.textWidth(num) + (theme::LIFE_FAUX_BOLD ? 1 : 0);
    tf = theme::fontButton();
    c.setFont(tf);
    if (numW + 6 + c.textWidth(target) > g.numMaxW) { tf = theme::fontSmall(); c.setFont(tf); }
    if (numW + 6 + c.textWidth(target) <= g.numMaxW) break;
  }
  c.setFont(tf);
  const int left = g.numCx - (numW + 6 + c.textWidth(target)) / 2;
  const uint16_t numCol = v.won ? theme::ACCENT : theme::TEXT;
  const int y = g.numCy - f->top - f->height / 2;
  c.setFont(f->font);
  c.setTextDatum(lgfx::textdatum_t::top_left);
  c.setTextColor(numCol);
  c.drawString(num, ox + left, oy + y);
  if (theme::LIFE_FAUX_BOLD) c.drawString(num, ox + left + 1, oy + y);
  c.setFont(tf);
  c.setTextDatum(lgfx::textdatum_t::baseline_left);
  c.setTextColor(v.won ? theme::ACCENT : theme::TEXT_DIM);
  c.drawString(target, ox + left + numW + 6, oy + g.numCy + f->height / 2);
}

// Compact cards: name pill at the top ("WINNER!" once won).
void drawLabel(lgfx::LovyanGFX& c, int ox, int oy, const ScoreGeom& g, uint8_t p, const char* text,
               bool filled, uint16_t textCol) {
  const Rect r = {(int16_t)(ox + g.label.x), (int16_t)(oy + g.label.y), g.label.w, g.label.h};
  if (filled) c.fillRoundRect(r.x, r.y, r.w, r.h, r.h / 2, theme::PLAYER[p]);
  c.setFont(theme::fontLabel());
  c.setTextDatum(lgfx::textdatum_t::middle_center);
  c.setTextColor(filled ? theme::TEXT_ON_ACCENT : textCol);
  c.drawString(text, r.cx(), r.cy() + 1);
}

// High roll: the D20 replaces the score and the buttons.
void paintRoll(lgfx::LovyanGFX& c, int ox, int oy, const ScoreGeom& g, uint8_t p, const CardView& v) {
  const bool won = v.die == DieState::Winner;
  drawPanel(c, ox, oy, g, won ? theme::WIN_PANEL : theme::PANEL, won,
            won ? theme::ACCENT : theme::PANEL_EDGE);
  drawD20(c, ox + g.dieCx, oy + g.dieCy, g.dieR, v.die, theme::PLAYER[p], v.face);
  char name[12];
  nameText(p, name, sizeof(name));
  if (!g.wide) {  // name pill, filled in the player's colour for the winner
    drawLabel(c, ox, oy, g, p, name, won, theme::PLAYER[p]);
    return;
  }
  const char* cap = name;
  uint16_t col = theme::PLAYER[p];
  if (won) { cap = "HIGH ROLL!"; col = theme::ACCENT; }
  else if (v.die == DieState::Tied) { cap = "TIE!"; col = theme::ACCENT; }
  c.setTextDatum(lgfx::textdatum_t::middle_center);
  c.setFont(theme::fontLabel());
  c.setTextColor(col);
  c.drawString(cap, ox + g.dieCx, oy + g.capY);
}

void paintCard(lgfx::LovyanGFX& c, int ox, int oy, const void* ctx) {
  const CardJob& j = *static_cast<const CardJob*>(ctx);
  const ScoreGeom& g = *j.g;
  const CardView& v = *j.v;
  const uint8_t p = j.p;
  if (v.die != DieState::None) { paintRoll(c, ox, oy, g, p, v); return; }

  drawPanel(c, ox, oy, g, v.won ? theme::WIN_PANEL : theme::PANEL, v.highlight,
            v.highlight ? theme::PLAYER[p] : theme::PANEL_EDGE);
  drawButton(c, ox, oy, g, p, ScoreZone::Minus, v.pressed == ScoreZone::Minus);
  drawButton(c, ox, oy, g, p, ScoreZone::Plus,  v.pressed == ScoreZone::Plus);
  drawScore(c, ox, oy, g, v);

  char name[12];
  nameText(p, name, sizeof(name));
  if (g.wide) {  // caption under the score: who this is, or that they won
    c.setTextDatum(lgfx::textdatum_t::middle_center);
    c.setFont(theme::fontLabel());
    c.setTextColor(v.won ? theme::ACCENT : theme::PLAYER[p]);
    c.drawString(v.won ? "WINNER!" : name, ox + g.capX, oy + g.capY);
  } else {
    drawLabel(c, ox, oy, g, p, v.won ? "WINNER!" : name, v.highlight,
              v.won ? theme::ACCENT : theme::PLAYER[p]);
  }
  if (g.bonus.w > 0) drawBonus(c, ox, oy, g, v);
}

void drawCard(uint8_t p, const CardView& v) {
  const Seat& s = table().seats[p];
  const ScoreGeom g = scoreGeom(s, mode().bonus);
  const CardJob job = {&g, p, &v};
  drawSeatCard(s, paintCard, &job);  // off-screen, turned to face the player, one copy
}

void drawHubs(int8_t pressed) {
  const TableLayout& L = table();
  for (uint8_t k = 0; k < L.hubCount; ++k) {
    const HubPos& h = L.hubs[k];
    const HubIcon icon = h.kind == HubKind::Menu ? HubIcon::Menu
                       : h.kind == HubKind::Dice ? HubIcon::Dice : HubIcon::Restart;
    drawHubButton(h.x, h.y, icon, pressed == (int8_t)k);
  }
}

// While a high roll is on screen: nothing works until the dice land; then
// the first touch or encoder action only goes back to the scores.
void handleRollInput(const InputEvent& e) {
  if (highRollBusy(s_roll)) { s_press = SCORE_NO_HIT; return; }
  switch (e.type) {
    case InputType::TouchDown:
      s_press = SCORE_NO_HIT;
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

void tapHub(uint8_t k) {
  switch (table().hubs[k].kind) {
    case HubKind::Menu:    goToScreen(mode().menu); break;
    case HubKind::Dice:    s_dice.open(); break;
    case HubKind::Restart: askRestart(); break;
  }
}

// ---------------------------------------------------------------- module functions
void onEnter() {
  s_press = SCORE_NO_HIT;
  s_restart.close();
  s_dice.close();
  highRollStop(s_roll);
  if (s_rollOnEnter) {
    s_rollOnEnter = false;
    startHighRoll();
  }
}

void handleInput(const InputEvent& e) {
  if (s_dice.isOpen()) { s_dice.handleInput(e); return; }  // BACK closes it: the table repaints
  if (highRollActive(s_roll)) { handleRollInput(e); return; }
  if (s_restart.isOpen()) {
    if (s_restart.handleInput(e) == ConfirmResult::Confirm) scoreRestart(game());
    return;
  }
  ScoreGame& g = game();
  switch (e.type) {
    case InputType::EncoderTurn:
      scoreAdjust(g, g.selected, e.delta);
      break;
    case InputType::EncoderClick:
      scoreSelectNext(g);
      break;
    case InputType::EncoderLongPress:
      askRestart();
      break;
    case InputType::TouchDown:
      s_press = scoreHitTest(g.players, mode().bonus, e.x, e.y);
      if (s_press.zone != ScoreZone::None && s_press.zone != ScoreZone::Hub)
        scoreSelect(g, s_press.index);
      if (s_press.zone == ScoreZone::Minus) scoreAdjust(g, s_press.index, -1);
      if (s_press.zone == ScoreZone::Plus)  scoreAdjust(g, s_press.index, +1);
      break;
    case InputType::TouchRepeat:  // hold-to-repeat on − / +
      if (s_press.zone == ScoreZone::Minus) scoreAdjust(g, s_press.index, -1);
      if (s_press.zone == ScoreZone::Plus)  scoreAdjust(g, s_press.index, +1);
      break;
    case InputType::TouchUp: {
      const ScoreHit released = s_press;
      s_press = SCORE_NO_HIT;
      if (!isTap(e, TOUCH_TAP_MAX_MS)) break;
      if (released.zone == ScoreZone::Hub) tapHub(released.index);
      else if (released.zone == ScoreZone::Bonus) scoreToggleBonus(g, released.index);
      break;
    }
    default:
      break;
  }
}

void render(bool full) {
  const int8_t page = s_dice.isOpen() ? 2 : s_restart.isOpen() ? 1 : 0;
  const bool pageChanged = page != s_drawnPage;
  s_drawnPage = page;
  if (page == 1) { s_restart.render(full || pageChanged); return; }
  if (page == 2) { s_dice.render(full || pageChanged); return; }
  full = full || pageChanged;  // back from a page: repaint the table

  auto& g = gfx();
  const int8_t hubPressed = s_press.zone == ScoreZone::Hub ? (int8_t)s_press.index : -1;
  bool hubDirty = full || hubPressed != s_drawnHubPressed;
  bool started = false;
  if (full) {
    g.startWrite();
    started = true;
    g.fillScreen(theme::BG);
  }
  for (uint8_t p = 0; p < game().players; ++p) {
    const CardView v = viewOf(p);
    if (!full && v == s_drawn[p]) continue;
    if (!started) { g.startWrite(); started = true; }
    drawCard(p, v);
    s_drawn[p] = v;
    hubDirty = true;  // the card edges lie under the round buttons
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

void scoreRequestHighRoll() { s_rollOnEnter = true; }
