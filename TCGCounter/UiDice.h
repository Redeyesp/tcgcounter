#pragma once
/* ============================================================================
 *  DiceOverlay — the Dice page, opened on top of a game and closed back to it.
 *
 *  CHOOSER                               POPUP (after picking a die)
 *  ┌──────────────────────────────┐      ┌──────────────────────────────┐
 *  │ [< BACK]    DICE             │      │ ┌──────────────────────────┐ │
 *  │ ┌──┐ ┌──┐ ┌──┐ ┌───┐ ┌───┐   │      │ │           D20            │ │
 *  │ │/\│ │[]│ │<>│ │ ⬠ │ │ ⬡ │   │      │ │            ⬡             │ │
 *  │ │D4│ │D6│ │D8│ │D12│ │D20│   │      │ │           17             │ │
 *  │ └──┘ └──┘ └──┘ └───┘ └───┘   │      │ │  [ REROLL ]  [ BACK ]    │ │
 *  │     Tap a die to roll it     │      │ └──────────────────────────┘ │
 *  └──────────────────────────────┘      └──────────────────────────────┘
 *
 *  The die's face changes fast, slows down and lands on a real roll (ESP32
 *  hardware RNG); landed = gold. Input is ignored while it rolls.
 *  Popup: REROLL rolls the same die again, BACK returns to the game,
 *  tapping outside the popup goes back to the chooser.
 *  Encoder: turn = move the yellow focus, press = choose,
 *  long-press = back (popup -> chooser, chooser -> game).
 *
 *  The owning screen opens it, hands it input / tick / render while
 *  isOpen(), and returns to its game when handleInput() says Back.
 *  Standalone (Home -> DICE): the popup's BACK goes to the chooser instead,
 *  and only the chooser's BACK leaves.
 * ==========================================================================*/
#include <stdint.h>
#include "InputEvents.h"

enum class DiceResult : uint8_t { None, Back };

class DiceOverlay {
 public:
  void open(bool standalone = false);  // shows the chooser
  void close() { open_ = false; }
  bool isOpen() const { return open_; }

  DiceResult handleInput(const InputEvent& e);
  void tick(uint32_t now);
  void render(bool full);           // the first render after open() is always full

  // state, for tests
  bool    inPopup() const { return popup_; }
  bool    rolling() const { return rolling_; }
  uint8_t sides() const { return sides_; }
  uint8_t value() const { return value_; }

 private:
  void startRoll(uint8_t sides);
  void drawChooser();
  void drawChooserButton(int8_t i);
  void drawPopup();
  void drawPopupButton(int8_t i);
  void drawPopupDie();

  bool     open_ = false;
  bool     standalone_ = false;
  bool     popup_ = false;
  bool     needFull_ = false;
  uint8_t  sides_ = 20;
  uint8_t  value_ = 0;
  bool     rolling_ = false;
  uint32_t rollStart_ = 0, nextFlip_ = 0;
  uint32_t shuffle_ = 0x6C8E9CF5u;
  int8_t   chooserFocus_ = 5;       // 0 = BACK, 1..5 = D4..D20
  int8_t   popupFocus_ = 0;         // 0 = REROLL, 1 = BACK
  int8_t   pressed_ = -1;
  // what is on the screen
  int8_t   drawnPage_ = 0;          // 0 nothing, 1 chooser, 2 popup
  int8_t   drawnFocus_ = -1, drawnPressed_ = -1;
  uint8_t  drawnValue_ = 0;
  bool     drawnRolling_ = false;
};
