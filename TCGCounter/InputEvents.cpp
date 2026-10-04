#include "InputEvents.h"

// Producers (updateTouch/updateEncoder) and the consumer (updateApplication)
// all run in loop(), so a plain ring buffer is enough. The encoder ISR never
// touches this queue — it only updates a counter that updateEncoder() reads.
static constexpr uint8_t QUEUE_SIZE = 16;
static InputEvent s_queue[QUEUE_SIZE];
static uint8_t s_head = 0;  // next write
static uint8_t s_tail = 0;  // next read

bool pushInput(const InputEvent& e) {
  uint8_t next = (uint8_t)((s_head + 1) % QUEUE_SIZE);
  if (next == s_tail) return false;
  s_queue[s_head] = e;
  s_head = next;
  return true;
}

bool popInput(InputEvent& out) {
  if (s_tail == s_head) return false;
  out = s_queue[s_tail];
  s_tail = (uint8_t)((s_tail + 1) % QUEUE_SIZE);
  return true;
}

void clearInput() { s_head = s_tail = 0; }
