/* ============================================================================
 *  ScreenCommander — 4-player life counter (2x2) with Lotus-style
 *  commander damage.
 *
 *  NORMAL MODE                         COMMANDER DAMAGE MODE (P1 swiped)
 *  ┌──────────┬──────────┐             ┌──────────┬──────────┐
 *  │ PLAYER 1 │ PLAYER 2 │             │ PLAYER 1 │ P2 -> P1 │  P1's card: P1's life
 *  │    40    │    40    │             │    33    │   7 /21  │  others: damage THEY
 *  │  [-] [+] │  [-] [+] │             │CMD DAMAGE│  [-] [+] │  dealt to P1 (indigo
 *  ├────────(⌂)──────────┤             ├────────(✕)──────────┤  cards = this mode)
 *  │ PLAYER 3 │ PLAYER 4 │             │ P3 -> P1 │ P4 -> P1 │
 *  └──────────┴──────────┘             └──────────┴──────────┘
 *
 *  Normal mode
 *    Touch:   tap a card = select · tap/hold −/+ = life (hold repeats)
 *             swipe left/right on a card's number = commander damage mode for that player
 *             tap centre ⌂ = Home
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
 *  Commander damage also costs life (CMD_DAMAGE_AFFECTS_LIFE, like Lotus).
 *  OUT (life <= 0, or 21+ from one commander): red card, "YOU ARE OUT";
 *  − / + keep working so mistakes can be undone.
 * ==========================================================================*/
#include <Arduino.h>
#include "Screens.h"
#include "App.h"
#include "GameState.h"
#include "Theme.h"
#include "Ui.h"
#include "Config.h"
#include <stdio.h>

