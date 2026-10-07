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
  SCREEN_POKEMON         = 8,  // Pokemon table, device standing (portrait) (v0.11+)
  SCREEN_POKEMON_SETUP   = 9,  // Pokemon menu: continue / coin flip / new game (v0.11+)
  SCREEN_DIGIMON         = 10, // Digimon memory gauge, device standing (portrait) (v0.12+)
  SCREEN_DIGIMON_SETUP   = 11, // Digimon menu: continue / new game / memory lock value (v0.12+)
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

// Table layouts per player count (where the cards sit; CommanderLayout.cpp
// has them): 2 -> 1, 3 -> 3 (P3 left / middle / right), 4 -> 1,
// 5 -> 2 (head of the table right / left), 6 -> 2 (3 + 3, ends).
uint8_t commanderLayoutCount(uint8_t players);

struct CommanderGame {
  uint8_t players;   // 2..6 players at the table (seats 0..players-1 are in use)
  uint8_t layout;    // table layout for this player count, 0..commanderLayoutCount()-1
  int16_t life[COMMANDER_MAX_PLAYERS];
  uint8_t selected;  // 0..players-1 = P1..Pn
  uint8_t cmdDamage[COMMANDER_MAX_PLAYERS][COMMANDER_MAX_PLAYERS];  // [victim][source]; diagonal unused
  // Partner: a player with two commanders. Each one's damage counts on its
  // own (21 from either is lethal); the partner's goes here.
  uint8_t partnerDamage[COMMANDER_MAX_PLAYERS][COMMANDER_MAX_PLAYERS];  // [victim][source]
  uint8_t partners;  // bit p set = player p has a partner
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

// ---- Pokemon TCG: two players, each with an Active Pokemon and a bench of 5.
// Damage in points (always a multiple of 10). PSN / BRN are on the Active only.
constexpr uint8_t POKEMON_BENCH       = 5;
constexpr int16_t POKEMON_DAMAGE_STEP = 10;
constexpr int16_t POKEMON_DAMAGE_MAX  = 990;  // keeps numbers to 3 digits
constexpr uint8_t POKEMON_PSN = 1;  // status bits
constexpr uint8_t POKEMON_BRN = 2;
constexpr int8_t  POKEMON_ACTIVE = -1;  // "slot" of the Active Pokemon (bench: 0..4)

struct PokemonSide {
  int16_t active;                 // damage on the Active Pokemon
  uint8_t status;                 // POKEMON_PSN | POKEMON_BRN
  int16_t bench[POKEMON_BENCH];   // damage on bench slots 1..5 (0 = no damage, or empty)
};

struct PokemonGame {
  PokemonSide side[2];
  uint8_t     selected;  // 0 / 1: the player the encoder counts for
};

// ---- Digimon TCG: the shared memory gauge, 10 .. 0 .. 10.
// `memory` > 0: on player 1's side (P1 has that much memory), < 0: on
// player 2's side. The turn passes when the memory reaches 1 or more on the
// other player's side. Memory lock: the new turn then always starts at
// exactly `lockAt` memory (instead of wherever the counter landed).
constexpr int8_t  DIGIMON_MEMORY_MAX   = 10;
constexpr uint8_t DIGIMON_PASS_MEMORY  = 3;   // a pass gives the opponent 3 memory
constexpr uint8_t DIGIMON_DEFAULT_LOCK = 3;

struct DigimonGame {
  int8_t  memory;  // -10..10, see above
  uint8_t turn;    // 0 / 1: whose turn it is
  bool    lock;    // memory lock on
  uint8_t lockAt;  // 1..10
};

/* FUTURE: add game data for new modes here (e.g. struct DiceState), add a
 * member to AppState below and extend AppStorage.cpp. */

struct AppState {
  Screen        screen;
  CommanderGame commander;
  ScoreGame     riftbound;
  ScoreGame     lorcana;
  PokemonGame   pokemon;
  DigimonGame   digimon;
};

extern AppState g_state;  // the single live copy of all persistent state

void appStateSetDefaults(AppState& s);
void appStateSanitize(AppState& s);  // clamp anything loaded from flash

bool operator==(const CommanderGame& a, const CommanderGame& b);
bool operator==(const ScoreGame& a, const ScoreGame& b);
bool operator==(const PokemonGame& a, const PokemonGame& b);
bool operator==(const DigimonGame& a, const DigimonGame& b);
inline bool operator!=(const DigimonGame& a, const DigimonGame& b) { return !(a == b); }
inline bool operator!=(const PokemonGame& a, const PokemonGame& b) { return !(a == b); }
bool operator==(const AppState& a, const AppState& b);
inline bool operator!=(const CommanderGame& a, const CommanderGame& b) { return !(a == b); }
inline bool operator!=(const ScoreGame& a, const ScoreGame& b) { return !(a == b); }
inline bool operator!=(const AppState& a, const AppState& b) { return !(a == b); }

// ---- Commander rules ----
// Fresh game for `players` people (clamped to 2..6) at table `layout`:
// everyone at 40, no commander damage, no partners. The UI only calls this
// from the Commander menu, after a confirmation whenever the running game
// has any progress in it.
void commanderNewGame(CommanderGame& g, uint8_t players = COMMANDER_DEFAULT_PLAYERS,
                      uint8_t layout = 0);
// Another table layout for the same players; the game itself is kept.
bool commanderSetLayout(CommanderGame& g, uint8_t layout);
// True while nothing has happened yet (all at 40, no commander damage):
// starting a new game would lose nothing, so no confirmation is needed.
bool commanderIsFresh(const CommanderGame& g);
bool commanderAdjustLife(CommanderGame& g, uint8_t player, int delta);  // true if changed
void commanderSelect(CommanderGame& g, uint8_t player);
void commanderSelectNext(CommanderGame& g);  // P1 -> P2 -> ... -> Pn -> P1

// Commander damage `victim` took from `source`'s commander (`which` 0) or
// partner (`which` 1, only while `source` has one). Also moves life by the
// opposite amount when CMD_DAMAGE_AFFECTS_LIFE. True if anything changed.
bool commanderAdjustCmdDamage(CommanderGame& g, uint8_t victim, uint8_t source, int delta,
                              uint8_t which = 0);
uint8_t commanderCmdDamage(const CommanderGame& g, uint8_t victim, uint8_t source, uint8_t which = 0);

// ---- Partner (two commanders) ----
inline bool commanderHasPartner(const CommanderGame& g, uint8_t p) {
  return p < g.players && ((g.partners >> p) & 1);
}
// The partner can only be taken away again while it has dealt no damage.
bool commanderCanDropPartner(const CommanderGame& g, uint8_t p);
bool commanderSetPartner(CommanderGame& g, uint8_t p, bool on);  // true if changed

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

// ---- Pokemon rules ----
// `slot`: POKEMON_ACTIVE or a bench slot 0..4. Anything out of range does nothing.
void pokemonNewGame(PokemonGame& g);   // everything at 0, no status, P1 selected
bool pokemonIsFresh(const PokemonGame& g);
int16_t pokemonDamage(const PokemonGame& g, uint8_t side, int8_t slot);
// delta in damage points; clamps 0..POKEMON_DAMAGE_MAX, stays a multiple of 10
bool pokemonAdjust(PokemonGame& g, uint8_t side, int8_t slot, int delta);
// Knocked out: that Pokemon's damage back to 0 (the Active also loses PSN / BRN).
bool pokemonKnockOut(PokemonGame& g, uint8_t side, int8_t slot);
// Bench slot `slot` becomes the Active, the Active goes to that slot with its
// damage. PSN / BRN end when the Active goes to the bench.
bool pokemonSwap(PokemonGame& g, uint8_t side, uint8_t slot);
bool pokemonToggleStatus(PokemonGame& g, uint8_t side, uint8_t bit);

// ---- Digimon rules ----
// New game: memory 0, `first` player's turn. The lock setting is kept.
void digimonNewGame(DigimonGame& g, uint8_t first);
bool digimonIsFresh(const DigimonGame& g);   // memory still 0
// Memory as player p sees it: on their side > 0, on the opponent's side < 0.
inline int8_t digimonMemoryOf(const DigimonGame& g, uint8_t p) {
  return p == 0 ? g.memory : (int8_t)-g.memory;
}
// Put the counter at `value` (-10..10) as player p sees it. Reaching 1+ on
// the side of the player who is waiting passes the turn to them (with the
// lock on, their memory is then exactly lockAt). True if anything changed.
bool digimonSetMemory(DigimonGame& g, uint8_t p, int value);
bool digimonAdjust(DigimonGame& g, uint8_t p, int delta);  // +delta for player p
// The player whose turn it is passes: the opponent gets 3 memory (lockAt
// with the lock on) and the turn.
void digimonPass(DigimonGame& g);
void digimonSetLock(DigimonGame& g, bool on);
void digimonSetLockAt(DigimonGame& g, int value);  // clamps 1..10
