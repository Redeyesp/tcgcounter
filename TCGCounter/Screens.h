#pragma once
/* ============================================================================
 *  Screens.h — the interface every screen/app module implements.
 *
 *  HOW TO ADD A NEW MODULE (e.g. the real Dice roller or Riftbound):
 *    1. GameState.h : add its persistent data (struct DiceState ...) to AppState
 *                     and any rules functions (diceRoll(...)) to GameState.cpp
 *                     or a new DiceGame.cpp. No drawing there.
 *    2. ScreenDice.cpp (new file): implement onEnter / handleInput / render
 *                     and define   const ScreenModule DiceScreen = {...};
 *    3. ScreenPlaceholder.cpp : delete the DiceScreen definition there.
 *    4. AppStorage.cpp : load/save the new AppState fields.
 *  The Screen enum, App.cpp's moduleFor() and the Home menu already know
 *  about SCREEN_DICE and SCREEN_RIFTBOUND, so nothing else changes.
 * ==========================================================================*/
#include "InputEvents.h"

struct ScreenModule {
  const char* name;
  void (*onEnter)();                       // screen just became active
  void (*handleInput)(const InputEvent&);  // one touch/encoder event
  void (*render)(bool full);               // full=true: repaint all; false: changes only
};

extern const ScreenModule HomeScreen;       // ScreenHome.cpp
extern const ScreenModule CommanderScreen;  // ScreenCommander.cpp
extern const ScreenModule DiceScreen;       // ScreenPlaceholder.cpp  (FUTURE: ScreenDice.cpp)
extern const ScreenModule RiftboundScreen;  // ScreenPlaceholder.cpp  (FUTURE: ScreenRiftbound.cpp)
