#pragma once
/* ============================================================================
 *  HighRoll — "who goes first": every player rolls a D20, highest wins.
 *
 *  Pure, time-driven logic (no drawing, no pins), shared by every game screen.
 *
 *    Rolling  the dice of the players still in show fast-changing faces that
 *             slow down for HIGHROLL_ROLL_MS, then land on their real result
 *    Tie      several players share the top number: they are marked for
 *             HIGHROLL_TIE_MS, then ONLY they roll again
 *    Result   one winner; stays until the screen dismisses it
 *
 *  The real results come from `rollD20` (the hardware random generator on the
 *  device). The fast-changing faces are only for show and use their own
 *  cheap generator, so they never influence a result.
 * ==========================================================================*/
#include <stdint.h>

constexpr uint8_t HIGHROLL_MAX_PLAYERS = 6;
constexpr uint8_t HIGHROLL_SIDES       = 20;

enum class RollPhase : uint8_t { Idle, Rolling, Tie, Result };

// How one player's die looks right now.
enum class DieState : uint8_t {
  None,     // no high roll running
  Rolling,  // face changing
  Out,      // lower than the best roll (or not in this re-roll): dimmed
  Tied,     // shares the top number: about to roll again
  Winner,   // highest roll
};

using RollFn = uint8_t (*)();  // returns 1..HIGHROLL_SIDES

struct HighRoll {
  RollPhase phase = RollPhase::Idle;
  uint8_t   players = 0;
  uint8_t   value[HIGHROLL_MAX_PLAYERS] = {};    // face shown (final once landed)
  bool      inRoll[HIGHROLL_MAX_PLAYERS] = {};   // part of the current (re-)roll
  bool      tied[HIGHROLL_MAX_PLAYERS] = {};     // Tie phase: shares the top number
  uint8_t   winner = 0;
  uint8_t   round = 0;                           // 1 = first roll, 2+ = tie-breaks
  uint32_t  phaseStart = 0;
  uint32_t  nextFlip = 0;
  uint32_t  shuffle = 1;                         // state of the show-only generator
};

void highRollStart(HighRoll& h, uint8_t players, uint32_t now, RollFn rollD20);
void highRollStop(HighRoll& h);
// Advance the animation. Returns true if any die changed (needs a redraw).
bool highRollUpdate(HighRoll& h, uint32_t now, RollFn rollD20);
DieState highRollDie(const HighRoll& h, uint8_t player);

inline bool highRollActive(const HighRoll& h) { return h.phase != RollPhase::Idle; }
// Rolling or between tie-break rolls: input is ignored until the result is in.
inline bool highRollBusy(const HighRoll& h) {
  return h.phase == RollPhase::Rolling || h.phase == RollPhase::Tie;
}
