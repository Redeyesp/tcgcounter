/* ============================================================================
 *  ScreenPokemonSetup — the Pokemon menu, drawn for the standing device
 *  (portrait), facing player 2 at the bottom edge.
 *
 *  ┌──────────────────────┐
 *  │ POKEMON              │
 *  │ [ CONTINUE        > ]│   back to the table
 *  │ [ (o) COIN FLIP   > ]│   back to the table, flipping a coin
 *  │ [ NEW GAME        > ]│   everything back to 0 (asks first)
 *  │ [ HOME            > ]│
 *  └──────────────────────┘
 *
 *  Reached from Home -> POKEMON and from the ≡ round button on the table.
 *  Touch:   tap a button.
 *  Encoder: turn = move the yellow focus frame, press = choose,
 *           long-press in the question = CANCEL.
 * ==========================================================================*/
#include "Screens.h"
#include "App.h"
#include "GameState.h"
#include "CommanderLayout.h"
#include "TableDraw.h"
#include "Theme.h"
#include "Ui.h"
#include "Config.h"

namespace {

// The whole panel as one "card" facing the right edge of the panel: the
// bottom of the standing device. 240 x 320 as player 2 sees it.
const Seat PAGE = {{0, 0, 320, 240}, Side::Right};
constexpr int PAGE_W = 240, PAGE_H = 320;

constexpr int8_t ITEM_CONTINUE = 0, ITEM_COIN = 1, ITEM_NEW = 2, ITEM_HOME = 3, ITEM_COUNT = 4;
Rect itemRect(int8_t i) { return Rect{12, (int16_t)(52 + i * 64), 216, 56}; }

// NEW GAME? question
constexpr int8_t ASK_CANCEL = 0, ASK_START = 1;
constexpr Rect ASK_BTN[2] = {{12, 196, 216, 52}, {12, 258, 216, 52}};  // CANCEL above START

constexpr uint16_t COIN_DARK = rgb565(150, 110, 0);

// ---------------------------------------------------------------- UI-only state
bool   s_ask = false;
int8_t s_focus = ITEM_CONTINUE, s_askFocus = ASK_CANCEL;
int8_t s_pressed = -1;
bool   s_dirty = true;  // something changed since the last paint

int8_t hitTest(int x, int y) {
  int lx, ly;
  seatToLocal(PAGE, x, y, lx, ly);
  if (s_ask) {
    for (int8_t i = 0; i < 2; ++i)
      if (ASK_BTN[i].contains(lx, ly)) return i;
    return -1;
  }
  for (int8_t i = 0; i < ITEM_COUNT; ++i)
    if (itemRect(i).contains(lx, ly)) return i;
  return -1;
}

// ---------------------------------------------------------------- actions
void startNewGame() {
  pokemonNewGame(g_state.pokemon);
  goToScreen(SCREEN_POKEMON);
}

void choose(int8_t i) {
  switch (i) {
    case ITEM_CONTINUE: goToScreen(SCREEN_POKEMON); break;
    case ITEM_COIN:
      pokemonRequestCoinFlip();  // the table opens flipping
      goToScreen(SCREEN_POKEMON);
      break;
    case ITEM_NEW:
      if (pokemonIsFresh(g_state.pokemon)) startNewGame();  // nothing to lose
      else { s_ask = true; s_askFocus = ASK_CANCEL; }
      break;
    case ITEM_HOME: goToScreen(SCREEN_HOME); break;
    default: break;
  }
}

void answer(int8_t i) {
  s_ask = false;
  if (i == ASK_START) startNewGame();
}

// ---------------------------------------------------------------- drawing
void button(lgfx::LovyanGFX& c, int ox, int oy, const Rect& r, uint16_t fill, bool focus) {
  c.fillRoundRect(ox + r.x, oy + r.y, r.w, r.h, 12, fill);
  if (focus) uiRoundFrame(c, ox + r.x, oy + r.y, r.w, r.h, 12, 3, theme::ACCENT);
}

void paintMenu(lgfx::LovyanGFX& c, int ox, int oy) {
  c.setFont(theme::fontButton());
  c.setTextDatum(lgfx::textdatum_t::middle_left);
  c.setTextColor(theme::TEXT_DIM);
  c.drawString("POKEMON", ox + 16, oy + 26);
  static const char* const LABELS[ITEM_COUNT] = {"CONTINUE", "COIN FLIP", "NEW GAME", "HOME"};
  for (int8_t i = 0; i < ITEM_COUNT; ++i) {
    const Rect r = itemRect(i);
    button(c, ox, oy, r, i == s_pressed ? theme::BUTTON_DOWN : theme::BUTTON, i == s_focus);
    int tx = r.x + 18;
    if (i == ITEM_COIN) {
      c.fillCircle(ox + r.x + 24, oy + r.cy(), 11, theme::ACCENT);
      c.drawCircle(ox + r.x + 24, oy + r.cy(), 8, COIN_DARK);
      tx = r.x + 44;
    }
    c.setFont(theme::fontButton());
    c.setTextDatum(lgfx::textdatum_t::middle_left);
    c.setTextColor(theme::TEXT);
    c.drawString(LABELS[i], ox + tx, oy + r.cy() + 1);
    uiChevron(c, ox + r.x + r.w - 20, oy + r.cy(), 16, 4, true, theme::TEXT_DIM);
  }
}

void paintAsk(lgfx::LovyanGFX& c, int ox, int oy) {
  c.setTextDatum(lgfx::textdatum_t::middle_center);
  c.setFont(theme::fontTitle());
  c.setTextColor(theme::TEXT);
  c.drawString("NEW GAME?", ox + PAGE_W / 2, oy + 60);
  c.setFont(theme::fontBody());
  c.drawString("Everything back to 0", ox + PAGE_W / 2, oy + 112);
  c.setFont(theme::fontSmall());
  c.setTextColor(theme::DANGER);
  c.drawString("The current game will be lost.", ox + PAGE_W / 2, oy + 144);
  for (int8_t i = 0; i < 2; ++i) {
    const bool down = i == s_pressed;
    uint16_t fill = down ? theme::BUTTON_DOWN : theme::BUTTON;
    if (i == ASK_START) fill = down ? theme::DANGER_FILL_DOWN : theme::DANGER_FILL;
    button(c, ox, oy, ASK_BTN[i], fill, i == s_askFocus);
    c.setFont(theme::fontButton());
    c.setTextDatum(lgfx::textdatum_t::middle_center);
    c.setTextColor(theme::TEXT);
    c.drawString(i == ASK_START ? "START" : "CANCEL", ox + ASK_BTN[i].cx(), oy + ASK_BTN[i].cy() + 1);
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
  const int8_t count = s_ask ? 2 : ITEM_COUNT;
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

const ScreenModule PokemonSetupScreen = {"PokemonSetup", onEnter, handleInput, render, nullptr};
