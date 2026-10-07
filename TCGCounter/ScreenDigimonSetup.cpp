/* ============================================================================
 *  ScreenDigimonSetup — the Digimon menu, drawn for the standing device
 *  (portrait), facing player 2 at the bottom edge.
 *
 *  ┌──────────────────────┐            NEW GAME (who goes first?)
 *  │ DIGIMON              │            ┌──────────────────────┐
 *  │ [ CONTINUE        > ]│            │ NEW GAME?            │
 *  │ [ NEW GAME        > ]│            │ Who goes first?      │
 *  │ MEMORY LOCK AT (ON)  │            │ [ TOP PLAYER       ] │
 *  │ [ - ]     3    [ + ] │            │ [ BOTTOM PLAYER    ] │
 *  │ [ HOME            > ]│            │ [ CANCEL           ] │
 *  └──────────────────────┘            └──────────────────────┘
 *
 *  The lock itself is switched on and off with the round lock button on the
 *  table; here you pick the memory a new turn starts with while it is on.
 *  Touch: tap a button. Encoder: turn = move the yellow focus, press = choose,
 *  long-press in the question = CANCEL.
 * ==========================================================================*/
#include "Screens.h"
#include "App.h"
#include "GameState.h"
#include "CommanderLayout.h"
#include "TableDraw.h"
#include "Theme.h"
#include "Ui.h"
#include "Config.h"
#include <stdio.h>

