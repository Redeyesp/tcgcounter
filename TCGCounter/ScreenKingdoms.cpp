/* ============================================================================
 *  ScreenKingdoms — deals the hidden Kingdoms roles one player at a time,
 *  and lets a player check their role again later. The deal itself is made
 *  in the Kingdoms menu (ScreenKingdomsSetup.cpp); the cards and who drew
 *  which are saved, so a restart keeps them.
 *
 *  CARDS (dealing)                          REVEAL
 *  ┌──────────────────────────────────────┐ ┌──────────────────────────────────────┐
 *  │ [< MENU]              PLAYER 2 OF 5  │ │ ┌──────────┐  YOU ARE               │
 *  │     Everyone else: eyes closed.      │ │ │  (art)   │  BANDIT                │
 *  │  Tap a face-down card to see yours.  │ │ │          │  Keep it secret. Win:  │
 *  │  [##] [ 1 ] [##] [##] [##]           │ │ │          │  the King is out. ...  │
 *  │        1 of 5 have looked            │ │ └──────────┘          [  NEXT  ]    │
 *  └──────────────────────────────────────┘ └──────────────────────────────────────┘
 *  PASS: "CLOSE YOUR EYES and call the person next to you" — the next player
 *  taps the screen. After the last player: DONE — "Everyone, open your eyes",
 *  the King reveals; COMMANDER TABLE starts Commander for that many players
 *  (asks first if a Commander game is running), MENU.
 *
 *  Checking your role again (Kingdoms menu -> CHECK MY ROLE, once everyone
 *  has drawn): the cards carry the order they were drawn in; hold yours
 *  KINGDOMS_PEEK_HOLD_MS to see it (DONE hides it again).
 *
 *  Encoder: cards: turn = pick a card, press = turn it over (dealing) / show
 *           it (checking), long-press = menu · reveal / close-your-eyes:
 *           press = go on · done: turn = pick a button, press = choose.
 * ==========================================================================*/
#include <Arduino.h>
#include "Screens.h"
#include "App.h"
#include "GameState.h"
#include "KingdomsArt.h"
#include "Theme.h"
#include "Ui.h"
#include "UiConfirm.h"
#include "Config.h"
#include <stdio.h>
#include <string.h>

