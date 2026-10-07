/* ============================================================================
 *  ScreenKingdomsSetup — the Kingdoms menu: Commander with hidden roles for
 *  4 to 6 players. Deal the roles, carry on dealing, or check your role.
 *
 *  ┌──────────────────────────────────────────┐
 *  │ [< HOME]                       KINGDOMS │
 *  │ ┌──────────────────────────────────────┐ │
 *  │ │ CONTINUE DEAL                 2/5   > │ │  or CHECK MY ROLE (5P) once all have drawn
 *  │ └──────────────────────────────────────┘ │
 *  │ DEAL HIDDEN ROLES                        │
 *  │ [   4    ] [   5    ] [   6    ]         │  shuffled with the hardware RNG
 *  │ [PLAYERS ] [PLAYERS ] [PLAYERS ]         │
 *  │      King, Knight, 2 Bandits, Traitor    │  (the roles of the focused button)
 *  └──────────────────────────────────────────┘
 *
 *  Dealing again throws the current roles away, so it asks first.
 *  Touch: tap a button. Encoder: turn = move the yellow focus, press =
 *  choose, long-press in the question = CANCEL.
 * ==========================================================================*/
#include <Arduino.h>
#include "Screens.h"
#include "App.h"
#include "GameState.h"
#include "Theme.h"
#include "Ui.h"
#include "UiConfirm.h"
#include "Config.h"
#include <stdio.h>
#include <string.h>

