#pragma once
/* ============================================================================
 *  Screens.h — the interface every screen/app module implements.
 *
 *  HOW TO ADD A NEW MODULE (a new game):
 *    1. GameState.h : add its persistent data to AppState and its rules to
 *                     GameState.cpp or a new file. No drawing there.
 *    2. ScreenXxx.cpp (new file): implement onEnter / handleInput / render
 *                     (+ tick for timers) and define const ScreenModule XxxScreen.
 *    3. GameState.h : add a Screen value at the END of the enum (SCREEN_DICE = 2
 *                     stays reserved: dice now live in DiceOverlay, UiDice.h).
 *       Screens.h / App.cpp : declare the module and map it in moduleFor().
 *    4. ScreenHome.cpp : add a menu entry.
 *    5. AppStorage.cpp : load/save the new AppState fields.
 * ==========================================================================*/
#include <stdint.h>
#include "InputEvents.h"

struct ScreenModule {
  const char* name;
  void (*onEnter)();                       // screen just became active
  void (*handleInput)(const InputEvent&);  // one touch/encoder event
  void (*render)(bool full);               // full=true: repaint all; false: changes only
  void (*tick)(uint32_t nowMs);            // optional (may be nullptr): timers, called every loop
};

extern const ScreenModule HomeScreen;       // ScreenHome.cpp
extern const ScreenModule CommanderScreen;  // ScreenCommander.cpp
extern const ScreenModule CommanderSetupScreen;  // ScreenCommanderSetup.cpp (players / new game)
extern const ScreenModule RiftboundScreen;  // ScreenScore.cpp (first to 8)
extern const ScreenModule LorcanaScreen;    // ScreenScore.cpp (first to 20 lore)
