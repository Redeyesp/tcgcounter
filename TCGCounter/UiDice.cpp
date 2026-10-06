#include "UiDice.h"
#include <Arduino.h>
#include <stdio.h>
#include "Gfx.h"
#include "Theme.h"
#include "Ui.h"
#include "TableDraw.h"
#include "Config.h"

namespace {

constexpr uint8_t DICE[] = {4, 6, 8, 12, 20};
constexpr int8_t  DIE_COUNT = sizeof(DICE) / sizeof(DICE[0]);

// ---- chooser page: 0 = BACK, 1..5 = dice
constexpr int8_t CH_BACK = 0, CH_ITEMS = 1 + DIE_COUNT;
constexpr Rect   BACK_BTN = {12, 6, 92, 34};
constexpr int    DIE_X0 = 11, DIE_Y = 56, DIE_W = 54, DIE_H = 124, DIE_PITCH = 61;
constexpr int    ICON_CY = 104, ICON_R = 21, LABEL_Y = 160, HINT_Y = 212;

Rect chooserRect(int8_t i) {
  if (i == CH_BACK) return BACK_BTN;
  return Rect{(int16_t)(DIE_X0 + (i - 1) * DIE_PITCH), (int16_t)DIE_Y, (int16_t)DIE_W, (int16_t)DIE_H};
}

// ---- popup: 0 = REROLL, 1 = BACK
constexpr int8_t POP_REROLL = 0, POP_BACK = 1, POP_ITEMS = 2;
constexpr Rect   POPUP = {24, 14, 272, 212};
constexpr Rect   POP_BTN[POP_ITEMS] = {{40, 164, 112, 46}, {168, 164, 112, 46}};
constexpr Rect   DIE_AREA = {96, 46, 128, 114};  // redrawn off-screen while the die rolls
constexpr int    POP_DIE_R = 48, POP_TITLE_Y = 32;

int8_t hit(const Rect* rects, int8_t n, int x, int y) {
  for (int8_t i = 0; i < n; ++i)
    if (rects[i].contains(x, y)) return i;
  return -1;
}

int8_t chooserHit(int x, int y) {
  for (int8_t i = 0; i < CH_ITEMS; ++i)
    if (chooserRect(i).contains(x, y)) return i;
  return -1;
}

// Show-only faces (xorshift32): the real result comes from random() on landing.
uint8_t shuffleFace(uint32_t& s, uint8_t sides, uint8_t avoid) {
  for (;;) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    if (!s) s = 0x9E3779B9u;
    const uint8_t f = (uint8_t)(1 + s % sides);
    if (f != avoid || sides < 2) return f;
  }
}

struct DieJob { uint8_t sides, value; bool rolling; };

void paintDie(lgfx::LovyanGFX& c, int ox, int oy, const void* ctx) {
  const DieJob& j = *static_cast<const DieJob*>(ctx);
  c.fillRect(ox, oy, DIE_AREA.w, DIE_AREA.h, theme::PANEL);
  drawDie(c, ox + DIE_AREA.w / 2, oy + DIE_AREA.h / 2, POP_DIE_R, j.sides,
          j.rolling ? DieState::Rolling : DieState::Winner, theme::TEXT, j.value);
}

}  // namespace

void DiceOverlay::open(bool standalone) {
  open_ = true;
  standalone_ = standalone;
  popup_ = false;
  rolling_ = false;
  pressed_ = -1;
  needFull_ = true;
}

void DiceOverlay::startRoll(uint8_t sides) {
  sides_ = sides;
  popup_ = true;
  rolling_ = true;
  pressed_ = -1;
  popupFocus_ = POP_REROLL;
  rollStart_ = nextFlip_ = millis();
  shuffle_ ^= (uint32_t)random(1, 0x7FFFFFFF) ^ rollStart_;
  value_ = shuffleFace(shuffle_, sides_, 0);
}

void DiceOverlay::tick(uint32_t now) {
  if (!open_ || !rolling_) return;
  const uint32_t elapsed = now - rollStart_;
  if (elapsed >= DICE_ROLL_MS) {  // land on the real roll
    value_ = (uint8_t)random(1, sides_ + 1);
    rolling_ = false;
    return;
  }
  if ((int32_t)(now - nextFlip_) < 0) return;
  value_ = shuffleFace(shuffle_, sides_, value_);
  const uint32_t span = HIGHROLL_FLIP_SLOW_MS - HIGHROLL_FLIP_FAST_MS;
  nextFlip_ = now + HIGHROLL_FLIP_FAST_MS + span * elapsed / DICE_ROLL_MS;  // slowing down
}

