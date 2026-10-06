#pragma once
/* ============================================================================
 *  CommanderLayout — where each player's card sits for 2..6 players, which
 *  way it faces, where things are inside a card, and touch hit-testing.
 *
 *  Pure geometry: no drawing, no state. ScreenCommander draws with it, and
 *  the host tests check the touch mapping with it.
 *
 *  Screen 320 x 240 (landscape). The device lies in the middle of the table;
 *  every card faces the player sitting at that edge (COMMANDER_FACE_SEATS).
 *  Some player counts have several layouts (picked in the Commander menu):
 *
 *   2 players           3 players: LEFT       MIDDLE           RIGHT
 *   ┌──────────────┐    ┌──────┬──────┐  ┌──────┬──────┐  ┌──────┬──────┐
 *   │   P1 (top)   │    │  P1  │  P2  │  │  P1  │  P2  │  │  P1  │  P2  │
 *   ├────≡──🎲─────┤    ├──────≡──────┤  ├──────≡──────┤  ├──────≡──────┤
 *   │  P2 (bottom) │    │  P3  │  🎲  │  │      P3     │  │  🎲  │  P3  │
 *   └──────────────┘    └──────┴──────┘  └─────────────┘  └──────┴──────┘
 *                                        (🎲 between P1 and P2)
 *   4 players            5 players: HEAD RIGHT        HEAD LEFT
 *   ┌──────┬──────┐      ┌────┬────┬──┐            ┌──┬────┬────┐
 *   │  P1  │  P2  │      │ P1 │ P2 │  │            │  │ P1 │ P2 │
 *   ├──────≡──────┤      ├────≡────┤P5│            │P5├────≡────┤
 *   │  P3  🎲  P4 │      │ P3 🎲 P4 │  │            │  │ P3 🎲 P4 │
 *   └──────┴──────┘      └────┴────┴──┘            └──┴────┴────┘
 *
 *   6 players: 3 + 3                 ENDS
 *   ┌────┬────┬────┐                 ┌──┬────┬────┬──┐
 *   │ P1 │ P2 │ P3 │                 │  │ P1 │ P2 │  │  P5 / P6 cards are
 *   ├────≡────🎲───┤                 │P6├────≡────🎲P5│  turned 90° for the
 *   │ P4 │ P5 │ P6 │                 │  │ P3 │ P4 │  │  players at the ends
 *   └────┴────┴────┘                 └──┴────┴────┴──┘
 *
 *   ≡ = Commander menu (✕ in commander damage mode; the menu also has HIGH
 *   ROLL), 🎲 = Dice page. Top row cards are upside down.
 *
 *  Numbering: top row left to right, then bottom row left to right, then the
 *  head(s) of the table — so 2..4 players keep the seats they had before v0.4.
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

enum class HubKind : uint8_t { Menu, Dice, Restart };
struct HubPos { int16_t x, y; HubKind kind; };  // round buttons, screen coordinates

constexpr uint8_t MAX_HUBS = 3;  // Commander uses 2, Riftbound / Lorcana 3 (ScoreLayout.h)
constexpr int HUB_R = 19;      // drawn radius
constexpr int HUB_MOAT = 3;    // dark ring around it, separates it from the cards
constexpr int HUB_HIT_R = 22;  // touch radius

struct TableLayout {
  uint8_t     players;
  Seat        seats[COMMANDER_MAX_PLAYERS];  // seats[i] = player i
  uint8_t     hubCount;
  HubPos      hubs[MAX_HUBS];
  const char* name;                          // shown in the layout picker
};

// Layout `variant` (0..commanderLayoutCount(players)-1, GameState.h) for 2..6
// players. Anything else: the 4-player layout / variant 0.
const TableLayout& tableLayout(uint8_t players, uint8_t variant = 0);

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
  Rect    partner;           // commander damage mode, opponent cards: + / x partner button
  Rect    pillShort;         // ... and the label pill beside it
  int16_t capX, capY;        // small caption: compact = centred at capX, wide = right-aligned at capX
  // touch zones
  int16_t zoneY;             // compact: touches at/below this row hit − (left half) or + (right half)
  int16_t zoneTop;           // wide: touches above this row are the label area
  int16_t minusEnd;          // wide: touches left of this column hit −
  int16_t plusStart;         // wide: touches at/right of this column hit +
  // high roll: the D20 drawn in place of the number and the − / + buttons
  int16_t dieCx, dieCy, dieR;
};
CardGeom cardGeom(int16_t w, int16_t h);
inline CardGeom cardGeom(const Seat& s) { return cardGeom(seatLocalW(s), seatLocalH(s)); }

// True if a rectangle in the seat's card coordinates stays clear of every
// round button of the table (moat included). Used to shorten captions.
bool clearOfHubs(const TableLayout& L, const Seat& s, const Rect& local);

// cardGeom() fitted to the table: a wide card's label pill is shortened when
// a round button sits on that card's edge (6-player ENDS layout).
CardGeom cardGeom(const TableLayout& L, const Seat& s);

// ---- Touch hit-testing
// Round button under (x, y), or -1.
int8_t tableHubAt(const TableLayout& L, int x, int y);
// Card under (x, y): the thin gaps between cards belong to the nearest card;
// -1 outside every card (the empty seat of the 3-player layout).
int8_t tableSeatAt(const TableLayout& L, int x, int y);

enum class Zone : uint8_t { None, Area, Minus, Plus, Hub };
struct Hit {
  Zone    zone;
  uint8_t index;  // player for Area/Minus/Plus, hub number for Hub
  bool operator==(const Hit& o) const { return zone == o.zone && index == o.index; }
  bool operator!=(const Hit& o) const { return !(*this == o); }
};
constexpr Hit NO_HIT = {Zone::None, 0};

// What a touch at screen (x, y) lands on with `players` at the table in
// layout `variant`. The thin gaps between cards belong to the nearest card;
// the empty seat of the 3-player layouts is NO_HIT.
Hit commanderHitTest(uint8_t players, int x, int y, uint8_t variant = 0);

// Is a TouchSwipe (screen direction code, see InputEvents.h) a sideways
// swipe for the player at this seat? (Head-of-table players swipe "sideways"
// along the screen's vertical axis.)
bool isSidewaysSwipe(const Seat& s, int16_t swipeDir);