namespace {

KingdomsGame& game() { return g_state.kingdoms; }
bool checking() { return kingdomsAllDrawn(game()); }  // everyone has drawn: cards are for checking

// ---------------------------------------------------------------- geometry
constexpr Rect BACK_BTN = {12, 6, 88, 34};
constexpr int  CARD_W = 46, CARD_H = 68, CARD_GAP = 6, CARD_Y = 100;

Rect cardRect(uint8_t k) {
  const int n = game().players;
  const int total = n * CARD_W + (n - 1) * CARD_GAP;
  return Rect{(int16_t)((320 - total) / 2 + k * (CARD_W + CARD_GAP)), CARD_Y, CARD_W, CARD_H};
}

constexpr Rect ART_PANEL = {8, 8, 120, 224};
constexpr int  TEXT_X = 138, TEXT_W = 176;
constexpr Rect REVEAL_BTN = {138, 190, 170, 42};

constexpr Rect DONE_BTN[2] = {{12, 180, 196, 48}, {216, 180, 92, 48}};  // COMMANDER TABLE, MENU

// ---------------------------------------------------------------- UI-only state
enum class Page : uint8_t { Cards, Reveal, Pass, Done };
constexpr int8_t HIT_BACK = 100;

Page     s_page = Page::Cards;
int8_t   s_card = -1;      // Reveal: the card shown
bool     s_peek = false;   // Reveal: checking (DONE) rather than dealing (NEXT)
int8_t   s_focus = 0;      // Cards: card · Done: button
int8_t   s_pressed = -1;   // card / button / HIT_BACK under the finger
uint32_t s_pressAt = 0;
ConfirmDialog s_ask;       // starting Commander over a running game

// what is on screen
bool   s_needFull = true;
bool   s_drawnAsk = false;
Page   s_drawnPage = Page::Cards;
int8_t s_drawnFocus = -1, s_drawnPressed = -1;

void setPage(Page p) {
  s_page = p;
  s_pressed = -1;
  s_needFull = true;
}

bool selectable(uint8_t k) {  // dealing: face-down cards · checking: every card
  return k < game().players && (checking() || game().drawnBy[k] == 0);
}

void focusFirst() {
  s_focus = 0;
  for (uint8_t k = 0; k < game().players; ++k)
    if (selectable(k)) { s_focus = (int8_t)k; return; }
}

void moveFocus(int delta) {
  const int n = game().players;
  if (n == 0) return;
  int f = s_focus;
  const int step = delta > 0 ? 1 : -1;
  for (int moves = delta > 0 ? delta : -delta; moves > 0; --moves) {
    for (int tries = 0; tries < n; ++tries) {
      f = (f + step + n) % n;
      if (selectable((uint8_t)f)) break;
    }
  }
  s_focus = (int8_t)f;
}

// ---------------------------------------------------------------- actions
void reveal(uint8_t k, bool peek) {
  s_card = (int8_t)k;
  s_peek = peek;
  setPage(Page::Reveal);
}

void drawCard(uint8_t k) {  // dealing: this player turns card k over
  if (kingdomsDraw(game(), k)) reveal(k, false);
}

void afterReveal() {
  if (s_peek) { setPage(Page::Cards); return; }
  if (checking()) {  // that was the last player: everyone opens their eyes
    s_focus = 0;
    setPage(Page::Done);
  } else {
    setPage(Page::Pass);
  }
}

void startCommander() {
  CommanderGame& c = g_state.commander;
  const uint8_t n = game().players;
  commanderNewGame(c, n, c.players == n ? c.layout : 0);
  goToScreen(SCREEN_COMMANDER);
}

void toCommander() {
  CommanderGame& c = g_state.commander;
  if (commanderIsFresh(c)) {  // nothing to lose
    if (c.players != game().players) commanderNewGame(c, game().players, 0);
    goToScreen(SCREEN_COMMANDER);
    return;
  }
  char body[48];
  snprintf(body, sizeof(body), "Commander: %u players at %d life", (unsigned)game().players,
           (int)COMMANDER_START_LIFE);
  s_ask.open("NEW GAME?", body, "The running Commander game is lost.", "START");
}

// ---------------------------------------------------------------- drawing helpers
void text(int x, int y, const char* t, const lgfx::IFont* f, uint16_t col,
          lgfx::textdatum_t datum = lgfx::textdatum_t::middle_center) {
  auto& g = gfx();
  g.setFont(f);
  g.setTextDatum(datum);
  g.setTextColor(col);
  g.drawString(t, x, y);
}

void button(const Rect& r, const char* label, bool pressed, bool focus, uint16_t fill = theme::BUTTON) {
  auto& g = gfx();
  g.fillRect(r.x, r.y, r.w, r.h, theme::BG);
  g.fillRoundRect(r.x, r.y, r.w, r.h, 10, pressed ? theme::BUTTON_DOWN : fill);
  if (focus) uiRoundFrame(g, r.x, r.y, r.w, r.h, 10, 3, theme::ACCENT);
  text(r.cx(), r.cy() + 1, label, theme::fontButton(), theme::TEXT);
}

void backButton(bool pressed) {
  auto& g = gfx();
  const Rect& r = BACK_BTN;
  g.fillRect(r.x, r.y, r.w, r.h, theme::BG);
  g.fillRoundRect(r.x, r.y, r.w, r.h, 10, pressed ? theme::BUTTON_DOWN : theme::BUTTON);
  uiChevron(g, r.x + 18, r.cy(), 14, 3, false, theme::TEXT);
  text(r.x + 32, r.cy() + 1, "MENU", theme::fontLabel(), theme::TEXT, lgfx::textdatum_t::middle_left);
}

// ---------------------------------------------------------------- pages
void drawCardSlot(uint8_t k) {
  auto& g = gfx();
  const KingdomsGame& kg = game();
  const Rect r = cardRect(k);
  const bool held = s_pressed == (int8_t)k;
  const bool focus = s_focus == (int8_t)k;
  const int lift = held ? 6 : 0;
  g.fillRect(r.x - 4, r.y - 10, r.w + 8, r.h + 14, theme::BG);
  const int y = r.y - lift;
  char num[4];
  snprintf(num, sizeof(num), "%u", (unsigned)kg.drawnBy[k]);
  if (!checking() && kg.drawnBy[k]) {  // dealing: a card already taken leaves an empty slot
    g.drawRoundRect(r.x, y, r.w, r.h, 6, theme::PANEL_EDGE);
    text(r.cx(), y + r.h / 2 + 1, num, theme::fontButton(), theme::TEXT_DIM);
  } else {
    drawKingdomsCardBack(g, r.x, y, r.w, r.h, false);
    if (checking()) {  // the order it was drawn in
      g.fillCircle(r.cx(), y + r.h - 13, 10, theme::ACCENT);
      text(r.cx(), y + r.h - 12, num, theme::fontLabel(), theme::TEXT_ON_ACCENT);
    }
  }
  if (focus || held) uiRoundFrame(g, r.x - 3, y - 3, r.w + 6, r.h + 6, 8, 2, theme::ACCENT);
}

void drawCardsPage() {
  auto& g = gfx();
  const KingdomsGame& kg = game();
  g.fillScreen(theme::BG);
  backButton(s_pressed == HIT_BACK);
  char buf[40];
  if (checking()) {
    text(308, 23, "CHECK YOUR ROLE", theme::fontLabel(), theme::TEXT_DIM, lgfx::textdatum_t::middle_right);
    text(160, 58, "Everyone else: look away.", theme::fontBody(), theme::TEXT);
    text(160, 80, "Hold your card to see your role again.", theme::fontSmall(), theme::TEXT_DIM);
    text(160, 196, "Numbers: the order you drew in", theme::fontSmall(), theme::TEXT_DIM);
  } else {
    snprintf(buf, sizeof(buf), "PLAYER %u OF %u", (unsigned)(kg.drawn + 1), (unsigned)kg.players);
    text(308, 23, buf, theme::fontLabel(), theme::ACCENT, lgfx::textdatum_t::middle_right);
    if (kg.drawn == 0) {
      text(160, 58, "Everyone close your eyes.", theme::fontBody(), theme::TEXT);
      text(160, 80, "First player: open yours and tap a card.", theme::fontSmall(), theme::TEXT_DIM);
    } else {
      text(160, 58, "Everyone else: eyes closed.", theme::fontBody(), theme::TEXT);
      text(160, 80, "Tap a face-down card to see your role.", theme::fontSmall(), theme::TEXT_DIM);
    }
    snprintf(buf, sizeof(buf), "%u of %u have looked", (unsigned)kg.drawn, (unsigned)kg.players);
    text(160, 196, buf, theme::fontSmall(), theme::TEXT_DIM);
  }
  for (uint8_t k = 0; k < kg.players; ++k) drawCardSlot(k);
}

void drawRevealPage() {
  auto& g = gfx();
  const uint8_t role = game().role[s_card < 0 ? 0 : s_card];
  g.fillScreen(theme::BG);
  const Rect& a = ART_PANEL;
  g.fillRoundRect(a.x, a.y, a.w, a.h, 12, kingdomsRoleTint(role));
  uiRoundFrame(g, a.x, a.y, a.w, a.h, 12, 2, kingdomsRoleColor(role));
  drawKingdomsArt(g, a.cx(), a.cy(), 104, role);
  text(TEXT_X, 22, s_peek ? "YOUR ROLE" : "YOU ARE", theme::fontSmall(), theme::TEXT_DIM, lgfx::textdatum_t::middle_left);
  const char* name = kingdomsRoleName(role);
  g.setFont(theme::fontTitle());
  const lgfx::IFont* nameFont = g.textWidth(name) <= TEXT_W ? theme::fontTitle() : theme::fontButton();
  text(TEXT_X, 48, name, nameFont, kingdomsRoleColor(role), lgfx::textdatum_t::middle_left);
  uiWrappedText(g, TEXT_X, 72, TEXT_W, 17, kingdomsRoleGoal(role), theme::fontSmall(), theme::TEXT);
  button(REVEAL_BTN, s_peek ? "DONE" : "NEXT", s_pressed == 0, true);
}

void drawPassPage() {
  auto& g = gfx();
  g.fillScreen(theme::BG);
  // a closed eye: the lid and its lashes
  const int ex = 160, ey = 40;
  g.fillArc(ex, ey, 38, 34, 20, 160, theme::TEXT);
  for (int a = 40; a <= 140; a += 25) {
    const float rad = a * 3.14159265f / 180.0f;
    uiThickLine(g, ex + (int)(38 * cosf(rad)), ey + (int)(38 * sinf(rad)), ex + (int)(48 * cosf(rad)),
                ey + (int)(48 * sinf(rad)), 3, theme::TEXT);
  }
  text(160, 106, "CLOSE YOUR EYES", theme::fontButton(), theme::ACCENT);
  text(160, 138, "and call the person", theme::fontBody(), theme::TEXT);
  text(160, 162, "next to you", theme::fontBody(), theme::TEXT);
  char buf[48];
  snprintf(buf, sizeof(buf), "Player %u of %u: tap when ready", (unsigned)(game().drawn + 1),
           (unsigned)game().players);
  text(160, 210, buf, theme::fontSmall(), theme::TEXT_DIM);
}

void drawDonePage() {
  auto& g = gfx();
  g.fillScreen(theme::BG);
  drawKingdomsArt(g, 160, 42, 64, KINGDOMS_KING);
  text(160, 96, "OPEN YOUR EYES!", theme::fontButton(), theme::TEXT);
  text(160, 122, "King, reveal yourself!", theme::fontSmall(), theme::ACCENT);
  char buf[48];
  snprintf(buf, sizeof(buf), "%d life, and you go first.", (int)KINGDOMS_KING_LIFE);
  text(160, 140, buf, theme::fontSmall(), theme::ACCENT);
  text(160, 162, "Forgot yours? CHECK MY ROLE", theme::fontSmall(), theme::TEXT_DIM);
  for (int8_t i = 0; i < 2; ++i) {
    const Rect& r = DONE_BTN[i];
    button(r, i == 0 ? "COMMANDER" : "MENU", s_pressed == i, s_focus == i);
  }
}

// ---------------------------------------------------------------- input per page
int8_t hitCards(int x, int y) {
  if (BACK_BTN.contains(x, y)) return HIT_BACK;
  for (uint8_t k = 0; k < game().players; ++k) {
    const Rect r = cardRect(k);
    if (x >= r.x - 3 && x < r.x + r.w + 3 && y >= r.y - 8 && y < r.y + r.h + 3) return (int8_t)k;
  }
  return -1;
}

void cardsInput(const InputEvent& e) {
  switch (e.type) {
    case InputType::EncoderTurn:      moveFocus(e.delta); break;
    case InputType::EncoderClick:
      if (!selectable((uint8_t)s_focus)) break;
      if (checking()) reveal((uint8_t)s_focus, true);
      else drawCard((uint8_t)s_focus);
      break;
    case InputType::EncoderLongPress: goToScreen(SCREEN_KINGDOMS_SETUP); break;
    case InputType::TouchDown: {
      const int8_t h = hitCards(e.x, e.y);
      s_pressed = (h == HIT_BACK || (h >= 0 && selectable((uint8_t)h))) ? h : -1;
      if (s_pressed >= 0 && s_pressed != HIT_BACK) s_focus = s_pressed;
      s_pressAt = millis();
      break;
    }
    case InputType::TouchUp: {
      const int8_t released = s_pressed;
      s_pressed = -1;
      if (released < 0 || !isTap(e, TOUCH_TAP_MAX_MS)) break;
      if (released == HIT_BACK) goToScreen(SCREEN_KINGDOMS_SETUP);
      else if (!checking()) drawCard((uint8_t)released);  // checking needs a hold (tick)
      break;
    }
    default: break;
  }
}

void revealInput(const InputEvent& e) {
  switch (e.type) {
    case InputType::EncoderClick:
    case InputType::EncoderLongPress: afterReveal(); break;
    case InputType::TouchDown:        s_pressed = REVEAL_BTN.contains(e.x, e.y) ? 0 : -1; break;
    case InputType::TouchUp: {
      const bool on = s_pressed == 0;
      s_pressed = -1;
      if (on && isTap(e, TOUCH_TAP_MAX_MS)) afterReveal();
      break;
    }
    default: break;
  }
}

void passInput(const InputEvent& e) {
  switch (e.type) {
    case InputType::EncoderClick: focusFirst(); setPage(Page::Cards); break;
    case InputType::TouchDown:    s_pressed = 0; break;  // anywhere
    case InputType::TouchUp: {
      const bool on = s_pressed == 0;
      s_pressed = -1;
      if (on && isTap(e, TOUCH_TAP_MAX_MS)) { focusFirst(); setPage(Page::Cards); }
      break;
    }
    default: break;
  }
}

void chooseDone(int8_t i) {
  if (i == 0) toCommander();
  else if (i == 1) goToScreen(SCREEN_KINGDOMS_SETUP);
}

void doneInput(const InputEvent& e) {
  switch (e.type) {
    case InputType::EncoderTurn:  s_focus = (int8_t)(((s_focus + e.delta) % 2 + 2) % 2); break;
    case InputType::EncoderClick: chooseDone(s_focus); break;
    case InputType::TouchDown:
      s_pressed = -1;
      for (int8_t i = 0; i < 2; ++i)
        if (DONE_BTN[i].contains(e.x, e.y)) s_pressed = s_focus = i;
      break;
    case InputType::TouchUp: {
      const int8_t released = s_pressed;
      s_pressed = -1;
      if (released >= 0 && isTap(e, TOUCH_TAP_MAX_MS)) chooseDone(released);
      break;
    }
    default: break;
  }
}

// ---------------------------------------------------------------- module functions
void onEnter() {
  s_ask.close();
  s_peek = false;
  s_card = -1;
  setPage(Page::Cards);
  focusFirst();
}

void handleInput(const InputEvent& e) {
  if (s_ask.isOpen()) {
    if (s_ask.handleInput(e) == ConfirmResult::Confirm) startCommander();
    else if (!s_ask.isOpen()) s_needFull = true;  // cancelled: back on the done page
    return;
  }
  if (!kingdomsDealt(game())) {  // nothing dealt (e.g. a broken save): back to the menu
    goToScreen(SCREEN_KINGDOMS_SETUP);
    return;
  }
  switch (s_page) {
    case Page::Cards:  cardsInput(e); break;
    case Page::Reveal: revealInput(e); break;
    case Page::Pass:   passInput(e); break;
    case Page::Done:   doneInput(e); break;
  }
}

void tick(uint32_t now) {
  // checking: holding your card long enough shows it
  if (s_page == Page::Cards && checking() && s_pressed >= 0 && s_pressed != HIT_BACK &&
      now - s_pressAt >= (uint32_t)KINGDOMS_PEEK_HOLD_MS) {
    const uint8_t k = (uint8_t)s_pressed;
    s_pressed = -1;  // the finger's release belongs to nothing now
    reveal(k, true);
  }
}

void render(bool full) {
  const bool ask = s_ask.isOpen();
  const bool askChanged = ask != s_drawnAsk;
  s_drawnAsk = ask;
  if (ask) { s_ask.render(full || askChanged); return; }
  if (!kingdomsDealt(game())) {  // nothing to show: the menu takes over on the next input
    if (full) { auto& g = gfx(); g.startWrite(); g.fillScreen(theme::BG); g.endWrite(); }
    return;
  }
  auto& g = gfx();
  full = full || askChanged || s_needFull || s_page != s_drawnPage;
  if (!full && s_focus == s_drawnFocus && s_pressed == s_drawnPressed) return;
  g.startWrite();
  if (full) {
    switch (s_page) {
      case Page::Cards:  drawCardsPage(); break;
      case Page::Reveal: drawRevealPage(); break;
      case Page::Pass:   drawPassPage(); break;
      case Page::Done:   drawDonePage(); break;
    }
  } else if (s_page == Page::Cards) {
    for (uint8_t k = 0; k < game().players; ++k) {
      const bool was = (k == s_drawnFocus || k == s_drawnPressed);
      const bool is  = (k == s_focus || k == s_pressed);
      if (was || is) drawCardSlot(k);
    }
    if (s_drawnPressed == HIT_BACK || s_pressed == HIT_BACK) backButton(s_pressed == HIT_BACK);
  } else if (s_page == Page::Reveal) {
    button(REVEAL_BTN, s_peek ? "DONE" : "NEXT", s_pressed == 0, true);
  } else if (s_page == Page::Done) {
    for (int8_t i = 0; i < 2; ++i) button(DONE_BTN[i], i == 0 ? "COMMANDER" : "MENU", s_pressed == i, s_focus == i);
  }
  g.endWrite();
  s_needFull = false;
  s_drawnPage = s_page;
  s_drawnFocus = s_focus;
  s_drawnPressed = s_pressed;
}

}  // namespace

const ScreenModule KingdomsScreen = {"Kingdoms", onEnter, handleInput, render, tick};
