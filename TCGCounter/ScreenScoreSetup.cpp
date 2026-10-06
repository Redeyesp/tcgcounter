/* ============================================================================
 *  ScreenScoreSetup — the Riftbound and Lorcana menus: continue the running
 *  game, roll for who goes first, or start a new game in another format.
 *  One module serves both; which game follows g_state.screen.
 *
 *  ┌──────────────────────────────────────────┐
 *  │ [< HOME]                  [H HIGH ROLL] │  HIGH ROLL: back to the table, rolling
 *  │ ┌──────────────────────────────────────┐ │
 *  │ │ CONTINUE                   4P TO 8 > │ │  back to the running game
 *  │ └──────────────────────────────────────┘ │
 *  │ RIFTBOUND - NEW GAME                     │
 *  │ [ 1v1  ] [  4P   ] [ 2v2  ]              │  Riftbound: 1v1 to 8, 4 players
 *  │ [ TO 8 ] [FFA TO 8] [TO 11]              │  free-for-all to 8, 2v2 teams to 11
 *  │       +1 on a card = plus life           │
 *  └──────────────────────────────────────────┘
 *  Lorcana: [2P TO 20] [2P TO 25] [4P TO 20] [4P TO 25] (lore)
 *
 *  A new game wipes the running one, so it asks first (CANCEL / START) —
 *  unless everyone is still at 0: then there is nothing to lose.
 *
 *  Reached from Home -> RIFTBOUND / LORCANA and from the ≡ button on the table.
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

// ---------------------------------------------------------------- which game
struct Format {
  uint8_t     players;
  uint8_t     target;
  bool        teams;
  const char* big;    // button text
  const char* small;  // under it
};
const Format RIFTBOUND_FORMATS[] = {
  {2, RIFTBOUND_TARGET,      false, "1v1", "TO 8"},
  {4, RIFTBOUND_TARGET,      false, "4P",  "FFA TO 8"},
  {2, RIFTBOUND_TEAM_TARGET, true,  "2v2", "TO 11"},
};
const Format LORCANA_FORMATS[] = {
  {2, LORCANA_TARGET,      false, "2P", "TO 20"},
  {2, LORCANA_LONG_TARGET, false, "2P", "TO 25"},
  {4, LORCANA_TARGET,      false, "4P", "TO 20"},
  {4, LORCANA_LONG_TARGET, false, "4P", "TO 25"},
};

struct GameMenu {
  Screen        table;
  const char*   name;
  ScoreGame&  (*game)();
  const Format* formats;
  int8_t        formatCount;
  const char*   hint;
  const char*   unit;  // in the NEW GAME? question: "first to 25 lore"
};
ScoreGame& riftbound() { return g_state.riftbound; }
ScoreGame& lorcana()   { return g_state.lorcana; }
const GameMenu MENUS[] = {
  {SCREEN_RIFTBOUND, "RIFTBOUND", riftbound, RIFTBOUND_FORMATS, 3, "+1 on a card = plus life (one each)", ""},
  {SCREEN_LORCANA,   "LORCANA",   lorcana,   LORCANA_FORMATS,   4, "First to 20 or 25 lore wins",        " lore"},
};
const GameMenu& menu() { return g_state.screen == SCREEN_LORCANA_SETUP ? MENUS[1] : MENUS[0]; }

// ---------------------------------------------------------------- items
constexpr int8_t ITEM_BACK = 0, ITEM_ROLL = 1, ITEM_CONTINUE = 2, ITEM_FIRST_FORMAT = 3;
int8_t itemCount() { return (int8_t)(ITEM_FIRST_FORMAT + menu().formatCount); }

constexpr Rect BACK     = {12, 6, 88, 34};
constexpr Rect ROLL_BTN = {172, 6, 136, 34};
constexpr Rect CONTINUE = {12, 48, 296, 64};
constexpr int  FORMAT_X0 = 12, FORMAT_Y = 146, FORMAT_H = 64, FORMAT_GAP = 8, FORMAT_SPAN = 296;
constexpr int  CAPTION_Y = 132, HINT_Y = 226;

Rect itemRect(int8_t item) {
  if (item == ITEM_BACK) return BACK;
  if (item == ITEM_ROLL) return ROLL_BTN;
  if (item == ITEM_CONTINUE) return CONTINUE;
  const int n = menu().formatCount, k = item - ITEM_FIRST_FORMAT;
  const int w = (FORMAT_SPAN - (n - 1) * FORMAT_GAP) / n;
  return Rect{(int16_t)(FORMAT_X0 + k * (w + FORMAT_GAP)), (int16_t)FORMAT_Y, (int16_t)w, (int16_t)FORMAT_H};
}

// "4P TO 8", "2v2 TO 11", "2P TO 25"
void formatText(const ScoreGame& g, char* buf, size_t n) {
  if (g.teams) snprintf(buf, n, "2v2 TO %u", (unsigned)g.target);
  else         snprintf(buf, n, "%uP TO %u", (unsigned)g.players, (unsigned)g.target);
}

// ---------------------------------------------------------------- UI-only state
ConfirmDialog s_ask;            // "NEW GAME?"
int8_t  s_askFormat = 0;        // format of the new game being asked about
int8_t  s_focus = ITEM_CONTINUE;
int8_t  s_pressed = -1;         // menu item under the finger
bool    s_drawnAsk = false;
int8_t  s_drawnFocus = -1, s_drawnPressed = -1;

int8_t hitTest(int x, int y) {
  for (int8_t i = 0; i < itemCount(); ++i)
    if (itemRect(i).contains(x, y)) return i;
  return -1;
}

// ---------------------------------------------------------------- actions
void startGame(int8_t k) {
  const Format& f = menu().formats[k];
  scoreNewGame(menu().game(), f.players, f.target, f.teams);
  goToScreen(menu().table);
}

void choose(int8_t item) {
  if (item == ITEM_BACK) {
    goToScreen(SCREEN_HOME);
  } else if (item == ITEM_ROLL) {
    scoreRequestHighRoll();  // the table starts rolling as it opens
    goToScreen(menu().table);
  } else if (item == ITEM_CONTINUE) {
    goToScreen(menu().table);
  } else if (item >= ITEM_FIRST_FORMAT && item < itemCount()) {
    const int8_t k = (int8_t)(item - ITEM_FIRST_FORMAT);
    if (scoreIsFresh(menu().game())) {
      startGame(k);  // nothing to lose: no need to ask
    } else {         // a new game wipes the running one: ask first
      const Format& f = menu().formats[k];
      char body[48];
      if (f.teams) snprintf(body, sizeof(body), "2 teams, first to %u", (unsigned)f.target);
      else snprintf(body, sizeof(body), "%u players, first to %u%s", (unsigned)f.players,
                    (unsigned)f.target, menu().unit);
      s_askFormat = k;
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
    const int bx = r.x + 18, by = r.cy();  // "H" badge + HIGH ROLL
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
    formatText(menu().game(), buf, sizeof(buf));
    g.setFont(theme::fontLabel());
    g.setTextDatum(lgfx::textdatum_t::middle_right);
    g.setTextColor(theme::TEXT_DIM);
    g.drawString(buf, r.x + r.w - 40, r.cy() + 1);
    uiChevron(g, r.x + r.w - 20, r.cy(), 18, 4, true, theme::TEXT_DIM);
  } else {
    const Format& f = menu().formats[item - ITEM_FIRST_FORMAT];
    g.setFont(theme::fontTitle());
    g.setTextDatum(lgfx::textdatum_t::middle_center);
    g.setTextColor(theme::TEXT);
    g.drawString(f.big, r.cx(), r.cy() - 8);
    g.setFont(theme::fontSmall());
    g.setTextColor(theme::TEXT_DIM);
    g.drawString(f.small, r.cx(), r.y + r.h - 12);
  }
}

void drawMenuPage() {
  auto& g = gfx();
  char buf[32];
  snprintf(buf, sizeof(buf), "%s - NEW GAME", menu().name);
  g.setFont(theme::fontLabel());
  g.setTextColor(theme::TEXT_DIM);
  g.setTextDatum(lgfx::textdatum_t::middle_left);
  g.drawString(buf, 16, CAPTION_Y);
  g.setFont(theme::fontSmall());
  g.setTextDatum(lgfx::textdatum_t::middle_center);
  g.drawString(menu().hint, 160, HINT_Y);
  for (int8_t i = 0; i < itemCount(); ++i) drawMenuItem(i);
}

// ---------------------------------------------------------------- module functions
void onEnter() {
  s_ask.close();
  s_pressed = -1;
  s_focus = ITEM_CONTINUE;
}

void handleInput(const InputEvent& e) {
  if (s_ask.isOpen()) {
    if (s_ask.handleInput(e) == ConfirmResult::Confirm) startGame(s_askFormat);
    return;
  }
  switch (e.type) {
    case InputType::EncoderTurn: {
      const int n = itemCount();
      int next = (s_focus + e.delta) % n;
      if (next < 0) next += n;
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
    for (int8_t i = 0; i < itemCount(); ++i) {
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

const ScreenModule RiftboundSetupScreen = {"RiftboundSetup", onEnter, handleInput, render, nullptr};
const ScreenModule LorcanaSetupScreen   = {"LorcanaSetup", onEnter, handleInput, render, nullptr};
