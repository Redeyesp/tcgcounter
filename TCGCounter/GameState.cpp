#include "GameState.h"

AppState g_state;

static int16_t clampLife(int v) {
  if (v < LIFE_MIN) return LIFE_MIN;
  if (v > LIFE_MAX) return LIFE_MAX;
  return (int16_t)v;
}

void commanderNewGame(CommanderGame& g) {
  for (uint8_t i = 0; i < COMMANDER_PLAYERS; ++i) {
    g.life[i] = COMMANDER_START_LIFE;
    for (uint8_t j = 0; j < COMMANDER_PLAYERS; ++j) g.cmdDamage[i][j] = 0;
  }
  g.selected = 0;
}

bool commanderAdjustCmdDamage(CommanderGame& g, uint8_t victim, uint8_t source, int delta) {
  if (victim >= COMMANDER_PLAYERS || source >= COMMANDER_PLAYERS || victim == source || delta == 0)
    return false;
  const int before = g.cmdDamage[victim][source];
  int after = before + delta;
  if (after < 0) after = 0;
  if (after > CMD_DAMAGE_MAX) after = CMD_DAMAGE_MAX;
  const int applied = after - before;  // what really changed after clamping
  if (applied == 0) return false;
  g.cmdDamage[victim][source] = (uint8_t)after;
  if (CMD_DAMAGE_AFFECTS_LIFE) g.life[victim] = clampLife((int)g.life[victim] - applied);
  return true;
}

uint8_t commanderOpponent(uint8_t player, uint8_t index) {
  // Skip the player's own seat: P1 -> P2,P3,P4 ; P3 -> P1,P2,P4 ...
  return (uint8_t)(index < player ? index : index + 1);
}

OutReason commanderOutReason(const CommanderGame& g, uint8_t player, uint8_t* source) {
  if (player >= COMMANDER_PLAYERS) return OutReason::None;
  for (uint8_t j = 0; j < COMMANDER_PLAYERS; ++j) {
    if (j != player && g.cmdDamage[player][j] >= CMD_DAMAGE_LETHAL) {
      if (source) *source = j;
      return OutReason::CommanderDamage;
    }
  }
  if (g.life[player] <= OUT_AT_LIFE) return OutReason::Life;
  return OutReason::None;
}

bool commanderAdjustLife(CommanderGame& g, uint8_t player, int delta) {
  if (player >= COMMANDER_PLAYERS || delta == 0) return false;
  int16_t before = g.life[player];
  g.life[player] = clampLife((int)before + delta);
  return g.life[player] != before;
}

void commanderSelect(CommanderGame& g, uint8_t player) {
  if (player < COMMANDER_PLAYERS) g.selected = player;
}

void commanderSelectNext(CommanderGame& g) {
  g.selected = (uint8_t)((g.selected + 1) % COMMANDER_PLAYERS);
}

void appStateSetDefaults(AppState& s) {
  s.screen = SCREEN_HOME;
  commanderNewGame(s.commander);
}

void appStateSanitize(AppState& s) {
  if (s.screen >= SCREEN_COUNT) s.screen = SCREEN_HOME;
  if (s.commander.selected >= COMMANDER_PLAYERS) s.commander.selected = 0;
  for (uint8_t i = 0; i < COMMANDER_PLAYERS; ++i) {
    s.commander.life[i] = clampLife(s.commander.life[i]);
    for (uint8_t j = 0; j < COMMANDER_PLAYERS; ++j) {
      uint8_t& d = s.commander.cmdDamage[i][j];
      if (i == j || d > CMD_DAMAGE_MAX) d = 0;
    }
  }
}

bool operator==(const CommanderGame& a, const CommanderGame& b) {
  if (a.selected != b.selected) return false;
  for (uint8_t i = 0; i < COMMANDER_PLAYERS; ++i) {
    if (a.life[i] != b.life[i]) return false;
    for (uint8_t j = 0; j < COMMANDER_PLAYERS; ++j)
      if (a.cmdDamage[i][j] != b.cmdDamage[i][j]) return false;
  }
  return true;
}

bool operator==(const AppState& a, const AppState& b) {
  return a.screen == b.screen && a.commander == b.commander;
}
