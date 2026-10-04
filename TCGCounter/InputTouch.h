#pragma once
/* ============================================================================
 *  InputTouch — XPT2046 resistive touch → debounced InputEvents.
 *
 *  - press accepted after TOUCH_PRESS_SAMPLES consecutive readings
 *  - release accepted after TOUCH_RELEASE_MS without a reading
 *  - while held, TouchRepeat fires (used for hold-to-repeat on +/-)
 *  - TouchUp reports the ORIGINAL press point (resistive panels report
 *    noisy coordinates as pressure fades, so the lift point is unreliable)
 * ==========================================================================*/

#include <stdint.h>
#include "Config.h"
#include "InputEvents.h"

// Swipe classification, kept pure so it can be unit-tested:
// SWIPE_LEFT / SWIPE_RIGHT / SWIPE_UP / SWIPE_DOWN, or 0 = not a straight swipe
// (too short, or diagonal).
inline int16_t classifySwipe(int dx, int dy) {
  const int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
  if (adx >= TOUCH_SWIPE_MIN_PX && adx >= 2 * ady) return dx < 0 ? SWIPE_LEFT : SWIPE_RIGHT;
  if (ady >= TOUCH_SWIPE_MIN_PX && ady >= 2 * adx) return dy < 0 ? SWIPE_UP : SWIPE_DOWN;
  return 0;
}
// True once the finger has moved far enough that this can't be a tap/hold.
inline bool touchMoved(int dx, int dy) {
  return dx * dx + dy * dy >= TOUCH_MOVE_CANCEL_PX * TOUCH_MOVE_CANCEL_PX;
}

void setupTouch();           // restore calibration; boot-time recalibration window
void updateTouch();          // call every loop
void runTouchCalibration();  // interactive 4-corner calibration, saved to NVS
