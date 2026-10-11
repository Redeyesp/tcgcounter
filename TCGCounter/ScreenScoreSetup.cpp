/* ============================================================================
 *  ScreenScoreSetup — the Riftbound and Lorcana menus: continue the running
 *  game, roll for who goes first, or start a new game in another format.
 *  One module serves both; which game follows g_state.screen.
 *
 *  ┌──────────────────────────────────────────┐
 *  │ [< HOME]                  [H HIGH ROLL] │  HIGH ROLL: back to the table, rolling
 *  │ ┌─────────────────────────┐ ┌──────────┐ │
 *  │ │ CONTINUE              > │ │    +1    │ │  CONTINUE: back to the running game
 *  │ │ 4P TO 8                 │ │PLUS LIFE │ │  +1: Riftbound only, the plus life page
 *  │ └─────────────────────────┘ └──────────┘ │
 *  │ RIFTBOUND - NEW GAME                     │
 *  │ [ 1v1  ] [  4P   ] [ 2v2  ]              │  Riftbound: 1v1 to 8, 4 players
 *  │ [ TO 8 ] [FFA TO 8] [TO 11]              │  free-for-all to 8, 2v2 teams to 11
 *  └──────────────────────────────────────────┘
 *  Lorcana: [2P TO 20] [2P TO 25] [4P TO 20] [4P TO 25] (lore), CONTINUE full width.
 *
 *  PLUS LIFE page (Riftbound): one button per player (team) that gives or
 *  takes back their +1 (gold = taken) — kept off the table so it is never
 *  hit by accident; DONE goes back to the game, < MENU to this menu.
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
  bool          plusLife;  // Riftbound: the +1 button and its page
};
ScoreGame& riftbound() { return g_state.riftbound; }
ScoreGame& lorcana()   { return g_state.lorcana; }
const GameMenu MENUS[] = {
  {SCREEN_RIFTBOUND, "RIFTBOUND", riftbound, RIFTBOUND_FORMATS, 3, "+1 = plus life: one extra point each", "", true},
  {SCREEN_LORCANA,   "LORCANA",   lorcana,   LORCANA_FORMATS,   4, "First to 20 or 25 lore wins",          " lore", false},
};
const GameMenu& menu() { return g_state.screen == SCREEN_LORCANA_SETUP ? MENUS[1] : MENUS[0]; }

// ---------------------------------------------------------------- items
// Menu page: < HOME, HIGH ROLL, CONTINUE, (+1 PLUS LIFE), the formats.
constexpr int8_t ITEM_BACK = 0, ITEM_ROLL = 1, ITEM_CONTINUE = 2, ITEM_PLUS = 3;
int8_t firstFormat() { return menu().plusLife ? 4 : 3; }
int8_t itemCount() { return (int8_t)(firstFormat() + menu().formatCount); }

constexpr Rect BACK     = {12, 6, 88, 34};
constexpr Rect ROLL_BTN = {172, 6, 136, 34};
constexpr Rect CONTINUE = {12, 48, 296, 64};
constexpr Rect CONTINUE_SHORT = {12, 48, 200, 64};  // beside the +1 button
constexpr Rect PLUS_BTN = {220, 48, 88, 64};
constexpr int  FORMAT_X0 = 12, FORMAT_Y = 146, FORMAT_H = 64, FORMAT_GAP = 8, FORMAT_SPAN = 296;
constexpr int  CAPTION_Y = 132, HINT_Y = 226;

Rect itemRect(int8_t item) {
  if (item == ITEM_BACK) return BACK;
  if (item == ITEM_ROLL) return ROLL_BTN;
  if (item == ITEM_CONTINUE) return menu().plusLife ? CONTINUE_SHORT : CONTINUE;
  if (item == ITEM_PLUS && menu().plusLife) return PLUS_BTN;
  const int n = menu().formatCount, k = item - firstFormat();
  const int w = (FORMAT_SPAN - (n - 1) * FORMAT_GAP) / n;
  return Rect{(int16_t)(FORMAT_X0 + k * (w + FORMAT_GAP)), (int16_t)FORMAT_Y, (int16_t)w, (int16_t)FORMAT_H};
}

// "4P TO 8", "2v2 TO 11", "2P TO 25"
void formatText(const ScoreGame& g, char* buf, size_t n) {
  if (g.teams) snprintf(buf, n, "2v2 TO %u", (unsigned)g.target);
  else         snprintf(buf, n, "%uP TO %u", (unsigned)g.players, (unsigned)g.target);
}

// Plus life page: < MENU, one button per player (team), DONE.
constexpr int8_t PL_BACK = 0, PL_FIRST = 1;
constexpr Rect   PL_BACK_BTN = {12, 6, 88, 34};
constexpr Rect   PL_DONE_BTN = {12, 198, 296, 38};
uint8_t plusCount() { return menu().game().players; }
int8_t  plusDone() { return (int8_t)(PL_FIRST + plusCount()); }
int8_t  plusItemCount() { return (int8_t)(plusDone() + 1); }

Rect plusRect(int8_t item) {
  if (item == PL_BACK) return PL_BACK_BTN;
  if (item == plusDone()) return PL_DONE_BTN;
  const int k = item - PL_FIRST, col = k % 2, row = k / 2;
  if (plusCount() <= 2) return Rect{(int16_t)(12 + col * 152), 98, 144, 64};
  return Rect{(int16_t)(12 + col * 152), (int16_t)(88 + row * 54), 144, 48};
}

// ---------------------------------------------------------------- UI-only state
ConfirmDialog s_ask;            // "NEW GAME?"
int8_t  s_askFormat = 0;        // format of the new game being asked about
bool    s_plusPage = false;     // the plus life page is open (Riftbound)
int8_t  s_focus = ITEM_CONTINUE;
int8_t  s_pressed = -1;         // item under the finger
int8_t  s_redraw = -1;          // an item whose look changed (a +1 given or taken back)
bool    s_drawnAsk = false, s_drawnPlusPage = false;
int8_t  s_drawnFocus = -1, s_drawnPressed = -1;

int8_t pageItemCount() { return s_plusPage ? plusItemCount() : itemCount(); }
Rect   pageRect(int8_t i) { return s_plusPage ? plusRect(i) : itemRect(i); }

int8_t hitTest(int x, int y) {
  for (int8_t i = 0; i < pageItemCount(); ++i)
    if (pageRect(i).contains(x, y)) return i;
  return -1;
}

// ---------------------------------------------------------------- actions
void startGame(int8_t k) {
  const Format& f = menu().formats[k];
  scoreNewGame(menu().game(), f.players, f.target, f.teams);
  goToScreen(menu().table);
}

void openPlusPage(bool open) {
  s_plusPage = open;
  s_pressed = -1;
  s_focus = open ? PL_FIRST : ITEM_PLUS;
}

void choosePlus(int8_t item) {
  if (item == PL_BACK) {
    openPlusPage(false);
  } else if (item == plusDone()) {
    goToScreen(menu().table);
  } else if (item >= PL_FIRST && item < plusDone()) {
    scoreToggleBonus(menu().game(), (uint8_t)(item - PL_FIRST));
    s_redraw = item;
  }
}

void choose(int8_t item) {
  if (s_plusPage) { choosePlus(item); return; }
  if (item == ITEM_PLUS && menu().plusLife) {
    openPlusPage(true);
  } else if (item == ITEM_BACK) {
    goToScreen(SCREEN_HOME);
  } else if (item == ITEM_ROLL) {
    scoreRequestHighRoll();  // the table starts rolling as it opens
    goToScreen(menu().table);
  } else if (item == ITEM_CONTINUE) {
    goToScreen(menu().table);
  } else if (item >= firstFormat() && item < itemCount()) {
    const int8_t k = (int8_t)(item - firstFormat());
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
    char buf[16];
    formatText(menu().game(), buf, sizeof(buf));
    g.setFont(theme::fontButton());
    g.setTextDatum(lgfx::textdatum_t::middle_left);
    g.setTextColor(theme::TEXT);
    if (menu().plusLife) {  // narrower: the format under CONTINUE
      g.drawString("CONTINUE", r.x + 16, r.cy() - 9);
      g.setFont(theme::fontSmall());
      g.setTextColor(theme::TEXT_DIM);
      g.drawString(buf, r.x + 16, r.cy() + 15);
    } else {
      g.drawString("CONTINUE", r.x + 18, r.cy() + 1);
      g.setFont(theme::fontLabel());
      g.setTextDatum(lgfx::textdatum_t::middle_right);
      g.setTextColor(theme::TEXT_DIM);
      g.drawString(buf, r.x + r.w - 40, r.cy() + 1);
    }
    uiChevron(g, r.x + r.w - 20, r.cy(), 18, 4, true, theme::TEXT_DIM);
  } else if (item == ITEM_PLUS && menu().plusLife) {
    bool any = false;
    for (uint8_t p = 0; p < menu().game().players; ++p) any = any || menu().game().bonus[p];
    g.setFont(theme::fontTitle());
    g.setTextDatum(lgfx::textdatum_t::middle_center);
    g.setTextColor(any ? theme::ACCENT : theme::TEXT);
    g.drawString("+1", r.cx(), r.cy() - 8);
    g.setFont(theme::fontSmall());
    g.setTextColor(theme::TEXT_DIM);
    g.drawString("PLUS LIFE", r.cx(), r.y + r.h - 12);
  } else {
    const Format& f = menu().formats[item - firstFormat()];
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

// ---------------------------------------------------------------- plus life page
void drawPlusItem(int8_t item) {
  auto& g = gfx();
  const Rect r = plusRect(item);
  const bool pressed = item == s_pressed, focus = item == s_focus;
  const ScoreGame& sg = menu().game();
  g.fillRect(r.x, r.y, r.w, r.h, theme::BG);
  if (item == PL_BACK) {
    g.fillRoundRect(r.x, r.y, r.w, r.h, 10, pressed ? theme::BUTTON_DOWN : theme::BUTTON);
    if (focus) uiRoundFrame(g, r.x, r.y, r.w, r.h, 10, 3, theme::ACCENT);
    uiChevron(g, r.x + 18, r.cy(), 14, 3, false, theme::TEXT);
    g.setFont(theme::fontLabel());
    g.setTextDatum(lgfx::textdatum_t::middle_left);
    g.setTextColor(theme::TEXT);
    g.drawString("MENU", r.x + 32, r.cy() + 1);
    return;
  }
  if (item == plusDone()) {
    uiTextButton(g, r, "DONE", theme::fontButton(), pressed ? theme::BUTTON_DOWN : theme::BUTTON, theme::TEXT, 12);
    if (focus) uiRoundFrame(g, r.x, r.y, r.w, r.h, 12, 3, theme::ACCENT);
    return;
  }
  const uint8_t p = (uint8_t)(item - PL_FIRST);
  const bool taken = sg.bonus[p] != 0;
  g.fillRoundRect(r.x, r.y, r.w, r.h, 12, pressed ? theme::BUTTON_DOWN : (taken ? theme::WIN_PANEL : theme::BUTTON));
  if (focus) uiRoundFrame(g, r.x, r.y, r.w, r.h, 12, 3, theme::ACCENT);
  char name[12];
  snprintf(name, sizeof(name), sg.teams ? "TEAM %u" : "PLAYER %u", (unsigned)(p + 1));
  const int top = r.y + (r.h - 40) / 2;  // the name, and the +1 under it
  g.setFont(theme::fontLabel());
  g.setTextDatum(lgfx::textdatum_t::middle_center);
  g.setTextColor(theme::PLAYER[p]);
  g.drawString(name, r.cx(), top + 8);
  const Rect pill = {(int16_t)(r.cx() - 21), (int16_t)(top + 19), 42, 21};  // gold once taken
  if (taken) g.fillRoundRect(pill.x, pill.y, pill.w, pill.h, 10, theme::ACCENT);
  else       g.drawRoundRect(pill.x, pill.y, pill.w, pill.h, 10, theme::PANEL_EDGE);
  const uint16_t ink = taken ? theme::TEXT_ON_ACCENT : theme::TEXT_DIM;
  uiPlus(g, pill.cx() - 6, pill.cy(), 9, 3, ink);
  g.setTextDatum(lgfx::textdatum_t::middle_center);
  g.setTextColor(ink);
  g.drawString("1", pill.cx() + 6, pill.cy() + 1);
}

void drawPlusPage() {
  auto& g = gfx();
  g.setFont(theme::fontLabel());
  g.setTextColor(theme::TEXT_DIM);
  g.setTextDatum(lgfx::textdatum_t::middle_right);
  g.drawString("PLUS LIFE", 308, 23);
  uiWrappedText(g, 16, 50, 288, 17, "One extra point each: a score may end one past the target.",
                theme::fontSmall(), theme::TEXT_DIM);
  for (int8_t i = 0; i < plusItemCount(); ++i) drawPlusItem(i);
}

void drawItem(int8_t i) {
  if (s_plusPage) drawPlusItem(i);
  else drawMenuItem(i);
}

// ---------------------------------------------------------------- module functions
void onEnter() {
  s_ask.close();
  s_plusPage = false;
  s_pressed = -1;
  s_redraw = -1;
  s_focus = ITEM_CONTINUE;
}

void handleInput(const InputEvent& e) {
  if (s_ask.isOpen()) {
    if (s_ask.handleInput(e) == ConfirmResult::Confirm) startGame(s_askFormat);
    return;
  }
  switch (e.type) {
    case InputType::EncoderTurn: {
      const int n = pageItemCount();
      int next = (s_focus + e.delta) % n;
      if (next < 0) next += n;
      s_focus = (int8_t)next;
      break;
    }
    case InputType::EncoderClick:
      choose(s_focus);
      break;
    case InputType::EncoderLongPress:
      if (s_plusPage) openPlusPage(false);
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
  const bool plusChanged = s_plusPage != s_drawnPlusPage;
  s_drawnPlusPage = s_plusPage;
  if (full || pageChanged || plusChanged) {  // (back on) a page: repaint everything
    g.startWrite();
    g.fillScreen(theme::BG);
    if (s_plusPage) drawPlusPage();
    else drawMenuPage();
    g.endWrite();
    s_redraw = -1;
  } else {
    if (s_focus == s_drawnFocus && s_pressed == s_drawnPressed && s_redraw < 0) return;
    g.startWrite();
    for (int8_t i = 0; i < pageItemCount(); ++i) {
      const bool was = (i == s_drawnFocus || i == s_drawnPressed);
      const bool is  = (i == s_focus || i == s_pressed || i == s_redraw);
      if (was || is) drawItem(i);
    }
    g.endWrite();
    s_redraw = -1;
  }
  s_drawnFocus = s_focus;
  s_drawnPressed = s_pressed;
}

}  // namespace

const ScreenModule RiftboundSetupScreen = {"RiftboundSetup", onEnter, handleInput, render, nullptr};
const ScreenModule LorcanaSetupScreen   = {"LorcanaSetup", onEnter, handleInput, render, nullptr};
