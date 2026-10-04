#include "AppStorage.h"
#include <Preferences.h>
#include <string.h>
#include "GameState.h"
#include "Log.h"

/* ---- NVS layout ---------------------------------------------------------
 *  namespace "tcg"       ver  (u8)  schema version
 *                        p1..p4 (i16) Commander life totals
 *                        sel  (u8)  selected player 0..3
 *                        scr  (u8)  last active Screen
 *                        cd   (16 bytes) commander damage [victim][source] (v0.2+;
 *                             missing on older saves -> all zero)
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
static const char* const LIFE_KEYS[COMMANDER_PLAYERS] = {"p1", "p2", "p3", "p4"};

static AppState s_saved;              // what flash currently holds
static bool     s_flashHasData = false;
static AppState s_lastSeen;           // g_state as of the previous loop
static uint32_t s_lastChangeMs = 0;

void loadState() {
  appStateSetDefaults(g_state);

  Preferences p;
  bool restored = false;
  if (p.begin(NS_GAME, false)) {
    if (p.getUChar("ver", 0) == SCHEMA_VERSION) {
      for (uint8_t i = 0; i < COMMANDER_PLAYERS; ++i)
        g_state.commander.life[i] = p.getShort(LIFE_KEYS[i], COMMANDER_START_LIFE);
      g_state.commander.selected = p.getUChar("sel", 0);
      g_state.screen = (Screen)p.getUChar("scr", SCREEN_HOME);
      if (p.isKey("cd") && p.getBytesLength("cd") == sizeof(g_state.commander.cmdDamage))
        p.getBytes("cd", g_state.commander.cmdDamage, sizeof(g_state.commander.cmdDamage));
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

  LOGF("[storage] %s: life %d/%d/%d/%d, selected P%u, screen %u\n",
       restored ? "restored" : "no saved game, defaults",
       g_state.commander.life[0], g_state.commander.life[1],
       g_state.commander.life[2], g_state.commander.life[3],
       g_state.commander.selected + 1, (unsigned)g_state.screen);
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
  for (uint8_t i = 0; i < COMMANDER_PLAYERS; ++i) {
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