namespace {

// ---------------------------------------------------------------- geometry
constexpr int QW = 159, QH = 119, GAP = 2;   // 159+2+159 = 320, 119+2+119 = 240
constexpr int FRAME_R = 10;                  // card corner radius
constexpr int FRAME_THICK = 4;               // highlight border thickness

// Positions inside a card (local coordinates)
constexpr Rect PILL       = {27, 8, 105, 20};  // label
constexpr Rect CENTER     = {6, 29, 147, 52};  // number / OUT message / caption
constexpr Rect BTN_MINUS  = {22, 82, 52, 30};
constexpr Rect BTN_PLUS   = {85, 82, 52, 30};
constexpr int  BTN_ZONE_Y = 76;  // touches at/below this line hit − (left half) or + (right half)
constexpr int  CAPTION_Y  = 45;  // small caption, rows 37..49 inside CENTER (below the digits)

// Centre button (screen coordinates): HOME in normal mode, CLOSE in damage mode
constexpr int HUB_X = 160, HUB_Y = 120, HUB_R = 19, HUB_HIT_R = 22;

Rect quadRect(uint8_t i) {
  return Rect{(int16_t)((i & 1) * (QW + GAP)), (int16_t)((i >> 1) * (QH + GAP)), QW, QH};
}

Rect offset(const Rect& r, const Rect& q) {
  return Rect{(int16_t)(q.x + r.x), (int16_t)(q.y + r.y), r.w, r.h};
}

// ---------------------------------------------------------------- hit test
enum class Zone : uint8_t { None, Area, Minus, Plus, Hub };
struct Hit {
  Zone zone;
  uint8_t player;
  bool operator==(const Hit& o) const { return zone == o.zone && player == o.player; }
  bool operator!=(const Hit& o) const { return !(*this == o); }
};
constexpr Hit NO_HIT = {Zone::None, 0};

Hit hitTest(int x, int y) {
  const int dx = x - HUB_X, dy = y - HUB_Y;
  if (dx * dx + dy * dy <= HUB_HIT_R * HUB_HIT_R) return {Zone::Hub, 0};
  // Split the screen in halves so the 2 px gaps are not dead zones.
  const uint8_t col = x >= 160 ? 1 : 0, row = y >= 120 ? 1 : 0;
  const uint8_t player = (uint8_t)(row * 2 + col);
  const Rect q = quadRect(player);
  const int lx = x - q.x, ly = y - q.y;
  if (ly >= BTN_ZONE_Y) return {lx < QW / 2 ? Zone::Minus : Zone::Plus, player};
  return {Zone::Area, player};
}

// ---------------------------------------------------------------- UI-only state
constexpr int8_t NONE = -1;
Hit      s_press  = NO_HIT;  // what the finger is holding (feedback / repeat / swipe)
int8_t   s_victim = NONE;    // commander damage mode: whose damage is shown (NONE = normal)
uint8_t  s_focus  = 0;       // damage mode: opponent the encoder adjusts
uint32_t s_modeUsedAt = 0;   // damage mode: last interaction (auto-close)

bool cmdMode() { return s_victim != NONE; }

void touchMode() { s_modeUsedAt = millis(); }

void enterCmdMode(uint8_t victim) {
  s_victim = (int8_t)victim;
  s_focus = commanderOpponent(victim, 0);
  commanderSelect(g_state.commander, victim);
  touchMode();
}

void exitCmdMode() { s_victim = NONE; }

void nextFocus() {  // cycle the encoder focus through the victim's opponents
  for (uint8_t k = 1; k <= COMMANDER_PLAYERS; ++k) {
    const uint8_t p = (uint8_t)((s_focus + k) % COMMANDER_PLAYERS);
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
  uint8_t m = 0;
  for (uint8_t j = 0; j < COMMANDER_PLAYERS; ++j)
    if (j != p && g_state.commander.cmdDamage[p][j] > m) m = g_state.commander.cmdDamage[p][j];
  return m;
}

// ---------------------------------------------------------------- card model
// Everything that decides how one card looks. Rendering compares this with
// what was last drawn and repaints only the parts that differ.
enum class Role : uint8_t { Life, Victim, Source };

struct CardView {
  Role      role;
  uint8_t   victim;     // Source cards: whose damage they show
  int16_t   value;      // life, or commander damage dealt to the victim
  OutReason out;        // this card's own player
  uint8_t   cmdMax;     // Life cards: biggest damage from one commander (badge)
  bool      highlight;  // thick coloured border
  Zone      pressed;    // Minus / Plus / None
  bool operator==(const CardView& o) const {
    return role == o.role && victim == o.victim && value == o.value && out == o.out &&
           cmdMax == o.cmdMax && highlight == o.highlight && pressed == o.pressed;
  }
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
  v.pressed = (s_press.player == i && (s_press.zone == Zone::Minus || s_press.zone == Zone::Plus))
                  ? s_press.zone : Zone::None;
  return v;
}

CardView s_drawn[COMMANDER_PLAYERS];
bool     s_drawnHubPressed = false;
bool     s_drawnCmdMode = false;

uint16_t cardBg(const CardView& v) {
  if (v.role == Role::Source) return theme::CMD_PANEL;  // one colour for the whole mode (red = OUT only)
  return v.out != OutReason::None ? theme::OUT_PANEL : theme::PANEL;
}

// ---------------------------------------------------------------- drawing
lgfx::LGFX_Sprite* centerSprite() {
  static lgfx::LGFX_Sprite spr(&gfx());
  static bool tried = false;
  if (!tried) {
    tried = true;
    spr.setColorDepth(16);
    spr.createSprite(CENTER.w, CENTER.h);  // ~15 KB; falls back to direct drawing if it fails
  }
  return spr.getBuffer() ? &spr : nullptr;
}

void drawBigNumber(lgfx::LovyanGFX& c, int cx, int cy, int value, uint16_t color) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", value);
  c.setFont(theme::fontLife());
  c.setTextDatum(lgfx::textdatum_t::middle_center);
  c.setTextColor(color);
  c.drawString(buf, cx, cy);
  if (theme::LIFE_FAUX_BOLD) c.drawString(buf, cx + 1, cy);
}

void drawCaption(lgfx::LovyanGFX& c, int cx, int y, const char* text, uint16_t color) {
  c.setFont(theme::fontSmall());
  c.setTextDatum(lgfx::textdatum_t::middle_center);
  c.setTextColor(color);
  c.drawString(text, cx, y);
}

// Contents of the CENTER box, drawn into canvas `c` with the box's top-left at (ox, oy).
void drawCenterContent(lgfx::LovyanGFX& c, int ox, int oy, const CardView& v) {
  const int cx = ox + CENTER.w / 2;
  c.fillRect(ox, oy, CENTER.w, CENTER.h, cardBg(v));

  if (v.role == Role::Source) {
    // ---- commander damage this card's player dealt to the victim
    const uint16_t col = v.value >= CMD_DAMAGE_LETHAL ? theme::DANGER : theme::TEXT;
    drawBigNumber(c, cx - 8, oy + 22, v.value, col);
    c.setFont(theme::fontSmall());
    c.setTextDatum(lgfx::textdatum_t::middle_left);
    c.setTextColor(theme::TEXT_DIM);
    c.drawString("/21", cx + 30, oy + 30);
    return;
  }

  if (v.out != OutReason::None) {
    // ---- YOU ARE OUT
    c.setTextDatum(lgfx::textdatum_t::middle_center);
    c.setTextColor(theme::TEXT);
    c.setFont(theme::fontLabel());
    c.drawString("YOU ARE", cx, oy + 9);
    c.setFont(theme::fontTitle());
    c.drawString("OUT", cx, oy + 32);
    c.drawString("OUT", cx + 1, oy + 32);
  } else {
    drawBigNumber(c, cx, oy + 22, v.value, theme::TEXT);  // life: digits rows 1..34
  }

  if (v.role == Role::Victim) {
    if (v.out == OutReason::None) drawCaption(c, cx, oy + CAPTION_Y, "CMD DAMAGE", theme::TEXT_DIM);
  } else if (v.cmdMax > 0 && v.out == OutReason::None) {
    char buf[12];
    snprintf(buf, sizeof(buf), "CMD %u", (unsigned)v.cmdMax);
    drawCaption(c, cx, oy + CAPTION_Y, buf, theme::TEXT_DIM);
  }
}

void drawCenter(uint8_t i, const CardView& v) {
  const Rect r = offset(CENTER, quadRect(i));
  if (auto* s = centerSprite()) {  // off-screen first: no flicker while numbers change
    drawCenterContent(*s, 0, 0, v);
    s->pushSprite(r.x, r.y);
  } else {
    drawCenterContent(gfx(), r.x, r.y, v);
  }
}

void drawFrame(uint8_t i, const CardView& v) {
  const Rect q = quadRect(i);
  if (v.highlight) {
    uiRoundFrame(q.x, q.y, q.w, q.h, FRAME_R, FRAME_THICK, theme::PLAYER[i]);
  } else {
    uiRoundFrame(q.x, q.y, q.w, q.h, FRAME_R, FRAME_THICK, cardBg(v));  // erase thick border
    uiRoundFrame(q.x, q.y, q.w, q.h, FRAME_R, 1, theme::PANEL_EDGE);
  }
}

void drawLabel(uint8_t i, const CardView& v) {
  auto& g = gfx();
  const Rect p = offset(PILL, quadRect(i));
  char buf[16];
  uint16_t textCol;
  g.fillRect(p.x, p.y, p.w, p.h, cardBg(v));
  if (v.role == Role::Source) {
    // "P2 -> P1": damage P2's commander dealt to P1
    snprintf(buf, sizeof(buf), "P%u -> P%u", (unsigned)(i + 1), (unsigned)(v.victim + 1));
    g.fillRoundRect(p.x, p.y, p.w, p.h, p.h / 2, theme::BUTTON);
    textCol = theme::PLAYER[i];
  } else {
    snprintf(buf, sizeof(buf), "PLAYER %u", (unsigned)(i + 1));
    if (v.highlight) g.fillRoundRect(p.x, p.y, p.w, p.h, p.h / 2, theme::PLAYER[i]);
    textCol = v.highlight ? theme::TEXT_ON_ACCENT : theme::PLAYER[i];
  }
  g.setFont(theme::fontLabel());
  g.setTextDatum(lgfx::textdatum_t::middle_center);
  g.setTextColor(textCol);
  g.drawString(buf, p.cx(), p.cy() + 1);
}

void drawButton(uint8_t i, Zone which, bool pressed, uint16_t bg) {
  const Rect b = offset(which == Zone::Minus ? BTN_MINUS : BTN_PLUS, quadRect(i));
  const uint16_t fill = pressed ? theme::PLAYER[i] : theme::BUTTON;
  const uint16_t sym  = pressed ? theme::TEXT_ON_ACCENT : theme::TEXT;
  gfx().fillRect(b.x, b.y, b.w, b.h, bg);
  gfx().fillRoundRect(b.x, b.y, b.w, b.h, 8, fill);
  if (which == Zone::Minus) uiMinus(b.cx(), b.cy(), 18, 4, sym);
  else                      uiPlus(b.cx(), b.cy(), 18, 4, sym);
}

void drawButtons(uint8_t i, const CardView& v) {
  drawButton(i, Zone::Minus, v.pressed == Zone::Minus, cardBg(v));
  drawButton(i, Zone::Plus,  v.pressed == Zone::Plus,  cardBg(v));
}

void drawCloseIcon(int cx, int cy, uint16_t color) {
  auto& g = gfx();
  for (int t = -1; t <= 1; ++t) {  // 3 px thick X
    g.drawLine(cx - 7 + t, cy - 7, cx + 7 + t, cy + 7, color);
    g.drawLine(cx - 7 + t, cy + 7, cx + 7 + t, cy - 7, color);
  }
}

void drawHub(bool pressed) {
  auto& g = gfx();
  const uint16_t fill = pressed ? theme::ACCENT : theme::BUTTON;
  const uint16_t icon = pressed ? theme::TEXT_ON_ACCENT : theme::TEXT;
  g.fillCircle(HUB_X, HUB_Y, HUB_R + 3, theme::BG);  // dark moat separates it from the frames
  g.fillCircle(HUB_X, HUB_Y, HUB_R, fill);
  g.drawCircle(HUB_X, HUB_Y, HUB_R, theme::PANEL_EDGE);
  if (cmdMode()) drawCloseIcon(HUB_X, HUB_Y, icon);
  else           uiHomeIcon(HUB_X, HUB_Y + 1, icon, fill);
}

void drawCard(uint8_t i, const CardView& v) {
  const Rect q = quadRect(i);
  gfx().fillRect(q.x, q.y, q.w, q.h, theme::BG);
  gfx().fillRoundRect(q.x, q.y, q.w, q.h, FRAME_R, cardBg(v));
  drawFrame(i, v);
  drawLabel(i, v);
  drawCenter(i, v);
  drawButtons(i, v);
}

// ---------------------------------------------------------------- module functions
void onEnter() {
  s_press = NO_HIT;
  exitCmdMode();  // always come back to the normal life view
}

void handleInput(const InputEvent& e) {
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
      s_press = hitTest(e.x, e.y);
      const Zone z = s_press.zone;
      const uint8_t p = s_press.player;
      if (z == Zone::Area || z == Zone::Minus || z == Zone::Plus) {
        if (!cmdMode()) commanderSelect(game, p);
        else { if (p != (uint8_t)s_victim) s_focus = p; touchMode(); }
      }
      if (z == Zone::Minus) adjustCard(p, -1);
      if (z == Zone::Plus)  adjustCard(p, +1);
      break;
    }

