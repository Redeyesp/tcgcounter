#include "AppStorage.h"
#include <Preferences.h>
#include <string.h>
#include "GameState.h"
#include "Log.h"

/* ---- NVS layout ---------------------------------------------------------
 *  namespace "tcg"       ver  (u8)  schema version
 *                        np   (u8)  Commander players 2..6 (v0.4+; missing -> 4)
 *                        lay  (u8)  Commander table layout for that count (v0.10+; missing -> 0)
 *                        p1..p6 (i16) Commander life totals (p5, p6 from v0.4)
 *                        sel  (u8)  selected player 0..5
 *                        scr  (u8)  last active Screen
 *                        cd   commander damage [victim][source]:
 *                             36 bytes (6x6) from v0.4,
 *                             16 bytes (4x4) in v0.2-v0.3 -> converted on load,
 *                             missing (v0.1) -> all zero
 *                        cd2  partner commander damage [victim][source], 36 bytes (v0.10+;
 *                             missing -> all zero)
 *                        pt   (u8)  partners: bit p = player p has two commanders (v0.10+)
 *                        pk   (27 bytes) Pokemon (v0.11+): for P1 then P2: Active damage
 *                             (i16, little endian), status bits, bench 1..5 damage (i16 each);
 *                             then the selected player. Missing -> a fresh game
 *                        dg   (2 bytes) Digimon (v0.13+): memory (i8, > 0 = player 1's
 *                             side), whose turn. v0.12 saved 4 bytes (+ memory lock):
 *                             the first two are read
 *                        st   (5 bytes) Standard (v0.14+): life P1, life P2 (i16, little
 *                             endian), selected player. Missing -> both at 20
 *                        rb   Riftbound, lc Lorcana:
 *                             12 bytes from v0.9: players, teams, target,
 *                               score P1..P4, plus life P1..P4, selected
 *                             3 bytes in v0.5-v0.8 (2 players, default target):
 *                               P1 score, P2 score, selected -> converted on load
 *  namespace "tcgtouch"  calv (u8)  calibration format version
 *                        cal  (16 bytes) LovyanGFX calibration (8 x u16)
 *
 *  FUTURE: new modules add their own keys (or their own namespace).
 *  Bump SCHEMA_VERSION only if an existing key changes meaning; unknown
 *  versions fall back to defaults instead of loading garbage.
 * ------------------------------------------------------------------------*/
static const char*   NS_GAME        = "tcg";
static const char*   NS_TOUCH       = "tcgtouch";
static const uint8_t SCHEMA_VERSION = 1;
static const uint8_t CAL_VERSION    = 1;
static const char* const LIFE_KEYS[COMMANDER_MAX_PLAYERS] = {"p1", "p2", "p3", "p4", "p5", "p6"};
static const uint8_t OLD_CD_SIDE = 4;  // v0.2-v0.3 saved a 4x4 commander damage table

static AppState s_saved;              // what flash currently holds
static bool     s_flashHasData = false;
static AppState s_lastSeen;           // g_state as of the previous loop
static uint32_t s_lastChangeMs = 0;

