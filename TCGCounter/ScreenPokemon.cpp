/* ============================================================================
 *  ScreenPokemon — Pokemon TCG counter for two players. The device stands
 *  up (portrait): player 1 sits at the top short edge (their half is upside
 *  down), player 2 at the bottom.
 *
 *  ┌──────────────────────────┐   each half, as its player sees it
 *  │ [PSN] [BRN]        [KO]  │   (top = middle of the device):
 *  │ [ − ]     120     [ + ]  │
 *  │          ACTIVE          │   Active: damage, − / + by 10, PSN / BRN, KO
 *  │ [1 ][2 ][3 ][4 ][5 ]     │   bench slots 1..5 (damage under the number)
 *  └──────────────────────────┘
 *  Round buttons on the middle line: coin flip, ≡ Pokemon menu.
 *
 *  Touch:   − / + (hold repeats) = damage of what is open (Active, or the
 *               opened bench slot) by 10
 *           PSN / BRN = poisoned / burned on/off (Active only)
 *           KO = knocked out: damage back to 0 (the Active also loses PSN / BRN)
 *           tap a bench slot = open it: the big area becomes that slot with
 *               − / +, KO, ⇅ SWAP and < (back to the Active); tap it again = close
 *           ⇅ SWAP = that bench Pokemon becomes the Active, the Active goes to
 *               that slot with its damage (PSN / BRN end)
 *           coin = coin flip, shown on both halves; ≡ = Pokemon menu
 *  Encoder: turn = damage of what the selected player has open, by 10
 *           press = the other player · long-press = back to the Active
 *  An opened bench slot closes by itself after POKEMON_BENCH_TIMEOUT_MS.
 * ==========================================================================*/
#include <Arduino.h>
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

// ---------------------------------------------------------------- geometry
// The two halves, in panel coordinates: player 1 faces the left edge of the
// panel (= top of the standing device), player 2 the right edge (= bottom).
const Seat SEATS[2] = {{{0, 0, 159, 240}, Side::Left}, {{161, 0, 159, 240}, Side::Right}};
constexpr int HALF_W = 240, HALF_H = 159;  // a half as its player sees it

// Round buttons on the middle line (panel coordinates); their dark rings
// reach 22 px into the halves' top rows, which stay empty there.
constexpr int COIN_X = 160, COIN_Y = 90, MENU_X = 160, MENU_Y = 150;

// Inside a half (its player's view). Top row: chips, or < / SWAP when a bench slot is open.
constexpr Rect PSN_CHIP = {10, 28, 60, 24};
constexpr Rect BRN_CHIP = {76, 28, 60, 24};
constexpr Rect KO_BTN   = {176, 28, 54, 24};
constexpr Rect BACK_BTN = {10, 28, 36, 24};
constexpr Rect SWAP_BTN = {52, 28, 92, 24};
constexpr Rect MINUS    = {10, 58, 52, 50};
constexpr Rect PLUS     = {178, 58, 52, 50};
constexpr int  NUM_CX = 120, NUM_CY = 78, CAPTION_Y = 104;
constexpr int  BENCH_X0 = 8, BENCH_Y = 114, BENCH_W = 42, BENCH_H = 39, BENCH_PITCH = 45;
constexpr int  TOP_ROW_Y0 = 24, TOP_ROW_Y1 = 56;  // touch band of the top row
constexpr int  PAD = 4;                           // touch slack around small buttons

Rect benchCell(int k) { return Rect{(int16_t)(BENCH_X0 + k * BENCH_PITCH), BENCH_Y, BENCH_W, BENCH_H}; }

// coin page, inside each half
constexpr int  COIN_CX = 62, COIN_CY = 64, COIN_R = 42;
constexpr Rect FLIP_BTN = {10, 116, 106, 34};
constexpr Rect COIN_BACK_BTN = {124, 116, 106, 34};

constexpr uint16_t PSN_COL   = rgb565(176, 92, 230);
constexpr uint16_t BRN_COL   = rgb565(255, 112, 40);
constexpr uint16_t SWAP_FILL = rgb565(40, 70, 120);
constexpr uint16_t COIN_FILL = rgb565(230, 170, 0);
constexpr uint16_t COIN_DARK = rgb565(150, 110, 0);

