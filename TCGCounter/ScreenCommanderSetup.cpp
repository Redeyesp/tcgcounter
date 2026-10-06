/* ============================================================================
 *  ScreenCommanderSetup — the Commander menu: how many players, where they
 *  sit, continue the running game, or start a new one.
 *
 *  MENU                                        TABLE PICKER (3, 5, 6 players)
 *  ┌──────────────────────────────────────┐    ┌──────────────────────────────────────┐
 *  │ [< HOME]              [H HIGH ROLL] │    │ [< BACK]   6 PLAYERS - TABLE         │
 *  │ ┌──────────────────────────┐ ┌────┐ │    │ ┌────────────────┐ ┌────────────────┐ │
 *  │ │ CONTINUE              4P>│ │ ▦  │ │    │ │  ▦ 3 + 3       │ │  ▦ ENDS        │ │
 *  │ └──────────────────────────┘ └TABLE┘│    │ │     IN USE     │ │                │ │
 *  │ NEW GAME - PLAYERS                  │    │ └────────────────┘ └────────────────┘ │
 *  │ [ 2 ] [ 3 ] [ 4 ] [ 5 ] [ 6 ]       │    │          Starts a new game           │
 *  │      Everyone starts at 40 life     │    └──────────────────────────────────────┘
 *  └──────────────────────────────────────┘
 *
 *  2 3 4 5 6 : a new game with that many players. Counts with more than one
 *              table layout (3: P3 left / middle / right, 5: head of the
 *              table right / left, 6: 3 + 3 / ends) open the TABLE PICKER first.
 *  TABLE     : another layout for the running game — nothing is reset.
 *  A new game wipes the running one, so it asks first (CANCEL / START) —
 *  unless nothing has happened yet in the running game.
 *  HIGH ROLL : back to the table, rolling for who goes first. Dice: the 🎲
 *  round button on the table.
 *
 *  Reached from Home -> COMMANDER and from the ≡ button in the game.
 *  Touch:   tap a button.
 *  Encoder: turn = move the yellow focus frame, press = choose,
 *           long-press = back (picker -> menu, question -> CANCEL).
 * ==========================================================================*/
#include "Screens.h"
#include "App.h"
#include "GameState.h"
#include "CommanderLayout.h"
#include "Theme.h"
#include "Ui.h"
#include "UiConfirm.h"
#include "Config.h"
#include <stdio.h>