// ScoreGame <-> blob {players, teams, target, score[4], bonus[4], selected}
static const size_t SCORE_BLOB = 3 + 2 * SCORE_MAX_PLAYERS + 1;
static const size_t OLD_SCORE_BLOB = 3;  // v0.5-v0.8: {score P1, score P2, selected}
static void loadScore(Preferences& p, const char* key, ScoreGame& g) {
  if (!p.isKey(key)) return;  // keep defaults
  const size_t len = p.getBytesLength(key);
  uint8_t b[SCORE_BLOB];
  if (len == SCORE_BLOB) {
    p.getBytes(key, b, sizeof(b));
    g.players = b[0];
    g.teams = b[1] != 0;
    g.target = b[2];
    for (uint8_t i = 0; i < SCORE_MAX_PLAYERS; ++i) {
      g.score[i] = b[3 + i];
      g.bonus[i] = b[3 + SCORE_MAX_PLAYERS + i];
    }
    g.selected = b[SCORE_BLOB - 1];
  } else if (len == OLD_SCORE_BLOB) {  // 2-player game, default target (already set)
    p.getBytes(key, b, OLD_SCORE_BLOB);
    g.score[0] = b[0];
    g.score[1] = b[1];
    g.selected = b[2];
  }
}
// PokemonGame <-> 27-byte blob
static const size_t PK_SIDE = 2 + 1 + 2 * POKEMON_BENCH;
static const size_t PK_BLOB = 2 * PK_SIDE + 1;
static void put16(uint8_t* b, int16_t v) { b[0] = (uint8_t)(v & 0xFF); b[1] = (uint8_t)((uint16_t)v >> 8); }
static int16_t get16(const uint8_t* b) { return (int16_t)(b[0] | (b[1] << 8)); }
static void loadPokemon(Preferences& p, PokemonGame& g) {
  if (!p.isKey("pk") || p.getBytesLength("pk") != PK_BLOB) return;  // keep defaults
  uint8_t b[PK_BLOB];
  p.getBytes("pk", b, sizeof(b));
  for (uint8_t i = 0; i < 2; ++i) {
    const uint8_t* s = b + i * PK_SIDE;
    g.side[i].active = get16(s);
    g.side[i].status = s[2];
    for (uint8_t k = 0; k < POKEMON_BENCH; ++k) g.side[i].bench[k] = get16(s + 3 + 2 * k);
  }
  g.selected = b[PK_BLOB - 1];
}
static void savePokemon(Preferences& p, const PokemonGame& g) {
  uint8_t b[PK_BLOB];
  for (uint8_t i = 0; i < 2; ++i) {
    uint8_t* s = b + i * PK_SIDE;
    put16(s, g.side[i].active);
    s[2] = g.side[i].status;
    for (uint8_t k = 0; k < POKEMON_BENCH; ++k) put16(s + 3 + 2 * k, g.side[i].bench[k]);
  }
  b[PK_BLOB - 1] = g.selected;
  p.putBytes("pk", b, sizeof(b));
}

// DigimonGame <-> 2-byte blob {memory, turn} (v0.12 saved 4: + memory lock, dropped)
static void loadDigimon(Preferences& p, DigimonGame& g) {
  if (!p.isKey("dg")) return;  // keep defaults
  const size_t len = p.getBytesLength("dg");
  if (len != 2 && len != 4) return;
  uint8_t b[4];
  p.getBytes("dg", b, len);
  g.memory = (int8_t)b[0];
  g.turn = b[1];
}
static void saveDigimon(Preferences& p, const DigimonGame& g) {
  const uint8_t b[2] = {(uint8_t)g.memory, g.turn};
  p.putBytes("dg", b, sizeof(b));
}

// Standard (a 2-player CommanderGame) <-> 5-byte blob {life P1, life P2, selected}
static const size_t ST_BLOB = 5;
static void loadStandard(Preferences& p, CommanderGame& g) {
  if (!p.isKey("st") || p.getBytesLength("st") != ST_BLOB) return;  // keep defaults
  uint8_t b[ST_BLOB];
  p.getBytes("st", b, sizeof(b));
  g.life[0] = (int16_t)(b[0] | (b[1] << 8));
  g.life[1] = (int16_t)(b[2] | (b[3] << 8));
  g.selected = b[4];
}
static void saveStandard(Preferences& p, const CommanderGame& g) {
  const uint8_t b[ST_BLOB] = {(uint8_t)(g.life[0] & 0xFF), (uint8_t)((uint16_t)g.life[0] >> 8),
                              (uint8_t)(g.life[1] & 0xFF), (uint8_t)((uint16_t)g.life[1] >> 8),
                              g.selected};
  p.putBytes("st", b, sizeof(b));
}

