#pragma once
/* ============================================================================
 *  CommanderLayout — where each player's card sits for 2..6 players, which
 *  way it faces, where things are inside a card, and touch hit-testing.
 *
 *  Pure geometry: no drawing, no state. ScreenCommander draws with it, and
 *  the host tests check the touch mapping with it.
 *
 *  Screen 320 x 240 (landscape). The device lies in the middle of the table;
 *  every card faces the player sitting at that edge (COMMANDER_FACE_SEATS):
 *
 *   2 players           3 players           4 players
 *   ┌──────────────┐    ┌──────┬──────┐    ┌──────┬──────┐
 *   │   P1 (top)   │    │  P1  │  P2  │    │  P1  │  P2  │   top row: upside down
 *   ├──────◉───────┤    ├──────◉──────┤    ├──────◉──────┤
 *   │  P2 (bottom) │    │  P3  │ empty│    │  P3  │  P4  │
 *   └──────────────┘    └──────┴──────┘    └──────┴──────┘
 *
 *   5 players (head of the table on the right)    6 players
 *   ┌────┬────┬──┐                                ┌────┬────┬────┐
 *   │ P1 │ P2 │  │                                │ P1 │ P2 │ P3 │
 *   ├────◉────┤P5│  P5 card is turned 90° so      ├────◉────◉────┤
 *   │ P3 │ P4 │  │  the head player reads it      │ P4 │ P5 │ P6 │
 *   └────┴────┴──┘                                └────┴────┴────┘
 *
 *   ◉ = centre button (menu, or ✕ in commander damage mode). Six players get
 *       two of them (the middle cards' labels sit where one centre button
 *       would go); both do the same thing.
 *
 *  Numbering: top row left to right, then bottom row left to right, then the
 *  head of the table — so 2..4 players keep the seats they had before v0.4.
 *
 *  Card shapes:
 *   compact (tall-ish cards)       wide (2 players, head of table)
 *   ┌───────────────┐              ┌──────────────────────────────┐
 *   │  (PLAYER 1)   │              │(PLAYER 1)              CMD 5 │
 *   │      40       │              │ ┌──┐                    ┌──┐ │
 *   │     CMD 5     │              │ │− │        40          │+ │ │
 *   │  [ − ] [ + ]  │              │ └──┘                    └──┘ │
 *   └───────────────┘              └──────────────────────────────┘
 * ==========================================================================*/
#include <stdint.h>
#include "Ui.h"
#include "GameState.h"

// Edge of the table the card's player sits at. The values are the LovyanGFX
// sprite rotations that draw the card upright for that player.
enum class Side : uint8_t { Bottom = 0, Left = 1, Top = 2, Right = 3 };

struct Seat {
  Rect r;     // on screen
  Side side;  // which way the card faces
};

struct HubPos { int16_t x, y; };  // centre button(s), screen coordinates

constexpr uint8_t MAX_HUBS = 2;
constexpr int HUB_R = 19;      // drawn radius
constexpr int HUB_MOAT = 3;    // dark ring around it, separates it from the cards
constexpr int HUB_HIT_R = 22;  // touch radius

struct TableLayout {
  uint8_t players;
  Seat    seats[COMMANDER_MAX_PLAYERS];  // seats[i] = player i
  uint8_t hubCount;
  HubPos  hubs[MAX_HUBS];
};

// Layout for 2..6 players (anything else: the 4-player layout).
const TableLayout& tableLayout(uint8_t players);

// ---- Card coordinates ("local"): as the card's player sees it, (0,0) top-left.
int16_t seatLocalW(const Seat& s);
int16_t seatLocalH(const Seat& s);
void seatToLocal(const Seat& s, int sx, int sy, int& lx, int& ly);   // clamped to the card
void localToScreen(const Seat& s, int lx, int ly, int& sx, int& sy);

// ---- Positions inside a card (local coordinates)
struct CardGeom {
  int16_t w, h;
  bool    wide;              // wide card: − at the left, number in the middle, + at the right
  Rect    pill;              // player label (fixed size; text is shortened to fit)
  Rect    minus, plus;       // − / + buttons
  int16_t numCx, numCy;      // centre of the big number's digits
  int16_t numMaxW, numMaxH;  // room for the number (picks the biggest font that fits)
  int16_t capX, capY;        // small caption: compact = centred at capX, wide = right-aligned at capX
  // touch zones
  int16_t zoneY;             // compact: touches at/below this row hit − (left half) or + (right half)
  int16_t zoneTop;           // wide: touches above this row are the label area
  int16_t minusEnd;          // wide: touches left of this column hit −
  int16_t plusStart;         // wide: touches at/right of this column hit +
};
CardGeom cardGeom(int16_t w, int16_t h);
inline CardGeom cardGeom(const Seat& s) { return cardGeom(seatLocalW(s), seatLocalH(s)); }

// ---- Touch hit-testing
enum class Zone : uint8_t { None, Area, Minus, Plus, Hub };
struct Hit {
  Zone    zone;
  uint8_t index;  // player for Area/Minus/Plus, hub number for Hub
  bool operator==(const Hit& o) const { return zone == o.zone && index == o.index; }
  bool operator!=(const Hit& o) const { return !(*this == o); }
};
constexpr Hit NO_HIT = {Zone::None, 0};

// What a touch at screen (x, y) lands on with `players` at the table.
// The thin gaps between cards belong to the nearest card; the empty seat of
// the 3-player layout is NO_HIT.
Hit commanderHitTest(uint8_t players, int x, int y);

// Is a TouchSwipe (screen direction code, see InputEvents.h) a sideways
// swipe for the player at this seat? (Head-of-table players swipe "sideways"
// along the screen's vertical axis.)
bool isSidewaysSwipe(const Seat& s, int16_t swipeDir);
