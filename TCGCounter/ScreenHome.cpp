/* ============================================================================
 *  ScreenHome — main menu: COMMANDER / RIFTBOUND / LORCANA.
 *
 *  Touch:   tap an entry to open it.
 *  Encoder: turn moves the yellow focus frame, press opens the focused entry.
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
  Screen target;
  bool available;  // false = drawn dimmed (for a mode that is not ready yet)
};

// To add a new mode, add a Screen value, a ScreenModule and an entry here
// (then re-space ITEM_Y / ITEM_H so the list still fits 240 px).
const MenuItem ITEMS[] = {
  {"COMMANDER", SCREEN_COMMANDER_SETUP, true},  // menu first: players / continue / new game
  {"RIFTBOUND", SCREEN_RIFTBOUND,       true},  // 2 players, first to 8
  {"LORCANA",   SCREEN_LORCANA,         true},  // 2 players, first to 20 lore
};
constexpr int8_t ITEM_COUNT = sizeof(ITEMS) / sizeof(ITEMS[0]);

constexpr int ITEM_X = 16, ITEM_W = 288, ITEM_H = 56, ITEM_Y0 = 44, ITEM_PITCH = 64;

Rect itemRect(int8_t i) { return Rect{ITEM_X, (int16_t)(ITEM_Y0 + i * ITEM_PITCH), ITEM_W, ITEM_H}; }

int8_t hitTest(int x, int y) {
  for (int8_t i = 0; i < ITEM_COUNT; ++i)
    if (itemRect(i).contains(x, y)) return i;
  return -1;
}

// UI-only state (not saved)
int8_t s_focus = 0;     // encoder focus
int8_t s_pressed = -1;  // entry under the finger
int8_t s_drawnFocus = -1, s_drawnPressed = -1;

void drawItem(int8_t i) {
  auto& g = gfx();
  const Rect r = itemRect(i);
  const MenuItem& it = ITEMS[i];
  const bool pressed = (i == s_pressed);
  const uint16_t fill = pressed ? theme::BUTTON_DOWN : theme::BUTTON;

  g.fillRect(r.x, r.y, r.w, r.h, theme::BG);
  g.fillRoundRect(r.x, r.y, r.w, r.h, 12, fill);
  if (i == s_focus) uiRoundFrame(g, r.x, r.y, r.w, r.h, 12, 3, theme::ACCENT);

  g.setFont(theme::fontTitle());
  g.setTextDatum(lgfx::textdatum_t::middle_left);
  g.setTextColor(it.available ? theme::TEXT : theme::TEXT_DIM);
  g.drawString(it.label, r.x + 18, r.cy() + 1);
  if (it.available) uiChevron(g, r.x + r.w - 20, r.cy(), 18, 4, true, theme::TEXT_DIM);
}

void open(int8_t i) {
  if (i >= 0 && i < ITEM_COUNT) goToScreen(ITEMS[i].target);
}

void onEnter() { s_pressed = -1; }

void handleInput(const InputEvent& e) {
  switch (e.type) {
    case InputType::EncoderTurn: {
      int f = (s_focus + e.delta) % ITEM_COUNT;
      if (f < 0) f += ITEM_COUNT;
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
    g.drawString("TCG COUNTER", ITEM_X + 4, 22);
    g.setFont(theme::fontSmall());
    g.setTextDatum(lgfx::textdatum_t::middle_right);
    g.drawString("v" FW_VERSION, ITEM_X + ITEM_W - 4, 22);
    for (int8_t i = 0; i < ITEM_COUNT; ++i) drawItem(i);
    g.endWrite();
    s_drawnFocus = s_focus;
    s_drawnPressed = s_pressed;
    return;
  }
  if (s_focus == s_drawnFocus && s_pressed == s_drawnPressed) return;

  g.startWrite();
  for (int8_t i = 0; i < ITEM_COUNT; ++i) {
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