static void saveScore(Preferences& p, const char* key, const ScoreGame& g) {
  uint8_t b[SCORE_BLOB];
  b[0] = g.players;
  b[1] = g.teams ? 1 : 0;
  b[2] = g.target;
  for (uint8_t i = 0; i < SCORE_MAX_PLAYERS; ++i) {
    b[3 + i] = g.score[i];
    b[3 + SCORE_MAX_PLAYERS + i] = g.bonus[i];
  }
  b[SCORE_BLOB - 1] = g.selected;
  p.putBytes(key, b, sizeof(b));
}

void loadState() {
  appStateSetDefaults(g_state);

  Preferences p;
  bool restored = false;
  if (p.begin(NS_GAME, false)) {
    if (p.getUChar("ver", 0) == SCHEMA_VERSION) {
      CommanderGame& c = g_state.commander;
      c.players = p.isKey("np") ? p.getUChar("np", COMMANDER_DEFAULT_PLAYERS)
                                : COMMANDER_DEFAULT_PLAYERS;  // saved before v0.4: 4 players
      for (uint8_t i = 0; i < COMMANDER_MAX_PLAYERS; ++i)
        if (p.isKey(LIFE_KEYS[i])) c.life[i] = p.getShort(LIFE_KEYS[i], COMMANDER_START_LIFE);
      c.layout = p.getUChar("lay", 0);
      c.partners = p.getUChar("pt", 0);
      if (p.isKey("cd2") && p.getBytesLength("cd2") == sizeof(c.partnerDamage))
        p.getBytes("cd2", c.partnerDamage, sizeof(c.partnerDamage));
      c.selected = p.getUChar("sel", 0);
      g_state.screen = (Screen)p.getUChar("scr", SCREEN_HOME);
      if (p.isKey("cd")) {
        const size_t len = p.getBytesLength("cd");
        if (len == sizeof(c.cmdDamage)) {
          p.getBytes("cd", c.cmdDamage, sizeof(c.cmdDamage));
        } else if (len == (size_t)OLD_CD_SIDE * OLD_CD_SIDE) {  // v0.2-v0.3 format
          uint8_t old[OLD_CD_SIDE][OLD_CD_SIDE];
          p.getBytes("cd", old, sizeof(old));
          for (uint8_t i = 0; i < OLD_CD_SIDE; ++i)
            for (uint8_t j = 0; j < OLD_CD_SIDE; ++j) c.cmdDamage[i][j] = old[i][j];
        }
      }
      loadScore(p, "rb", g_state.riftbound);
      loadScore(p, "lc", g_state.lorcana);
      loadPokemon(p, g_state.pokemon);
      loadDigimon(p, g_state.digimon);
      loadStandard(p, g_state.standard);
      restored = true;
    }
    p.end();
  } else {
    LOGF("[storage] NVS open failed, using defaults\n");
  }

  appStateSanitize(g_state);
  s_saved = s_lastSeen = g_state;
  s_flashHasData = restored;
  s_lastChangeMs = millis();

  const CommanderGame& c = g_state.commander;
  LOGF("[storage] %s: %u players (layout %u), life %d/%d/%d/%d/%d/%d, selected P%u, screen %u\n",
       restored ? "restored" : "no saved game, defaults", c.players, c.layout,
       c.life[0], c.life[1], c.life[2], c.life[3], c.life[4], c.life[5],
       c.selected + 1, (unsigned)g_state.screen);
  const ScoreGame& r = g_state.riftbound;
  const ScoreGame& l = g_state.lorcana;
  LOGF("[storage] riftbound %u cards%s to %u: %u-%u-%u-%u, lorcana %u cards to %u: %u-%u-%u-%u\n",
       r.players, r.teams ? " (teams)" : "", r.target, scoreTotal(r, 0), scoreTotal(r, 1),
       scoreTotal(r, 2), scoreTotal(r, 3), l.players, l.target, scoreTotal(l, 0),
       scoreTotal(l, 1), scoreTotal(l, 2), scoreTotal(l, 3));
}

