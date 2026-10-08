/* ============================================================================
 *  ScreenHome — main menu: one button per game, two columns.
 *
 *  ┌──────────────────────────────────────────┐
 *  │ TCG COUNTER                   [↑ vX.Y.Z] │  version button: firmware update over Wi-Fi
 *  │ [ COMMANDER        ] [ STANDARD         ] │  each button: the game, and
 *  │ [ RIFTBOUND        ] [ LORCANA          ] │  a short line under it
 *  │ [ POKEMON          ] [ DIGIMON          ] │
 *  │ [ KINGDOMS         ] [ DICE             ] │
 *  └──────────────────────────────────────────┘
 *
 *  Touch:   tap a button to open it.
 *  Encoder: turn moves the yellow focus frame (left to right, then down,
 *           then the version button), press opens the focused button.
 * ==========================================================================*/
#include "Screens.h"
#include "App.h"
#include "GameState.h"
#include "Theme.h"
#include "Ui.h"
#include "Config.h"

namespace {

struct MenuItem {
  const char* label;
  const char* hint;    // short line under the name
  Screen      target;
  uint16_t    color;   // the stripe on the button's left edge
};

// To add a new mode, add a Screen value, a ScreenModule and an entry here.
// The buttons fill two columns; an odd one out at the end spans both.
const MenuItem ITEMS[] = {
  {"COMMANDER", "2-6 players",          SCREEN_COMMANDER_SETUP, theme::PLAYER[3]},
  {"STANDARD",  "1v1, 20 life",         SCREEN_STANDARD_SETUP,  theme::PLAYER[4]},
  {"RIFTBOUND", "to 8, 2v2 to 11",      SCREEN_RIFTBOUND_SETUP, theme::PLAYER[2]},
  {"LORCANA",   "20 or 25 lore",        SCREEN_LORCANA_SETUP,   theme::PLAYER[1]},
  {"POKEMON",   "damage, bench",        SCREEN_POKEMON_SETUP,   theme::PLAYER[5]},
  {"DIGIMON",   "memory gauge",         SCREEN_DIGIMON_SETUP,   theme::PLAYER[0]},
  {"KINGDOMS",  "hidden roles",         SCREEN_KINGDOMS_SETUP,  theme::ACCENT},
  {"DICE",      "D4 to D20",            SCREEN_DICE,            theme::TEXT_DIM},
};
constexpr int8_t ITEM_COUNT = sizeof(ITEMS) / sizeof(ITEMS[0]);

constexpr int GRID_X = 10, GRID_W = 300, GRID_Y0 = 36, COL_GAP = 8, ROW_H = 44, ROW_PITCH = 50;
constexpr int COL_W = (GRID_W - COL_GAP) / 2;

// The version button (top right) opens the firmware update; it comes after
// the games for the encoder.
constexpr int8_t UPDATE_SLOT = ITEM_COUNT, SLOT_COUNT = ITEM_COUNT + 1;
constexpr Rect   UPDATE_BTN = {206, 4, 104, 28};

Rect itemRect(int8_t i) {
  if (i == UPDATE_SLOT) return UPDATE_BTN;
  const int row = i / 2, col = i % 2;
  const bool alone = (ITEM_COUNT % 2 == 1) && i == ITEM_COUNT - 1;  // last odd one: full width
  return Rect{(int16_t)(GRID_X + (alone ? 0 : col * (COL_W + COL_GAP))), (int16_t)(GRID_Y0 + row * ROW_PITCH),
              (int16_t)(alone ? GRID_W : COL_W), ROW_H};
}

int8_t hitTest(int x, int y) {
  for (int8_t i = 0; i < SLOT_COUNT; ++i)
    if (itemRect(i).contains(x, y)) return i;
  return -1;
}

// UI-only state (not saved)
int8_t s_focus = 0;     // encoder focus
int8_t s_pressed = -1;  // entry under the finger
int8_t s_drawnFocus = -1, s_drawnPressed = -1;

void drawUpdateButton() {
  auto& g = gfx();
  const Rect& r = UPDATE_BTN;
  const uint16_t fill = s_pressed == UPDATE_SLOT ? theme::BUTTON_DOWN : theme::BUTTON;
  g.fillRect(r.x, r.y, r.w, r.h, theme::BG);
  g.fillRoundRect(r.x, r.y, r.w, r.h, 8, fill);
  if (s_focus == UPDATE_SLOT) uiRoundFrame(g, r.x, r.y, r.w, r.h, 8, 2, theme::ACCENT);
  const int ax = r.x + 15, ay = r.cy();  // an arrow up out of a tray: update
  uiThickLine(g, ax, ay + 4, ax, ay - 7, 2, theme::ACCENT);
  uiThickLine(g, ax - 5, ay - 2, ax, ay - 7, 2, theme::ACCENT);
  uiThickLine(g, ax + 5, ay - 2, ax, ay - 7, 2, theme::ACCENT);
  uiThickLine(g, ax - 7, ay + 8, ax + 7, ay + 8, 2, theme::ACCENT);
  g.setFont(theme::fontSmall());
  g.setTextDatum(lgfx::textdatum_t::middle_left);
  g.setTextColor(theme::TEXT);
  g.drawString("v" FW_VERSION, r.x + 28, r.cy() + 1);
}

void drawItem(int8_t i) {
  if (i == UPDATE_SLOT) { drawUpdateButton(); return; }
  auto& g = gfx();
  const Rect r = itemRect(i);
  const MenuItem& it = ITEMS[i];
  const bool pressed = (i == s_pressed);
  const uint16_t fill = pressed ? theme::BUTTON_DOWN : theme::BUTTON;

  g.fillRect(r.x, r.y, r.w, r.h, theme::BG);
  g.fillRoundRect(r.x, r.y, r.w, r.h, 10, fill);
  g.fillRoundRect(r.x + 6, r.y + 8, 5, r.h - 16, 2, it.color);  // the game's stripe
  if (i == s_focus) uiRoundFrame(g, r.x, r.y, r.w, r.h, 10, 3, theme::ACCENT);

  const bool wide = r.w > COL_W;
  const int tx = wide ? r.cx() : r.x + 19;
  const lgfx::textdatum_t top = wide ? lgfx::textdatum_t::middle_center : lgfx::textdatum_t::middle_left;
  g.setTextDatum(top);
  g.setFont(theme::fontLabel());
  g.setTextColor(theme::TEXT);
  g.drawString(it.label, tx, r.y + 15);
  g.setFont(theme::fontSmall());
  g.setTextColor(theme::TEXT_DIM);
  g.drawString(it.hint, tx, r.y + 31);
}

void open(int8_t i) {
  if (i == UPDATE_SLOT) goToScreen(SCREEN_UPDATE);
  else if (i >= 0 && i < ITEM_COUNT) goToScreen(ITEMS[i].target);
}

void onEnter() { s_pressed = -1; }

void handleInput(const InputEvent& e) {
  switch (e.type) {
    case InputType::EncoderTurn: {
      int f = (s_focus + e.delta) % SLOT_COUNT;
      if (f < 0) f += SLOT_COUNT;
      s_focus = (int8_t)f;
      break;
    }
    case InputType::EncoderClick:
      open(s_focus);
      break;
    case InputType::TouchDown:
      s_pressed = hitTest(e.x, e.y);
      if (s_pressed >= 0) s_focus = s_pressed;
      break;
    case InputType::TouchUp: {
      const int8_t released = s_pressed;
      s_pressed = -1;
      if (released >= 0 && isTap(e, TOUCH_TAP_MAX_MS)) open(released);
      break;
    }
    default:
      break;
  }
}

void render(bool full) {
  auto& g = gfx();
  if (full) {
    g.startWrite();
    g.fillScreen(theme::BG);
    g.setFont(theme::fontButton());
    g.setTextDatum(lgfx::textdatum_t::middle_left);
    g.setTextColor(theme::TEXT_DIM);
    g.drawString("TCG COUNTER", GRID_X + 4, 18);
    for (int8_t i = 0; i < SLOT_COUNT; ++i) drawItem(i);
    g.endWrite();
    s_drawnFocus = s_focus;
    s_drawnPressed = s_pressed;
    return;
  }
  if (s_focus == s_drawnFocus && s_pressed == s_drawnPressed) return;

  g.startWrite();
  for (int8_t i = 0; i < SLOT_COUNT; ++i) {
    const bool was = (i == s_drawnFocus || i == s_drawnPressed);
    const bool is  = (i == s_focus || i == s_pressed);
    if (was || is) drawItem(i);
  }
  g.endWrite();
  s_drawnFocus = s_focus;
  s_drawnPressed = s_pressed;
}

}  // namespace

const ScreenModule HomeScreen = {"Home", onEnter, handleInput, render, nullptr};
