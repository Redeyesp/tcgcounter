/* ============================================================================
 *  ScreenCommanderSetup — the Commander menu: how many players, continue
 *  the running game, or start a new one.
 *
 *  ┌──────────────────────────────────────────┐
 *  │ [< HOME]                  [H HIGH ROLL] │  HIGH ROLL: back to the table, rolling
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
 *  HIGH ROLL goes back to the table and starts a high roll there (who goes
 *  first). Dice: the 🎲 round button on the table.
 *  Touch:   tap a button.
 *  Encoder: turn = move the yellow focus frame, press = choose,
 *           long-press in the question = CANCEL.
 * ==========================================================================*/
#include "Screens.h"
#include "App.h"
#include "GameState.h"
#include "Theme.h"
#include "Ui.h"
#include "UiConfirm.h"
#include "Config.h"
#include <stdio.h>

namespace {

constexpr int8_t ITEM_BACK = 0, ITEM_ROLL = 1, ITEM_CONTINUE = 2, ITEM_FIRST_COUNT = 3;
constexpr int8_t COUNT_BUTTONS = COMMANDER_MAX_PLAYERS - COMMANDER_MIN_PLAYERS + 1;  // 2..6
constexpr int8_t ITEM_COUNT = ITEM_FIRST_COUNT + COUNT_BUTTONS;

constexpr Rect BACK     = {12, 6, 88, 34};
constexpr Rect ROLL_BTN = {172, 6, 136, 34};
constexpr Rect CONTINUE = {12, 48, 296, 64};
constexpr int  COUNT_X0 = 13, COUNT_Y = 146, COUNT_W = 54, COUNT_H = 64, COUNT_PITCH = 60;
constexpr int  CAPTION_Y = 132, HINT_Y = 226;

uint8_t playersFor(int8_t item) { return (uint8_t)(COMMANDER_MIN_PLAYERS + item - ITEM_FIRST_COUNT); }

Rect itemRect(int8_t item) {
  if (item == ITEM_BACK) return BACK;
  if (item == ITEM_ROLL) return ROLL_BTN;
  if (item == ITEM_CONTINUE) return CONTINUE;
  const int k = item - ITEM_FIRST_COUNT;
  return Rect{(int16_t)(COUNT_X0 + k * COUNT_PITCH), (int16_t)COUNT_Y, (int16_t)COUNT_W, (int16_t)COUNT_H};
}

// ---------------------------------------------------------------- UI-only state
ConfirmDialog s_ask;            // "NEW GAME?"
uint8_t s_askPlayers = 0;       // players of the new game being asked about
int8_t  s_focus = ITEM_CONTINUE;
int8_t  s_pressed = -1;         // menu item under the finger
bool    s_drawnAsk = false;
int8_t  s_drawnFocus = -1, s_drawnPressed = -1;

int8_t hitTest(int x, int y) {
  for (int8_t i = 0; i < ITEM_COUNT; ++i)
    if (itemRect(i).contains(x, y)) return i;
  return -1;
}

// ---------------------------------------------------------------- actions
void startGame(uint8_t players) {
  commanderNewGame(g_state.commander, players);
  goToScreen(SCREEN_COMMANDER);
}

void choose(int8_t item) {
  if (item == ITEM_BACK) {
    goToScreen(SCREEN_HOME);
  } else if (item == ITEM_ROLL) {
    commanderRequestHighRoll();  // the table starts rolling as it opens
    goToScreen(SCREEN_COMMANDER);
  } else if (item == ITEM_CONTINUE) {
    goToScreen(SCREEN_COMMANDER);
  } else if (item >= ITEM_FIRST_COUNT && item < ITEM_COUNT) {
    const uint8_t n = playersFor(item);
    if (commanderIsFresh(g_state.commander)) {
      startGame(n);  // nothing to lose: no need to ask
    } else {         // a new game wipes the running one: ask first
      char body[48];
      snprintf(body, sizeof(body), "%u players, everyone at %d life", (unsigned)n,
               (int)COMMANDER_START_LIFE);
      s_askPlayers = n;
      s_ask.open("NEW GAME?", body, "The current game will be lost.", "START");
    }
  }
}

// ---------------------------------------------------------------- drawing
void drawMenuItem(int8_t item) {
  auto& g = gfx();
  const Rect r = itemRect(item);
  const bool pressed = (item == s_pressed);
  const uint16_t fill = pressed ? theme::BUTTON_DOWN : theme::BUTTON;
  const int radius = (item == ITEM_BACK || item == ITEM_ROLL) ? 10 : 12;

  g.fillRect(r.x, r.y, r.w, r.h, theme::BG);
  g.fillRoundRect(r.x, r.y, r.w, r.h, radius, fill);
  if (item == s_focus) uiRoundFrame(g, r.x, r.y, r.w, r.h, radius, 3, theme::ACCENT);

  if (item == ITEM_BACK) {
    uiChevron(g, r.x + 18, r.cy(), 14, 3, false, theme::TEXT);
    g.setFont(theme::fontLabel());
    g.setTextDatum(lgfx::textdatum_t::middle_left);
    g.setTextColor(theme::TEXT);
    g.drawString("HOME", r.x + 32, r.cy() + 1);
  } else if (item == ITEM_ROLL) {
    // "H" badge (like the old round button) + HIGH ROLL
    const int bx = r.x + 18, by = r.cy();
    g.fillCircle(bx, by, 10, theme::TEXT);
    g.setFont(theme::fontLabel());
    g.setTextDatum(lgfx::textdatum_t::middle_center);
    g.setTextColor(fill);
    g.drawString("H", bx + 1, by + 1);
    g.setTextDatum(lgfx::textdatum_t::middle_left);
    g.setTextColor(theme::TEXT);
    g.drawString("HIGH ROLL", r.x + 34, r.cy() + 1);
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

void drawMenuPage() {
  auto& g = gfx();
  g.setFont(theme::fontLabel());
  g.setTextColor(theme::TEXT_DIM);
  g.setTextDatum(lgfx::textdatum_t::middle_left);
  g.drawString("NEW GAME - PLAYERS", 16, CAPTION_Y);
  g.setFont(theme::fontSmall());
  g.setTextDatum(lgfx::textdatum_t::middle_center);
  char buf[32];
  snprintf(buf, sizeof(buf), "Everyone starts at %d life", (int)COMMANDER_START_LIFE);
  g.drawString(buf, 160, HINT_Y);
  for (int8_t i = 0; i < ITEM_COUNT; ++i) drawMenuItem(i);
}

// ---------------------------------------------------------------- module functions
void onEnter() {
  s_ask.close();
  s_pressed = -1;
  s_focus = ITEM_CONTINUE;
}

void handleInput(const InputEvent& e) {
  if (s_ask.isOpen()) {
    if (s_ask.handleInput(e) == ConfirmResult::Confirm) startGame(s_askPlayers);
    return;
  }
  switch (e.type) {
    case InputType::EncoderTurn: {
      int next = (s_focus + e.delta) % ITEM_COUNT;
      if (next < 0) next += ITEM_COUNT;
      s_focus = (int8_t)next;
      break;
    }
    case InputType::EncoderClick:
      choose(s_focus);
      break;
    case InputType::TouchDown:
      s_pressed = hitTest(e.x, e.y);
      if (s_pressed >= 0) s_focus = s_pressed;
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
  const bool ask = s_ask.isOpen();
  const bool pageChanged = ask != s_drawnAsk;
  s_drawnAsk = ask;
  if (ask) {
    s_ask.render(full || pageChanged);
    return;
  }
  if (full || pageChanged) {  // (back on) the menu page: repaint everything
    g.startWrite();
    g.fillScreen(theme::BG);
    drawMenuPage();
    g.endWrite();
  } else {
    if (s_focus == s_drawnFocus && s_pressed == s_drawnPressed) return;
    g.startWrite();
    for (int8_t i = 0; i < ITEM_COUNT; ++i) {
      const bool was = (i == s_drawnFocus || i == s_drawnPressed);
      const bool is  = (i == s_focus || i == s_pressed);
      if (was || is) drawMenuItem(i);
    }
    g.endWrite();
  }
  s_drawnFocus = s_focus;
  s_drawnPressed = s_pressed;
}

}  // namespace

const ScreenModule CommanderSetupScreen = {"CommanderSetup", onEnter, handleInput, render, nullptr};
