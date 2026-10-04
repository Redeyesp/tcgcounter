#pragma once
/* ============================================================================
 *  Gfx.h — the drawing surface used by all UI code.
 *
 *  Screens draw through gfx(), a generic LovyanGFX canvas. On the device it
 *  is the CYD's LCD (see AppDisplay.cpp). Keeping UI code on this generic
 *  type means screens never touch hardware-specific objects.
 * ==========================================================================*/
#ifndef LGFX_USE_V1
#define LGFX_USE_V1
#endif
#include <LovyanGFX.hpp>

lgfx::LovyanGFX& gfx();
