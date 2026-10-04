/* ============================================================================
 *  ScreenCommanderSetup — the Commander menu: how many players, continue
 *  the running game, or start a new one.
 *
 *  ┌──────────────────────────────────────────┐
 *  │ [< HOME]                       COMMANDER │
 *  │ ┌──────────────────────────────────────┐ │
 *  │ │ CONTINUE                 4 PLAYERS > │ │  back to the running game
 *  │ └──────────────────────────────────────┘ │
 *  │ NEW GAME - PLAYERS                       │
 *  │ [ 2 ] [ 3 ] [ 4 ] [ 5 ] [ 6 ]            │  new game with that many players
 *  │        Everyone starts at 40 life        │
 *  └──────────────────────────────────────────┘
 *
 *  A new game wipes the running one, so it asks first (CANCEL / START) —
 *  unless nothing has happened yet in the running game (all at 40, no
 *  commander damage): then there is nothing to lose and it starts directly.
 *
 *  Reached from Home -> COMMANDER and from the ≡ button in the game.
 *  Touch:   tap a button.
 *  Encoder: turn = move the yellow focus frame, press = choose,
 *           long-press in the question = CANCEL.
 * ==========================================================================*/
#include "Screens.h"
#include "App.h"
#include "GameState.h"
#include "Theme.h"
#include "Ui.h"
#include "Config.h"
#include <stdio.h>

