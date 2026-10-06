/* ============================================================================
 *  ScreenCommander — life counter for 2..6 players with Lotus-style
 *  commander damage. Where the cards sit: CommanderLayout.h.
 *
 *  NORMAL MODE (4 players)             COMMANDER DAMAGE MODE (P1 swiped)
 *  ┌──────────┬──────────┐             ┌──────────┬──────────┐
 *  │ PLAYER 1 │ PLAYER 2 │             │ PLAYER 1 │ P2 -> P1 │  P1's card: P1's life
 *  │    40    │    40    │             │    33    │   7 /21  │  others: damage THEY
 *  │  [-] [+] │  [-] [+] │             │CMD DAMAGE│  [-] [+] │  dealt to P1 (indigo
 *  ├────────(≡)──────────┤             ├────────(✕)──────────┤  cards = this mode)
 *  │ PLAYER 3 │ PLAYER 4 │             │ P3 -> P1 │ P4 -> P1 │
 *  └──────────┴──────────┘             └──────────┴──────────┘
 *
 *  Normal mode
 *    Touch:   tap a card = select · tap/hold −/+ = life (hold repeats)
 *             swipe sideways on a card's number = commander damage mode for that player
 *             tap centre ≡ = Commander menu (high roll, player count, new game, home)
 *             tap 🎲 = Dice page
 *    Encoder: turn = selected player's life · press = next player
 *             long-press = commander damage mode for the selected player
 *
 *  Commander damage mode (victim = the player who swiped)
 *    Touch:   −/+ on an opponent's card = damage that opponent dealt to the victim
 *             −/+ on the victim's card  = victim's life
 *             tap an opponent's card = focus it (for the encoder)
 *             swipe the victim's card again, or tap centre ✕ = back to normal
 *             swipe another card = switch the victim
 *    Encoder: turn = damage from the focused opponent · press = next opponent
 *             long-press = back to normal
 *    Closes by itself after CMD_MODE_TIMEOUT_MS without use.
 *
 *  Every card is drawn the right way round for the player at its table edge
 *  (COMMANDER_FACE_SEATS); its touch points are turned the same way, and
 *  "sideways" swipes are sideways for that player.
 *
 *  Dice: the 🎲 round button (every table has one) opens the Dice page on top
 *  of the game (D4..D20, REROLL); its BACK returns here unchanged.
 *
 *  High roll: ≡ menu -> HIGH ROLL — every player's card shows a D20 whose
 *  face changes fast, slows down and lands. Highest roll = gold card;
 *  players tied for the top roll again by themselves. Tap anywhere (or use
 *  the encoder) to go back to the life totals.
 *
 *  Commander damage also costs life (CMD_DAMAGE_AFFECTS_LIFE, like Lotus).
 *  OUT (life <= 0, or 21+ from one commander): red card, "YOU ARE OUT";
 *  − / + keep working so mistakes can be undone.
 * ==========================================================================*/
#include <Arduino.h>
#include "Screens.h"
#include "App.h"
#include "GameState.h"
#include "CommanderLayout.h"
#include "TableDraw.h"
#include "HighRoll.h"
#include "UiDice.h"
#include "Theme.h"
#include "Ui.h"
#include "Config.h"
#include <stdio.h>

