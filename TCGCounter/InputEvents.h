#pragma once
/* ============================================================================
 *  InputEvents — one queue that both input devices feed.
 *
 *  Touch and encoder drivers translate hardware activity into InputEvents.
 *  The application only ever sees these events, never raw pins, so both
 *  input methods are always active at the same time and screens don't care
 *  where an action came from.
 * ==========================================================================*/
#include <stdint.h>

enum class InputType : uint8_t {
  TouchDown,         // finger accepted at (x, y)
  TouchRepeat,       // finger still held: auto-repeat tick, (x, y) = original press point
                     // (stops once the finger starts moving: no repeats during a swipe)
  TouchSwipe,        // horizontal swipe finished; delta = -1 left / +1 right,
                     // (x, y) = where it started. Always followed by TouchUp.
  TouchUp,           // finger lifted; (x, y) = original press point, durationMs = hold time,
                     // delta = TOUCH_UP_TAP (0), -1/+1 after a swipe, TOUCH_UP_DRAGGED otherwise
  EncoderTurn,       // delta = +n clockwise / -n counter-clockwise detents
  EncoderClick,      // short press of the encoder switch (fires on release)
  EncoderLongPress,  // switch held >= ENC_LONG_PRESS_MS (reserved; unused in V0.1)
};

struct InputEvent {
  InputType type;
  int16_t   x = 0;
  int16_t   y = 0;
  int16_t   delta = 0;
  uint16_t  durationMs = 0;
};

constexpr int16_t TOUCH_UP_TAP     = 0;  // finger stayed put
constexpr int16_t TOUCH_UP_DRAGGED = 2;  // finger moved, but not a horizontal swipe

// A "tap": TouchUp where the finger didn't travel and wasn't held too long.
inline bool isTap(const InputEvent& e, uint16_t maxMs) {
  return e.type == InputType::TouchUp && e.delta == TOUCH_UP_TAP && e.durationMs <= maxMs;
}

bool pushInput(const InputEvent& e);  // false if the queue is full (event dropped)
bool popInput(InputEvent& out);       // false if empty
void clearInput();
