#pragma once
/* Tiny logging helper. Set DEBUG_LOG to 0 in Config.h to silence it. */
#include <Arduino.h>
#include "Config.h"

#if DEBUG_LOG
  #define LOGF(...) Serial.printf(__VA_ARGS__)
#else
  #define LOGF(...) do { } while (0)
#endif
