#pragma once
/* ============================================================================
 *  AppStorage — everything that touches NVS flash (ESP32 Preferences).
 *
 *  Game state uses a delayed-write strategy: a change restarts a
 *  SAVE_DELAY_MS timer, and only when the state has been quiet that long is
 *  it written — and then only the keys whose values actually changed.
 *  Spinning the encoder 30 clicks = one flash write, not 30.
 * ==========================================================================*/
#include <stdint.h>

void loadState();          // fill g_state from flash, or defaults if nothing saved
void saveStateIfNeeded();  // call every loop; writes ~SAVE_DELAY_MS after the last change
void saveStateNow();       // flush immediately (future: before sleep / low battery)

// Touch calibration lives in its own namespace so a future "reset game"
// can never wipe it.
bool loadTouchCalibration(uint16_t cal[8]);
void saveTouchCalibration(const uint16_t cal[8]);
