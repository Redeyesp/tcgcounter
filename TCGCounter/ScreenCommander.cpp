/* ============================================================================
 *  ScreenCommander — 4-player life counter (2x2) with commander damage.
 *
 *  ┌──────────┬──────────┐   P1 top-left, P2 top-right,
 *  │ PLAYER 1 │ PLAYER 2 │   P3 bottom-left, P4 bottom-right.
 *  │    40    │    40    │   Round button in the centre = HOME.
 *  │  • ○ ○ ○ │  • ○ ○ ○ │   Dots = which page the card shows.
 *  │  [-] [+] │  [-] [+] │
 *  ├────────(⌂)──────────┤
 *  │ PLAYER 3 │ PLAYER 4 │
 *  └──────────┴──────────┘
 *
 *  Every player card has 4 pages, flipped by swiping left/right on the
 *  number (or encoder long-press for the selected player):
 *    page 0         life
 *    pages 1..3     commander damage taken FROM each opponent (seat order)
 *  − / + and the encoder change whatever the card currently shows.
 *  Commander damage also costs life (CMD_DAMAGE_AFFECTS_LIFE, like Lotus).
 *  A card returns to its life page after CMD_PAGE_TIMEOUT_MS without use.
 *
 *  OUT: life <= 0 or 21+ commander damage from one opponent. The card turns
 *  red and shows "YOU ARE OUT"; − / + still work, so mistakes can be undone.
 *
 *  Touch:   tap a card = select player · tap/hold −/+ = change (hold repeats)
 *           swipe on the number = next/previous page · tap centre = Home
 *  Encoder: turn = change selected card ±1 per click · press = next player
 *           long-press = next page of the selected player
 * ==========================================================================*/
#include <Arduino.h>
#include "Screens.h"
#include "App.h"
#include "GameState.h"
#include "Theme.h"
#include "Ui.h"
#include "Config.h"
#include <stdio.h>
#include <string.h>

