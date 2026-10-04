#include "InputTouch.h"
#include "AppDisplay.h"
#include "AppStorage.h"
#include "App.h"
#include "InputEvents.h"
#include "Theme.h"
#include "Log.h"

namespace {

enum class TouchState : uint8_t { Idle, Pressed };

TouchState s_state      = TouchState::Idle;
uint8_t    s_candidates = 0;   // consecutive "touching" samples while idle
int32_t    s_sumX = 0, s_sumY = 0;
int16_t    s_downX = 0, s_downY = 0;
int16_t    s_lastX = 0, s_lastY = 0;   // latest good sample while pressed
bool       s_moved = false;             // finger travelled: swipe, not tap/hold
uint32_t   s_lastPoll   = 0;
uint32_t   s_downAt     = 0;
uint32_t   s_lastSeen   = 0;
uint32_t   s_nextRepeat = 0;

bool readTouch(int16_t& x, int16_t& y) {
  lgfx::touch_point_t tp;
  if (lcd().getTouch(&tp, 1) == 0) return false;
  if (tp.size < TOUCH_MIN_PRESSURE) return false;
  x = (int16_t)constrain((int)tp.x, 0, lcd().width() - 1);
  y = (int16_t)constrain((int)tp.y, 0, lcd().height() - 1);
  return true;
}

// Calibration-independent "is anything pressing the panel right now?"
bool rawTouchPresent() {
  lgfx::touch_point_t tp;
  return lcd().getTouchRaw(&tp, 1) > 0;
}

void emit(InputType type, uint16_t duration = 0, int16_t delta = 0) {
  InputEvent e;
  e.type = type;
  e.x = s_downX;
  e.y = s_downY;
  e.durationMs = duration;
  e.delta = delta;
  if (!pushInput(e)) LOGF("[touch] event queue full, dropped\n");
}

void waitForRelease() {
  uint32_t quietSince = millis();
  while (millis() - quietSince < 300) {
    if (rawTouchPresent()) quietSince = millis();
    delay(10);
  }
}

}  // namespace

void runTouchCalibration() {
  LGFX& d = lcd();
  const int cx = d.width() / 2;

  d.fillScreen(theme::BG);
  d.setTextDatum(lgfx::textdatum_t::middle_center);
  d.setFont(theme::fontButton());
  d.setTextColor(theme::TEXT);
  d.drawString("TOUCH CALIBRATION", cx, 80);
  d.setFont(theme::fontSmall());
  d.setTextColor(theme::TEXT_DIM);
  d.drawString("Release the screen, then tap", cx, 116);
  d.drawString("the tip of each arrow as it appears.", cx, 138);
  d.drawString("A stylus or fingernail works best.", cx, 160);

  waitForRelease();
  LOGF("[touch] calibration started\n");

  uint16_t cal[8];
  d.calibrateTouch(cal, theme::ACCENT, theme::BG, 14);  // blocks until 4 corners tapped
  saveTouchCalibration(cal);
  LOGF("[touch] calibration saved: %u %u %u %u %u %u %u %u\n",
       cal[0], cal[1], cal[2], cal[3], cal[4], cal[5], cal[6], cal[7]);

  d.fillScreen(theme::BG);
  d.setFont(theme::fontButton());
  d.setTextColor(theme::TEXT);
  d.drawString("Calibration saved", cx, d.height() / 2);
  waitForRelease();
  delay(600);

  clearInput();
  s_state = TouchState::Idle;
  s_candidates = 0;
  requestFullRedraw();
}

void setupTouch() {
  uint16_t cal[8];
  const bool haveCal = loadTouchCalibration(cal);
  if (haveCal) lcd().setTouchCalibrate(cal);
  LOGF("[touch] calibration %s\n", haveCal ? "restored from NVS" : "not found (using defaults)");

  // Boot window: the boot screen is showing. Touching and holding the panel
  // (or pressing BOOT) during this time forces a recalibration.
  pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);
  bool requested = false;
  uint32_t heldSince = 0;
  const uint32_t start = millis();
  while (millis() - start < SPLASH_MS && !requested) {
    const bool pressed = rawTouchPresent() || digitalRead(PIN_BOOT_BUTTON) == LOW;
    if (!pressed) heldSince = 0;
    else if (heldSince == 0) heldSince = millis();
    else if (millis() - heldSince >= 300) requested = true;
    delay(10);
  }

  if (requested || (!haveCal && TOUCH_CAL_ON_FIRST_BOOT)) runTouchCalibration();
}

void updateTouch() {
  const uint32_t now = millis();
  if (now - s_lastPoll < TOUCH_POLL_MS) return;
  s_lastPoll = now;

  int16_t x = 0, y = 0;
  const bool touching = readTouch(x, y);

  if (s_state == TouchState::Idle) {
    if (!touching) {
      s_candidates = 0;
      s_sumX = s_sumY = 0;
      return;
    }
    s_sumX += x;
    s_sumY += y;
    if (++s_candidates < TOUCH_PRESS_SAMPLES) return;

    s_downX = (int16_t)(s_sumX / s_candidates);
    s_downY = (int16_t)(s_sumY / s_candidates);
    s_candidates = 0;
    s_sumX = s_sumY = 0;
    s_state = TouchState::Pressed;
    s_lastX = s_downX;
    s_lastY = s_downY;
    s_moved = false;
    s_downAt = s_lastSeen = now;
    s_nextRepeat = now + TOUCH_REPEAT_DELAY_MS;
    LOGF("[touch] down x=%d y=%d\n", s_downX, s_downY);
    emit(InputType::TouchDown);
    return;
  }

  // Pressed
  if (touching) {
    s_lastSeen = now;
    s_lastX = x;
    s_lastY = y;
    if (!s_moved && touchMoved(x - s_downX, y - s_downY)) s_moved = true;
    if (!s_moved && (int32_t)(now - s_nextRepeat) >= 0) {
      s_nextRepeat = now + TOUCH_REPEAT_INTERVAL_MS;
      emit(InputType::TouchRepeat);
    }
  } else if (now - s_lastSeen >= TOUCH_RELEASE_MS) {
    s_state = TouchState::Idle;
    const uint32_t held = s_lastSeen - s_downAt;
    const uint16_t held16 = (uint16_t)(held > 65535 ? 65535 : held);
    const int16_t swipe = classifySwipe(s_lastX - s_downX, s_lastY - s_downY);
    if (swipe != 0) {
      LOGF("[touch] swipe %s (dx=%d dy=%d)\n",
           swipe == SWIPE_LEFT ? "left" : swipe == SWIPE_RIGHT ? "right" : swipe == SWIPE_UP ? "up" : "down",
           s_lastX - s_downX, s_lastY - s_downY);
      emit(InputType::TouchSwipe, held16, swipe);
    }
    LOGF("[touch] up (held %lu ms)\n", (unsigned long)held);
    emit(InputType::TouchUp, held16,
         swipe != 0 ? swipe : (s_moved ? TOUCH_UP_DRAGGED : TOUCH_UP_TAP));
  }
}