// ---------------------------------------------------------------- hit test
enum class Hit : uint8_t { None, Area, Psn, Brn, Ko, Back, Swap, Minus, Plus, Bench, Coin, Menu, Flip, CoinBack };
struct Touch {
  Hit     what;
  uint8_t side;
  uint8_t slot;  // Bench: 0..4
  bool operator==(const Touch& o) const { return what == o.what && side == o.side && slot == o.slot; }
};
constexpr Touch NO_TOUCH = {Hit::None, 0, 0};

bool inPadded(const Rect& r, int x, int y) {
  return x >= r.x - PAD && x < r.x + r.w + PAD && y >= r.y - PAD && y < r.y + r.h + PAD;
}
bool inHub(int x, int y, int hx, int hy) {
  const int dx = x - hx, dy = y - hy;
  return dx * dx + dy * dy <= HUB_HIT_R * HUB_HIT_R;
}
uint8_t sideAt(int x) { return x < 160 ? 0 : 1; }  // the 2 px gap goes to the nearer half

// ---------------------------------------------------------------- UI-only state
int8_t   s_open[2] = {POKEMON_ACTIVE, POKEMON_ACTIVE};  // what each half shows big
uint32_t s_usedAt[2] = {0, 0};   // last touch on that half (closes an opened bench slot)
Touch    s_press = NO_TOUCH;

// coin flip
bool     s_coin = false;          // coin page on screen
bool     s_coinFlipping = false;
uint8_t  s_coinResult = 0;        // 0 heads, 1 tails
uint8_t  s_coinTick = 0;          // animation step while flipping
uint32_t s_coinStart = 0, s_coinNext = 0;
bool     s_coinOnEnter = false;   // the menu asked for a coin flip

Touch hitTable(int x, int y) {
  if (inHub(x, y, COIN_X, COIN_Y)) return {Hit::Coin, 0, 0};
  if (inHub(x, y, MENU_X, MENU_Y)) return {Hit::Menu, 0, 0};
  const uint8_t side = sideAt(x);
  int lx, ly;
  seatToLocal(SEATS[side], x, y, lx, ly);
  if (ly >= BENCH_Y - 4) {  // bench row: the gaps belong to the nearer slot
    int k = (lx - BENCH_X0 + (BENCH_PITCH - BENCH_W) / 2) / BENCH_PITCH;
    if (k < 0) k = 0;
    if (k >= POKEMON_BENCH) k = POKEMON_BENCH - 1;
    return {Hit::Bench, side, (uint8_t)k};
  }
  if (inPadded(MINUS, lx, ly)) return {Hit::Minus, side, 0};
  if (inPadded(PLUS, lx, ly)) return {Hit::Plus, side, 0};
  if (ly >= TOP_ROW_Y0 && ly < TOP_ROW_Y1) {
    if (inPadded(KO_BTN, lx, ly)) return {Hit::Ko, side, 0};
    if (s_open[side] == POKEMON_ACTIVE) {
      if (inPadded(PSN_CHIP, lx, ly)) return {Hit::Psn, side, 0};
      if (inPadded(BRN_CHIP, lx, ly)) return {Hit::Brn, side, 0};
    } else {
      if (inPadded(BACK_BTN, lx, ly)) return {Hit::Back, side, 0};
      if (inPadded(SWAP_BTN, lx, ly)) return {Hit::Swap, side, 0};
    }
  }
  return {Hit::Area, side, 0};
}

Touch hitCoin(int x, int y) {
  const uint8_t side = sideAt(x);
  int lx, ly;
  seatToLocal(SEATS[side], x, y, lx, ly);
  if (inPadded(FLIP_BTN, lx, ly)) return {Hit::Flip, side, 0};
  if (inPadded(COIN_BACK_BTN, lx, ly)) return {Hit::CoinBack, side, 0};
  return NO_TOUCH;
}

// ---------------------------------------------------------------- actions
PokemonGame& game() { return g_state.pokemon; }

void useSide(uint8_t side) {
  s_usedAt[side] = millis();
  game().selected = side;
}

void openSlot(uint8_t side, int8_t slot) {
  s_open[side] = slot;
  s_usedAt[side] = millis();
}

void adjust(uint8_t side, int steps) {
  pokemonAdjust(game(), side, s_open[side], steps * POKEMON_DAMAGE_STEP);
  s_usedAt[side] = millis();
}

void startCoin() {
  s_coin = true;
  s_coinFlipping = true;
  s_coinResult = (uint8_t)random(0, 2);  // hardware RNG on the ESP32
  s_coinTick = 0;
  s_coinStart = s_coinNext = millis();
  s_press = NO_TOUCH;
}