static void writeState(const AppState& s) {
  Preferences p;
  if (!p.begin(NS_GAME, false)) {
    LOGF("[storage] NVS open failed, save skipped\n");
    return;
  }
  const bool all = !s_flashHasData;
  uint8_t writes = 0;
  if (all) { p.putUChar("ver", SCHEMA_VERSION); ++writes; }
  if (all || s.commander.players != s_saved.commander.players) {
    p.putUChar("np", s.commander.players);
    ++writes;
  }
  if (all || s.commander.layout != s_saved.commander.layout) {
    p.putUChar("lay", s.commander.layout);
    ++writes;
  }
  if (all || s.commander.partners != s_saved.commander.partners) {
    p.putUChar("pt", s.commander.partners);
    ++writes;
  }
  if (all || memcmp(s.commander.partnerDamage, s_saved.commander.partnerDamage,
                    sizeof(s.commander.partnerDamage)) != 0) {
    p.putBytes("cd2", s.commander.partnerDamage, sizeof(s.commander.partnerDamage));
    ++writes;
  }
  for (uint8_t i = 0; i < COMMANDER_MAX_PLAYERS; ++i) {
    if (all || s.commander.life[i] != s_saved.commander.life[i]) {
      p.putShort(LIFE_KEYS[i], s.commander.life[i]);
      ++writes;
    }
  }
  if (all || s.commander.selected != s_saved.commander.selected) {
    p.putUChar("sel", s.commander.selected);
    ++writes;
  }
  if (all || s.screen != s_saved.screen) {
    p.putUChar("scr", (uint8_t)s.screen);
    ++writes;
  }
  if (all || memcmp(s.commander.cmdDamage, s_saved.commander.cmdDamage,
                    sizeof(s.commander.cmdDamage)) != 0) {
    p.putBytes("cd", s.commander.cmdDamage, sizeof(s.commander.cmdDamage));
    ++writes;
  }
  if (all || s.riftbound != s_saved.riftbound) { saveScore(p, "rb", s.riftbound); ++writes; }
  if (all || s.lorcana != s_saved.lorcana)     { saveScore(p, "lc", s.lorcana);   ++writes; }
  if (all || s.pokemon != s_saved.pokemon)     { savePokemon(p, s.pokemon);       ++writes; }
  if (all || s.digimon != s_saved.digimon)     { saveDigimon(p, s.digimon);       ++writes; }
  if (all || s.standard != s_saved.standard)   { saveStandard(p, s.standard);     ++writes; }
  p.end();

  s_saved = s;
  s_flashHasData = true;
  LOGF("[storage] saved (%u key%s)\n", writes, writes == 1 ? "" : "s");
}

void saveStateIfNeeded() {
  const uint32_t now = millis();
  if (g_state != s_lastSeen) {  // something changed since last loop: restart the timer
    s_lastSeen = g_state;
    s_lastChangeMs = now;
  }
  if (g_state == s_saved) return;                      // flash already up to date
  if (now - s_lastChangeMs < SAVE_DELAY_MS) return;    // still changing / not quiet yet
  writeState(g_state);
}

void saveStateNow() {
  if (g_state != s_saved) writeState(g_state);
}

bool loadTouchCalibration(uint16_t cal[8]) {
  Preferences p;
  if (!p.begin(NS_TOUCH, false)) return false;
  bool ok = p.getUChar("calv", 0) == CAL_VERSION &&
            p.getBytesLength("cal") == sizeof(uint16_t) * 8 &&
            p.getBytes("cal", cal, sizeof(uint16_t) * 8) == sizeof(uint16_t) * 8;
  p.end();
  return ok;
}

void saveTouchCalibration(const uint16_t cal[8]) {
  Preferences p;
  if (!p.begin(NS_TOUCH, false)) return;
  p.putBytes("cal", cal, sizeof(uint16_t) * 8);
  p.putUChar("calv", CAL_VERSION);
  p.end();
}