DiceResult DiceOverlay::handleInput(const InputEvent& e) {
  if (!open_) return DiceResult::None;

  if (!popup_) {  // ---------------------------------------------- chooser
    int8_t chosen = -1;
    switch (e.type) {
      case InputType::EncoderTurn: {
        int f = (chooserFocus_ + e.delta) % CH_ITEMS;
        if (f < 0) f += CH_ITEMS;
        chooserFocus_ = (int8_t)f;
        break;
      }
      case InputType::EncoderClick:     chosen = chooserFocus_; break;
      case InputType::EncoderLongPress: chosen = CH_BACK; break;
      case InputType::TouchDown:
        pressed_ = chooserHit(e.x, e.y);
        if (pressed_ >= 0) chooserFocus_ = pressed_;
        break;
      case InputType::TouchUp: {
        const int8_t released = pressed_;
        pressed_ = -1;
        if (released >= 0 && isTap(e, TOUCH_TAP_MAX_MS)) chosen = released;
        break;
      }
      default: break;
    }
    if (chosen == CH_BACK) { open_ = false; return DiceResult::Back; }
    if (chosen > 0) startRoll(DICE[chosen - 1]);
    return DiceResult::None;
  }

  // ------------------------------------------------------------ popup
  if (rolling_) { pressed_ = -1; return DiceResult::None; }  // wait for the die to land
  int8_t chosen = -1;
  switch (e.type) {
    case InputType::EncoderTurn:
      if (e.delta % 2 != 0) popupFocus_ = (int8_t)(1 - popupFocus_);
      break;
    case InputType::EncoderClick:     chosen = popupFocus_; break;
    case InputType::EncoderLongPress: popup_ = false; break;  // back to the chooser
    case InputType::TouchDown:
      if (!POPUP.contains(e.x, e.y)) { popup_ = false; pressed_ = -1; break; }  // outside: chooser
      pressed_ = hit(POP_BTN, POP_ITEMS, e.x, e.y);
      if (pressed_ >= 0) popupFocus_ = pressed_;
      break;
    case InputType::TouchUp: {
      const int8_t released = pressed_;
      pressed_ = -1;
      if (released >= 0 && isTap(e, TOUCH_TAP_MAX_MS)) chosen = released;
      break;
    }
    default: break;
  }
  if (chosen == POP_REROLL) startRoll(sides_);
  else if (chosen == POP_BACK && standalone_) popup_ = false;  // pick another die
  else if (chosen == POP_BACK) { open_ = false; return DiceResult::Back; }
  return DiceResult::None;
}

// ---------------------------------------------------------------- drawing
void DiceOverlay::drawChooserButton(int8_t i) {
  auto& g = gfx();
  const Rect r = chooserRect(i);
  const bool pressed = (i == pressed_ && !popup_);
  const uint16_t fill = pressed ? theme::BUTTON_DOWN : theme::BUTTON;
  const int radius = i == CH_BACK ? 10 : 12;
  g.fillRect(r.x, r.y, r.w, r.h, theme::BG);
  g.fillRoundRect(r.x, r.y, r.w, r.h, radius, fill);
  if (i == chooserFocus_) uiRoundFrame(g, r.x, r.y, r.w, r.h, radius, 3, theme::ACCENT);
  g.setTextDatum(lgfx::textdatum_t::middle_center);
  g.setTextColor(theme::TEXT);
  if (i == CH_BACK) {
    uiChevron(g, r.x + 18, r.cy(), 14, 3, false, theme::TEXT);
    g.setFont(theme::fontLabel());
    g.setTextDatum(lgfx::textdatum_t::middle_left);
    g.drawString(standalone_ ? "HOME" : "BACK", r.x + 32, r.cy() + 1);
    return;
  }
  const uint8_t sides = DICE[i - 1];
  drawDie(g, r.cx(), ICON_CY, ICON_R, sides, DieState::Rolling, theme::TEXT, sides);
  char label[6];
  snprintf(label, sizeof(label), "D%u", (unsigned)sides);
  g.setFont(theme::fontButton());
  g.setTextDatum(lgfx::textdatum_t::middle_center);
  g.setTextColor(theme::TEXT);
  g.drawString(label, r.cx(), LABEL_Y);
}