void tapTable(const Touch& t) {
  PokemonGame& g = game();
  switch (t.what) {
    case Hit::Psn: pokemonToggleStatus(g, t.side, POKEMON_PSN); break;
    case Hit::Brn: pokemonToggleStatus(g, t.side, POKEMON_BRN); break;
    case Hit::Ko:  pokemonKnockOut(g, t.side, s_open[t.side]); break;
    case Hit::Back: openSlot(t.side, POKEMON_ACTIVE); break;
    case Hit::Swap:
      if (s_open[t.side] != POKEMON_ACTIVE) pokemonSwap(g, t.side, (uint8_t)s_open[t.side]);
      openSlot(t.side, POKEMON_ACTIVE);
      break;
    case Hit::Bench:  // open it, or close it when it is already open
      openSlot(t.side, s_open[t.side] == (int8_t)t.slot ? POKEMON_ACTIVE : (int8_t)t.slot);
      break;
    case Hit::Coin: startCoin(); break;
    case Hit::Menu: goToScreen(SCREEN_POKEMON_SETUP); break;
    default: break;
  }
}

// ---------------------------------------------------------------- drawing
struct HalfView {
  int16_t value;               // damage of what is open
  int8_t  open;
  uint8_t status;
  int16_t bench[POKEMON_BENCH];
  bool    selected;
  Hit     pressed;
  bool operator==(const HalfView& o) const {
    if (value != o.value || open != o.open || status != o.status || selected != o.selected ||
        pressed != o.pressed)
      return false;
    for (uint8_t k = 0; k < POKEMON_BENCH; ++k)
      if (bench[k] != o.bench[k]) return false;
    return true;
  }
  bool operator!=(const HalfView& o) const { return !(*this == o); }
};

HalfView viewOf(uint8_t side) {
  const PokemonSide& s = game().side[side];
  HalfView v;
  v.open = s_open[side];
  v.value = pokemonDamage(game(), side, v.open);
  v.status = s.status;
  for (uint8_t k = 0; k < POKEMON_BENCH; ++k) v.bench[k] = s.bench[k];
  v.selected = game().selected == side;
  const bool held = s_press.side == side && (s_press.what == Hit::Minus || s_press.what == Hit::Plus ||
                                             s_press.what == Hit::Ko || s_press.what == Hit::Swap);
  v.pressed = held ? s_press.what : Hit::None;
  return v;
}

struct HalfJob { uint8_t side; const HalfView* v; };

void rbox(lgfx::LovyanGFX& c, int ox, int oy, const Rect& r, int rad, uint16_t fill) {
  c.fillRoundRect(ox + r.x, oy + r.y, r.w, r.h, rad, fill);
}

void label(lgfx::LovyanGFX& c, int x, int y, const char* t, const lgfx::IFont* f, uint16_t col,
           lgfx::textdatum_t datum = lgfx::textdatum_t::middle_center) {
  c.setFont(f);
  c.setTextDatum(datum);
  c.setTextColor(col);
  c.drawString(t, x, y);
}

void swapIcon(lgfx::LovyanGFX& c, int cx, int cy, uint16_t col) {  // ⇅
  c.fillRect(cx - 6, cy - 6, 3, 12, col);
  c.fillTriangle(cx - 10, cy - 3, cx - 1, cy - 3, cx - 5, cy - 9, col);
  c.fillRect(cx + 4, cy - 6, 3, 12, col);
  c.fillTriangle(cx + 1, cy + 3, cx + 10, cy + 3, cx + 5, cy + 9, col);
}

void bigNumber(lgfx::LovyanGFX& c, int cx, int cy, int v) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", v);
  uint8_t n = 0;
  const theme::NumberFont& f = theme::numberFonts(n)[1];  // 36 px digits
  c.setFont(f.font);
  c.setTextDatum(lgfx::textdatum_t::top_center);
  c.setTextColor(theme::TEXT);
  const int y = cy - f.top - f.height / 2;
  c.drawString(buf, cx, y);
  if (theme::LIFE_FAUX_BOLD) c.drawString(buf, cx + 1, y);
}

