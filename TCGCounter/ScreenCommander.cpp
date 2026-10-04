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
 *             tap centre ≡ = Commander menu (player count, new game, home)
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
 *  Commander damage also costs life (CMD_DAMAGE_AFFECTS_LIFE, like Lotus).
 *  OUT (life <= 0, or 21+ from one commander): red card, "YOU ARE OUT";
 *  − / + keep working so mistakes can be undone.
 * ==========================================================================*/
#include <Arduino.h>
#include "Screens.h"
#include "App.h"
#include "GameState.h"
#include "CommanderLayout.h"
#include "Theme.h"
#include "Ui.h"
#include "Config.h"
#include <stdio.h>
#include <stdlib.h>

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
  bool operator==(const CardView& o) const {
    return role == o.role && victim == o.victim && value == o.value && out == o.out &&
           cmdMax == o.cmdMax && highlight == o.highlight && pressed == o.pressed;
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
  return v;
}

CardView s_drawn[COMMANDER_MAX_PLAYERS];
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

// Caption text: the long form if it fits its slot, else the short one.
const char* fitCaption(lgfx::LovyanGFX& c, const CardGeom& g, const lgfx::IFont* font,
                       const char* longText, const char* shortText) {
  // compact: centred inside the card; wide: right-aligned, must not reach the label pill
  const int room = g.wide ? g.capX - (g.pill.x + g.pill.w) - 10 : g.w - 16;
  c.setFont(font);
  return c.textWidth(longText) <= room ? longText : shortText;
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
      drawCaption(c, ox, oy, g, "YOU ARE", theme::fontLabel(), theme::TEXT);
    }
    return;
  }

  // ---- life
  const theme::NumberFont& f = pickNumberFont(c, num, g.numMaxW, g.numMaxH);
  drawDigits(c, f, num, cx, cy, lgfx::textdatum_t::top_center, theme::TEXT);
  if (v.role == Role::Victim) {
    drawCaption(c, ox, oy, g, fitCaption(c, g, theme::fontSmall(), "CMD DAMAGE", "CMD DMG"),
                theme::fontSmall(), theme::TEXT_DIM);
  } else if (v.cmdMax > 0) {
    char buf[12];
    snprintf(buf, sizeof(buf), "CMD %u", (unsigned)v.cmdMax);
    drawCaption(c, ox, oy, g, buf, theme::fontSmall(), theme::TEXT_DIM);
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

void drawCardContent(lgfx::LovyanGFX& c, int ox, int oy, const CardGeom& g, uint8_t i, const CardView& v) {
  c.fillRect(ox, oy, g.w, g.h, theme::BG);
  c.fillRoundRect(ox, oy, g.w, g.h, FRAME_R, cardBg(v));
  drawFrame(c, ox, oy, g, i, v);
  drawLabel(c, ox, oy, g, i, v);
  drawCenter(c, ox, oy, g, i, v);
  drawButton(c, ox, oy, g, i, Zone::Minus, v.pressed == Zone::Minus);
  drawButton(c, ox, oy, g, i, Zone::Plus,  v.pressed == Zone::Plus);
}

// ---------------------------------------------------------------- off-screen buffer
// Cards are drawn off-screen (no flicker), turned to face their player by
// the sprite's rotation, then copied to the LCD. One buffer is reused for
// every card; a card bigger than the buffer (the 2-player cards) is drawn
// in horizontal bands.
constexpr uint32_t CARD_BUF_PIXELS = 20000;  // 40 KB: a whole 159x119 card

struct CardBuffer { uint16_t* px; uint32_t pixels; };

const CardBuffer& cardBuffer() {
  static CardBuffer b = [] {
    CardBuffer r = {nullptr, 0};
    // Low on memory? Smaller buffers just mean more bands.
    for (uint32_t n = CARD_BUF_PIXELS; n >= 320 && !r.px; n /= 2) {
      r.px = static_cast<uint16_t*>(malloc(n * sizeof(uint16_t)));
      if (r.px) r.pixels = n;
    }
    return r;
  }();
  return b;
}

void drawCard(uint8_t i, const CardView& v) {
  const Seat& s = table().seats[i];
  const CardGeom g = cardGeom(s);
  const CardBuffer& buf = cardBuffer();
  if (!buf.px) return;  // no memory at all: nothing sensible to draw

  static lgfx::LGFX_Sprite spr(&gfx());
  const int w = s.r.w, h = s.r.h;
  const int maxRows = (int)(buf.pixels / (uint32_t)w);
  const int bands = (h + maxRows - 1) / maxRows;
  const int bandH = (h + bands - 1) / bands;

  // pushSprite() would send the buffer by DMA in the background, and drawing
  // the next band/card into it while that runs mixes cards up on the screen.
  // So: nothing may still be in flight, and every push is a blocking transfer.
  gfx().waitDMA();
  for (int b0 = 0; b0 < h; b0 += bandH) {
    const int bh = (h - b0 < bandH) ? h - b0 : bandH;
    spr.setBuffer(buf.px, w, bh, 16);
    spr.setRotation((uint8_t)s.side);  // turn the drawing to face the seat
    // Card position (as its player sees it) of this band's first pixel:
    int ax, ay, bx, by;
    seatToLocal(s, s.r.x, s.r.y + b0, ax, ay);
    seatToLocal(s, s.r.x + w - 1, s.r.y + b0 + bh - 1, bx, by);
    drawCardContent(spr, -(ax < bx ? ax : bx), -(ay < by ? ay : by), g, i, v);
    gfx().pushImage(s.r.x, s.r.y + b0, w, bh, static_cast<const lgfx::swap565_t*>((void*)buf.px));
  }
}

// ---------------------------------------------------------------- centre button(s)
void drawMenuIcon(int cx, int cy, uint16_t color) {
  auto& g = gfx();
  for (int k = -1; k <= 1; ++k) g.fillRect(cx - 8, cy - 1 + 6 * k, 17, 3, color);
}

void drawCloseIcon(int cx, int cy, uint16_t color) {
  auto& g = gfx();
  for (int t = -1; t <= 1; ++t) {  // 3 px thick X
    g.drawLine(cx - 7 + t, cy - 7, cx + 7 + t, cy + 7, color);
    g.drawLine(cx - 7 + t, cy + 7, cx + 7 + t, cy - 7, color);
  }
}

void drawHub(const HubPos& h, bool pressed) {
  auto& g = gfx();
  const uint16_t fill = pressed ? theme::ACCENT : theme::BUTTON;
  const uint16_t icon = pressed ? theme::TEXT_ON_ACCENT : theme::TEXT;
  g.fillCircle(h.x, h.y, HUB_R + HUB_MOAT, theme::BG);  // dark moat separates it from the cards
  g.fillCircle(h.x, h.y, HUB_R, fill);
  g.drawCircle(h.x, h.y, HUB_R, theme::PANEL_EDGE);
  if (cmdMode()) drawCloseIcon(h.x, h.y, icon);
  else           drawMenuIcon(h.x, h.y, icon);
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
        if (cmdMode()) exitCmdMode();                   // centre ✕ closes damage mode
        else goToScreen(SCREEN_COMMANDER_SETUP);        // centre ≡ opens the Commander menu
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
