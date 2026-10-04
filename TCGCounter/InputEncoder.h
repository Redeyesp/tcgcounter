#pragma once
/* ============================================================================
 *  InputEncoder — EC11 rotary encoder + push switch → InputEvents.
 *
 *  Rotation: pin-change interrupts on A and B feed a Gray-code state table.
 *  Contact bounce produces back-and-forth (or invalid) transitions that
 *  cancel out, and a detent is only counted when the encoder settles in its
 *  resting state, so rotation is debounced without any delays.
 *
 *  Switch: polled, must be stable for ENC_BTN_DEBOUNCE_MS. A short press
 *  emits EncoderClick on release; holding emits EncoderLongPress once.
 * ==========================================================================*/

void setupEncoder();
void updateEncoder();  // call every loop