void paintHalf(lgfx::LovyanGFX& c, int ox, int oy, const void* ctx) {
  const HalfJob& j = *static_cast<const HalfJob*>(ctx);
  const HalfView& v = *j.v;
  const uint16_t pc = theme::PLAYER[j.side];
  c.fillRect(ox, oy, HALF_W, HALF_H, theme::BG);
  c.fillRoundRect(ox, oy, HALF_W, HALF_H, 10, theme::PANEL);
  uiRoundFrame(c, ox, oy, HALF_W, HALF_H, 10, v.selected ? 4 : 1, v.selected ? pc : theme::PANEL_EDGE);

  const bool bench = v.open != POKEMON_ACTIVE;
  if (!bench) {
    const bool psn = v.status & POKEMON_PSN, brn = v.status & POKEMON_BRN;
    rbox(c, ox, oy, PSN_CHIP, PSN_CHIP.h / 2, psn ? PSN_COL : theme::BUTTON);
    label(c, ox + PSN_CHIP.cx(), oy + PSN_CHIP.cy() + 1, "PSN", theme::fontLabel(), psn ? theme::TEXT_ON_ACCENT : theme::TEXT_DIM);
    rbox(c, ox, oy, BRN_CHIP, BRN_CHIP.h / 2, brn ? BRN_COL : theme::BUTTON);
    label(c, ox + BRN_CHIP.cx(), oy + BRN_CHIP.cy() + 1, "BRN", theme::fontLabel(), brn ? theme::TEXT_ON_ACCENT : theme::TEXT_DIM);
  } else {
    rbox(c, ox, oy, BACK_BTN, BACK_BTN.h / 2, theme::BUTTON);
    uiChevron(c, ox + BACK_BTN.cx() - 1, oy + BACK_BTN.cy(), 10, 3, false, theme::TEXT);
    const bool down = v.pressed == Hit::Swap;
    rbox(c, ox, oy, SWAP_BTN, 8, down ? theme::ACCENT : SWAP_FILL);
    const uint16_t ink = down ? theme::TEXT_ON_ACCENT : theme::TEXT;
    swapIcon(c, ox + SWAP_BTN.x + 20, oy + SWAP_BTN.cy(), ink);
    label(c, ox + SWAP_BTN.x + 36, oy + SWAP_BTN.cy() + 1, "SWAP", theme::fontLabel(), ink, lgfx::textdatum_t::middle_left);
  }
  rbox(c, ox, oy, KO_BTN, 8, v.pressed == Hit::Ko ? theme::DANGER_FILL_DOWN : theme::DANGER_FILL);
  label(c, ox + KO_BTN.cx(), oy + KO_BTN.cy() + 1, "KO", theme::fontLabel(), theme::TEXT);

  bigNumber(c, ox + NUM_CX, oy + NUM_CY, v.value);
  char cap[12];
  if (bench) snprintf(cap, sizeof(cap), "BENCH %d", v.open + 1);
  else       snprintf(cap, sizeof(cap), "ACTIVE");
  label(c, ox + NUM_CX, oy + CAPTION_Y, cap, bench ? theme::fontLabel() : theme::fontSmall(),
        bench ? pc : theme::TEXT_DIM);

  const bool minusDown = v.pressed == Hit::Minus, plusDown = v.pressed == Hit::Plus;
  rbox(c, ox, oy, MINUS, 10, minusDown ? pc : theme::BUTTON);
  uiMinus(c, ox + MINUS.cx(), oy + MINUS.cy(), 22, 5, minusDown ? theme::TEXT_ON_ACCENT : theme::TEXT);
  rbox(c, ox, oy, PLUS, 10, plusDown ? pc : theme::BUTTON);
  uiPlus(c, ox + PLUS.cx(), oy + PLUS.cy(), 22, 5, plusDown ? theme::TEXT_ON_ACCENT : theme::TEXT);

  for (int k = 0; k < POKEMON_BENCH; ++k) {  // bench row: slot number on top, damage under it
    const Rect r = benchCell(k);
    const bool isOpen = v.open == k;
    rbox(c, ox, oy, r, 8, isOpen ? pc : theme::BUTTON);
    char n[4], d[8];
    snprintf(n, sizeof(n), "%d", k + 1);
    snprintf(d, sizeof(d), "%d", v.bench[k]);
    label(c, ox + r.cx(), oy + r.y + 9, n, theme::fontSmall(), isOpen ? theme::TEXT_ON_ACCENT : theme::TEXT_DIM);
    label(c, ox + r.cx(), oy + r.y + 27, d, v.bench[k] >= 100 ? theme::fontLabel() : theme::fontButton(),
          isOpen ? theme::TEXT_ON_ACCENT : (v.bench[k] ? theme::TEXT : theme::PANEL_EDGE));
  }
}

