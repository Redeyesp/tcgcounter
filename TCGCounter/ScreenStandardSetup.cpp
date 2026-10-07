/* ============================================================================
 *  ScreenStandardSetup — the Standard menu (MTG 1v1, 20 life): continue the
 *  running game, roll for who goes first, or start a new game.
 *
 *  ┌──────────────────────────────────────────┐
 *  │ [< HOME]                  [H HIGH ROLL] │  HIGH ROLL: back to the table, rolling
 *  │ ┌──────────────────────────────────────┐ │
 *  │ │ CONTINUE                LIFE 18 · 7 > │ │  back to the running game
 *  │ └──────────────────────────────────────┘ │
 *  │ STANDARD - NEW GAME                      │
 *  │ ┌──────────────────────────────────────┐ │
 *  │ │                 1v1                  │ │  both players back to 20
 *  │ │               20 LIFE                │ │
 *  │ └──────────────────────────────────────┘ │
 *  │     Magic: 2 players, 20 life each       │
 *  └──────────────────────────────────────────┘
 *
 *  A new game wipes the running one, so it asks first (CANCEL / START) —
 *  unless both players are still at 20: then there is nothing to lose.
 *
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

// ---------------------------------------------------------------- items
constexpr int8_t ITEM_BACK = 0, ITEM_ROLL = 1, ITEM_CONTINUE = 2, ITEM_NEW = 3, ITEM_COUNT = 4;
constexpr Rect ITEMS[ITEM_COUNT] = {
  {12, 6, 88, 34},     // < HOME
  {172, 6, 136, 34},   // HIGH ROLL
  {12, 48, 296, 64},   // CONTINUE
  {12, 146, 296, 64},  // NEW GAME: 1v1, 20 life
};
constexpr int CAPTION_Y = 132, HINT_Y = 226;

// ---------------------------------------------------------------- UI-only state
ConfirmDialog s_ask;  // "NEW GAME?"
int8_t  s_focus = ITEM_CONTINUE;
int8_t  s_pressed = -1;
bool    s_drawnAsk = false;
int8_t  s_drawnFocus = -1, s_drawnPressed = -1;

int8_t hitTest(int x, int y) {
  for (int8_t i = 0; i < ITEM_COUNT; ++i)
    if (ITEMS[i].contains(x, y)) return i;
  return -1;
}

// ---------------------------------------------------------------- actions
CommanderGame& game() { return g_state.standard; }

void startGame() {
  standardNewGame(game());
  goToScreen(SCREEN_STANDARD);
}

void choose(int8_t item) {
  switch (item) {
    case ITEM_BACK:
      goToScreen(SCREEN_HOME);
      break;
    case ITEM_ROLL:
      commanderRequestHighRoll();  // the table starts rolling as it opens
      goToScreen(SCREEN_STANDARD);
      break;
    case ITEM_CONTINUE:
      goToScreen(SCREEN_STANDARD);
      break;
    case ITEM_NEW:
      if (standardIsFresh(game())) {
        startGame();  // nothing to lose: no need to ask
      } else {        // a new game wipes the running one: ask first
        char body[40];
        snprintf(body, sizeof(body), "Both players back to %d life", (int)STANDARD_START_LIFE);
        s_ask.open("NEW GAME?", body, "The current game will be lost.", "START");
      }
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------- drawing
void text(int x, int y, const char* t, const lgfx::IFont* f, uint16_t col, lgfx::textdatum_t datum) {
  auto& g = gfx();
  g.setFont(f);
  g.setTextDatum(datum);
  g.setTextColor(col);
  g.drawString(t, x, y);
}

void drawMenuItem(int8_t item) {
  auto& g = gfx();
  const Rect& r = ITEMS[item];
  const bool pressed = (item == s_pressed);
  const uint16_t fill = pressed ? theme::BUTTON_DOWN : theme::BUTTON;
  const int radius = (item == ITEM_BACK || item == ITEM_ROLL) ? 10 : 12;

  g.fillRect(r.x, r.y, r.w, r.h, theme::BG);
  g.fillRoundRect(r.x, r.y, r.w, r.h, radius, fill);
  if (item == s_focus) uiRoundFrame(g, r.x, r.y, r.w, r.h, radius, 3, theme::ACCENT);

  if (item == ITEM_BACK) {
    uiChevron(g, r.x + 18, r.cy(), 14, 3, false, theme::TEXT);
    text(r.x + 32, r.cy() + 1, "HOME", theme::fontLabel(), theme::TEXT, lgfx::textdatum_t::middle_left);
  } else if (item == ITEM_ROLL) {
    const int bx = r.x + 18, by = r.cy();  // "H" badge + HIGH ROLL
    g.fillCircle(bx, by, 10, theme::TEXT);
    text(bx + 1, by + 1, "H", theme::fontLabel(), fill, lgfx::textdatum_t::middle_center);
    text(r.x + 34, r.cy() + 1, "HIGH ROLL", theme::fontLabel(), theme::TEXT, lgfx::textdatum_t::middle_left);
  } else if (item == ITEM_CONTINUE) {
    text(r.x + 18, r.cy() + 1, "CONTINUE", theme::fontButton(), theme::TEXT, lgfx::textdatum_t::middle_left);
    // both life totals, each in its player's colour: "18 · 7"
    const int right = r.x + r.w - 40, y = r.cy() + 11;
    char a[8], b[8];
    snprintf(a, sizeof(a), "%d", (int)game().life[0]);
    snprintf(b, sizeof(b), "%d", (int)game().life[1]);
    g.setFont(theme::fontLabel());
    const int wb = g.textWidth(b), wdot = g.textWidth(" - ");
    text(right, y, b, theme::fontLabel(), theme::PLAYER[1], lgfx::textdatum_t::middle_right);
    text(right - wb, y, " - ", theme::fontLabel(), theme::TEXT_DIM, lgfx::textdatum_t::middle_right);
    text(right - wb - wdot, y, a, theme::fontLabel(), theme::PLAYER[0], lgfx::textdatum_t::middle_right);
    text(right, r.cy() - 10, "LIFE", theme::fontSmall(), theme::TEXT_DIM, lgfx::textdatum_t::middle_right);
    uiChevron(g, r.x + r.w - 20, r.cy(), 18, 4, true, theme::TEXT_DIM);
  } else {
    text(r.cx(), r.cy() - 8, "1v1", theme::fontTitle(), theme::TEXT, lgfx::textdatum_t::middle_center);
    char buf[16];
    snprintf(buf, sizeof(buf), "%d LIFE", (int)STANDARD_START_LIFE);
    text(r.cx(), r.y + r.h - 12, buf, theme::fontSmall(), theme::TEXT_DIM, lgfx::textdatum_t::middle_center);
  }
}

void drawMenuPage() {
  text(16, CAPTION_Y, "STANDARD - NEW GAME", theme::fontLabel(), theme::TEXT_DIM, lgfx::textdatum_t::middle_left);
  char hint[48];
  snprintf(hint, sizeof(hint), "Magic: 2 players, %d life each", (int)STANDARD_START_LIFE);
  text(160, HINT_Y, hint, theme::fontSmall(), theme::TEXT_DIM, lgfx::textdatum_t::middle_center);
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
    if (s_ask.handleInput(e) == ConfirmResult::Confirm) startGame();
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

const ScreenModule StandardSetupScreen = {"StandardSetup", onEnter, handleInput, render, nullptr};
