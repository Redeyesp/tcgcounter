#include "AppStorage.h"
#include <Preferences.h>
#include <string.h>
#include "GameState.h"
#include "Log.h"

/* ---- NVS layout ---------------------------------------------------------
 *  namespace "tcg"       ver  (u8)  schema version
 *                        np   (u8)  Commander players 2..6 (v0.4+; missing -> 4)
 *                        p1..p6 (i16) Commander life totals (p5, p6 from v0.4)
 *                        sel  (u8)  selected player 0..5
 *                        scr  (u8)  last active Screen
 *                        cd   commander damage [victim][source]:
 *                             36 bytes (6x6) from v0.4,
 *                             16 bytes (4x4) in v0.2-v0.3 -> converted on load,
 *                             missing (v0.1) -> all zero
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
  LOGF("[storage] %s: %u players, life %d/%d/%d/%d/%d/%d, selected P%u, screen %u\n",
       restored ? "restored" : "no saved game, defaults", c.players,
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