void DiceOverlay::drawChooser() {
  auto& g = gfx();
  g.fillScreen(theme::BG);
  g.setFont(theme::fontButton());
  g.setTextDatum(lgfx::textdatum_t::middle_right);
  g.setTextColor(theme::TEXT_DIM);
  g.drawString("DICE", 308, BACK_BTN.cy() + 1);
  g.setFont(theme::fontSmall());
  g.setTextDatum(lgfx::textdatum_t::middle_center);
  g.drawString("Tap a die to roll it", 160, HINT_Y);
  for (int8_t i = 0; i < CH_ITEMS; ++i) drawChooserButton(i);
}

void DiceOverlay::drawPopupButton(int8_t i) {
  auto& g = gfx();
  const Rect& r = POP_BTN[i];
  const bool pressed = (i == pressed_);
  const uint16_t fill = pressed ? theme::BUTTON_DOWN : theme::BUTTON;
  g.fillRect(r.x, r.y, r.w, r.h, theme::PANEL);
  uiTextButton(g, r, i == POP_REROLL ? "REROLL" : "BACK", theme::fontButton(), fill,
               rolling_ ? theme::TEXT_DIM : theme::TEXT, 10);
  if (i == popupFocus_ && !rolling_) uiRoundFrame(g, r.x, r.y, r.w, r.h, 10, 3, theme::ACCENT);
}

void DiceOverlay::drawPopupDie() {
  const DieJob job = {sides_, value_, rolling_};
  const Seat area = {DIE_AREA, Side::Bottom};
  drawSeatCard(area, paintDie, &job);  // off-screen, one copy: no flicker while it rolls
}

void DiceOverlay::drawPopup() {
  auto& g = gfx();
  g.fillRoundRect(POPUP.x, POPUP.y, POPUP.w, POPUP.h, 14, theme::PANEL);
  uiRoundFrame(g, POPUP.x, POPUP.y, POPUP.w, POPUP.h, 14, 2, theme::ACCENT);
  char title[6];
  snprintf(title, sizeof(title), "D%u", (unsigned)sides_);
  g.setFont(theme::fontButton());
  g.setTextDatum(lgfx::textdatum_t::middle_center);
  g.setTextColor(theme::ACCENT);
  g.drawString(title, 160, POP_TITLE_Y);
  drawPopupDie();
  for (int8_t i = 0; i < POP_ITEMS; ++i) drawPopupButton(i);
}

void DiceOverlay::render(bool full) {
  if (!open_) return;
  auto& g = gfx();
  const int8_t page = popup_ ? 2 : 1;
  const int8_t focus = popup_ ? popupFocus_ : chooserFocus_;
  const bool whole = full || needFull_ || page != drawnPage_;

  if (whole) {
    g.startWrite();
    if (page == 1 || drawnPage_ != 1 || full || needFull_) drawChooser();  // popup sits on the chooser
    if (page == 2) drawPopup();
    g.endWrite();
  } else {
    const bool dieChanged = popup_ && (value_ != drawnValue_ || rolling_ != drawnRolling_);
    const bool buttonsChanged = focus != drawnFocus_ || pressed_ != drawnPressed_ ||
                                (popup_ && rolling_ != drawnRolling_);
    if (!dieChanged && !buttonsChanged) return;
    g.startWrite();
    if (dieChanged) drawPopupDie();
    if (buttonsChanged) {
      if (popup_) {
        for (int8_t i = 0; i < POP_ITEMS; ++i) drawPopupButton(i);
      } else {
        for (int8_t i = 0; i < CH_ITEMS; ++i) {
          const bool was = (i == drawnFocus_ || i == drawnPressed_);
          const bool is  = (i == focus || i == pressed_);
          if (was || is) drawChooserButton(i);
        }
      }
    }
    g.endWrite();
  }
  needFull_ = false;
  drawnPage_ = page;
  drawnFocus_ = focus;
  drawnPressed_ = pressed_;
  drawnValue_ = value_;
  drawnRolling_ = rolling_;
}
