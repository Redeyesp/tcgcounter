#include "HighRoll.h"
#include "Config.h"

namespace {

// Show-only faces: xorshift32, seeded from a real roll at every start.
uint8_t shuffleFace(HighRoll& h, uint8_t avoid) {
  for (;;) {
    uint32_t x = h.shuffle;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    h.shuffle = x ? x : 0x9E3779B9u;
    const uint8_t face = (uint8_t)(1 + (h.shuffle % HIGHROLL_SIDES));
    if (face != avoid) return face;  // every flip visibly changes the face
  }
}

void beginRound(HighRoll& h, uint32_t now) {
  h.phase = RollPhase::Rolling;
  h.phaseStart = now;
  h.nextFlip = now;  // first flip on the next update
  ++h.round;
}

// Flip interval grows as the roll goes on: fast at first, then settling.
uint32_t flipInterval(uint32_t elapsed) {
  const uint32_t span = HIGHROLL_FLIP_SLOW_MS - HIGHROLL_FLIP_FAST_MS;
  const uint32_t t = elapsed < HIGHROLL_ROLL_MS ? elapsed : HIGHROLL_ROLL_MS;
  return HIGHROLL_FLIP_FAST_MS + span * t / HIGHROLL_ROLL_MS;
}

}  // namespace

void highRollStart(HighRoll& h, uint8_t players, uint32_t now, RollFn rollD20) {
  if (players > HIGHROLL_MAX_PLAYERS) players = HIGHROLL_MAX_PLAYERS;
  h = HighRoll();
  h.players = players;
  h.shuffle = 0x2545F491u ^ ((uint32_t)rollD20() << 16) ^ ((uint32_t)rollD20() << 8) ^ now;
  for (uint8_t p = 0; p < players; ++p) {
    h.inRoll[p] = true;
    h.value[p] = shuffleFace(h, 0);
  }
  beginRound(h, now);
}

void highRollStop(HighRoll& h) { h.phase = RollPhase::Idle; }

bool highRollUpdate(HighRoll& h, uint32_t now, RollFn rollD20) {
  switch (h.phase) {
    case RollPhase::Idle:
    case RollPhase::Result:
      return false;

    case RollPhase::Tie:
      if (now - h.phaseStart < HIGHROLL_TIE_MS) return false;
      for (uint8_t p = 0; p < h.players; ++p) h.inRoll[p] = h.tied[p];  // only the tied roll again
      beginRound(h, now);
      return true;

    case RollPhase::Rolling: {
      const uint32_t elapsed = now - h.phaseStart;
      if (elapsed < HIGHROLL_ROLL_MS) {
        if ((int32_t)(now - h.nextFlip) < 0) return false;
        for (uint8_t p = 0; p < h.players; ++p)
          if (h.inRoll[p]) h.value[p] = shuffleFace(h, h.value[p]);
        h.nextFlip = now + flipInterval(elapsed);
        return true;
      }
      // The dice land: real results, then find the best.
      uint8_t best = 0;
      for (uint8_t p = 0; p < h.players; ++p) {
        if (!h.inRoll[p]) continue;
        uint8_t r = rollD20();
        if (r < 1) r = 1;
        if (r > HIGHROLL_SIDES) r = HIGHROLL_SIDES;
        h.value[p] = r;
        if (r > best) best = r;
      }
      uint8_t atTop = 0;
      for (uint8_t p = 0; p < h.players; ++p) {
        h.tied[p] = h.inRoll[p] && h.value[p] == best;
        if (h.tied[p]) { ++atTop; h.winner = p; }
      }
      h.phaseStart = now;
      h.phase = atTop == 1 ? RollPhase::Result : RollPhase::Tie;
      return true;
    }
  }
  return false;
}

DieState highRollDie(const HighRoll& h, uint8_t p) {
  if (p >= h.players) return DieState::None;
  switch (h.phase) {
    case RollPhase::Idle:    return DieState::None;
    case RollPhase::Rolling: return h.inRoll[p] ? DieState::Rolling : DieState::Out;
    case RollPhase::Tie:     return h.tied[p] ? DieState::Tied : DieState::Out;
    case RollPhase::Result:  return p == h.winner ? DieState::Winner : DieState::Out;
  }
  return DieState::None;
}
