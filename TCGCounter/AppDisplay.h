#pragma once
/* ============================================================================
 *  AppDisplay — owns the physical LCD object.
 * ==========================================================================*/
#include "Board.h"

LGFX& lcd();              // the hardware device (touch + calibration need this)

void setupDisplay();      // init panel, rotation, backlight; shows boot screen