namespace {

// ---------------------------------------------------------------- menu page
constexpr int8_t ITEM_BACK = 0, ITEM_CONTINUE = 1, ITEM_FIRST_COUNT = 2;
constexpr int8_t COUNT_BUTTONS = COMMANDER_MAX_PLAYERS - COMMANDER_MIN_PLAYERS + 1;  // 2..6
constexpr int8_t ITEM_COUNT = ITEM_FIRST_COUNT + COUNT_BUTTONS;

constexpr Rect BACK     = {12, 6, 92, 34};
constexpr Rect CONTINUE = {12, 48, 296, 64};
constexpr int  COUNT_X0 = 13, COUNT_Y = 146, COUNT_W = 54, COUNT_H = 64, COUNT_PITCH = 60;
constexpr int  CAPTION_Y = 132, HINT_Y = 226;

uint8_t playersFor(int8_t item) { return (uint8_t)(COMMANDER_MIN_PLAYERS + item - ITEM_FIRST_COUNT); }

Rect itemRect(int8_t item) {
  if (item == ITEM_BACK) return BACK;
  if (item == ITEM_CONTINUE) return CONTINUE;
  const int k = item - ITEM_FIRST_COUNT;
  return Rect{(int16_t)(COUNT_X0 + k * COUNT_PITCH), (int16_t)COUNT_Y, (int16_t)COUNT_W, (int16_t)COUNT_H};
}

// ---------------------------------------------------------------- question page
constexpr int8_t ASK_CANCEL = 0, ASK_START = 1, ASK_COUNT = 2;
constexpr Rect ASK_BTN[ASK_COUNT] = {{16, 164, 136, 60}, {168, 164, 136, 60}};

// ---------------------------------------------------------------- UI-only state
uint8_t s_ask = 0;              // players of the new game being asked about; 0 = menu page
int8_t  s_focus = ITEM_CONTINUE;
int8_t  s_askFocus = ASK_CANCEL;
int8_t  s_pressed = -1;         // button under the finger (menu item or ASK_* on the question page)

uint8_t s_drawnAsk = 0xFF;
int8_t  s_drawnFocus = -1, s_drawnAskFocus = -1, s_drawnPressed = -1;

int8_t itemCount() { return s_ask ? ASK_COUNT : ITEM_COUNT; }

int8_t hitTest(int x, int y) {
  for (int8_t i = 0; i < itemCount(); ++i) {
    const Rect r = s_ask ? ASK_BTN[i] : itemRect(i);
    if (r.contains(x, y)) return i;
  }
  return -1;
}

// ---------------------------------------------------------------- actions
void startGame(uint8_t players) {
  commanderNewGame(g_state.commander, players);
  s_ask = 0;
  goToScreen(SCREEN_COMMANDER);
}

void choose(int8_t item) {
  if (s_ask) {
    if (item == ASK_START) startGame(s_ask);
    else if (item == ASK_CANCEL) s_ask = 0;
    return;
  }
  if (item == ITEM_BACK) {
    goToScreen(SCREEN_HOME);
  } else if (item == ITEM_CONTINUE) {
    goToScreen(SCREEN_COMMANDER);
  } else if (item >= ITEM_FIRST_COUNT && item < ITEM_COUNT) {
    const uint8_t n = playersFor(item);
    if (commanderIsFresh(g_state.commander)) {
      startGame(n);  // nothing to lose: no need to ask
    } else {
      s_ask = n;     // a new game wipes the running one: ask first
      s_askFocus = ASK_CANCEL;
    }
  }
}

// ---------------------------------------------------------------- drawing
void drawMenuItem(int8_t item) {
  auto& g = gfx();
  const Rect r = itemRect(item);
  const bool pressed = (item == s_pressed);
  const uint16_t fill = pressed ? theme::BUTTON_DOWN : theme::BUTTON;
  const int radius = item == ITEM_BACK ? 10 : 12;

  g.fillRect(r.x, r.y, r.w, r.h, theme::BG);
  g.fillRoundRect(r.x, r.y, r.w, r.h, radius, fill);
  if (item == s_focus) uiRoundFrame(g, r.x, r.y, r.w, r.h, radius, 3, theme::ACCENT);

  if (item == ITEM_BACK) {
    uiChevron(g, r.x + 18, r.cy(), 14, 3, false, theme::TEXT);
    g.setFont(theme::fontLabel());
    g.setTextDatum(lgfx::textdatum_t::middle_left);
    g.setTextColor(theme::TEXT);
    g.drawString("HOME", r.x + 32, r.cy() + 1);
  } else if (item == ITEM_CONTINUE) {
    g.setFont(theme::fontButton());
    g.setTextDatum(lgfx::textdatum_t::middle_left);
    g.setTextColor(theme::TEXT);
    g.drawString("CONTINUE", r.x + 18, r.cy() + 1);
    char buf[16];
    snprintf(buf, sizeof(buf), "%u PLAYERS", (unsigned)g_state.commander.players);
    g.setFont(theme::fontLabel());
    g.setTextDatum(lgfx::textdatum_t::middle_right);
    g.setTextColor(theme::TEXT_DIM);
    g.drawString(buf, r.x + r.w - 40, r.cy() + 1);
    uiChevron(g, r.x + r.w - 20, r.cy(), 18, 4, true, theme::TEXT_DIM);
  } else {
    char buf[4];
    snprintf(buf, sizeof(buf), "%u", (unsigned)playersFor(item));
    g.setFont(theme::fontHuge());
    g.setTextDatum(lgfx::textdatum_t::middle_center);
    g.setTextColor(theme::TEXT);
    g.drawString(buf, r.cx(), r.cy() + 2);
  }
}

void drawAskButton(int8_t i) {
  auto& g = gfx();
  const Rect& r = ASK_BTN[i];
  const bool pressed = (i == s_pressed);
  uint16_t fill, text;
  if (i == ASK_START) {
    fill = pressed ? theme::DANGER_FILL_DOWN : theme::DANGER_FILL;
    text = theme::TEXT;
  } else {
    fill = pressed ? theme::BUTTON_DOWN : theme::BUTTON;
    text = theme::TEXT;
  }
  g.fillRect(r.x, r.y, r.w, r.h, theme::BG);
  uiTextButton(g, r, i == ASK_START ? "START" : "CANCEL", theme::fontButton(), fill, text, 12);
  if (i == s_askFocus) uiRoundFrame(g, r.x, r.y, r.w, r.h, 12, 3, theme::ACCENT);
}

void drawMenuPage() {
  auto& g = gfx();
  g.setFont(theme::fontButton());
  g.setTextDatum(lgfx::textdatum_t::middle_right);
  g.setTextColor(theme::TEXT_DIM);
  g.drawString("COMMANDER", 308, BACK.cy() + 1);
  g.setFont(theme::fontLabel());
  g.setTextDatum(lgfx::textdatum_t::middle_left);
  g.drawString("NEW GAME - PLAYERS", 16, CAPTION_Y);
  g.setFont(theme::fontSmall());
  g.setTextDatum(lgfx::textdatum_t::middle_center);
  char buf[32];
  snprintf(buf, sizeof(buf), "Everyone starts at %d life", (int)COMMANDER_START_LIFE);
  g.drawString(buf, 160, HINT_Y);
  for (int8_t i = 0; i < ITEM_COUNT; ++i) drawMenuItem(i);
}

void drawAskPage() {
  auto& g = gfx();
  char buf[40];
  g.setTextDatum(lgfx::textdatum_t::middle_center);
  g.setFont(theme::fontTitle());
  g.setTextColor(theme::TEXT);
  g.drawString("NEW GAME?", 160, 44);
  g.setFont(theme::fontBody());
  snprintf(buf, sizeof(buf), "%u players, everyone at %d life", (unsigned)s_ask, (int)COMMANDER_START_LIFE);
  g.drawString(buf, 160, 92);
  g.setFont(theme::fontSmall());
  g.setTextColor(theme::DANGER);
  g.drawString("The current game will be lost.", 160, 124);
  for (int8_t i = 0; i < ASK_COUNT; ++i) drawAskButton(i);
}

// ---------------------------------------------------------------- module functions
void onEnter() {
  s_ask = 0;
  s_pressed = -1;
  s_focus = ITEM_CONTINUE;
}

void handleInput(const InputEvent& e) {
  switch (e.type) {
    case InputType::EncoderTurn: {
      int8_t& f = s_ask ? s_askFocus : s_focus;
      const int n = itemCount();
      int next = (f + e.delta) % n;
      if (next < 0) next += n;
      f = (int8_t)next;
      break;
    }
    case InputType::EncoderClick:
      choose(s_ask ? s_askFocus : s_focus);
      break;
    case InputType::EncoderLongPress:
      if (s_ask) s_ask = 0;  // = CANCEL
      break;
    case InputType::TouchDown:
      s_pressed = hitTest(e.x, e.y);
      if (s_pressed >= 0) (s_ask ? s_askFocus : s_focus) = s_pressed;
      break;
    case InputType::TouchUp: {
      const int8_t released = s_pressed;
      s_pressed = -1;
      if (released >= 0 && isTap(e, TOUCH_TAP_MAX_MS)) choose(released);
      break;
    }
    default:
      break;
  }
}

void render(bool full) {
  auto& g = gfx();
  if (full || s_ask != s_drawnAsk) {  // page changed: repaint everything
    g.startWrite();
    g.fillScreen(theme::BG);
    if (s_ask) drawAskPage();
    else drawMenuPage();
    g.endWrite();
  } else {
    const int8_t focus = s_ask ? s_askFocus : s_focus;
    const int8_t drawnFocus = s_ask ? s_drawnAskFocus : s_drawnFocus;
    if (focus == drawnFocus && s_pressed == s_drawnPressed) return;
    g.startWrite();
    for (int8_t i = 0; i < itemCount(); ++i) {
      const bool was = (i == drawnFocus || i == s_drawnPressed);
      const bool is  = (i == focus || i == s_pressed);
      if (!was && !is) continue;
      if (s_ask) drawAskButton(i);
      else drawMenuItem(i);
    }
    g.endWrite();
  }
  s_drawnAsk = s_ask;
  s_drawnFocus = s_focus;
  s_drawnAskFocus = s_askFocus;
  s_drawnPressed = s_pressed;
}

}  // namespace

const ScreenModule CommanderSetupScreen = {"CommanderSetup", onEnter, handleInput, render, nullptr};