namespace {

KingdomsGame& game() { return g_state.kingdoms; }

// ---------------------------------------------------------------- items
constexpr int8_t ITEM_BACK = 0, ITEM_CONTINUE = 1, ITEM_FIRST_DEAL = 2, ITEM_COUNT = 5;  // deal 4 / 5 / 6
constexpr Rect ITEMS[ITEM_COUNT] = {
  {12, 6, 88, 34},     // < HOME
  {12, 48, 296, 64},   // CONTINUE DEAL / CHECK MY ROLE
  {12, 146, 93, 64},   // 4 players
  {113, 146, 94, 64},  // 5 players
  {215, 146, 93, 64},  // 6 players
};
constexpr int CAPTION_Y = 132, HINT_Y = 226;

uint8_t playersOf(int8_t item) { return (uint8_t)(KINGDOMS_MIN_PLAYERS + (item - ITEM_FIRST_DEAL)); }

// "King, Knight, 2 Bandits, Traitor"
void deckText(uint8_t players, char* buf, size_t n) {
  static const char* const NAMES[KINGDOMS_ROLE_COUNT] = {"King", "Knight", "Bandit", "Traitor", "Usurper"};
  buf[0] = 0;
  for (uint8_t r = 0; r < KINGDOMS_ROLE_COUNT; ++r) {
    const uint8_t c = kingdomsRoleCount(players, r);
    if (!c) continue;
    char part[16];
    if (c == 1) snprintf(part, sizeof(part), "%s", NAMES[r]);
    else        snprintf(part, sizeof(part), "%u %ss", (unsigned)c, NAMES[r]);
    if (buf[0]) strncat(buf, ", ", n - strlen(buf) - 1);
    strncat(buf, part, n - strlen(buf) - 1);
  }
}

// ---------------------------------------------------------------- UI-only state
ConfirmDialog s_ask;  // "DEAL AGAIN?"
uint8_t s_askPlayers = 0;
int8_t  s_focus = ITEM_FIRST_DEAL;
int8_t  s_pressed = -1;
bool    s_drawnAsk = false;
int8_t  s_drawnFocus = -1, s_drawnPressed = -1;

int8_t hitTest(int x, int y) {
  for (int8_t i = 0; i < ITEM_COUNT; ++i)
    if (ITEMS[i].contains(x, y)) return i;
  return -1;
}

// ---------------------------------------------------------------- actions
uint8_t randBelow(uint8_t n) { return (uint8_t)random(0, n); }  // hardware RNG on the ESP32

void deal(uint8_t players) {
  kingdomsDeal(game(), players, randBelow);
  goToScreen(SCREEN_KINGDOMS);
}

void choose(int8_t item) {
  if (item == ITEM_BACK) {
    goToScreen(SCREEN_HOME);
  } else if (item == ITEM_CONTINUE) {
    if (kingdomsDealt(game())) goToScreen(SCREEN_KINGDOMS);
  } else if (item >= ITEM_FIRST_DEAL && item < ITEM_COUNT) {
    const uint8_t n = playersOf(item);
    if (!kingdomsDealt(game())) {
      deal(n);  // nothing to lose
    } else {
      char body[48];
      snprintf(body, sizeof(body), "%u players, new hidden roles", (unsigned)n);
      s_askPlayers = n;
      s_ask.open("DEAL AGAIN?", body, "The roles dealt now are lost.", "DEAL");
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

void drawHint() {
  auto& g = gfx();
  g.fillRect(0, HINT_Y - 12, 320, 24, theme::BG);
  char buf[64];
  if (s_focus >= ITEM_FIRST_DEAL) deckText(playersOf(s_focus), buf, sizeof(buf));
  else snprintf(buf, sizeof(buf), "Commander with hidden roles");
  text(160, HINT_Y, buf, theme::fontSmall(), theme::TEXT_DIM, lgfx::textdatum_t::middle_center);
}

void drawMenuItem(int8_t item) {
  auto& g = gfx();
  const Rect& r = ITEMS[item];
  const bool pressed = (item == s_pressed);
  const int radius = item == ITEM_BACK ? 10 : 12;
  const KingdomsGame& kg = game();

  g.fillRect(r.x, r.y, r.w, r.h, theme::BG);
  g.fillRoundRect(r.x, r.y, r.w, r.h, radius, pressed ? theme::BUTTON_DOWN : theme::BUTTON);
  if (item == s_focus) uiRoundFrame(g, r.x, r.y, r.w, r.h, radius, 3, theme::ACCENT);

  if (item == ITEM_BACK) {
    uiChevron(g, r.x + 18, r.cy(), 14, 3, false, theme::TEXT);
    text(r.x + 32, r.cy() + 1, "HOME", theme::fontLabel(), theme::TEXT, lgfx::textdatum_t::middle_left);
  } else if (item == ITEM_CONTINUE) {
    if (!kingdomsDealt(kg)) {
      text(r.x + 18, r.cy() + 1, "NO ROLES DEALT YET", theme::fontLabel(), theme::TEXT_DIM, lgfx::textdatum_t::middle_left);
      return;
    }
    const bool all = kingdomsAllDrawn(kg);
    text(r.x + 18, r.cy() + 1, all ? "CHECK MY ROLE" : "CONTINUE DEAL", theme::fontButton(), theme::TEXT,
         lgfx::textdatum_t::middle_left);
    char v[12];  // checking: how many players · dealing: how many have looked
    if (all) snprintf(v, sizeof(v), "%uP", (unsigned)kg.players);
    else     snprintf(v, sizeof(v), "%u/%u", (unsigned)kg.drawn, (unsigned)kg.players);
    text(r.x + r.w - 40, r.cy() + 1, v, theme::fontLabel(), theme::ACCENT, lgfx::textdatum_t::middle_right);
    uiChevron(g, r.x + r.w - 20, r.cy(), 18, 4, true, theme::TEXT_DIM);
  } else {
    char n[4];
    snprintf(n, sizeof(n), "%u", (unsigned)playersOf(item));
    text(r.cx(), r.cy() - 8, n, theme::fontTitle(), theme::TEXT, lgfx::textdatum_t::middle_center);
    text(r.cx(), r.y + r.h - 12, "PLAYERS", theme::fontSmall(), theme::TEXT_DIM, lgfx::textdatum_t::middle_center);
  }
}

void drawMenuPage() {
  text(308, 23, "KINGDOMS", theme::fontLabel(), theme::TEXT_DIM, lgfx::textdatum_t::middle_right);
  text(16, CAPTION_Y, "DEAL HIDDEN ROLES", theme::fontLabel(), theme::TEXT_DIM, lgfx::textdatum_t::middle_left);
  for (int8_t i = 0; i < ITEM_COUNT; ++i) drawMenuItem(i);
  drawHint();
}

// ---------------------------------------------------------------- module functions
void onEnter() {
  s_ask.close();
  s_pressed = -1;
  s_focus = kingdomsDealt(game()) ? ITEM_CONTINUE : ITEM_FIRST_DEAL;
}

void handleInput(const InputEvent& e) {
  if (s_ask.isOpen()) {
    if (s_ask.handleInput(e) == ConfirmResult::Confirm) deal(s_askPlayers);
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
    if (s_focus != s_drawnFocus) drawHint();
    g.endWrite();
  }
  s_drawnFocus = s_focus;
  s_drawnPressed = s_pressed;
}

}  // namespace

const ScreenModule KingdomsSetupScreen = {"KingdomsSetup", onEnter, handleInput, render, nullptr};