namespace {

constexpr int FRAME_R = 10;     // card corner radius
constexpr int FRAME_THICK = 4;  // highlight border thickness

const TableLayout& table() { return tableLayout(g_state.commander.players); }

// ---------------------------------------------------------------- UI-only state
constexpr int8_t NONE = -1;
Hit      s_press  = NO_HIT;  // what the finger is holding (feedback / repeat / swipe)
int8_t   s_victim = NONE;    // commander damage mode: whose damage is shown (NONE = normal)
uint8_t  s_focus  = 0;       // damage mode: opponent the encoder adjusts
uint32_t s_modeUsedAt = 0;   // damage mode: last interaction (auto-close)

HighRoll s_roll;             // high roll (who goes first)
DiceOverlay s_dice;          // Dice page on top of the table
bool     s_drawnDice = false;
bool     s_rollOnEnter = false;  // the menu asked for a high roll

bool cmdMode() { return s_victim != NONE; }

void touchMode() { s_modeUsedAt = millis(); }

void enterCmdMode(uint8_t victim) {
  s_victim = (int8_t)victim;
  s_focus = commanderOpponent(victim, 0);
  commanderSelect(g_state.commander, victim);
  touchMode();
}

void exitCmdMode() { s_victim = NONE; }

uint8_t rollD20() { return (uint8_t)random(1, HIGHROLL_SIDES + 1); }  // hardware RNG on the ESP32

void startHighRoll() {
  exitCmdMode();
  highRollStart(s_roll, g_state.commander.players, millis(), rollD20);
}

void nextFocus() {  // cycle the encoder focus through the victim's opponents
  const uint8_t n = g_state.commander.players;
  for (uint8_t k = 1; k <= n; ++k) {
    const uint8_t p = (uint8_t)((s_focus + k) % n);
    if (p != (uint8_t)s_victim) { s_focus = p; return; }
  }
}

// −/+ on card p (or encoder on the selected / focused card)
void adjustCard(uint8_t p, int delta) {
  CommanderGame& game = g_state.commander;
  if (cmdMode() && p != (uint8_t)s_victim) {
    commanderAdjustCmdDamage(game, (uint8_t)s_victim, p, delta);  // p dealt damage to victim
    s_focus = p;
  } else {
    commanderAdjustLife(game, p, delta);
  }
  if (cmdMode()) touchMode();
}

uint8_t biggestCmdDamage(uint8_t p) {
  const CommanderGame& g = g_state.commander;
  uint8_t m = 0;
  for (uint8_t j = 0; j < g.players; ++j)
    if (j != p && g.cmdDamage[p][j] > m) m = g.cmdDamage[p][j];
  return m;
}

// ---------------------------------------------------------------- card model
// Everything that decides how one card looks. Rendering compares this with
// what was last drawn and repaints only the cards that differ.
enum class Role : uint8_t { Life, Victim, Source };

struct CardView {
  Role      role;
  uint8_t   victim;     // Source cards: whose damage they show
  int16_t   value;      // life, or commander damage dealt to the victim
  OutReason out;        // this card's own player
  uint8_t   cmdMax;     // Life cards: biggest damage from one commander (badge)
  bool      highlight;  // thick coloured border
  Zone      pressed;    // Minus / Plus / None
  DieState  die;        // high roll running: how this player's D20 looks
  uint8_t   face;       // high roll: number on the D20
  bool operator==(const CardView& o) const {
    return role == o.role && victim == o.victim && value == o.value && out == o.out &&
           cmdMax == o.cmdMax && highlight == o.highlight && pressed == o.pressed &&
           die == o.die && face == o.face;
  }
  bool operator!=(const CardView& o) const { return !(*this == o); }
};

CardView viewOf(uint8_t i) {
  const CommanderGame& g = g_state.commander;
  CardView v;
  v.victim = cmdMode() ? (uint8_t)s_victim : 0;
  v.out = commanderOutReason(g, i);
  v.cmdMax = 0;
  if (!cmdMode()) {
    v.role = Role::Life;
    v.value = g.life[i];
    v.cmdMax = biggestCmdDamage(i);
    v.highlight = (g.selected == i);
  } else if (i == v.victim) {
    v.role = Role::Victim;
    v.value = g.life[i];
    v.highlight = true;
  } else {
    v.role = Role::Source;
    v.value = g.cmdDamage[v.victim][i];
    v.highlight = (s_focus == i);
  }
  const bool onButton = s_press.zone == Zone::Minus || s_press.zone == Zone::Plus;
  v.pressed = (onButton && s_press.index == i) ? s_press.zone : Zone::None;
  v.die = highRollDie(s_roll, i);
  v.face = v.die != DieState::None ? s_roll.value[i] : 0;
  if (v.die != DieState::None) v.pressed = Zone::None;
  return v;
}

CardView s_drawn[COMMANDER_MAX_PLAYERS];
const Seat* s_paintSeat = nullptr;  // seat of the card being painted (caption vs round buttons)
int8_t   s_drawnHubPressed = NONE;  // centre button drawn pressed (index), NONE = none
bool     s_drawnCmdMode = false;

uint16_t cardBg(const CardView& v) {
  if (v.role == Role::Source) return theme::CMD_PANEL;  // one colour for the whole mode (red = OUT only)
  return v.out != OutReason::None ? theme::OUT_PANEL : theme::PANEL;
}

// ---------------------------------------------------------------- drawing a card
// Card parts draw on canvas `c` with the card's top-left (as its player sees
// it) at (ox, oy), using the local positions in CardGeom.

// Player label: the longest form that fits the pill.
//   "PLAYER 5" -> "P5"     commander damage: "P5 -> P1" -> "P5>P1" -> "P5"
// `*shortSource` = a commander damage label lost its victim part.
void labelText(lgfx::LovyanGFX& c, const CardGeom& g, uint8_t i, const CardView& v,
               char* buf, size_t n, bool* shortSource) {
  const unsigned p = i + 1, victim = v.victim + 1;
  const int room = g.pill.w - 12;
  c.setFont(theme::fontLabel());
  bool victimShown = false;
  if (v.role == Role::Source) {
    snprintf(buf, n, "P%u -> P%u", p, victim);
    if (c.textWidth(buf) > room) snprintf(buf, n, "P%u>P%u", p, victim);
    victimShown = c.textWidth(buf) <= room;
  } else {
    snprintf(buf, n, "PLAYER %u", p);
  }
  if (c.textWidth(buf) > room) snprintf(buf, n, "P%u", p);
  if (shortSource) *shortSource = v.role == Role::Source && !victimShown;
}

// Does this caption fit its slot without reaching the label pill or the
// table's round buttons? (compact: centred; wide: right-aligned)
bool captionFits(lgfx::LovyanGFX& c, const CardGeom& g, const lgfx::IFont* font, const char* text) {
  const int room = g.wide ? g.capX - (g.pill.x + g.pill.w) - 10 : g.w - 16;
  c.setFont(font);
  const int w = c.textWidth(text);
  const Rect r = {(int16_t)(g.wide ? g.capX - w : g.capX - w / 2), (int16_t)(g.capY - 8), (int16_t)w, 16};
  return w <= room && (!s_paintSeat || clearOfHubs(table(), *s_paintSeat, r));
}

// Caption text: the first option that fits, else the last (shortest) one.
const char* fitCaption(lgfx::LovyanGFX& c, const CardGeom& g, const lgfx::IFont* font,
                       const char* longText, const char* shortText, const char* shortest = nullptr) {
  const char* options[3] = {longText, shortText, shortest};
  const char* last = longText;
  for (const char* t : options) {
    if (!t) continue;
    last = t;
    if (captionFits(c, g, font, t)) return t;
  }
  return last;
}

const theme::NumberFont& pickNumberFont(lgfx::LovyanGFX& c, const char* text, int maxW, int maxH) {
  uint8_t n = 0;
  const theme::NumberFont* f = theme::numberFonts(n);
  for (uint8_t k = 0; k + 1 < n; ++k) {
    c.setFont(f[k].font);
    if (f[k].height <= maxH && c.textWidth(text) + (theme::LIFE_FAUX_BOLD ? 1 : 0) <= maxW) return f[k];
  }
  return f[n - 1];  // smallest
}

int numberWidth(lgfx::LovyanGFX& c, const theme::NumberFont& f, const char* text) {
  c.setFont(f.font);
  return c.textWidth(text) + (theme::LIFE_FAUX_BOLD ? 1 : 0);
}

// Digits vertically centred on cy; x is the left edge (top_left) or the centre (top_center).
void drawDigits(lgfx::LovyanGFX& c, const theme::NumberFont& f, const char* text, int x, int cy,
                lgfx::textdatum_t datum, uint16_t color) {
  const int y = cy - f.top - f.height / 2;
  c.setFont(f.font);
  c.setTextDatum(datum);
  c.setTextColor(color);
  c.drawString(text, x, y);
  if (theme::LIFE_FAUX_BOLD) c.drawString(text, x + 1, y);
}

void drawCaption(lgfx::LovyanGFX& c, int ox, int oy, const CardGeom& g, const char* text,
                 const lgfx::IFont* font, uint16_t color) {
  c.setFont(font);
  c.setTextDatum(g.wide ? lgfx::textdatum_t::middle_right : lgfx::textdatum_t::middle_center);
  c.setTextColor(color);
  c.drawString(text, ox + g.capX, oy + g.capY);
}

void drawCenter(lgfx::LovyanGFX& c, int ox, int oy, const CardGeom& g, uint8_t i, const CardView& v) {
  const int cx = ox + g.numCx, cy = oy + g.numCy;
  char num[8];
  snprintf(num, sizeof(num), "%d", v.value);

  if (v.role == Role::Source) {
    // ---- commander damage this card's player dealt to the victim:  "7 /21"
    const uint16_t col = v.value >= CMD_DAMAGE_LETHAL ? theme::DANGER : theme::TEXT;
    const lgfx::IFont* suffixFont = theme::fontSmall();
    c.setFont(suffixFont);
    int suffixW = c.textWidth("/21");
    const theme::NumberFont& f = pickNumberFont(c, num, g.numMaxW - suffixW - 4, g.numMaxH);
    const int numW = numberWidth(c, f, num);
    if (f.height >= 60) {  // big digits get a bigger "/21" if it fits
      c.setFont(theme::fontButton());
      const int bigW = c.textWidth("/21");
      if (numW + 4 + bigW <= g.numMaxW) { suffixFont = theme::fontButton(); suffixW = bigW; }
    }
    const int left = cx - (numW + 4 + suffixW) / 2;
    drawDigits(c, f, num, left, cy, lgfx::textdatum_t::top_left, col);
    c.setFont(suffixFont);
    c.setTextDatum(lgfx::textdatum_t::baseline_left);
    c.setTextColor(theme::TEXT_DIM);
    c.drawString("/21", left + numW + 4, cy + f.height / 2);  // sits on the digits' baseline

    char label[16];
    bool shortSource = false;
    labelText(c, g, i, v, label, sizeof(label), &shortSource);
    if (shortSource) {  // narrow card: the label only says "P5", so name the victim here
      char cap[12];
      snprintf(cap, sizeof(cap), "TO P%u", (unsigned)(v.victim + 1));
      drawCaption(c, ox, oy, g, cap, theme::fontSmall(), theme::TEXT_DIM);
    }
    return;
  }

  if (v.out != OutReason::None) {
    // ---- YOU ARE OUT
    c.setTextDatum(lgfx::textdatum_t::middle_center);
    c.setTextColor(theme::TEXT);
    if (!g.wide) {
      c.setFont(theme::fontLabel());
      c.drawString("YOU ARE", cx, cy - 10);
      c.setFont(theme::fontTitle());
      c.drawString("OUT", cx, cy + 16);
      c.drawString("OUT", cx + 1, cy + 16);
    } else {  // wide card: "YOU ARE" in the caption corner, big "OUT" in the middle
      c.setFont(g.numMaxH >= 60 ? theme::fontHuge() : theme::fontTitle());
      c.drawString("OUT", cx, cy);
      c.drawString("OUT", cx + 1, cy);
      drawCaption(c, ox, oy, g, fitCaption(c, g, theme::fontLabel(), "YOU ARE", "YOU ARE"),
                  theme::fontLabel(), theme::TEXT);
    }
    return;
  }

  // ---- life
  const theme::NumberFont& f = pickNumberFont(c, num, g.numMaxW, g.numMaxH);
  drawDigits(c, f, num, cx, cy, lgfx::textdatum_t::top_center, theme::TEXT);
  if (v.role == Role::Victim) {
    drawCaption(c, ox, oy, g, fitCaption(c, g, theme::fontSmall(), "CMD DAMAGE", "CMD DMG", "DMG"),
                theme::fontSmall(), theme::TEXT_DIM);
  } else if (v.cmdMax > 0) {
    char buf[12], shortBuf[6];
    snprintf(buf, sizeof(buf), "CMD %u", (unsigned)v.cmdMax);
    snprintf(shortBuf, sizeof(shortBuf), "%u", (unsigned)v.cmdMax);
    drawCaption(c, ox, oy, g, fitCaption(c, g, theme::fontSmall(), buf, shortBuf), theme::fontSmall(),
                theme::TEXT_DIM);
  }
}

void drawFrame(lgfx::LovyanGFX& c, int ox, int oy, const CardGeom& g, uint8_t i, const CardView& v) {
  if (v.highlight) uiRoundFrame(c, ox, oy, g.w, g.h, FRAME_R, FRAME_THICK, theme::PLAYER[i]);
  else             uiRoundFrame(c, ox, oy, g.w, g.h, FRAME_R, 1, theme::PANEL_EDGE);
}

void drawLabel(lgfx::LovyanGFX& c, int ox, int oy, const CardGeom& g, uint8_t i, const CardView& v) {
  const Rect p = {(int16_t)(ox + g.pill.x), (int16_t)(oy + g.pill.y), g.pill.w, g.pill.h};
  char buf[16];
  labelText(c, g, i, v, buf, sizeof(buf), nullptr);
  uint16_t textCol;
  if (v.role == Role::Source) {  // "P2 -> P1": damage P2's commander dealt to P1
    c.fillRoundRect(p.x, p.y, p.w, p.h, p.h / 2, theme::BUTTON);
    textCol = theme::PLAYER[i];
  } else {
    if (v.highlight) c.fillRoundRect(p.x, p.y, p.w, p.h, p.h / 2, theme::PLAYER[i]);
    textCol = v.highlight ? theme::TEXT_ON_ACCENT : theme::PLAYER[i];
  }
  c.setFont(theme::fontLabel());
  c.setTextDatum(lgfx::textdatum_t::middle_center);
  c.setTextColor(textCol);
  c.drawString(buf, p.cx(), p.cy() + 1);
}

void drawButton(lgfx::LovyanGFX& c, int ox, int oy, const CardGeom& g, uint8_t i, Zone which, bool pressed) {
  const Rect& r = (which == Zone::Minus) ? g.minus : g.plus;
  const Rect b = {(int16_t)(ox + r.x), (int16_t)(oy + r.y), r.w, r.h};
  const uint16_t fill = pressed ? theme::PLAYER[i] : theme::BUTTON;
  const uint16_t sym  = pressed ? theme::TEXT_ON_ACCENT : theme::TEXT;
  const int shortSide = b.w < b.h ? b.w : b.h;
  const int len = shortSide >= 52 ? 26 : 18;  // bigger symbol on the big 2-player buttons
  const int thick = shortSide >= 52 ? 5 : 4;
  c.fillRoundRect(b.x, b.y, b.w, b.h, 8, fill);
  if (which == Zone::Minus) uiMinus(c, b.cx(), b.cy(), len, thick, sym);
  else                      uiPlus(c, b.cx(), b.cy(), len, thick, sym);
}

// High roll: the D20 replaces the number and the − / + buttons.
void drawRollContent(lgfx::LovyanGFX& c, int ox, int oy, const CardGeom& g, uint8_t i, const CardView& v) {
  const bool won = v.die == DieState::Winner;
  c.fillRect(ox, oy, g.w, g.h, theme::BG);
  c.fillRoundRect(ox, oy, g.w, g.h, FRAME_R, won ? theme::WIN_PANEL : theme::PANEL);
  if (won) uiRoundFrame(c, ox, oy, g.w, g.h, FRAME_R, FRAME_THICK, theme::ACCENT);
  else     uiRoundFrame(c, ox, oy, g.w, g.h, FRAME_R, 1, theme::PANEL_EDGE);
  CardView label = v;  // plain player label; filled in the player's colour for the winner
  label.role = Role::Life;
  label.highlight = won;
  drawLabel(c, ox, oy, g, i, label);
  drawD20(c, ox + g.dieCx, oy + g.dieCy, g.dieR, v.die, theme::PLAYER[i], v.face);
  if (g.wide && (won || v.die == DieState::Tied)) {  // wide cards have a free caption corner
    const char* options[] = {"HIGH ROLL!", "TOP ROLL!", "WIN!"};
    const char* cap = nullptr;
    if (!won) {
      cap = "TIE!";
    } else {
      // the longest text that fits its slot, misses the round buttons and the die
      const int dieRight = g.dieCx + g.dieR * 7 / 8 + 4;
      const int dieTop = g.dieCy - g.dieR, dieBottom = g.dieCy + g.dieR;
      for (const char* t : options) {
        c.setFont(theme::fontLabel());
        const int left = g.capX - c.textWidth(t);
        const bool hitsDie = left < dieRight && g.capY - 8 < dieBottom && g.capY + 8 > dieTop;
        if (!hitsDie && captionFits(c, g, theme::fontLabel(), t)) { cap = t; break; }
      }
    }
    if (cap) drawCaption(c, ox, oy, g, cap, theme::fontLabel(), theme::ACCENT);
  }
}

void drawCardContent(lgfx::LovyanGFX& c, int ox, int oy, const CardGeom& g, uint8_t i, const CardView& v) {
  if (v.die != DieState::None) { drawRollContent(c, ox, oy, g, i, v); return; }
  c.fillRect(ox, oy, g.w, g.h, theme::BG);
  c.fillRoundRect(ox, oy, g.w, g.h, FRAME_R, cardBg(v));
  drawFrame(c, ox, oy, g, i, v);
  drawLabel(c, ox, oy, g, i, v);
  drawCenter(c, ox, oy, g, i, v);
  drawButton(c, ox, oy, g, i, Zone::Minus, v.pressed == Zone::Minus);
  drawButton(c, ox, oy, g, i, Zone::Plus,  v.pressed == Zone::Plus);
}

// ---------------------------------------------------------------- whole card / centre buttons
struct CardJob { const CardGeom* g; uint8_t i; const CardView* v; };

void paintCard(lgfx::LovyanGFX& c, int ox, int oy, const void* ctx) {
  const CardJob& j = *static_cast<const CardJob*>(ctx);
  drawCardContent(c, ox, oy, *j.g, j.i, *j.v);
}

void drawCard(uint8_t i, const CardView& v) {
  const Seat& s = table().seats[i];
  const CardGeom g = cardGeom(s);
  const CardJob job = {&g, i, &v};
  s_paintSeat = &s;
  drawSeatCard(s, paintCard, &job);  // off-screen, turned to face the player, one copy
  s_paintSeat = nullptr;
}

void drawHub(const HubPos& h, bool pressed) {
  HubIcon icon = HubIcon::Dice;
  if (h.kind == HubKind::Menu) icon = cmdMode() ? HubIcon::Close : HubIcon::Menu;
  drawHubButton(h.x, h.y, icon, pressed);
}

// While a high roll is on screen: nothing works until the dice land; then
// the first touch or encoder action only goes back to the game.
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
  exitCmdMode();  // always come back to the normal life view
  s_dice.close();
  highRollStop(s_roll);
  if (s_rollOnEnter) {  // ≡ menu -> HIGH ROLL
    s_rollOnEnter = false;
    startHighRoll();
  }
}