    case InputType::TouchRepeat:  // hold-to-repeat on − / +
      if (s_press.zone == Zone::Minus) adjustCard(s_press.player, -1);
      if (s_press.zone == Zone::Plus)  adjustCard(s_press.player, +1);
      break;

    case InputType::TouchSwipe:  // only swipes that start on a card's number/label area
      if (s_press.zone != Zone::Area) break;
      if (!cmdMode()) enterCmdMode(s_press.player);
      else if (s_press.player == (uint8_t)s_victim) exitCmdMode();
      else enterCmdMode(s_press.player);  // switch to that player's damage
      break;

    case InputType::TouchUp: {
      const Hit released = s_press;
      s_press = NO_HIT;
      if (released.zone == Zone::Hub && isTap(e, TOUCH_TAP_MAX_MS)) {
        if (cmdMode()) exitCmdMode();   // centre ✕ closes damage mode
        else goToScreen(SCREEN_HOME);   // centre ⌂ goes home
      }
      break;
    }
  }
}

void tick(uint32_t now) {
  if (!cmdMode() || CMD_MODE_TIMEOUT_MS == 0) return;
  const bool holding = s_press.zone != Zone::None;
  if (!holding && now - s_modeUsedAt >= (uint32_t)CMD_MODE_TIMEOUT_MS) exitCmdMode();
}

