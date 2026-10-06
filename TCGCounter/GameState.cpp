#include "GameState.h"

AppState g_state;

static int16_t clampLife(int v) {
  if (v < LIFE_MIN) return LIFE_MIN;
  if (v > LIFE_MAX) return LIFE_MAX;
  return (int16_t)v;
}

static uint8_t clampPlayers(int n) {
  if (n < COMMANDER_MIN_PLAYERS || n > COMMANDER_MAX_PLAYERS) return COMMANDER_DEFAULT_PLAYERS;
  return (uint8_t)n;
}

void commanderNewGame(CommanderGame& g, uint8_t players) {
  g.players = clampPlayers(players);
  for (uint8_t i = 0; i < COMMANDER_MAX_PLAYERS; ++i) {  // unused seats too: no stale values
    g.life[i] = COMMANDER_START_LIFE;
    for (uint8_t j = 0; j < COMMANDER_MAX_PLAYERS; ++j) g.cmdDamage[i][j] = 0;
  }
  g.selected = 0;
}

bool commanderIsFresh(const CommanderGame& g) {
  for (uint8_t i = 0; i < g.players; ++i) {
    if (g.life[i] != COMMANDER_START_LIFE) return false;
    for (uint8_t j = 0; j < g.players; ++j)
      if (g.cmdDamage[i][j] != 0) return false;
  }
  return true;
}

bool commanderAdjustCmdDamage(CommanderGame& g, uint8_t victim, uint8_t source, int delta) {
  if (victim >= g.players || source >= g.players || victim == source || delta == 0)
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
  if (player >= g.players) return OutReason::None;
  for (uint8_t j = 0; j < g.players; ++j) {
    if (j != player && g.cmdDamage[player][j] >= CMD_DAMAGE_LETHAL) {
      if (source) *source = j;
      return OutReason::CommanderDamage;
    }
  }
  if (g.life[player] <= OUT_AT_LIFE) return OutReason::Life;
  return OutReason::None;
}

bool commanderAdjustLife(CommanderGame& g, uint8_t player, int delta) {
  if (player >= g.players || delta == 0) return false;
  int16_t before = g.life[player];
  g.life[player] = clampLife((int)before + delta);
  return g.life[player] != before;
}

void commanderSelect(CommanderGame& g, uint8_t player) {
  if (player < g.players) g.selected = player;
}

void commanderSelectNext(CommanderGame& g) {
  g.selected = (uint8_t)((g.selected + 1) % g.players);
}

static uint8_t clampScorePlayers(int n) { return n == 4 ? 4 : 2; }

void scoreNewGame(ScoreGame& g, uint8_t players, uint8_t target, bool teams) {
  g.players = clampScorePlayers(players);
  g.teams = teams && g.players == 2;
  g.target = target < 1 ? 1 : (target > SCORE_TARGET_MAX ? SCORE_TARGET_MAX : target);
  scoreRestart(g);
}

void scoreRestart(ScoreGame& g) {
  for (uint8_t i = 0; i < SCORE_MAX_PLAYERS; ++i) {  // unused cards too: no stale values
    g.score[i] = 0;
    g.bonus[i] = 0;
  }
  g.selected = 0;
}

bool scoreIsFresh(const ScoreGame& g) {
  for (uint8_t i = 0; i < g.players; ++i)
    if (g.score[i] != 0 || g.bonus[i] != 0) return false;
  return true;
}

bool scoreAdjust(ScoreGame& g, uint8_t player, int delta) {
  if (player >= g.players || delta == 0) return false;
  int v = (int)g.score[player] + delta;
  if (v < 0) v = 0;
  if (v > g.target) v = g.target;
  if (v == g.score[player]) return false;
  g.score[player] = (uint8_t)v;
  return true;
}

bool scoreToggleBonus(ScoreGame& g, uint8_t player) {
  if (player >= g.players) return false;
  g.bonus[player] = g.bonus[player] ? 0 : SCORE_BONUS_MAX;
  return true;
}

void scoreSelect(ScoreGame& g, uint8_t player) {
  if (player < g.players) g.selected = player;
}

void scoreSelectNext(ScoreGame& g) { g.selected = (uint8_t)((g.selected + 1) % g.players); }

// Anything loaded from flash: a game that makes sense (else `defaultTarget`).
static void scoreSanitize(ScoreGame& g, uint8_t defaultTarget) {
  g.players = clampScorePlayers(g.players);
  g.teams = g.teams && g.players == 2;
  if (g.target < 1 || g.target > SCORE_TARGET_MAX) g.target = defaultTarget;
  for (uint8_t i = 0; i < SCORE_MAX_PLAYERS; ++i) {
    if (g.score[i] > g.target) g.score[i] = g.target;
    if (g.bonus[i] > SCORE_BONUS_MAX) g.bonus[i] = SCORE_BONUS_MAX;
    if (i >= g.players) g.score[i] = g.bonus[i] = 0;
  }
  if (g.selected >= g.players) g.selected = 0;
}

void appStateSetDefaults(AppState& s) {
  s.screen = SCREEN_HOME;
  commanderNewGame(s.commander);
  scoreNewGame(s.riftbound, 2, RIFTBOUND_TARGET);
  scoreNewGame(s.lorcana, 2, LORCANA_TARGET);
}

void appStateSanitize(AppState& s) {
  if (s.screen >= SCREEN_COUNT) s.screen = SCREEN_HOME;
  scoreSanitize(s.riftbound, RIFTBOUND_TARGET);
  scoreSanitize(s.lorcana, LORCANA_TARGET);
  s.commander.players = clampPlayers(s.commander.players);
  if (s.commander.selected >= s.commander.players) s.commander.selected = 0;
  for (uint8_t i = 0; i < COMMANDER_MAX_PLAYERS; ++i) {
    s.commander.life[i] = clampLife(s.commander.life[i]);
    for (uint8_t j = 0; j < COMMANDER_MAX_PLAYERS; ++j) {
      uint8_t& d = s.commander.cmdDamage[i][j];
      if (i == j || d > CMD_DAMAGE_MAX) d = 0;
    }
  }
}

bool operator==(const CommanderGame& a, const CommanderGame& b) {
  if (a.players != b.players || a.selected != b.selected) return false;
  for (uint8_t i = 0; i < COMMANDER_MAX_PLAYERS; ++i) {
    if (a.life[i] != b.life[i]) return false;
    for (uint8_t j = 0; j < COMMANDER_MAX_PLAYERS; ++j)
      if (a.cmdDamage[i][j] != b.cmdDamage[i][j]) return false;
  }
  return true;
}

bool operator==(const ScoreGame& a, const ScoreGame& b) {
  if (a.players != b.players || a.teams != b.teams || a.target != b.target ||
      a.selected != b.selected)
    return false;
  for (uint8_t i = 0; i < SCORE_MAX_PLAYERS; ++i)
    if (a.score[i] != b.score[i] || a.bonus[i] != b.bonus[i]) return false;
  return true;
}

bool operator==(const AppState& a, const AppState& b) {
  return a.screen == b.screen && a.commander == b.commander &&
         a.riftbound == b.riftbound && a.lorcana == b.lorcana;
}
