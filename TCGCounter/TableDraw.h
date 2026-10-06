#pragma once
/* ============================================================================
 *  TableDraw — drawing shared by the table screens (Commander, Riftbound,
 *  Lorcana): cards that face their player, and the round centre buttons.
 * ==========================================================================*/
#include <stdint.h>
#include "Gfx.h"
#include "CommanderLayout.h"
#include "HighRoll.h"

// Paints one card in its own coordinates (as its player sees it) with the
// card's top-left at (ox, oy) on canvas c. `ctx` is passed through.
using CardPainter = void (*)(lgfx::LovyanGFX& c, int ox, int oy, const void* ctx);

// Draws the card off-screen (no flicker), turned to face the seat, and
// copies it to the LCD with blocking transfers. One 40 KB buffer serves all
// cards; bigger cards are drawn in bands.
void drawSeatCard(const Seat& seat, CardPainter paint, const void* ctx);

enum class HubIcon : uint8_t { Menu, Close, Home, Restart, HighRoll, Dice };

// Round button between the cards, with a dark moat around it
// (HUB_R / HUB_MOAT / HUB_HIT_R in CommanderLayout.h).
void drawHubButton(int x, int y, HubIcon icon, bool pressed);

// A D20 seen face-on (hexagon outline, centre face, facet lines) with its
// number, coloured for the die's state: player colour while rolling, gold
// for the winner, amber for a tie, dimmed when out.
void drawD20(lgfx::LovyanGFX& c, int cx, int cy, int r, DieState state, uint16_t playerColor, uint8_t value);

// Any of the dice: 4 (triangle), 6 (square), 8 (diamond), 12 (pentagon) or
// 20 (hexagon) sides, about r pixels from the centre to the outline. Same
// colours as drawD20; value 0 = no number.
void drawDie(lgfx::LovyanGFX& c, int cx, int cy, int r, uint8_t sides, DieState state,
             uint16_t edgeColor, uint8_t value);