namespace {

// ---------------------------------------------------------------- menu items
constexpr int8_t ITEM_BACK = 0, ITEM_ROLL = 1, ITEM_CONTINUE = 2, ITEM_TABLE = 3, ITEM_FIRST_COUNT = 4;
constexpr int8_t COUNT_BUTTONS = COMMANDER_MAX_PLAYERS - COMMANDER_MIN_PLAYERS + 1;  // 2..6
constexpr int8_t ITEM_COUNT = ITEM_FIRST_COUNT + COUNT_BUTTONS;

constexpr Rect BACK      = {12, 6, 88, 34};
constexpr Rect ROLL_BTN  = {172, 6, 136, 34};
constexpr Rect CONTINUE  = {12, 48, 214, 64};
constexpr Rect TABLE_BTN = {234, 48, 74, 64};
constexpr int  COUNT_X0 = 13, COUNT_Y = 146, COUNT_W = 54, COUNT_H = 64, COUNT_PITCH = 60;
constexpr int  CAPTION_Y = 132, HINT_Y = 226;

uint8_t playersFor(int8_t item) { return (uint8_t)(COMMANDER_MIN_PLAYERS + item - ITEM_FIRST_COUNT); }

Rect itemRect(int8_t item) {
  if (item == ITEM_BACK) return BACK;
  if (item == ITEM_ROLL) return ROLL_BTN;
  if (item == ITEM_CONTINUE) return CONTINUE;
  if (item == ITEM_TABLE) return TABLE_BTN;
  const int k = item - ITEM_FIRST_COUNT;
  return Rect{(int16_t)(COUNT_X0 + k * COUNT_PITCH), (int16_t)COUNT_Y, (int16_t)COUNT_W, (int16_t)COUNT_H};
}

// TABLE only does something when the running game's player count has layouts to pick from
bool tableEnabled() { return commanderLayoutCount(g_state.commander.players) > 1; }
bool itemEnabled(int8_t item) { return item != ITEM_TABLE || tableEnabled(); }

// ---------------------------------------------------------------- picker items
constexpr int8_t PICK_BACK = 0, PICK_FIRST = 1;
constexpr Rect   PICK_BACK_RECT = {12, 6, 88, 34};
constexpr int    PICK_Y = 50, PICK_H = 152, PICK_SPAN = 296, PICK_GAP = 8, PICK_X0 = 12;
constexpr int    THUMB_DIV = 4;  // thumbnails: the table at 1/4 size (80 x 60)

// ---------------------------------------------------------------- UI-only state
ConfirmDialog s_ask;            // "NEW GAME?"
uint8_t s_askPlayers = 0;       // the new game being asked about
uint8_t s_askLayout = 0;
int8_t  s_focus = ITEM_CONTINUE;
int8_t  s_pressed = -1;         // item under the finger (menu or picker)
bool    s_picker = false;       // the table picker is open
bool    s_pickNew = true;       // picker: true = for a new game, false = TABLE (keep the game)
uint8_t s_pickPlayers = 0;      // picker: player count whose layouts are shown
int8_t  s_pickFocus = PICK_FIRST;
int8_t  s_drawnPage = -1;       // 0 menu, 1 picker, 2 question
int8_t  s_drawnFocus = -1, s_drawnPressed = -1;

int8_t pickCount() { return (int8_t)(PICK_FIRST + commanderLayoutCount(s_pickPlayers)); }

Rect pickRect(int8_t item) {
  if (item == PICK_BACK) return PICK_BACK_RECT;
  const int n = commanderLayoutCount(s_pickPlayers), k = item - PICK_FIRST;
  const int w = (PICK_SPAN - (n - 1) * PICK_GAP) / n;
  return Rect{(int16_t)(PICK_X0 + k * (w + PICK_GAP)), (int16_t)PICK_Y, (int16_t)w, (int16_t)PICK_H};
}

int8_t hitTest(int x, int y) {
  if (s_picker) {
    for (int8_t i = 0; i < pickCount(); ++i)
      if (pickRect(i).contains(x, y)) return i;
    return -1;
  }
  for (int8_t i = 0; i < ITEM_COUNT; ++i)
    if (itemEnabled(i) && itemRect(i).contains(x, y)) return i;
  return -1;
}

// ---------------------------------------------------------------- actions
void startGame(uint8_t players, uint8_t layout) {
  commanderNewGame(g_state.commander, players, layout);
  goToScreen(SCREEN_COMMANDER);
}

void askNewGame(uint8_t players, uint8_t layout) {
  if (commanderIsFresh(g_state.commander)) {  // nothing to lose: no need to ask
    startGame(players, layout);
    return;
  }
  char body[48];
  if (commanderLayoutCount(players) > 1)  // the table's name instead of the life (fits one line)
    snprintf(body, sizeof(body), "%u players, %s", (unsigned)players, tableLayout(players, layout).name);
  else
    snprintf(body, sizeof(body), "%u players, everyone at %d life", (unsigned)players,
             (int)COMMANDER_START_LIFE);
  s_askPlayers = players;
  s_askLayout = layout;
  s_ask.open("NEW GAME?", body, "The current game will be lost.", "START");
}

void openPicker(uint8_t players, bool forNewGame) {
  s_picker = true;
  s_pickNew = forNewGame;
  s_pickPlayers = players;
  s_pressed = -1;
  const CommanderGame& c = g_state.commander;
  s_pickFocus = (int8_t)(PICK_FIRST + (c.players == players ? c.layout : 0));
}

void closePicker() {
  s_picker = false;
  s_pressed = -1;
}

void choosePick(int8_t item) {
  if (item == PICK_BACK) { closePicker(); return; }
  const uint8_t layout = (uint8_t)(item - PICK_FIRST);
  if (s_pickNew) {
    askNewGame(s_pickPlayers, layout);  // the picker stays under the question
  } else {
    commanderSetLayout(g_state.commander, layout);  // same game, other seats
    closePicker();
    goToScreen(SCREEN_COMMANDER);
  }
}

void choose(int8_t item) {
  if (item == ITEM_BACK) {
    goToScreen(SCREEN_HOME);
  } else if (item == ITEM_ROLL) {
    commanderRequestHighRoll();  // the table starts rolling as it opens
    goToScreen(SCREEN_COMMANDER);
  } else if (item == ITEM_CONTINUE) {
    goToScreen(SCREEN_COMMANDER);
  } else if (item == ITEM_TABLE) {
    if (tableEnabled()) openPicker(g_state.commander.players, false);
  } else if (item >= ITEM_FIRST_COUNT && item < ITEM_COUNT) {
    const uint8_t n = playersFor(item);
    if (commanderLayoutCount(n) > 1) openPicker(n, true);  // where do they sit?
    else askNewGame(n, 0);
  }
}

// ---------------------------------------------------------------- drawing
// The table at 1/div size: every seat in its player's colour with a light
// bar on the edge that player sits at (and its number when `numbers`).
void drawThumb(lgfx::LovyanGFX& g, const TableLayout& L, int x, int y, int div, bool numbers, bool dim) {
  const int w = 320 / div, h = 240 / div;
  g.fillRect(x, y, w, h, theme::BG);
  g.drawRect(x - 1, y - 1, w + 2, h + 2, theme::PANEL_EDGE);
  for (uint8_t i = 0; i < L.players; ++i) {
    const Seat& s = L.seats[i];
    const int sx = x + (s.r.x + div / 2) / div, sy = y + (s.r.y + div / 2) / div;
    const int sw = (s.r.x + s.r.w) / div - (s.r.x + div / 2) / div;
    const int sh = (s.r.y + s.r.h) / div - (s.r.y + div / 2) / div;
    const uint16_t col = dim ? theme::PANEL_EDGE : theme::PLAYER[i];
    g.fillRect(sx, sy, sw, sh, col);
    const uint16_t bar = dim ? theme::TEXT_DIM : theme::TEXT;
    switch (s.side) {  // where the player sits
      case Side::Bottom: g.fillRect(sx, sy + sh - 2, sw, 2, bar); break;
      case Side::Top:    g.fillRect(sx, sy, sw, 2, bar); break;
      case Side::Left:   g.fillRect(sx, sy, 2, sh, bar); break;
      case Side::Right:  g.fillRect(sx + sw - 2, sy, 2, sh, bar); break;
    }
    if (numbers) {
      char buf[2] = {(char)('1' + i), 0};
      g.setFont(theme::fontLabel());
      g.setTextDatum(lgfx::textdatum_t::middle_center);
      g.setTextColor(theme::TEXT_ON_ACCENT);
      g.drawString(buf, sx + sw / 2, sy + sh / 2 + 1);
    }
  }
}

void drawButtonBase(const Rect& r, int radius, bool pressed, bool focused) {
  auto& g = gfx();
  g.fillRect(r.x, r.y, r.w, r.h, theme::BG);
  g.fillRoundRect(r.x, r.y, r.w, r.h, radius, pressed ? theme::BUTTON_DOWN : theme::BUTTON);
  if (focused) uiRoundFrame(g, r.x, r.y, r.w, r.h, radius, 3, theme::ACCENT);
}

void drawBackButton(const Rect& r, const char* label) {
  auto& g = gfx();
  uiChevron(g, r.x + 18, r.cy(), 14, 3, false, theme::TEXT);
  g.setFont(theme::fontLabel());
  g.setTextDatum(lgfx::textdatum_t::middle_left);
  g.setTextColor(theme::TEXT);
  g.drawString(label, r.x + 32, r.cy() + 1);
}

void drawMenuItem(int8_t item) {
  auto& g = gfx();
  const Rect r = itemRect(item);
  const bool pressed = (item == s_pressed);
  const uint16_t fill = pressed ? theme::BUTTON_DOWN : theme::BUTTON;
  const int radius = (item == ITEM_BACK || item == ITEM_ROLL) ? 10 : 12;
  drawButtonBase(r, radius, pressed, item == s_focus);

  if (item == ITEM_BACK) {
    drawBackButton(r, "HOME");
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
    char buf[8];
    snprintf(buf, sizeof(buf), "%uP", (unsigned)g_state.commander.players);
    g.setFont(theme::fontLabel());
    g.setTextDatum(lgfx::textdatum_t::middle_right);
    g.setTextColor(theme::TEXT_DIM);
    g.drawString(buf, r.x + r.w - 38, r.cy() + 1);
    uiChevron(g, r.x + r.w - 20, r.cy(), 18, 4, true, theme::TEXT_DIM);
  } else if (item == ITEM_TABLE) {
    // the running game's table, small; dimmed when there is nothing to pick
    const bool on = tableEnabled();
    const CommanderGame& c = g_state.commander;
    drawThumb(g, tableLayout(c.players, c.layout), r.cx() - 26, r.y + 5, 6, false, !on);
    g.setFont(theme::fontSmall());
    g.setTextDatum(lgfx::textdatum_t::middle_center);
    g.setTextColor(on ? theme::TEXT : theme::TEXT_DIM);
    g.drawString("TABLE", r.cx(), r.y + r.h - 9);
  } else {
    char buf[4];
    snprintf(buf, sizeof(buf), "%u", (unsigned)playersFor(item));
    g.setFont(theme::fontHuge());
    g.setTextDatum(lgfx::textdatum_t::middle_center);
    g.setTextColor(theme::TEXT);
    g.drawString(buf, r.cx(), r.cy() + 2);
  }
}

void drawPickItem(int8_t item) {
  auto& g = gfx();
  const Rect r = pickRect(item);
  drawButtonBase(r, item == PICK_BACK ? 10 : 12, item == s_pressed, item == s_pickFocus);
  if (item == PICK_BACK) { drawBackButton(r, "BACK"); return; }
  const uint8_t layout = (uint8_t)(item - PICK_FIRST);
  const TableLayout& L = tableLayout(s_pickPlayers, layout);
  const int tw = 320 / THUMB_DIV;
  drawThumb(g, L, r.cx() - tw / 2, r.y + 14, THUMB_DIV, true, false);
  g.setFont(theme::fontLabel());
  g.setTextDatum(lgfx::textdatum_t::middle_center);
  g.setTextColor(theme::TEXT);
  g.drawString(L.name, r.cx(), r.y + 100);
  const CommanderGame& c = g_state.commander;
  if (c.players == s_pickPlayers && c.layout == layout) {
    g.setFont(theme::fontSmall());
    g.setTextColor(theme::ACCENT);
    g.drawString("IN USE", r.cx(), r.y + 126);
  }
}

void drawMenuPage() {
  auto& g = gfx();
  g.fillScreen(theme::BG);
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

void drawPickerPage() {
  auto& g = gfx();
  g.fillScreen(theme::BG);
  char buf[32];
  snprintf(buf, sizeof(buf), "%u PLAYERS - TABLE", (unsigned)s_pickPlayers);
  g.setFont(theme::fontLabel());
  g.setTextColor(theme::TEXT_DIM);
  g.setTextDatum(lgfx::textdatum_t::middle_left);
  g.drawString(buf, 112, PICK_BACK_RECT.cy() + 1);
  g.setFont(theme::fontSmall());
  g.setTextDatum(lgfx::textdatum_t::middle_center);
  g.drawString(s_pickNew ? "Starts a new game" : "Same game, everyone keeps their life", 160, HINT_Y);
  for (int8_t i = 0; i < pickCount(); ++i) drawPickItem(i);
}

// ---------------------------------------------------------------- module functions
void onEnter() {
  s_ask.close();
  s_picker = false;
  s_pressed = -1;
  s_focus = ITEM_CONTINUE;
}

void handleInput(const InputEvent& e) {
  if (s_ask.isOpen()) {
    if (s_ask.handleInput(e) == ConfirmResult::Confirm) {
      s_picker = false;
      startGame(s_askPlayers, s_askLayout);
    }
    return;
  }
  int8_t& focus = s_picker ? s_pickFocus : s_focus;
  switch (e.type) {
    case InputType::EncoderTurn: {
      const int8_t count = s_picker ? pickCount() : ITEM_COUNT;
      int steps = e.delta < 0 ? -e.delta : e.delta;
      while (steps-- > 0) {
        int f = focus;
        do {
          f = (f + (e.delta > 0 ? 1 : -1)) % count;
          if (f < 0) f += count;
        } while (!s_picker && !itemEnabled((int8_t)f));
        focus = (int8_t)f;
      }
      break;
    }
    case InputType::EncoderClick:
      if (s_picker) choosePick(s_pickFocus);
      else choose(s_focus);
      break;
    case InputType::EncoderLongPress:
      if (s_picker) closePicker();
      break;
    case InputType::TouchDown:
      s_pressed = hitTest(e.x, e.y);
      if (s_pressed >= 0) focus = s_pressed;
      break;
    case InputType::TouchUp: {
      const int8_t released = s_pressed;
      s_pressed = -1;
      if (released < 0 || !isTap(e, TOUCH_TAP_MAX_MS)) break;
      if (s_picker) choosePick(released);
      else choose(released);
      break;
    }
    default:
      break;
  }
}

void render(bool full) {
  auto& g = gfx();
  const int8_t page = s_ask.isOpen() ? 2 : s_picker ? 1 : 0;
  const bool pageChanged = page != s_drawnPage;
  s_drawnPage = page;
  if (page == 2) {
    s_ask.render(full || pageChanged);
    return;
  }
  const int8_t focus = page == 1 ? s_pickFocus : s_focus;
  if (full || pageChanged) {  // (back on) this page: repaint everything
    g.startWrite();
    if (page == 1) drawPickerPage();
    else drawMenuPage();
    g.endWrite();
  } else {
    if (focus == s_drawnFocus && s_pressed == s_drawnPressed) return;
    g.startWrite();
    const int8_t count = page == 1 ? pickCount() : ITEM_COUNT;
    for (int8_t i = 0; i < count; ++i) {
      const bool was = (i == s_drawnFocus || i == s_drawnPressed);
      const bool is  = (i == focus || i == s_pressed);
      if (!(was || is)) continue;
      if (page == 1) drawPickItem(i);
      else drawMenuItem(i);
    }
    g.endWrite();
  }
  s_drawnFocus = focus;
  s_drawnPressed = s_pressed;
}

}  // namespace

const ScreenModule CommanderSetupScreen = {"CommanderSetup", onEnter, handleInput, render, nullptr};