void handleInput(const InputEvent& e) {
  if (s_dice.isOpen()) { s_dice.handleInput(e); return; }  // BACK closes it: the table repaints
  if (highRollActive(s_roll)) { handleRollInput(e); return; }
  CommanderGame& game = g_state.commander;
  switch (e.type) {
    case InputType::EncoderTurn:
      adjustCard(cmdMode() ? s_focus : game.selected, e.delta);
      break;

    case InputType::EncoderClick:
      if (cmdMode()) { nextFocus(); touchMode(); }
      else commanderSelectNext(game);
      break;

    case InputType::EncoderLongPress:
      if (cmdMode()) exitCmdMode();
      else enterCmdMode(game.selected);
      break;

    case InputType::TouchDown: {
      s_press = commanderHitTest(game.players, e.x, e.y);
      const Zone z = s_press.zone;
      const uint8_t p = s_press.index;
      if (z == Zone::Area || z == Zone::Minus || z == Zone::Plus) {
        if (!cmdMode()) commanderSelect(game, p);
        else { if (p != (uint8_t)s_victim) s_focus = p; touchMode(); }
      }
      if (z == Zone::Minus) adjustCard(p, -1);
      if (z == Zone::Plus)  adjustCard(p, +1);
      break;
    }

    case InputType::TouchRepeat:  // hold-to-repeat on − / +
      if (s_press.zone == Zone::Minus) adjustCard(s_press.index, -1);
      if (s_press.zone == Zone::Plus)  adjustCard(s_press.index, +1);
      break;

    case InputType::TouchSwipe: {  // only swipes that start on a card's number/label area
      if (s_press.zone != Zone::Area) break;
      const uint8_t p = s_press.index;
      if (!isSidewaysSwipe(table().seats[p], e.delta)) break;  // sideways for that player only
      if (!cmdMode()) enterCmdMode(p);
      else if (p == (uint8_t)s_victim) exitCmdMode();
      else enterCmdMode(p);  // switch to that player's damage
      break;
    }

    case InputType::TouchUp: {
      const Hit released = s_press;
      s_press = NO_HIT;
      if (released.zone == Zone::Hub && isTap(e, TOUCH_TAP_MAX_MS)) {
        if (table().hubs[released.index].kind == HubKind::Dice) {  // 🎲
          exitCmdMode();
          s_dice.open();
        }
        else if (cmdMode()) exitCmdMode();         // centre ✕ closes damage mode
        else goToScreen(SCREEN_COMMANDER_SETUP);   // centre ≡ opens the Commander menu
      }
      break;
    }
  }
}

