/* ============================================================================
 *  ScreenDigimonSetup — the Digimon menu: continue the running game, or
 *  start a new one by choosing who goes first. Faces the bottom player, like
 *  the other landscape menus.
 *
 *  ┌──────────────────────────────────────────┐
 *  │ [< HOME]                        DIGIMON │
 *  │ ┌──────────────────────────────────────┐ │
 *  │ │ CONTINUE                 TURN TOP  > │ │  back to the running game
 *  │ └──────────────────────────────────────┘ │
 *  │ NEW GAME - WHO GOES FIRST?               │
 *  │ [    BOTTOM     ]   [      TOP      ]    │  memory 0, that player's turn
 *  │   PASS: the opponent gets 3 memory       │
 *  └──────────────────────────────────────────┘
 *
 *  A new game wipes the running one, so it asks first (CANCEL / START) —
 *  unless memory is still at 0: then there is nothing to lose.
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
constexpr int8_t ITEM_BACK = 0, ITEM_CONTINUE = 1, ITEM_FIRST_BOTTOM = 2, ITEM_FIRST_TOP = 3, ITEM_COUNT = 4;
constexpr Rect ITEMS[ITEM_COUNT] = {
  {12, 6, 88, 34},     // < HOME
  {12, 48, 296, 64},   // CONTINUE
  {12, 146, 144, 64},  // BOTTOM goes first (player 1)
  {164, 146, 144, 64}, // TOP goes first (player 2)
};
constexpr int CAPTION_Y = 132, HINT_Y = 226;
const char* const SEAT_NAME[2] = {"BOTTOM", "TOP"};

// ---------------------------------------------------------------- UI-only state
ConfirmDialog s_ask;  // "NEW GAME?"
uint8_t s_askFirst = 0;
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
DigimonGame& game() { return g_state.digimon; }

void startGame(uint8_t first) {
  digimonNewGame(game(), first);
  goToScreen(SCREEN_DIGIMON);
}

void choose(int8_t item) {
  if (item == ITEM_BACK) {
    goToScreen(SCREEN_HOME);
  } else if (item == ITEM_CONTINUE) {
    goToScreen(SCREEN_DIGIMON);
  } else if (item == ITEM_FIRST_BOTTOM || item == ITEM_FIRST_TOP) {
    const uint8_t first = item == ITEM_FIRST_TOP ? 1 : 0;
    if (digimonIsFresh(game())) {
      startGame(first);  // nothing to lose: no need to ask
    } else {             // a new game wipes the running one: ask first
      s_askFirst = first;
      s_ask.open("NEW GAME?", first ? "Top player goes first" : "Bottom player goes first",
                 "Memory goes back to 0.", "START");
    }
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
  const int radius = item == ITEM_BACK ? 10 : 12;

  g.fillRect(r.x, r.y, r.w, r.h, theme::BG);
  g.fillRoundRect(r.x, r.y, r.w, r.h, radius, pressed ? theme::BUTTON_DOWN : theme::BUTTON);
  if (item == s_focus) uiRoundFrame(g, r.x, r.y, r.w, r.h, radius, 3, theme::ACCENT);

  if (item == ITEM_BACK) {
    uiChevron(g, r.x + 18, r.cy(), 14, 3, false, theme::TEXT);
    text(r.x + 32, r.cy() + 1, "HOME", theme::fontLabel(), theme::TEXT, lgfx::textdatum_t::middle_left);
  } else if (item == ITEM_CONTINUE) {
    text(r.x + 18, r.cy() + 1, "CONTINUE", theme::fontButton(), theme::TEXT, lgfx::textdatum_t::middle_left);
    text(r.x + r.w - 40, r.cy() - 10, "TURN", theme::fontSmall(), theme::TEXT_DIM, lgfx::textdatum_t::middle_right);
    text(r.x + r.w - 40, r.cy() + 11, SEAT_NAME[game().turn], theme::fontLabel(), theme::PLAYER[game().turn],
         lgfx::textdatum_t::middle_right);
    uiChevron(g, r.x + r.w - 20, r.cy(), 18, 4, true, theme::TEXT_DIM);
  } else {
    const uint8_t p = item == ITEM_FIRST_TOP ? 1 : 0;
    text(r.cx(), r.cy() - 8, SEAT_NAME[p], theme::fontButton(), theme::PLAYER[p], lgfx::textdatum_t::middle_center);
    text(r.cx(), r.y + r.h - 12, "GOES FIRST", theme::fontSmall(), theme::TEXT_DIM, lgfx::textdatum_t::middle_center);
  }
}

void drawMenuPage() {
  text(308, 23, "DIGIMON", theme::fontLabel(), theme::TEXT_DIM, lgfx::textdatum_t::middle_right);
  text(16, CAPTION_Y, "NEW GAME - WHO GOES FIRST?", theme::fontLabel(), theme::TEXT_DIM,
       lgfx::textdatum_t::middle_left);
  char hint[48];
  snprintf(hint, sizeof(hint), "PASS: the opponent gets %u memory", (unsigned)DIGIMON_PASS_MEMORY);
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
    if (s_ask.handleInput(e) == ConfirmResult::Confirm) startGame(s_askFirst);
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

const ScreenModule DigimonSetupScreen = {"DigimonSetup", onEnter, handleInput, render, nullptr};
