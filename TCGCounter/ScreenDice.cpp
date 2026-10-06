/* ============================================================================
 *  ScreenDice — Home -> DICE: the Dice page on its own, no game behind it.
 *
 *  Pick D4 / D6 / D8 / D12 / D20, the popup rolls it; REROLL rolls again,
 *  the popup's BACK returns to the die choice, and HOME (top left of the
 *  choice) goes back to the home menu. Same page as the 🎲 button in the
 *  games (DiceOverlay, UiDice.h).
 * ==========================================================================*/
#include "Screens.h"
#include "App.h"
#include "UiDice.h"

namespace {

DiceOverlay s_dice;

void onEnter() { s_dice.open(true); }  // standalone: popup BACK = pick another die

void handleInput(const InputEvent& e) {
  if (s_dice.handleInput(e) == DiceResult::Back) goToScreen(SCREEN_HOME);
}

void render(bool full) { s_dice.render(full); }

void tick(uint32_t now) { s_dice.tick(now); }

}  // namespace

const ScreenModule DiceScreen = {"Dice", onEnter, handleInput, render, tick};
