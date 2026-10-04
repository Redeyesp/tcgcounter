#include "GameState.h"

AppState g_state;

static int16_t clampLife(int v) {
  if (v < LIFE_MIN) return LIFE_MIN;
  if (v > LIFE_MAX) return LIFE_MAX;
  return (int16_t)v;
}

void commanderNewGame(CommanderGame& g) {
  for (uint8_t i = 0; i < COMMANDER_PLAYERS; ++i) g.life[i] = COMMANDER_START_LIFE;
  g.selected = 0;
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
  for (uint8_t i = 0; i < COMMANDER_PLAYERS; ++i) s.commander.life[i] = clampLife(s.commander.life[i]);
}

bool operator==(const CommanderGame& a, const CommanderGame& b) {
  if (a.selected != b.selected) return false;
  for (uint8_t i = 0; i < COMMANDER_PLAYERS; ++i)
    if (a.life[i] != b.life[i]) return false;
  return true;
}

bool operator==(const AppState& a, const AppState& b) {
  return a.screen == b.screen && a.commander == b.commander;
}
