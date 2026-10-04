#pragma once
/* ============================================================================
 *  ConfirmDialog — full-screen yes/no question before throwing a game away.
 *
 *  ┌──────────────────────────────────────┐
 *  │              NEW GAME?               │  title
 *  │    5 players, everyone at 40 life    │  body
 *  │    The current game will be lost.    │  note (red)
 *  │   [  CANCEL  ]       [  START  ]     │  START/RESTART is red
 *  └──────────────────────────────────────┘
 *
 *  Touch: tap a button. Encoder: turn = switch, press = choose,
 *  long-press = cancel. CANCEL is pre-selected, so a stray encoder press
 *  never wipes anything.
 *
 *  The owning screen opens it, then while isOpen() hands it every input
 *  event and its render() calls; once it closes, the screen repaints itself.
 * ==========================================================================*/
#include <stdint.h>
#include "InputEvents.h"

enum class ConfirmResult : uint8_t { None, Cancel, Confirm };

class ConfirmDialog {
 public:
  void open(const char* title, const char* body, const char* note, const char* confirmLabel);
  void close() { open_ = false; }
  bool isOpen() const { return open_; }

  // Returns Cancel / Confirm when the question is answered (and closes it).
  ConfirmResult handleInput(const InputEvent& e);

  // full = repaint the whole page. The first render after open() is always full.
  void render(bool full);

 private:
  void drawButton(int8_t i);
  int8_t hitTest(int x, int y) const;

  bool   open_ = false;
  bool   needFull_ = false;
  int8_t focus_ = 0;     // 0 = cancel, 1 = confirm
  int8_t pressed_ = -1;
  int8_t drawnFocus_ = -1, drawnPressed_ = -1;
  char   title_[24] = "";
  char   body_[48] = "";
  char   note_[48] = "";
  char   confirm_[12] = "";
};