void render(bool full) {
  auto& g = gfx();
  const bool hubPressed = (s_press.zone == Zone::Hub);

  if (full) {
    g.startWrite();
    g.fillScreen(theme::BG);
    for (uint8_t i = 0; i < COMMANDER_PLAYERS; ++i) {
      s_drawn[i] = viewOf(i);
      drawCard(i, s_drawn[i]);
    }
    drawHub(hubPressed);
    s_drawnHubPressed = hubPressed;
    s_drawnCmdMode = cmdMode();
    g.endWrite();
    return;
  }

  bool hubDirty = (hubPressed != s_drawnHubPressed) || (cmdMode() != s_drawnCmdMode);
  bool started = false;
  for (uint8_t i = 0; i < COMMANDER_PLAYERS; ++i) {
    const CardView v = viewOf(i);
    const CardView& d = s_drawn[i];
    if (v == d) continue;
    if (!started) { g.startWrite(); started = true; }

    if (cardBg(v) != cardBg(d) || v.role != d.role || v.victim != d.victim) {
      drawCard(i, v);  // background or role changed: repaint the whole card
      hubDirty = true;
    } else {
      if (v.highlight != d.highlight) { drawFrame(i, v); hubDirty = true; }
      if (v.highlight != d.highlight) drawLabel(i, v);
      if (v.value != d.value || v.out != d.out || v.cmdMax != d.cmdMax) drawCenter(i, v);
      if (v.pressed != d.pressed) drawButtons(i, v);
    }
    s_drawn[i] = v;
  }
  if (hubDirty) {
    if (!started) { g.startWrite(); started = true; }
    drawHub(hubPressed);
    s_drawnHubPressed = hubPressed;
    s_drawnCmdMode = cmdMode();
  }
  if (started) g.endWrite();
}

}  // namespace

const ScreenModule CommanderScreen = {"Commander", onEnter, handleInput, render, tick};
