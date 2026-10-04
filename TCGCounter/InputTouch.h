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

// Swipe classification, kept pure so it can be unit-tested:
// -1 = swipe left, +1 = swipe right, 0 = not a horizontal swipe.
inline int8_t classifySwipe(int dx, int dy) {
  const int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
  if (adx < TOUCH_SWIPE_MIN_PX) return 0;
  if (adx < 2 * ady) return 0;  // mostly vertical / diagonal: ignore
  return dx < 0 ? -1 : 1;
}
// True once the finger has moved far enough that this can't be a tap/hold.
inline bool touchMoved(int dx, int dy) {
  return dx * dx + dy * dy >= TOUCH_MOVE_CANCEL_PX * TOUCH_MOVE_CANCEL_PX;
}

void setupTouch();           // restore calibration; boot-time recalibration window
void updateTouch();          // call every loop
void runTouchCalibration();  // interactive 4-corner calibration, saved to NVS