void tick(uint32_t now) {
  if (s_dice.isOpen()) { s_dice.tick(now); return; }
  if (highRollActive(s_roll)) { highRollUpdate(s_roll, now, rollD20); return; }
  if (!cmdMode() || CMD_MODE_TIMEOUT_MS == 0) return;
  const bool holding = s_press.zone != Zone::None;
  if (!holding && now - s_modeUsedAt >= (uint32_t)CMD_MODE_TIMEOUT_MS) exitCmdMode();
}

void render(bool full) {
  const bool dice = s_dice.isOpen();
  const bool pageChanged = dice != s_drawnDice;
  s_drawnDice = dice;
  if (dice) { s_dice.render(full || pageChanged); return; }
  full = full || pageChanged;  // back from the Dice page: repaint the table

  auto& g = gfx();
  const TableLayout& L = table();
  const int8_t hubPressed = (s_press.zone == Zone::Hub) ? (int8_t)s_press.index : NONE;
  bool hubDirty = full || (hubPressed != s_drawnHubPressed) || (cmdMode() != s_drawnCmdMode);
  bool started = false;

  if (full) {
    g.startWrite();
    started = true;
    g.fillScreen(theme::BG);  // also blanks the empty seat of the 3-player table
  }
  for (uint8_t i = 0; i < L.players; ++i) {
    const CardView v = viewOf(i);
    if (!full && v == s_drawn[i]) continue;
    if (!started) { g.startWrite(); started = true; }
    drawCard(i, v);  // whole card off-screen, then one copy: no flicker
    s_drawn[i] = v;
    hubDirty = true;  // card corners lie under the centre button(s)
  }
  if (hubDirty) {
    if (!started) { g.startWrite(); started = true; }
    for (uint8_t k = 0; k < L.hubCount; ++k) drawHub(L.hubs[k], (int8_t)k == hubPressed);
    s_drawnHubPressed = hubPressed;
    s_drawnCmdMode = cmdMode();
  }
  if (started) g.endWrite();
}

}  // namespace

const ScreenModule CommanderScreen = {"Commander", onEnter, handleInput, render, tick};

void commanderRequestHighRoll() { s_rollOnEnter = true; }