// coin page: both halves the same, each facing its player
struct CoinView {
  bool    flipping;
  uint8_t tick, result;
  Hit     pressed[2];
  bool operator==(const CoinView& o) const {
    return flipping == o.flipping && tick == o.tick && result == o.result &&
           pressed[0] == o.pressed[0] && pressed[1] == o.pressed[1];
  }
  bool operator!=(const CoinView& o) const { return !(*this == o); }
};
struct CoinJob { uint8_t side; const CoinView* v; };

void paintCoin(lgfx::LovyanGFX& c, int ox, int oy, const void* ctx) {
  const CoinJob& j = *static_cast<const CoinJob*>(ctx);
  const CoinView& v = *j.v;
  c.fillRect(ox, oy, HALF_W, HALF_H, theme::BG);
  c.fillRoundRect(ox, oy, HALF_W, HALF_H, 10, theme::PANEL);
  uiRoundFrame(c, ox, oy, HALF_W, HALF_H, 10, 2, theme::ACCENT);
  // the coin: squashed while it spins, round once it lands
  static const uint8_t SQUASH[4] = {42, 28, 8, 28};
  const int rx = v.flipping ? SQUASH[v.tick % 4] : COIN_R;
  const uint8_t face = v.flipping ? (uint8_t)((v.tick / 2) % 2) : v.result;
  const int cx = ox + COIN_CX, cy = oy + COIN_CY;
  c.fillEllipse(cx, cy, rx, COIN_R, theme::ACCENT);
  if (rx > 12) {
    c.fillEllipse(cx, cy, rx - 8, COIN_R - 8, COIN_FILL);
    c.drawEllipse(cx, cy, rx - 8, COIN_R - 8, COIN_DARK);
  }
  if (rx > 24) label(c, cx, cy + 2, face ? "T" : "H", theme::fontHuge(), theme::TEXT_ON_ACCENT);
  label(c, ox + 174, oy + 34, "COIN FLIP", theme::fontLabel(), theme::ACCENT);
  if (!v.flipping) label(c, ox + 174, oy + 68, v.result ? "TAILS" : "HEADS", theme::fontTitle(), theme::TEXT);
  const Hit down = v.pressed[j.side];
  const uint16_t dim = v.flipping ? theme::PANEL_EDGE : theme::TEXT;
  uiTextButton(c, Rect{(int16_t)(ox + FLIP_BTN.x), (int16_t)(oy + FLIP_BTN.y), FLIP_BTN.w, FLIP_BTN.h}, "FLIP",
               theme::fontButton(), down == Hit::Flip ? theme::BUTTON_DOWN : theme::BUTTON, dim, 10);
  uiTextButton(c, Rect{(int16_t)(ox + COIN_BACK_BTN.x), (int16_t)(oy + COIN_BACK_BTN.y), COIN_BACK_BTN.w, COIN_BACK_BTN.h},
               "BACK", theme::fontButton(), down == Hit::CoinBack ? theme::BUTTON_DOWN : theme::BUTTON, dim, 10);
}

CoinView coinView() {
  CoinView v;
  v.flipping = s_coinFlipping;
  v.tick = s_coinFlipping ? s_coinTick : 0;
  v.result = s_coinResult;
  for (uint8_t i = 0; i < 2; ++i) v.pressed[i] = s_press.side == i ? s_press.what : Hit::None;
  return v;
}

HalfView s_drawn[2];
CoinView s_drawnCoin;
int8_t   s_drawnPage = -1;   // 0 table, 1 coin
Hit      s_drawnHub = Hit::None;

void drawHubs(Hit pressed) {
  drawHubButton(COIN_X, COIN_Y, HubIcon::Coin, pressed == Hit::Coin);
  drawHubButton(MENU_X, MENU_Y, HubIcon::MenuUpright, pressed == Hit::Menu);
}

// ---------------------------------------------------------------- input
void handleCoinInput(const InputEvent& e) {
  if (s_coinFlipping) { s_press = NO_TOUCH; return; }  // nothing until it lands
  switch (e.type) {
    case InputType::TouchDown: s_press = hitCoin(e.x, e.y); break;
    case InputType::TouchUp: {
      const Touch t = s_press;
      s_press = NO_TOUCH;
      if (!isTap(e, TOUCH_TAP_MAX_MS)) break;
      if (t.what == Hit::Flip) startCoin();
      else if (t.what == Hit::CoinBack) s_coin = false;
      break;
    }
    case InputType::EncoderClick:     startCoin(); break;
    case InputType::EncoderLongPress: s_coin = false; break;
    default: break;
  }
}

