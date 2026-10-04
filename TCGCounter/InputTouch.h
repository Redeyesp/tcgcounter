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

void setupTouch();           // restore calibration; boot-time recalibration window
void updateTouch();          // call every loop
void runTouchCalibration();  // interactive 4-corner calibration, saved to NVS
