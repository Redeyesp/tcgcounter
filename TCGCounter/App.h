#pragma once
/* ============================================================================
 *  App — the screen state machine.
 *
 *  updateApplication() hands every queued InputEvent to the active screen.
 *  renderIfNeeded() asks the active screen to draw; each screen compares the
 *  state with what it last drew and only repaints what changed.
 * ==========================================================================*/
#include "GameState.h"

void setupApplication();   // call once after loadState()
void updateApplication();  // consume input events
void renderIfNeeded();     // draw changes (no-op when nothing changed)

void goToScreen(Screen s);
void requestFullRedraw();  // repaint the whole active screen on next render