namespace {

// The whole panel as one card facing its right edge: the bottom of the
// standing device. 240 x 320 as player 2 sees it.
const Seat PAGE = {{0, 0, 320, 240}, Side::Right};
constexpr int PAGE_W = 240, PAGE_H = 320;

constexpr int8_t ITEM_CONTINUE = 0, ITEM_NEW = 1, ITEM_LOCK_DOWN = 2, ITEM_LOCK_UP = 3, ITEM_HOME = 4,
                 ITEM_COUNT = 5;
constexpr Rect ITEMS[ITEM_COUNT] = {
  {12, 48, 216, 52},   // CONTINUE
  {12, 108, 216, 52},  // NEW GAME
  {12, 192, 64, 52},   // lock value -
  {164, 192, 64, 52},  // lock value +
  {12, 260, 216, 52},  // HOME
};
constexpr int LOCK_LABEL_Y = 178, LOCK_VALUE_Y = 218;

// NEW GAME? who goes first
constexpr int8_t ASK_TOP = 0, ASK_BOTTOM = 1, ASK_CANCEL = 2, ASK_COUNT = 3;
constexpr Rect ASK_BTN[ASK_COUNT] = {{12, 136, 216, 52}, {12, 196, 216, 52}, {12, 256, 216, 52}};

// ---------------------------------------------------------------- UI-only state
bool   s_ask = false;
int8_t s_focus = ITEM_CONTINUE, s_askFocus = ASK_CANCEL;
int8_t s_pressed = -1;
bool   s_dirty = true;

int8_t hitTest(int x, int y) {
  int lx, ly;
  seatToLocal(PAGE, x, y, lx, ly);
  const Rect* rects = s_ask ? ASK_BTN : ITEMS;
  const int8_t n = s_ask ? ASK_COUNT : ITEM_COUNT;
  for (int8_t i = 0; i < n; ++i)
    if (rects[i].contains(lx, ly)) return i;
  return -1;
}

// ---------------------------------------------------------------- actions
DigimonGame& game() { return g_state.digimon; }

void choose(int8_t i) {
  switch (i) {
    case ITEM_CONTINUE:  goToScreen(SCREEN_DIGIMON); break;
    case ITEM_NEW:       s_ask = true; s_askFocus = ASK_CANCEL; break;  // who goes first?
    case ITEM_LOCK_DOWN: digimonSetLockAt(game(), game().lockAt - 1); break;
    case ITEM_LOCK_UP:   digimonSetLockAt(game(), game().lockAt + 1); break;
    case ITEM_HOME:      goToScreen(SCREEN_HOME); break;
    default: break;
  }
}

void answer(int8_t i) {
  s_ask = false;
  if (i == ASK_TOP || i == ASK_BOTTOM) {
    digimonNewGame(game(), i == ASK_TOP ? 0 : 1);  // the top player is player 1
    goToScreen(SCREEN_DIGIMON);
  }
}

// ---------------------------------------------------------------- drawing
void button(lgfx::LovyanGFX& c, int ox, int oy, const Rect& r, bool pressed, bool focus) {
  c.fillRoundRect(ox + r.x, oy + r.y, r.w, r.h, 12, pressed ? theme::BUTTON_DOWN : theme::BUTTON);
  if (focus) uiRoundFrame(c, ox + r.x, oy + r.y, r.w, r.h, 12, 3, theme::ACCENT);
}

void text(lgfx::LovyanGFX& c, int x, int y, const char* t, const lgfx::IFont* f, uint16_t col,
          lgfx::textdatum_t datum) {
  c.setFont(f);
  c.setTextDatum(datum);
  c.setTextColor(col);
  c.drawString(t, x, y);
}

void wideButton(lgfx::LovyanGFX& c, int ox, int oy, const Rect& r, const char* t, bool pressed, bool focus) {
  button(c, ox, oy, r, pressed, focus);
  text(c, ox + r.x + 18, oy + r.cy() + 1, t, theme::fontButton(), theme::TEXT, lgfx::textdatum_t::middle_left);
  uiChevron(c, ox + r.x + r.w - 20, oy + r.cy(), 16, 4, true, theme::TEXT_DIM);
}

void paintMenu(lgfx::LovyanGFX& c, int ox, int oy) {
  text(c, ox + 16, oy + 24, "DIGIMON", theme::fontButton(), theme::TEXT_DIM, lgfx::textdatum_t::middle_left);
  wideButton(c, ox, oy, ITEMS[ITEM_CONTINUE], "CONTINUE", s_pressed == ITEM_CONTINUE, s_focus == ITEM_CONTINUE);
  wideButton(c, ox, oy, ITEMS[ITEM_NEW], "NEW GAME", s_pressed == ITEM_NEW, s_focus == ITEM_NEW);
  text(c, ox + 16, oy + LOCK_LABEL_Y, "MEMORY LOCK AT", theme::fontLabel(), theme::TEXT_DIM,
       lgfx::textdatum_t::middle_left);
  text(c, ox + PAGE_W - 16, oy + LOCK_LABEL_Y, game().lock ? "ON" : "OFF", theme::fontLabel(),
       game().lock ? theme::ACCENT : theme::TEXT_DIM, lgfx::textdatum_t::middle_right);
  for (int8_t i : {ITEM_LOCK_DOWN, ITEM_LOCK_UP}) {
    const Rect& r = ITEMS[i];
    button(c, ox, oy, r, s_pressed == i, s_focus == i);
    if (i == ITEM_LOCK_DOWN) uiMinus(c, ox + r.cx(), oy + r.cy(), 20, 5, theme::TEXT);
    else                     uiPlus(c, ox + r.cx(), oy + r.cy(), 20, 5, theme::TEXT);
  }
  char v[4];
  snprintf(v, sizeof(v), "%u", (unsigned)game().lockAt);
  text(c, ox + PAGE_W / 2, oy + LOCK_VALUE_Y, v, theme::fontTitle(), game().lock ? theme::ACCENT : theme::TEXT,
       lgfx::textdatum_t::middle_center);
  wideButton(c, ox, oy, ITEMS[ITEM_HOME], "HOME", s_pressed == ITEM_HOME, s_focus == ITEM_HOME);
}

void paintAsk(lgfx::LovyanGFX& c, int ox, int oy) {
  text(c, ox + PAGE_W / 2, oy + 44, "NEW GAME?", theme::fontTitle(), theme::TEXT, lgfx::textdatum_t::middle_center);
  text(c, ox + PAGE_W / 2, oy + 86, "Who goes first?", theme::fontBody(), theme::TEXT, lgfx::textdatum_t::middle_center);
  if (!digimonIsFresh(game()))
    text(c, ox + PAGE_W / 2, oy + 114, "Memory goes back to 0.", theme::fontSmall(), theme::DANGER,
         lgfx::textdatum_t::middle_center);
  static const char* const LABELS[ASK_COUNT] = {"TOP PLAYER", "BOTTOM PLAYER", "CANCEL"};
  for (int8_t i = 0; i < ASK_COUNT; ++i) {
    const Rect& r = ASK_BTN[i];
    button(c, ox, oy, r, s_pressed == i, s_askFocus == i);
    text(c, ox + r.cx(), oy + r.cy() + 1, LABELS[i], theme::fontButton(),
         i == ASK_CANCEL ? theme::TEXT_DIM : theme::PLAYER[i], lgfx::textdatum_t::middle_center);
  }
}

void paintPage(lgfx::LovyanGFX& c, int ox, int oy, const void*) {
  c.fillRect(ox, oy, PAGE_W, PAGE_H, theme::BG);
  if (s_ask) paintAsk(c, ox, oy);
  else       paintMenu(c, ox, oy);
}

// ---------------------------------------------------------------- module functions
void onEnter() {
  s_ask = false;
  s_pressed = -1;
  s_focus = ITEM_CONTINUE;
  s_dirty = true;
}

void handleInput(const InputEvent& e) {
  int8_t& focus = s_ask ? s_askFocus : s_focus;
  const int8_t count = s_ask ? ASK_COUNT : ITEM_COUNT;
  switch (e.type) {
    case InputType::EncoderTurn: {
      int f = (focus + e.delta) % count;
      if (f < 0) f += count;
      focus = (int8_t)f;
      break;
    }
    case InputType::EncoderClick:
      if (s_ask) answer(s_askFocus);
      else choose(s_focus);
      break;
    case InputType::EncoderLongPress:
      if (s_ask) answer(ASK_CANCEL);
      break;
    case InputType::TouchDown:
      s_pressed = hitTest(e.x, e.y);
      if (s_pressed >= 0) focus = s_pressed;
      break;
    case InputType::TouchUp: {
      const int8_t released = s_pressed;
      s_pressed = -1;
      if (released < 0 || !isTap(e, TOUCH_TAP_MAX_MS)) break;
      if (s_ask) answer(released);
      else choose(released);
      break;
    }
    default:
      break;
  }
  s_dirty = true;
}

void render(bool full) {
  if (!full && !s_dirty) return;
  s_dirty = false;
  auto& g = gfx();
  g.startWrite();
  drawSeatCard(PAGE, paintPage, nullptr);  // the whole page, turned upright for player 2
  g.endWrite();
}

}  // namespace

const ScreenModule DigimonSetupScreen = {"DigimonSetup", onEnter, handleInput, render, nullptr};
