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
  TouchUp,           // finger lifted; (x, y) = original press point, durationMs = hold time
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

bool pushInput(const InputEvent& e);  // false if the queue is full (event dropped)
bool popInput(InputEvent& out);       // false if empty
void clearInput();
