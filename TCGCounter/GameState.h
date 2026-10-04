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
  SCREEN_COUNT
};

constexpr uint8_t COMMANDER_PLAYERS    = 4;
constexpr int16_t COMMANDER_START_LIFE = 40;
constexpr int16_t LIFE_MIN             = -99;   // keeps numbers to 3 characters
constexpr int16_t LIFE_MAX             = 999;

struct CommanderGame {
  int16_t life[COMMANDER_PLAYERS];
  uint8_t selected;  // 0..3 = P1..P4
  // FUTURE (not in V0.1): commander damage, poison, energy, commander tax.
};

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
void commanderNewGame(CommanderGame& g);  // NOT reachable from the UI in V0.1.
                                          // Future reset must go through a
                                          // confirmation screen first.
bool commanderAdjustLife(CommanderGame& g, uint8_t player, int delta);  // true if changed
void commanderSelect(CommanderGame& g, uint8_t player);
void commanderSelectNext(CommanderGame& g);  // P1 -> P2 -> P3 -> P4 -> P1