namespace {

// ---------------------------------------------------------------- geometry
constexpr int QW = 159, QH = 119, GAP = 2;   // 159+2+159 = 320, 119+2+119 = 240
constexpr int FRAME_R = 10;                  // card corner radius
constexpr int FRAME_SELECTED = 4;            // selection border thickness
constexpr uint8_t PAGES = COMMANDER_PLAYERS; // life + one page per opponent

// Positions inside a card (local coordinates)
constexpr Rect PILL       = {27, 8, 105, 20};  // label ("PLAYER n" / "FROM Pn")
constexpr Rect CENTER     = {6, 29, 147, 52};  // number / OUT message + page dots
constexpr Rect BTN_MINUS  = {22, 82, 52, 30};
constexpr Rect BTN_PLUS   = {85, 82, 52, 30};
constexpr int  BTN_ZONE_Y = 76;  // touches at/below this line hit − (left half) or + (right half)
constexpr int  DOTS_Y     = 47;  // page dots, inside CENTER

// Centre HOME button (screen coordinates)
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
Hit      s_press = NO_HIT;              // what the finger is holding (feedback/repeat/swipe)
uint8_t  s_page[COMMANDER_PLAYERS];     // page shown on each card (0 = life)
uint32_t s_usedAt[COMMANDER_PLAYERS];   // last interaction per card (page timeout)

uint8_t pageSource(uint8_t player, uint8_t page) {  // page 1..3 -> opponent
  return commanderOpponent(player, (uint8_t)(page - 1));
}

void touchCard(uint8_t p) { s_usedAt[p] = millis(); }

void adjustCard(uint8_t p, int delta) {
  CommanderGame& game = g_state.commander;
  if (s_page[p] == 0) commanderAdjustLife(game, p, delta);
  else commanderAdjustCmdDamage(game, p, pageSource(p, s_page[p]), delta);
  touchCard(p);
}

void flipPage(uint8_t p, int dir) {
  s_page[p] = (uint8_t)((s_page[p] + PAGES + (dir > 0 ? 1 : -1)) % PAGES);
  touchCard(p);
}

// Everything that decides how one card looks. Rendering compares this with
// what was last drawn and repaints only the parts that differ.
struct CardView {
  uint8_t   page;
  uint8_t   source;     // opponent for a damage page
  int16_t   value;      // life or commander damage on screen
  OutReason out;
  uint8_t   outSource;
  bool      selected;
  Zone      pressed;    // Minus / Plus / None
  bool operator==(const CardView& o) const {
    return page == o.page && source == o.source && value == o.value && out == o.out &&
           outSource == o.outSource && selected == o.selected && pressed == o.pressed;
  }
};

CardView viewOf(uint8_t i) {
  const CommanderGame& g = g_state.commander;
  CardView v;
  v.page = s_page[i];
  v.source = v.page ? pageSource(i, v.page) : 0;
  v.value = v.page ? (int16_t)g.cmdDamage[i][v.source] : g.life[i];
  v.outSource = 0;
  v.out = commanderOutReason(g, i, &v.outSource);
  v.selected = (g.selected == i);
  v.pressed = (s_press.player == i && (s_press.zone == Zone::Minus || s_press.zone == Zone::Plus))
                  ? s_press.zone : Zone::None;
  return v;
}

CardView s_drawn[COMMANDER_PLAYERS];
bool     s_drawnHubPressed = false;

uint16_t cardBg(const CardView& v) { return v.out != OutReason::None ? theme::OUT_PANEL : theme::PANEL; }

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

void drawDots(lgfx::LovyanGFX& c, int cx, int y, uint8_t page, uint16_t on, uint16_t off) {
  constexpr int R = 2, STEP = 10;
  const int x0 = cx - STEP * (PAGES - 1) / 2;
  for (uint8_t k = 0; k < PAGES; ++k) c.fillCircle(x0 + k * STEP, y, R, k == page ? on : off);
}

// Contents of the CENTER box, drawn into canvas `c` with the box's top-left at (ox, oy).
void drawCenterContent(lgfx::LovyanGFX& c, int ox, int oy, const CardView& v) {
  const uint16_t bg = cardBg(v);
  const int cx = ox + CENTER.w / 2;
  c.fillRect(ox, oy, CENTER.w, CENTER.h, bg);

  if (v.page == 0 && v.out != OutReason::None) {
    // ---- YOU ARE OUT
    c.setTextDatum(lgfx::textdatum_t::middle_center);
    c.setTextColor(theme::TEXT);
    c.setFont(theme::fontLabel());
    c.drawString("YOU ARE", cx, oy + 9);
    c.setFont(theme::fontTitle());
    c.drawString("OUT", cx, oy + 32);
    c.drawString("OUT", cx + 1, oy + 32);
  } else if (v.page == 0) {
    // ---- life
    drawBigNumber(c, cx, oy + 22, v.value, theme::TEXT);
  } else {
    // ---- commander damage from v.source
    const uint16_t col = (v.out != OutReason::None) ? theme::TEXT : theme::PLAYER[v.source];
    drawBigNumber(c, cx - 8, oy + 22, v.value, col);
    c.setFont(theme::fontSmall());
    c.setTextDatum(lgfx::textdatum_t::middle_left);
    c.setTextColor(theme::TEXT_DIM);
    c.drawString("/21", cx + 30, oy + 30);
  }
  drawDots(c, cx, oy + DOTS_Y, v.page, theme::TEXT, theme::DOT_OFF);
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
  if (v.selected) {
    uiRoundFrame(q.x, q.y, q.w, q.h, FRAME_R, FRAME_SELECTED, theme::PLAYER[i]);
  } else {
    uiRoundFrame(q.x, q.y, q.w, q.h, FRAME_R, FRAME_SELECTED, cardBg(v));  // erase thick border
    uiRoundFrame(q.x, q.y, q.w, q.h, FRAME_R, 1, theme::PANEL_EDGE);
  }
}

void drawLabel(uint8_t i, const CardView& v) {
  auto& g = gfx();
  const Rect p = offset(PILL, quadRect(i));
  char buf[12];
  g.fillRect(p.x, p.y, p.w, p.h, cardBg(v));
  uint16_t textCol;
  if (v.page == 0) {
    snprintf(buf, sizeof(buf), "PLAYER %u", (unsigned)(i + 1));
    if (v.selected) g.fillRoundRect(p.x, p.y, p.w, p.h, p.h / 2, theme::PLAYER[i]);
    textCol = v.selected ? theme::TEXT_ON_ACCENT : theme::PLAYER[i];
  } else {
    // Damage page: neutral pill, text in the attacker's colour
    snprintf(buf, sizeof(buf), "FROM P%u", (unsigned)(v.source + 1));
    g.fillRoundRect(p.x, p.y, p.w, p.h, p.h / 2, theme::BUTTON);
    textCol = theme::PLAYER[v.source];
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

void drawHub(bool pressed) {
  auto& g = gfx();
  const uint16_t fill = pressed ? theme::ACCENT : theme::BUTTON;
  g.fillCircle(HUB_X, HUB_Y, HUB_R + 3, theme::BG);  // dark moat separates it from the frames
  g.fillCircle(HUB_X, HUB_Y, HUB_R, fill);
  g.drawCircle(HUB_X, HUB_Y, HUB_R, theme::PANEL_EDGE);
  uiHomeIcon(HUB_X, HUB_Y + 1, pressed ? theme::TEXT_ON_ACCENT : theme::TEXT, fill);
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
  memset(s_page, 0, sizeof(s_page));  // always come back to the life view
}

void handleInput(const InputEvent& e) {
  CommanderGame& game = g_state.commander;
  switch (e.type) {
    case InputType::EncoderTurn:
      adjustCard(game.selected, e.delta);
      break;

    case InputType::EncoderClick:
      commanderSelectNext(game);
      touchCard(game.selected);
      break;

    case InputType::EncoderLongPress:  // flip the selected card to its next page
      flipPage(game.selected, +1);
      break;

    case InputType::TouchDown:
      s_press = hitTest(e.x, e.y);
      if (s_press.zone == Zone::Area || s_press.zone == Zone::Minus || s_press.zone == Zone::Plus) {
        commanderSelect(game, s_press.player);
        touchCard(s_press.player);
      }
      if (s_press.zone == Zone::Minus) adjustCard(s_press.player, -1);
      if (s_press.zone == Zone::Plus)  adjustCard(s_press.player, +1);
      break;

    case InputType::TouchRepeat:  // hold-to-repeat on − / +
      if (s_press.zone == Zone::Minus) adjustCard(s_press.player, -1);
      if (s_press.zone == Zone::Plus)  adjustCard(s_press.player, +1);
      break;

    case InputType::TouchSwipe:  // only swipes that start on the number/label area
      // Swipe left (finger moves left) = next page, like turning a page.
      if (s_press.zone == Zone::Area) flipPage(s_press.player, e.delta < 0 ? +1 : -1);
      break;

    case InputType::TouchUp: {
      const Hit released = s_press;
      s_press = NO_HIT;
      if (released.zone == Zone::Hub && isTap(e, TOUCH_TAP_MAX_MS)) goToScreen(SCREEN_HOME);
      break;
    }
  }
}

void tick(uint32_t now) {
  if (CMD_PAGE_TIMEOUT_MS == 0) return;
  for (uint8_t p = 0; p < COMMANDER_PLAYERS; ++p) {
    const bool held = (s_press.player == p && s_press.zone != Zone::None && s_press.zone != Zone::Hub);
    if (s_page[p] != 0 && !held && now - s_usedAt[p] >= (uint32_t)CMD_PAGE_TIMEOUT_MS) s_page[p] = 0;
  }
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
    g.endWrite();
    return;
  }

  bool hubDirty = (hubPressed != s_drawnHubPressed);
  bool started = false;
  for (uint8_t i = 0; i < COMMANDER_PLAYERS; ++i) {
    const CardView v = viewOf(i);
    const CardView& d = s_drawn[i];
    if (v == d) continue;
    if (!started) { g.startWrite(); started = true; }

    const bool bgChanged = (v.out != OutReason::None) != (d.out != OutReason::None);
    if (bgChanged) {
      drawCard(i, v);  // background colour changes: repaint the whole card
      hubDirty = true;
    } else {
      if (v.selected != d.selected) { drawFrame(i, v); hubDirty = true; }
      if (v.selected != d.selected || v.page != d.page || v.source != d.source) drawLabel(i, v);
      if (v.page != d.page || v.source != d.source || v.value != d.value || v.out != d.out)
        drawCenter(i, v);
      if (v.pressed != d.pressed) drawButtons(i, v);
    }
    s_drawn[i] = v;
  }
  if (hubDirty) {
    if (!started) { g.startWrite(); started = true; }
    drawHub(hubPressed);
    s_drawnHubPressed = hubPressed;
  }
  if (started) g.endWrite();
}

}  // namespace

const ScreenModule CommanderScreen = {"Commander", onEnter, handleInput, render, tick};
