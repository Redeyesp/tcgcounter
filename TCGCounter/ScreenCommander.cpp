/* ============================================================================
 *  ScreenCommander — 4-player life counter (2x2 grid).
 *
 *  ┌──────────┬──────────┐   P1 top-left, P2 top-right,
 *  │ PLAYER 1 │ PLAYER 2 │   P3 bottom-left, P4 bottom-right.
 *  │    40    │    40    │
 *  │  [-] [+] │  [-] [+] │   Round button in the centre = HOME.
 *  ├────────(⌂)──────────┤
 *  │ PLAYER 3 │ PLAYER 4 │
 *  └──────────┴──────────┘
 *
 *  Touch:   tap a quadrant = select that player
 *           tap/hold − or + = select that player and change life (hold repeats)
 *           tap centre button = back to Home (game is kept)
 *  Encoder: turn = selected player's life ±1 per click, press = next player
 * ==========================================================================*/
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
constexpr int FRAME_R = 10;                  // quadrant corner radius
constexpr int FRAME_SELECTED = 4;            // selection border thickness

// Positions inside a quadrant (local coordinates)
constexpr Rect PILL       = {27, 8, 105, 20};  // "PLAYER n" label (text is 88 px wide)
constexpr Rect LIFE       = {6, 30, 147, 50};  // life number area
constexpr Rect BTN_MINUS  = {22, 82, 52, 30};
constexpr Rect BTN_PLUS   = {85, 82, 52, 30};
constexpr int  BTN_ZONE_Y = 76;  // touches at/below this line hit − (left half) or + (right half)

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
Hit s_press = NO_HIT;  // what the finger is currently holding (for feedback/repeat)

struct Drawn {
  int16_t life[COMMANDER_PLAYERS];
  uint8_t selected;
  Hit press;
};
Drawn s_drawn;

// ---------------------------------------------------------------- drawing
lgfx::LGFX_Sprite* lifeSprite() {
  static lgfx::LGFX_Sprite spr(&gfx());
  static bool tried = false;
  if (!tried) {
    tried = true;
    spr.setColorDepth(16);
    spr.createSprite(LIFE.w, LIFE.h);  // ~15 KB; falls back to direct drawing if it fails
  }
  return spr.getBuffer() ? &spr : nullptr;
}

void drawLife(uint8_t i) {
  const Rect r = offset(LIFE, quadRect(i));
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", g_state.commander.life[i]);

  // Draw off-screen then push in one go: no flicker while numbers change.
  if (auto* s = lifeSprite()) {
    s->fillScreen(theme::PANEL);
    s->setFont(theme::fontLife());
    s->setTextDatum(lgfx::textdatum_t::middle_center);
    s->setTextColor(theme::TEXT);
    s->drawString(buf, LIFE.w / 2, LIFE.h / 2);
    if (theme::LIFE_FAUX_BOLD) s->drawString(buf, LIFE.w / 2 + 1, LIFE.h / 2);
    s->pushSprite(r.x, r.y);
  } else {
    auto& g = gfx();
    g.fillRect(r.x, r.y, r.w, r.h, theme::PANEL);
    g.setFont(theme::fontLife());
    g.setTextDatum(lgfx::textdatum_t::middle_center);
    g.setTextColor(theme::TEXT);
    g.drawString(buf, r.cx(), r.cy());
    if (theme::LIFE_FAUX_BOLD) g.drawString(buf, r.cx() + 1, r.cy());
  }
}

void drawFrame(uint8_t i, bool selected) {
  const Rect q = quadRect(i);
  if (selected) {
    uiRoundFrame(q.x, q.y, q.w, q.h, FRAME_R, FRAME_SELECTED, theme::PLAYER[i]);
  } else {
    uiRoundFrame(q.x, q.y, q.w, q.h, FRAME_R, FRAME_SELECTED, theme::PANEL);  // erase thick border
    uiRoundFrame(q.x, q.y, q.w, q.h, FRAME_R, 1, theme::PANEL_EDGE);
  }
}

void drawLabel(uint8_t i, bool selected) {
  auto& g = gfx();
  const Rect p = offset(PILL, quadRect(i));
  char buf[12];
  snprintf(buf, sizeof(buf), "PLAYER %u", (unsigned)(i + 1));
  g.fillRect(p.x, p.y, p.w, p.h, theme::PANEL);
  if (selected) g.fillRoundRect(p.x, p.y, p.w, p.h, p.h / 2, theme::PLAYER[i]);
  g.setFont(theme::fontLabel());
  g.setTextDatum(lgfx::textdatum_t::middle_center);
  g.setTextColor(selected ? theme::TEXT_ON_ACCENT : theme::PLAYER[i]);
  g.drawString(buf, p.cx(), p.cy() + 1);
}