// ---------------------------------------------------------------- module functions
void onEnter() {
  s_press = NO_TOUCH;
  s_open[0] = s_open[1] = POKEMON_ACTIVE;
  s_coin = s_coinFlipping = false;
  if (s_coinOnEnter) {  // menu -> COIN FLIP
    s_coinOnEnter = false;
    startCoin();
  }
}

void handleInput(const InputEvent& e) {
  if (s_coin) { handleCoinInput(e); return; }
  PokemonGame& g = game();
  switch (e.type) {
    case InputType::EncoderTurn:
      adjust(g.selected, e.delta);
      break;
    case InputType::EncoderClick:
      g.selected = (uint8_t)(1 - g.selected);
      break;
    case InputType::EncoderLongPress:
      openSlot(g.selected, POKEMON_ACTIVE);
      break;
    case InputType::TouchDown:
      s_press = hitTable(e.x, e.y);
      if (s_press.what != Hit::Coin && s_press.what != Hit::Menu) useSide(s_press.side);
      if (s_press.what == Hit::Minus) adjust(s_press.side, -1);
      if (s_press.what == Hit::Plus)  adjust(s_press.side, +1);
      break;
    case InputType::TouchRepeat:  // hold-to-repeat on − / +
      if (s_press.what == Hit::Minus) adjust(s_press.side, -1);
      if (s_press.what == Hit::Plus)  adjust(s_press.side, +1);
      break;
    case InputType::TouchUp: {
      const Touch t = s_press;
      s_press = NO_TOUCH;
      if (isTap(e, TOUCH_TAP_MAX_MS)) tapTable(t);
      break;
    }
    default:
      break;
  }
}

void tick(uint32_t now) {
  if (s_coin && s_coinFlipping) {
    const uint32_t elapsed = now - s_coinStart;
    if (elapsed >= (uint32_t)COIN_FLIP_MS) {
      s_coinFlipping = false;  // landed on s_coinResult
    } else if ((int32_t)(now - s_coinNext) >= 0) {
      ++s_coinTick;  // spins fast, then slows down
      s_coinNext = now + 40 + 160 * elapsed / COIN_FLIP_MS;
    }
    return;
  }
  if (POKEMON_BENCH_TIMEOUT_MS == 0 || s_press.what != Hit::None) return;
  for (uint8_t i = 0; i < 2; ++i)
    if (s_open[i] != POKEMON_ACTIVE && now - s_usedAt[i] >= (uint32_t)POKEMON_BENCH_TIMEOUT_MS)
      s_open[i] = POKEMON_ACTIVE;
}

void render(bool full) {
  auto& g = gfx();
  const int8_t page = s_coin ? 1 : 0;
  full = full || page != s_drawnPage;
  s_drawnPage = page;
  if (page == 1) {
    const CoinView v = coinView();
    if (!full && v == s_drawnCoin) return;
    g.startWrite();
    if (full) g.fillScreen(theme::BG);
    for (uint8_t i = 0; i < 2; ++i) {
      const CoinJob job = {i, &v};
      drawSeatCard(SEATS[i], paintCoin, &job);
    }
    g.endWrite();
    s_drawnCoin = v;
    return;
  }
  const Hit hub = (s_press.what == Hit::Coin || s_press.what == Hit::Menu) ? s_press.what : Hit::None;
  bool hubDirty = full || hub != s_drawnHub;
  bool started = false;
  if (full) {
    g.startWrite();
    started = true;
    g.fillScreen(theme::BG);
  }
  for (uint8_t i = 0; i < 2; ++i) {
    const HalfView v = viewOf(i);
    if (!full && v == s_drawn[i]) continue;
    if (!started) { g.startWrite(); started = true; }
    const HalfJob job = {i, &v};
    drawSeatCard(SEATS[i], paintHalf, &job);
    s_drawn[i] = v;
    hubDirty = true;  // the halves' edges lie under the round buttons
  }
  if (hubDirty) {
    if (!started) { g.startWrite(); started = true; }
    drawHubs(hub);
    s_drawnHub = hub;
  }
  if (started) g.endWrite();
}

}  // namespace

const ScreenModule PokemonScreen = {"Pokemon", onEnter, handleInput, render, tick};

void pokemonRequestCoinFlip() { s_coinOnEnter = true; }
