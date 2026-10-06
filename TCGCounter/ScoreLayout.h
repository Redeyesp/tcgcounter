#pragma once
/* ============================================================================
 *  ScoreLayout — the Riftbound / Lorcana tables: where the 2 or 4 cards sit,
 *  where things are inside a card, and touch hit-testing.
 *
 *  Pure geometry (no drawing, no state): ScreenScore draws with it, the host
 *  tests check the touch mapping with it. The seats are the Commander ones
 *  (CommanderLayout.h); these tables have three round buttons: ≡ menu,
 *  🎲 dice, ↻ restart.
 *
 *   2 cards (1v1, 2v2 teams)              4 cards
 *   ┌──────────────────────────────┐      ┌─────────────┬─────────────┐
 *   │ [+]  (+1)  3 /8          [−] │      │  PLAYER 2   ↻   PLAYER 1  │  top row:
 *   │          PLAYER 1            │      │  [−] [+]    │   [−] [+]   │  upside down
 *   ├─────────(≡)──(🎲)──(↻)────────┤      ├─────────────≡─────────────┤
 *   │ [−]        5 /8  (+1)    [+] │      │  PLAYER 3   │   PLAYER 4  │
 *   │          PLAYER 2            │      │  [−] [+]    🎲   [−] [+]  │
 *   └──────────────────────────────┘      └─────────────┴─────────────┘
 *
 *   (+1) = Riftbound "plus life" button (bonus = true). Lorcana has none.
 *
 *  Card shapes (as the card's player sees it):
 *   wide 320 x 119                          compact 159 x 119
 *   ┌────────────────────────────────┐      ┌───────────────┐
 *   │ ┌──┐               (+1)  ┌──┐ │      │  (PLAYER 1)   │  name / WINNER!
 *   │ │− │       5 /8          │+ │ │      │   5 /8  (+1)  │
 *   │ └──┘     PLAYER 1        └──┘ │      │  [ − ] [ + ]  │
 *   └────────────────────────────────┘      └───────────────┘
 * ==========================================================================*/
#include <stdint.h>
#include "CommanderLayout.h"

// Table for 2 or 4 cards (anything else: 2).
const TableLayout& scoreTable(uint8_t players);

struct ScoreGeom {
  int16_t w, h;
  bool    wide;                // 2-card table: − / + are tall buttons at the card's sides
  Rect    minus, plus;         // − / + buttons
  Rect    bonus;               // +1 button (w = 0: none)
  Rect    label;               // compact: name pill at the top
  int16_t numCx, numCy;        // centre of "5 /8"
  int16_t numMaxW, numMaxH;    // room for it
  int16_t capX, capY;          // wide: centre of the name / WINNER! caption under the score
  int16_t dieCx, dieCy, dieR;  // high roll: the D20 in place of the score
  // touch zones (local coordinates)
  int16_t minusEnd, plusStart; // wide: left of minusEnd = −, at/right of plusStart = +
  int16_t zoneY;               // compact: at/below this row = − (left half) / + (right half)
  Rect    bonusZone;           // touches here = +1 (w = 0: none)
};
ScoreGeom scoreGeom(int16_t w, int16_t h, bool bonus);
inline ScoreGeom scoreGeom(const Seat& s, bool bonus) {
  return scoreGeom(seatLocalW(s), seatLocalH(s), bonus);
}

enum class ScoreZone : uint8_t { None, Area, Minus, Plus, Bonus, Hub };
struct ScoreHit {
  ScoreZone zone;
  uint8_t   index;  // player for Area/Minus/Plus/Bonus, hub number for Hub
  bool operator==(const ScoreHit& o) const { return zone == o.zone && index == o.index; }
  bool operator!=(const ScoreHit& o) const { return !(*this == o); }
};
constexpr ScoreHit SCORE_NO_HIT = {ScoreZone::None, 0};

// What a touch at screen (x, y) lands on, with `players` cards and the +1
// buttons shown (`bonus`) or not.
ScoreHit scoreHitTest(uint8_t players, bool bonus, int x, int y);