void drawButton(uint8_t i, Zone which, bool pressed) {
  const Rect b = offset(which == Zone::Minus ? BTN_MINUS : BTN_PLUS, quadRect(i));
  const uint16_t fill = pressed ? theme::PLAYER[i] : theme::BUTTON;
  const uint16_t sym  = pressed ? theme::TEXT_ON_ACCENT : theme::TEXT;
  gfx().fillRoundRect(b.x, b.y, b.w, b.h, 8, fill);
  if (which == Zone::Minus) uiMinus(b.cx(), b.cy(), 18, 4, sym);
  else                      uiPlus(b.cx(), b.cy(), 18, 4, sym);
}

void drawHub(bool pressed) {
  auto& g = gfx();
  const uint16_t fill = pressed ? theme::ACCENT : theme::BUTTON;
  g.fillCircle(HUB_X, HUB_Y, HUB_R + 3, theme::BG);  // dark moat separates it from the frames
  g.fillCircle(HUB_X, HUB_Y, HUB_R, fill);
  g.drawCircle(HUB_X, HUB_Y, HUB_R, theme::PANEL_EDGE);
  uiHomeIcon(HUB_X, HUB_Y + 1, pressed ? theme::TEXT_ON_ACCENT : theme::TEXT, fill);
}

void drawQuadrant(uint8_t i, bool selected) {
  const Rect q = quadRect(i);
  gfx().fillRoundRect(q.x, q.y, q.w, q.h, FRAME_R, theme::PANEL);
  drawFrame(i, selected);
  drawLabel(i, selected);
  drawLife(i);
  drawButton(i, Zone::Minus, false);
  drawButton(i, Zone::Plus, false);
}

bool isButton(const Hit& h) { return h.zone == Zone::Minus || h.zone == Zone::Plus; }

// ---------------------------------------------------------------- module functions
void onEnter() { s_press = NO_HIT; }

void handleInput(const InputEvent& e) {
  CommanderGame& game = g_state.commander;
  switch (e.type) {
    case InputType::EncoderTurn:
      commanderAdjustLife(game, game.selected, e.delta);
      break;

    case InputType::EncoderClick:
      commanderSelectNext(game);
      break;

    case InputType::EncoderLongPress:
      // Reserved. FUTURE: open a game menu (reset with confirmation, etc.).
      break;

    case InputType::TouchDown: {
      s_press = hitTest(e.x, e.y);
      if (s_press.zone == Zone::Area || isButton(s_press)) commanderSelect(game, s_press.player);
      if (s_press.zone == Zone::Minus) commanderAdjustLife(game, s_press.player, -1);
      if (s_press.zone == Zone::Plus)  commanderAdjustLife(game, s_press.player, +1);
      break;
    }

    case InputType::TouchRepeat:  // hold-to-repeat on − / +
      if (s_press.zone == Zone::Minus) commanderAdjustLife(game, s_press.player, -1);
      if (s_press.zone == Zone::Plus)  commanderAdjustLife(game, s_press.player, +1);
      break;

    case InputType::TouchUp: {
      const Hit released = s_press;
      s_press = NO_HIT;
      if (released.zone == Zone::Hub && e.durationMs <= TOUCH_TAP_MAX_MS) goToScreen(SCREEN_HOME);
      break;
    }
  }
}

void render(bool full) {
  const CommanderGame& game = g_state.commander;
  auto& g = gfx();

  if (full) {
    g.startWrite();
    g.fillScreen(theme::BG);
    for (uint8_t i = 0; i < COMMANDER_PLAYERS; ++i) drawQuadrant(i, i == game.selected);
    drawHub(s_press.zone == Zone::Hub);
    g.endWrite();
    for (uint8_t i = 0; i < COMMANDER_PLAYERS; ++i) s_drawn.life[i] = game.life[i];
    s_drawn.selected = game.selected;
    s_drawn.press = s_press;
    return;
  }

  // Incremental: compare with what is on screen and repaint only the differences.
  bool changed = game.selected != s_drawn.selected || s_press != s_drawn.press;
  for (uint8_t i = 0; i < COMMANDER_PLAYERS && !changed; ++i) changed = game.life[i] != s_drawn.life[i];
  if (!changed) return;

  g.startWrite();
  bool hubDirty = false;

  if (game.selected != s_drawn.selected) {
    drawFrame(s_drawn.selected, false);
    drawLabel(s_drawn.selected, false);
    drawFrame(game.selected, true);
    drawLabel(game.selected, true);
    hubDirty = true;  // frames pass under the centre button
    s_drawn.selected = game.selected;
  }

  for (uint8_t i = 0; i < COMMANDER_PLAYERS; ++i) {
    if (game.life[i] != s_drawn.life[i]) {
      drawLife(i);
      s_drawn.life[i] = game.life[i];
    }
  }

  if (s_press != s_drawn.press) {
    if (isButton(s_drawn.press)) drawButton(s_drawn.press.player, s_drawn.press.zone, false);
    if (isButton(s_press))       drawButton(s_press.player, s_press.zone, true);
    if (s_drawn.press.zone == Zone::Hub || s_press.zone == Zone::Hub) hubDirty = true;
    s_drawn.press = s_press;
  }

  if (hubDirty) drawHub(s_press.zone == Zone::Hub);
  g.endWrite();
}

}  // namespace

const ScreenModule CommanderScreen = {"Commander", onEnter, handleInput, render};
