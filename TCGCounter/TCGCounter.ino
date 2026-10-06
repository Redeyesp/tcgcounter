/* ============================================================================
 *  TCG Counter — Firmware
 *  Target: ESP32-2432S028R "Cheap Yellow Display" + external EC11 encoder
 *
 *  Commander (2-6 players, commander damage), Riftbound (to 8) and
 *  Lorcana (to 20) counters, a High Roll (D20) in every game's menu and a
 *  Dice page (D4..D20): the 🎲 button on every table, or DICE on Home.
 *
 *  All GPIO numbers live in Config.h. This file only wires the modules
 *  together; each module is described at the top of its own header.
 * ==========================================================================*/
#include "Config.h"
#include "Log.h"
#include "AppDisplay.h"
#include "InputTouch.h"
#include "InputEncoder.h"
#include "AppStorage.h"
#include "App.h"

void setup() {
  Serial.begin(115200);
  delay(50);
  LOGF("\n=== %s v%s ===\n", FW_NAME, FW_VERSION);

  setupDisplay();      // LCD + backlight, shows the boot/check screen
  setupTouch();        // restore or run touch calibration
  setupEncoder();      // EC11 pins + interrupts
  loadState();         // restore the previous game from NVS (or defaults)
  setupApplication();  // enter the restored screen
}

void loop() {
  updateTouch();        // touch panel  -> input events
  updateEncoder();      // encoder      -> input events
  updateApplication();  // input events -> game state / screen changes
  renderIfNeeded();     // game state   -> display (only what changed)
  saveStateIfNeeded();  // game state   -> NVS, ~1.5 s after the last change
  delay(1);             // yield; encoder is interrupt-driven so nothing is missed
}
