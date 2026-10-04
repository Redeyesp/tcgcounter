#include "InputEncoder.h"
#include "InputEvents.h"
#include "Log.h"

#if ENC_ENABLED

namespace {

/* Quadrature transition table, index = (previous AB << 2) | current AB.
 * +1 / -1 = one valid quarter-step, 0 = no change or an invalid jump
 * (both bits changed at once = missed edge or bounce -> ignored). */
const int8_t kQuadTable[16] = {
   0, -1, +1,  0,
  +1,  0,  0, -1,
  -1,  0,  0, +1,
   0, +1, -1,  0,
};

// Quarter-steps that must accumulate before the rest state counts as a detent.
// Half of a full detent, so a single missed edge doesn't lose a click.
constexpr int8_t kRestThreshold = (ENC_STEPS_PER_DETENT >= 2) ? ENC_STEPS_PER_DETENT / 2 : 1;

portMUX_TYPE   s_mux     = portMUX_INITIALIZER_UNLOCKED;
volatile uint8_t s_ab      = 0;  // last AB state seen by the ISR
volatile int8_t  s_quarter = 0;  // quarter-steps since the last rest state
volatile int32_t s_detents = 0;  // finished detents not yet consumed by updateEncoder()

// Switch debounce (loop context only)
bool     s_swRaw = false, s_swStable = false, s_longFired = false;
uint32_t s_swChangedAt = 0, s_swPressedAt = 0;

inline uint8_t readAB() {
  return (uint8_t)((digitalRead(ENC_A) ? 2 : 0) | (digitalRead(ENC_B) ? 1 : 0));
}

inline bool isRestState(uint8_t ab) {
  // Typical EC11 with pull-ups rests at A=1,B=1 on every detent.
  // Half-step types (2 steps/detent) also rest at A=0,B=0.
  return ab == 0b11 || (ENC_STEPS_PER_DETENT == 2 && ab == 0b00);
}

void IRAM_ATTR encoderIsr() {
  const uint8_t ab = readAB();
  portENTER_CRITICAL_ISR(&s_mux);
  const uint8_t prev = s_ab;
  if (ab != prev) {
    s_ab = ab;
    const int8_t step = kQuadTable[(prev << 2) | ab];
    if (ENC_STEPS_PER_DETENT == 1) {
      s_detents = s_detents + step;
    } else {
      const int8_t q = (int8_t)(s_quarter + step);
      if (isRestState(ab)) {
        if (q >= kRestThreshold) s_detents = s_detents + 1;
        else if (q <= -kRestThreshold) s_detents = s_detents - 1;
        s_quarter = 0;
      } else {
        s_quarter = q;
      }
    }
  }
  portEXIT_CRITICAL_ISR(&s_mux);
}

void configureInput(int pin, bool wantPullup, const char* name) {
  const bool canPullup = GPIO_HAS_INTERNAL_PULLUP(pin);
  pinMode(pin, (wantPullup && canPullup) ? INPUT_PULLUP : INPUT);
  if (wantPullup && !canPullup)
    LOGF("[encoder] NOTE: %s on GPIO%d has no internal pull-up -> fit an external 10k to 3.3V\n",
         name, pin);
}

bool switchPressedRaw() {
  const int level = digitalRead(ENC_SW);
  return ENC_SW_ACTIVE_LOW ? (level == LOW) : (level == HIGH);
}

void emit(InputType type, int16_t delta = 0) {
  InputEvent e;
  e.type = type;
  e.delta = delta;
  if (!pushInput(e)) LOGF("[encoder] event queue full, dropped\n");
}

}  // namespace

void setupEncoder() {
  configureInput(ENC_A, ENC_AB_INTERNAL_PULLUP, "ENC_A");
  configureInput(ENC_B, ENC_AB_INTERNAL_PULLUP, "ENC_B");
  configureInput(ENC_SW, ENC_SW_INTERNAL_PULLUP, "ENC_SW");
  delay(2);  // let pull-ups settle

  s_ab = readAB();
  s_swRaw = s_swStable = switchPressedRaw();
  s_longFired = s_swStable;  // a switch held at boot must not fire a click
  s_swChangedAt = s_swPressedAt = millis();

  attachInterrupt(digitalPinToInterrupt(ENC_A), encoderIsr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B), encoderIsr, CHANGE);

  LOGF("[encoder] A=GPIO%d B=GPIO%d SW=GPIO%d, %d steps/detent%s, idle AB=%d%d\n",
       ENC_A, ENC_B, ENC_SW, ENC_STEPS_PER_DETENT, ENC_REVERSE ? ", reversed" : "",
       (s_ab >> 1) & 1, s_ab & 1);
}

void updateEncoder() {
  // ---- Rotation: take whatever the ISR has counted ----
  portENTER_CRITICAL(&s_mux);
  int32_t detents = s_detents;
  s_detents = 0;
  portEXIT_CRITICAL(&s_mux);

  if (detents != 0) {
    if (ENC_REVERSE) detents = -detents;
    if (detents > 100) detents = 100;
    if (detents < -100) detents = -100;
    LOGF("[encoder] turn %+ld\n", (long)detents);
    emit(InputType::EncoderTurn, (int16_t)detents);
  }

  // ---- Switch: time-based debounce ----
  const uint32_t now = millis();
  const bool raw = switchPressedRaw();
  if (raw != s_swRaw) {
    s_swRaw = raw;
    s_swChangedAt = now;
  } else if (raw != s_swStable && now - s_swChangedAt >= ENC_BTN_DEBOUNCE_MS) {
    s_swStable = raw;
    if (raw) {  // pressed
      s_swPressedAt = now;
      s_longFired = false;
    } else if (!s_longFired) {  // released after a short press
      LOGF("[encoder] click\n");
      emit(InputType::EncoderClick);
    }
  }
  if (s_swStable && !s_longFired && now - s_swPressedAt >= ENC_LONG_PRESS_MS) {
    s_longFired = true;
    LOGF("[encoder] long press\n");
    emit(InputType::EncoderLongPress);
  }
}

#else  // ENC_ENABLED == 0

void setupEncoder() { LOGF("[encoder] disabled in Config.h\n"); }
void updateEncoder() {}

#endif
