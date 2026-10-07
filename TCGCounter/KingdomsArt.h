#pragma once
/* ============================================================================
 *  KingdomsArt — the Kingdoms role cards: names, goals, colours and the
 *  artwork, drawn from shapes (no image files):
 *    King     gold crown with gems            (Plains in the original rules)
 *    Knight   green shield over a sword       (Forest)
 *    Bandit   masked face, red bandana, dagger (Mountain)
 *    Traitor  purple hood, glowing eyes, dagger (Swamp)
 *    Usurper  cracked blue crown, dagger through it (Island)
 *  and the back of a face-down card.
 * ==========================================================================*/
#include <stdint.h>
#include "Gfx.h"

const char* kingdomsRoleName(uint8_t role);  // "KING", "KNIGHT", ...
const char* kingdomsRoleGoal(uint8_t role);  // one short paragraph (word-wrapped when drawn)
uint16_t    kingdomsRoleColor(uint8_t role); // bright: name, accents
uint16_t    kingdomsRoleTint(uint8_t role);  // very dark: the panel behind the artwork

// The role's picture centred on (cx, cy), about `size` px square.
void drawKingdomsArt(lgfx::LovyanGFX& c, int cx, int cy, int size, uint8_t role);

// The back of a face-down card (w x h at x, y); `dim` = a card already taken.
void drawKingdomsCardBack(lgfx::LovyanGFX& c, int x, int y, int w, int h, bool dim);
