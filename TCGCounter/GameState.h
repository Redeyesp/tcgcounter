#pragma once
/* ============================================================================
 *  GameState — pure game/application data and the rules that change it.
 *
 *  Nothing in here draws, reads pins or touches flash. Screens call these
 *  functions to change state; the renderer and the storage module notice
 *  the change on their own by comparing against what they last drew/saved.
 * ==========================================================================*/
#include <stdint.h>

/* Values are stored in NVS ("last active screen"): append new screens at
 * the end, never renumber existing ones. */
enum Screen : uint8_t {
  SCREEN_HOME      = 0,
  SCREEN_COMMANDER = 1,
  SCREEN_DICE      = 2,
  SCREEN_RIFTBOUND = 3,
  SCREEN_COMMANDER_SETUP = 4,  // player count / continue / new game (v0.4+)
  SCREEN_COUNT
};

constexpr uint8_t COMMANDER_MIN_PLAYERS     = 2;
constexpr uint8_t COMMANDER_MAX_PLAYERS     = 6;
constexpr uint8_t COMMANDER_DEFAULT_PLAYERS = 4;  // first boot, and saves from before v0.4
constexpr int16_t COMMANDER_START_LIFE = 40;
constexpr int16_t LIFE_MIN             = -99;   // keeps numbers to 3 characters
constexpr int16_t LIFE_MAX             = 999;

// Commander rules for being OUT:
//   life <= OUT_AT_LIFE  (0 or less)
//   or >= CMD_DAMAGE_LETHAL combat damage from ONE opponent's commander
constexpr int16_t OUT_AT_LIFE          = 0;
constexpr uint8_t CMD_DAMAGE_LETHAL    = 21;
constexpr uint8_t CMD_DAMAGE_MAX       = 99;
// Commander damage is also normal damage: adding it lowers life by the same
// amount (and removing it gives the life back), like the Lotus app.
constexpr bool    CMD_DAMAGE_AFFECTS_LIFE = true;

struct CommanderGame {
  uint8_t players;   // 2..6 players at the table (seats 0..players-1 are in use)
  int16_t life[COMMANDER_MAX_PLAYERS];
  uint8_t selected;  // 0..players-1 = P1..Pn
  uint8_t cmdDamage[COMMANDER_MAX_PLAYERS][COMMANDER_MAX_PLAYERS];  // [victim][source]; diagonal unused
  // FUTURE: poison, energy, commander tax.
};

enum class OutReason : uint8_t { None, Life, CommanderDamage };

/* FUTURE: add game data for new modes here, e.g.
 *   struct DiceState     { uint8_t sides; uint8_t count; int16_t lastRoll[6]; };
 *   struct RiftboundGame { int16_t points[2]; ... };
 * then add a member to AppState below and extend AppStorage.cpp. */

struct AppState {
  Screen        screen;
  CommanderGame commander;
  // FUTURE: DiceState dice;  RiftboundGame riftbound;
};

extern AppState g_state;  // the single live copy of all persistent state

void appStateSetDefaults(AppState& s);
void appStateSanitize(AppState& s);  // clamp anything loaded from flash

bool operator==(const CommanderGame& a, const CommanderGame& b);
bool operator==(const AppState& a, const AppState& b);
inline bool operator!=(const CommanderGame& a, const CommanderGame& b) { return !(a == b); }
inline bool operator!=(const AppState& a, const AppState& b) { return !(a == b); }

// ---- Commander rules ----
// Fresh game for `players` people (clamped to 2..6): everyone at 40, no
// commander damage. The UI only calls this from the Commander menu, after a
// confirmation whenever the running game has any progress in it.
void commanderNewGame(CommanderGame& g, uint8_t players = COMMANDER_DEFAULT_PLAYERS);
// True while nothing has happened yet (all at 40, no commander damage):
// starting a new game would lose nothing, so no confirmation is needed.
bool commanderIsFresh(const CommanderGame& g);
bool commanderAdjustLife(CommanderGame& g, uint8_t player, int delta);  // true if changed
void commanderSelect(CommanderGame& g, uint8_t player);
void commanderSelectNext(CommanderGame& g);  // P1 -> P2 -> ... -> Pn -> P1

// Commander damage `victim` took from `source`'s commander. Also moves life
// by the opposite amount when CMD_DAMAGE_AFFECTS_LIFE. True if anything changed.
bool commanderAdjustCmdDamage(CommanderGame& g, uint8_t victim, uint8_t source, int delta);

// Opponents of `player` in seat order: index 0..players-2 -> player number.
uint8_t commanderOpponent(uint8_t player, uint8_t index);

// OUT state. `source` receives the opponent for OutReason::CommanderDamage.
OutReason commanderOutReason(const CommanderGame& g, uint8_t player, uint8_t* source = nullptr);
inline bool commanderIsOut(const CommanderGame& g, uint8_t player) {
  return commanderOutReason(g, player) != OutReason::None;
}
