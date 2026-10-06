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
  SCREEN_DICE      = 2,  // Home -> DICE (dice page on its own; back on the menu since v0.8)
  SCREEN_RIFTBOUND = 3,
  SCREEN_COMMANDER_SETUP = 4,  // player count / continue / new game (v0.4+)
  SCREEN_LORCANA   = 5,        // v0.5+
  SCREEN_RIFTBOUND_SETUP = 6,  // Riftbound menu: continue / 1v1, 4-player, 2v2 (v0.9+)
  SCREEN_LORCANA_SETUP   = 7,  // Lorcana menu: continue / 2 or 4 players, 20 or 25 lore (v0.9+)
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

// ---- Score race (Riftbound, Lorcana): first to the target wins.
// 2 or 4 cards on the table; in Riftbound 2v2 the two cards are the teams.
constexpr uint8_t SCORE_MAX_PLAYERS      = 4;
constexpr uint8_t RIFTBOUND_TARGET       = 8;   // victory points: 1v1 and 4-player free-for-all
constexpr uint8_t RIFTBOUND_TEAM_TARGET  = 11;  // 2v2: points per team
constexpr uint8_t LORCANA_TARGET         = 20;  // lore
constexpr uint8_t LORCANA_LONG_TARGET    = 25;  // lore, longer (multiplayer) game
constexpr uint8_t SCORE_TARGET_MAX       = 99;
// Riftbound "plus life": the +1 on a card. At most one per player (team),
// counted in the score, so a score can end up one past the target (9 / 8).
constexpr uint8_t SCORE_BONUS_MAX        = 1;

struct ScoreGame {
  uint8_t players;                    // cards: 2 or 4
  bool    teams;                      // the 2 cards are teams (Riftbound 2v2)
  uint8_t target;                     // score that wins
  uint8_t score[SCORE_MAX_PLAYERS];   // 0..target, from − / +
  uint8_t bonus[SCORE_MAX_PLAYERS];   // 0..SCORE_BONUS_MAX, the +1 on top of the score
  uint8_t selected;                   // 0..players-1 (encoder target)
};

/* FUTURE: add game data for new modes here (e.g. struct DiceState), add a
 * member to AppState below and extend AppStorage.cpp. */

struct AppState {
  Screen        screen;
  CommanderGame commander;
  ScoreGame     riftbound;
  ScoreGame     lorcana;
};

extern AppState g_state;  // the single live copy of all persistent state

void appStateSetDefaults(AppState& s);
void appStateSanitize(AppState& s);  // clamp anything loaded from flash

bool operator==(const CommanderGame& a, const CommanderGame& b);
bool operator==(const ScoreGame& a, const ScoreGame& b);
bool operator==(const AppState& a, const AppState& b);
inline bool operator!=(const CommanderGame& a, const CommanderGame& b) { return !(a == b); }
inline bool operator!=(const ScoreGame& a, const ScoreGame& b) { return !(a == b); }
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

// ---- Score race rules (Riftbound, Lorcana) ----
// New game: `players` cards (2 or 4; anything else -> 2), first to `target`
// (1..SCORE_TARGET_MAX), `teams` only with 2 cards. Everyone at 0, P1 selected.
void scoreNewGame(ScoreGame& g, uint8_t players, uint8_t target, bool teams = false);
void scoreRestart(ScoreGame& g);                // same game again: everyone back to 0
bool scoreIsFresh(const ScoreGame& g);          // all at 0: a restart would lose nothing
bool scoreAdjust(ScoreGame& g, uint8_t player, int delta);  // the − / + part, clamps 0..target
bool scoreToggleBonus(ScoreGame& g, uint8_t player);        // +1 on / off
void scoreSelect(ScoreGame& g, uint8_t player);
void scoreSelectNext(ScoreGame& g);             // P1 -> P2 -> ... -> P1
inline uint8_t scoreTotal(const ScoreGame& g, uint8_t player) {  // what the card shows
  return player < SCORE_MAX_PLAYERS ? (uint8_t)(g.score[player] + g.bonus[player]) : 0;
}
inline bool scoreHasWon(const ScoreGame& g, uint8_t player) {
  return player < g.players && scoreTotal(g, player) >= g.target;
}
