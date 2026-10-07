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

uint8_t commanderLayoutCount(uint8_t players) {
  static const uint8_t COUNT[COMMANDER_MAX_PLAYERS + 1] = {0, 0, 1, 3, 1, 2, 2};
  return players <= COMMANDER_MAX_PLAYERS ? COUNT[players] : 0;
}

static uint8_t clampLayout(uint8_t players, uint8_t layout) {
  return layout < commanderLayoutCount(players) ? layout : 0;
}

void commanderNewGame(CommanderGame& g, uint8_t players, uint8_t layout) {
  g.players = clampPlayers(players);
  g.layout = clampLayout(g.players, layout);
  for (uint8_t i = 0; i < COMMANDER_MAX_PLAYERS; ++i) {  // unused seats too: no stale values
    g.life[i] = COMMANDER_START_LIFE;
    for (uint8_t j = 0; j < COMMANDER_MAX_PLAYERS; ++j) g.cmdDamage[i][j] = g.partnerDamage[i][j] = 0;
  }
  g.partners = 0;
  g.selected = 0;
}

void standardNewGame(CommanderGame& g) {
  commanderNewGame(g, 2, 0);
  for (int16_t& l : g.life) l = STANDARD_START_LIFE;
}

bool standardIsFresh(const CommanderGame& g) {
  return g.life[0] == STANDARD_START_LIFE && g.life[1] == STANDARD_START_LIFE;
}

bool commanderSetLayout(CommanderGame& g, uint8_t layout) {
  if (layout >= commanderLayoutCount(g.players) || layout == g.layout) return false;
  g.layout = layout;
  return true;
}

bool commanderIsFresh(const CommanderGame& g) {
  for (uint8_t i = 0; i < g.players; ++i) {
    if (g.life[i] != COMMANDER_START_LIFE) return false;
    for (uint8_t j = 0; j < g.players; ++j)
      if (g.cmdDamage[i][j] != 0 || g.partnerDamage[i][j] != 0) return false;
  }
  return true;
}

uint8_t commanderCmdDamage(const CommanderGame& g, uint8_t victim, uint8_t source, uint8_t which) {
  if (victim >= COMMANDER_MAX_PLAYERS || source >= COMMANDER_MAX_PLAYERS) return 0;
  return which ? g.partnerDamage[victim][source] : g.cmdDamage[victim][source];
}

bool commanderAdjustCmdDamage(CommanderGame& g, uint8_t victim, uint8_t source, int delta,
                              uint8_t which) {
  if (victim >= g.players || source >= g.players || victim == source || delta == 0)
    return false;
  if (which && !commanderHasPartner(g, source)) return false;
  uint8_t& d = which ? g.partnerDamage[victim][source] : g.cmdDamage[victim][source];
  const int before = d;
  int after = before + delta;
  if (after < 0) after = 0;
  if (after > CMD_DAMAGE_MAX) after = CMD_DAMAGE_MAX;
  const int applied = after - before;  // what really changed after clamping
  if (applied == 0) return false;
  d = (uint8_t)after;
  if (CMD_DAMAGE_AFFECTS_LIFE) g.life[victim] = clampLife((int)g.life[victim] - applied);
  return true;
}

bool commanderCanDropPartner(const CommanderGame& g, uint8_t p) {
  if (p >= COMMANDER_MAX_PLAYERS) return false;
  for (uint8_t v = 0; v < COMMANDER_MAX_PLAYERS; ++v)
    if (g.partnerDamage[v][p] != 0) return false;
  return true;
}

bool commanderSetPartner(CommanderGame& g, uint8_t p, bool on) {
  if (p >= g.players || commanderHasPartner(g, p) == on) return false;
  if (!on && !commanderCanDropPartner(g, p)) return false;
  if (on) g.partners = (uint8_t)(g.partners | (1u << p));
  else    g.partners = (uint8_t)(g.partners & ~(1u << p));
  return true;
}

uint8_t commanderOpponent(uint8_t player, uint8_t index) {
  // Skip the player's own seat: P1 -> P2,P3,P4 ; P3 -> P1,P2,P4 ...
  return (uint8_t)(index < player ? index : index + 1);
}

