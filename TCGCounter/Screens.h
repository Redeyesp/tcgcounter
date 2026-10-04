#pragma once
/* ============================================================================
 *  Screens.h — the interface every screen/app module implements.
 *
 *  HOW TO ADD A NEW MODULE (e.g. Dice, which is reserved as SCREEN_DICE):
 *    1. GameState.h : add its persistent data (struct DiceState ...) to AppState
 *                     and any rules functions (diceRoll(...)) to GameState.cpp
 *                     or a new DiceGame.cpp. No drawing there.
 *    2. ScreenDice.cpp (new file): implement onEnter / handleInput / render
 *                     and define   const ScreenModule DiceScreen = {...};
 *    3. Screens.h / App.cpp : declare it and map SCREEN_DICE to it in moduleFor().
 *    4. ScreenHome.cpp : add a menu entry. GameState.cpp : stop sanitising
 *                     SCREEN_DICE to Home.
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
