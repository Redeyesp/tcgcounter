#include "UiConfirm.h"
#include <string.h>
#include "Gfx.h"
#include "Theme.h"
#include "Ui.h"
#include "Config.h"

namespace {
constexpr int8_t BTN_CANCEL = 0, BTN_CONFIRM = 1, BTN_COUNT = 2;
constexpr Rect BUTTONS[BTN_COUNT] = {{16, 164, 136, 60}, {168, 164, 136, 60}};

void copyText(char* dst, size_t n, const char* src) {
  strncpy(dst, src ? src : "", n - 1);
  dst[n - 1] = '\0';
}
}  // namespace

void ConfirmDialog::open(const char* title, const char* body, const char* note, const char* confirmLabel) {
  copyText(title_, sizeof(title_), title);
  copyText(body_, sizeof(body_), body);
  copyText(note_, sizeof(note_), note);
  copyText(confirm_, sizeof(confirm_), confirmLabel);
  open_ = true;
  needFull_ = true;
  focus_ = BTN_CANCEL;
  pressed_ = -1;
}

int8_t ConfirmDialog::hitTest(int x, int y) const {
  for (int8_t i = 0; i < BTN_COUNT; ++i)
    if (BUTTONS[i].contains(x, y)) return i;
  return -1;
}

ConfirmResult ConfirmDialog::handleInput(const InputEvent& e) {
  if (!open_) return ConfirmResult::None;
  int8_t chosen = -1;
  switch (e.type) {
    case InputType::EncoderTurn:
      if (e.delta % 2 != 0) focus_ = (int8_t)(1 - focus_);  // two buttons: odd steps switch
      break;
    case InputType::EncoderClick:
      chosen = focus_;
      break;
    case InputType::EncoderLongPress:
      chosen = BTN_CANCEL;
      break;
    case InputType::TouchDown:
      pressed_ = hitTest(e.x, e.y);
      if (pressed_ >= 0) focus_ = pressed_;
      break;
    case InputType::TouchUp: {
      const int8_t released = pressed_;
      pressed_ = -1;
      if (released >= 0 && isTap(e, TOUCH_TAP_MAX_MS)) chosen = released;
      break;
    }
    default:
      break;
  }
  if (chosen < 0) return ConfirmResult::None;
  open_ = false;
  return chosen == BTN_CONFIRM ? ConfirmResult::Confirm : ConfirmResult::Cancel;
}

void ConfirmDialog::drawButton(int8_t i) {
  auto& g = gfx();
  const Rect& r = BUTTONS[i];
  const bool pressed = (i == pressed_);
  uint16_t fill;
  if (i == BTN_CONFIRM) fill = pressed ? theme::DANGER_FILL_DOWN : theme::DANGER_FILL;
  else                  fill = pressed ? theme::BUTTON_DOWN : theme::BUTTON;
  g.fillRect(r.x, r.y, r.w, r.h, theme::BG);
  uiTextButton(g, r, i == BTN_CONFIRM ? confirm_ : "CANCEL", theme::fontButton(), fill, theme::TEXT, 12);
  if (i == focus_) uiRoundFrame(g, r.x, r.y, r.w, r.h, 12, 3, theme::ACCENT);
}

void ConfirmDialog::render(bool full) {
  if (!open_) return;
  const bool whole = full || needFull_;
  if (!whole && focus_ == drawnFocus_ && pressed_ == drawnPressed_) return;  // nothing changed
  auto& g = gfx();
  g.startWrite();
  if (whole) {
    g.fillScreen(theme::BG);
    g.setTextDatum(lgfx::textdatum_t::middle_center);
    g.setFont(theme::fontTitle());
    g.setTextColor(theme::TEXT);
    g.drawString(title_, 160, 44);
    g.setFont(theme::fontBody());
    g.drawString(body_, 160, 92);
    g.setFont(theme::fontSmall());
    g.setTextColor(theme::DANGER);
    g.drawString(note_, 160, 124);
    for (int8_t i = 0; i < BTN_COUNT; ++i) drawButton(i);
    needFull_ = false;
  } else {
    for (int8_t i = 0; i < BTN_COUNT; ++i) {
      const bool was = (i == drawnFocus_ || i == drawnPressed_);
      const bool is  = (i == focus_ || i == pressed_);
      if (was || is) drawButton(i);
    }
  }
  g.endWrite();
  drawnFocus_ = focus_;
  drawnPressed_ = pressed_;
}