OutReason commanderOutReason(const CommanderGame& g, uint8_t player, uint8_t* source) {
  if (player >= g.players) return OutReason::None;
  for (uint8_t j = 0; j < g.players; ++j) {
    if (j != player && (g.cmdDamage[player][j] >= CMD_DAMAGE_LETHAL ||
                        g.partnerDamage[player][j] >= CMD_DAMAGE_LETHAL)) {
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

// ---- Pokemon
static int16_t clampDamage(int v) {
  if (v < 0) v = 0;
  if (v > POKEMON_DAMAGE_MAX) v = POKEMON_DAMAGE_MAX;
  return (int16_t)(v - v % POKEMON_DAMAGE_STEP);
}

static int16_t* pokemonSlot(PokemonGame& g, uint8_t side, int8_t slot) {
  if (side > 1) return nullptr;
  if (slot == POKEMON_ACTIVE) return &g.side[side].active;
  if (slot >= 0 && slot < (int8_t)POKEMON_BENCH) return &g.side[side].bench[slot];
  return nullptr;
}

void pokemonNewGame(PokemonGame& g) {
  for (PokemonSide& s : g.side) {
    s.active = 0;
    s.status = 0;
    for (int16_t& b : s.bench) b = 0;
  }
  g.selected = 0;
}

bool pokemonIsFresh(const PokemonGame& g) {
  for (const PokemonSide& s : g.side) {
    if (s.active != 0 || s.status != 0) return false;
    for (int16_t b : s.bench)
      if (b != 0) return false;
  }
  return true;
}

int16_t pokemonDamage(const PokemonGame& g, uint8_t side, int8_t slot) {
  if (side > 1) return 0;
  if (slot == POKEMON_ACTIVE) return g.side[side].active;
  if (slot >= 0 && slot < (int8_t)POKEMON_BENCH) return g.side[side].bench[slot];
  return 0;
}

bool pokemonAdjust(PokemonGame& g, uint8_t side, int8_t slot, int delta) {
  int16_t* d = pokemonSlot(g, side, slot);
  if (!d || delta == 0) return false;
  const int16_t after = clampDamage((int)*d + delta);
  if (after == *d) return false;
  *d = after;
  return true;
}

bool pokemonKnockOut(PokemonGame& g, uint8_t side, int8_t slot) {
  int16_t* d = pokemonSlot(g, side, slot);
  if (!d) return false;
  const bool changed = *d != 0 || (slot == POKEMON_ACTIVE && g.side[side].status != 0);
  *d = 0;
  if (slot == POKEMON_ACTIVE) g.side[side].status = 0;
  return changed;
}

bool pokemonSwap(PokemonGame& g, uint8_t side, uint8_t slot) {
  if (side > 1 || slot >= POKEMON_BENCH) return false;
  PokemonSide& s = g.side[side];
  const int16_t a = s.active;
  s.active = s.bench[slot];
  s.bench[slot] = a;
  s.status = 0;
  return true;
}

bool pokemonToggleStatus(PokemonGame& g, uint8_t side, uint8_t bit) {
  if (side > 1 || (bit != POKEMON_PSN && bit != POKEMON_BRN)) return false;
  g.side[side].status = (uint8_t)(g.side[side].status ^ bit);
  return true;
}

static void pokemonSanitize(PokemonGame& g) {
  for (PokemonSide& s : g.side) {
    s.active = clampDamage(s.active);
    s.status = (uint8_t)(s.status & (POKEMON_PSN | POKEMON_BRN));
    for (int16_t& b : s.bench) b = clampDamage(b);
  }
  if (g.selected > 1) g.selected = 0;
}

// ---- Digimon
static int8_t clampMemory(int v) {
  if (v < -DIGIMON_MEMORY_MAX) return -DIGIMON_MEMORY_MAX;
  if (v > DIGIMON_MEMORY_MAX) return DIGIMON_MEMORY_MAX;
  return (int8_t)v;
}

void digimonNewGame(DigimonGame& g, uint8_t first) {
  g.memory = 0;
  g.turn = first ? 1 : 0;
}

bool digimonIsFresh(const DigimonGame& g) { return g.memory == 0; }

bool digimonSetMemory(DigimonGame& g, uint8_t p, int value) {
  if (p > 1) return false;
  const DigimonGame before = g;
  int8_t m = clampMemory(value);
  if (p == 1) m = (int8_t)-m;  // to player 1's point of view
  // whose side is it on now: the waiting player's -> their turn
  const int8_t waiting = (int8_t)(1 - g.turn);
  const int8_t forWaiting = waiting == 0 ? m : (int8_t)-m;
  if (forWaiting >= 1) g.turn = (uint8_t)waiting;
  g.memory = m;
  return g != before;
}

bool digimonAdjust(DigimonGame& g, uint8_t p, int delta) {
  if (p > 1 || delta == 0) return false;
  return digimonSetMemory(g, p, digimonMemoryOf(g, p) + delta);
}

void digimonPass(DigimonGame& g) {
  const uint8_t next = (uint8_t)(1 - g.turn);
  const int8_t m = (int8_t)DIGIMON_PASS_MEMORY;
  g.memory = next == 0 ? m : (int8_t)-m;
  g.turn = next;
}

// ---- Kingdoms
static const uint8_t KINGDOMS_DECK[KINGDOMS_MAX_PLAYERS] = {
  KINGDOMS_KING, KINGDOMS_BANDIT, KINGDOMS_BANDIT, KINGDOMS_TRAITOR, KINGDOMS_KNIGHT, KINGDOMS_USURPER};

void kingdomsReset(KingdomsGame& g) {
  g.players = 0;
  g.drawn = 0;
  for (uint8_t k = 0; k < KINGDOMS_MAX_PLAYERS; ++k) g.role[k] = g.drawnBy[k] = 0;
}

uint8_t kingdomsRoleCount(uint8_t players, uint8_t role) {
  if (players < KINGDOMS_MIN_PLAYERS || players > KINGDOMS_MAX_PLAYERS) return 0;
  uint8_t n = 0;
  for (uint8_t k = 0; k < players; ++k) n += KINGDOMS_DECK[k] == role;
  return n;
}

bool kingdomsDeal(KingdomsGame& g, uint8_t players, uint8_t (*randBelow)(uint8_t n)) {
  if (players < KINGDOMS_MIN_PLAYERS || players > KINGDOMS_MAX_PLAYERS || !randBelow) return false;
  kingdomsReset(g);
  g.players = players;
  for (uint8_t k = 0; k < players; ++k) g.role[k] = KINGDOMS_DECK[k];
  for (uint8_t k = players - 1; k > 0; --k) {  // Fisher-Yates
    uint8_t j = randBelow((uint8_t)(k + 1));
    if (j > k) j = k;
    const uint8_t t = g.role[k];
    g.role[k] = g.role[j];
    g.role[j] = t;
  }
  return true;
}

bool kingdomsDraw(KingdomsGame& g, uint8_t card) {
  if (card >= g.players || g.drawnBy[card] != 0 || g.drawn >= g.players) return false;
  g.drawnBy[card] = ++g.drawn;
  return true;
}

bool kingdomsValid(const KingdomsGame& g) {
  if (g.players == 0) {
    if (g.drawn) return false;
    for (uint8_t k = 0; k < KINGDOMS_MAX_PLAYERS; ++k)
      if (g.role[k] || g.drawnBy[k]) return false;
    return true;
  }
  if (g.players < KINGDOMS_MIN_PLAYERS || g.players > KINGDOMS_MAX_PLAYERS || g.drawn > g.players) return false;
  uint8_t count[KINGDOMS_ROLE_COUNT] = {};
  uint8_t seen = 0;  // bit n-1: some card was taken n-th
  for (uint8_t k = 0; k < KINGDOMS_MAX_PLAYERS; ++k) {
    if (k >= g.players) {
      if (g.role[k] || g.drawnBy[k]) return false;
      continue;
    }
    if (g.role[k] >= KINGDOMS_ROLE_COUNT) return false;
    ++count[g.role[k]];
    const uint8_t n = g.drawnBy[k];
    if (n == 0) continue;
    if (n > g.drawn || (seen & (1u << (n - 1)))) return false;
    seen = (uint8_t)(seen | (1u << (n - 1)));
  }
  if (seen != (uint8_t)((1u << g.drawn) - 1)) return false;  // exactly 1..drawn
  for (uint8_t r = 0; r < KINGDOMS_ROLE_COUNT; ++r)
    if (count[r] != kingdomsRoleCount(g.players, r)) return false;
  return true;
}

bool operator==(const KingdomsGame& a, const KingdomsGame& b) {
  if (a.players != b.players || a.drawn != b.drawn) return false;
  for (uint8_t k = 0; k < KINGDOMS_MAX_PLAYERS; ++k)
    if (a.role[k] != b.role[k] || a.drawnBy[k] != b.drawnBy[k]) return false;
  return true;
}

static void digimonSanitize(DigimonGame& g) {
  g.memory = clampMemory(g.memory);
  if (g.turn > 1) g.turn = 0;
}

void appStateSetDefaults(AppState& s) {
  s.screen = SCREEN_HOME;
  commanderNewGame(s.commander);
  standardNewGame(s.standard);
  kingdomsReset(s.kingdoms);
  scoreNewGame(s.riftbound, 2, RIFTBOUND_TARGET);
  scoreNewGame(s.lorcana, 2, LORCANA_TARGET);
  pokemonNewGame(s.pokemon);
  digimonNewGame(s.digimon, 0);
}

void appStateSanitize(AppState& s) {
  if (s.screen >= SCREEN_COUNT) s.screen = SCREEN_HOME;
  scoreSanitize(s.riftbound, RIFTBOUND_TARGET);
  scoreSanitize(s.lorcana, LORCANA_TARGET);
  pokemonSanitize(s.pokemon);
  digimonSanitize(s.digimon);
  if (!kingdomsValid(s.kingdoms)) kingdomsReset(s.kingdoms);  // a broken deal: deal again
  CommanderGame& c = s.commander;
  c.players = clampPlayers(c.players);
  c.layout = clampLayout(c.players, c.layout);
  if (c.selected >= c.players) c.selected = 0;
  c.partners = (uint8_t)(c.partners & ((1u << COMMANDER_MAX_PLAYERS) - 1));
  for (uint8_t i = 0; i < COMMANDER_MAX_PLAYERS; ++i) {
    c.life[i] = clampLife(c.life[i]);
    for (uint8_t j = 0; j < COMMANDER_MAX_PLAYERS; ++j) {
      uint8_t& d = c.cmdDamage[i][j];
      if (i == j || d > CMD_DAMAGE_MAX) d = 0;
      uint8_t& pd = c.partnerDamage[i][j];
      if (i == j || pd > CMD_DAMAGE_MAX) pd = 0;
      if (pd) c.partners = (uint8_t)(c.partners | (1u << j));  // damage from a partner: it exists
    }
  }
  // Standard: always the 2-player table, life only
  CommanderGame& st = s.standard;
  const int16_t life0 = clampLife(st.life[0]), life1 = clampLife(st.life[1]);
  const uint8_t sel = st.selected < 2 ? st.selected : 0;
  standardNewGame(st);
  st.life[0] = life0;
  st.life[1] = life1;
  st.selected = sel;
}

bool operator==(const CommanderGame& a, const CommanderGame& b) {
  if (a.players != b.players || a.layout != b.layout || a.selected != b.selected ||
      a.partners != b.partners)
    return false;
  for (uint8_t i = 0; i < COMMANDER_MAX_PLAYERS; ++i) {
    if (a.life[i] != b.life[i]) return false;
    for (uint8_t j = 0; j < COMMANDER_MAX_PLAYERS; ++j)
      if (a.cmdDamage[i][j] != b.cmdDamage[i][j] || a.partnerDamage[i][j] != b.partnerDamage[i][j])
        return false;
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

bool operator==(const PokemonGame& a, const PokemonGame& b) {
  if (a.selected != b.selected) return false;
  for (uint8_t i = 0; i < 2; ++i) {
    const PokemonSide &x = a.side[i], &y = b.side[i];
    if (x.active != y.active || x.status != y.status) return false;
    for (uint8_t k = 0; k < POKEMON_BENCH; ++k)
      if (x.bench[k] != y.bench[k]) return false;
  }
  return true;
}

bool operator==(const DigimonGame& a, const DigimonGame& b) {
  return a.memory == b.memory && a.turn == b.turn;
}

bool operator==(const AppState& a, const AppState& b) {
  return a.screen == b.screen && a.commander == b.commander && a.standard == b.standard &&
         a.riftbound == b.riftbound && a.lorcana == b.lorcana && a.pokemon == b.pokemon &&
         a.digimon == b.digimon && a.kingdoms == b.kingdoms;
}
