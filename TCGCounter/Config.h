#pragma once
/* ============================================================================
 *  Config.h  —  HARDWARE CONFIGURATION
 *
 *  This is the ONLY file in the project that contains GPIO numbers.
 *  If your CYD revision differs, or when the encoder wiring is final,
 *  edit this file and nothing else.
 *
 *  Sections:
 *    1. Rotary encoder (EC11)         <-- you will edit this
 *    2. Display (ILI9341 / ST7789)
 *    3. Touch controller (XPT2046)
 *    4. Other on-board CYD hardware   (listed only so the pin check knows them)
 *    5. Behaviour tunables            (timings, save delay, debug log)
 *    6. Compile-time pin sanity checks
 * ==========================================================================*/

#define FW_NAME     "TCG Counter"
#define FW_VERSION  "0.5.1"

/* ============================================================================
 *  1. ROTARY ENCODER (EC11 with push switch)
 * ----------------------------------------------------------------------------
 *  PROVISIONAL PINS — replace with the free GPIOs you confirm on your board.
 *
 *  On a stock ESP32-2432S028R the only free GPIOs on the connectors are:
 *      GPIO 35  (P3 connector)   input-only, NO internal pull-up
 *      GPIO 22  (P3 and CN1)     full GPIO, internal pull-up available
 *      GPIO 27  (CN1 connector)  full GPIO, internal pull-up available
 *
 *  Default assignment below (KY-040 module) and why:
 *      ENC_A  = 22  A/CLK  (CN1), internal pull-up.
 *      ENC_B  = 35  B/DT   (P3). Needs a pull-up: the KY-040 board has one on
 *                   DT (10 kΩ to its + pin), otherwise fit 10 kΩ from GPIO35
 *                   to 3.3 V. If this pin is left floating, the quadrature
 *                   decoder rejects the noise (one channel alone can never
 *                   form a valid step), so it cannot fake rotation.
 *      ENC_SW = 27  push switch (CN1), internal pull-up. The switch is kept
 *                   OFF GPIO35 on purpose: a floating switch input WOULD
 *                   cause phantom button presses (most KY-040 boards have
 *                   no pull-up on SW).
 *
 *  KY-040 wiring: one 4-wire cable on CN1 + one wire to P3
 *      CN1: GND -> GND   3.3V -> +   IO22 -> CLK   IO27 -> SW
 *      P3:  IO35 -> DT
 *  (Bare EC11: common pin and one switch leg to GND, A -> ENC_A, B -> ENC_B,
 *   other switch leg -> ENC_SW, and a 10 kΩ pull-up on GPIO35.)
 *
 *  Never use 3.3 V-incompatible modules: power encoder modules from 3.3 V,
 *  not 5 V.
 * ==========================================================================*/
#define ENC_ENABLED              1    // 0 = ignore the encoder completely

#define ENC_A                    22   // A / CLK  (CN1)
#define ENC_B                    35   // B / DT   (P3)
#define ENC_SW                   27   // push switch

#define ENC_AB_INTERNAL_PULLUP   1    // enable ESP32 pull-ups on A/B (ignored on GPIO 34-39)
#define ENC_SW_INTERNAL_PULLUP   1    // enable ESP32 pull-up on SW   (ignored on GPIO 34-39)
#define ENC_SW_ACTIVE_LOW        1    // 1 = switch connects SW to GND when pressed
#define ENC_REVERSE              0    // 1 = swap clockwise / counter-clockwise
#define ENC_STEPS_PER_DETENT     4    // 4 for most EC11 (20 detent / 20 pulse),
                                      // 2 for 30 detent / 15 pulse types, 1 = raw

/* ============================================================================
 *  2. DISPLAY  (SPI bus "HSPI")
 * ----------------------------------------------------------------------------
 *  Most ESP32-2432S028R boards use an ILI9341. The two-USB-port revision
 *  ("CYD2USB") uses an ST7789. Symptoms of the wrong choice: white or
 *  garbled screen, mirrored image, or inverted colours.
 * ==========================================================================*/
#define DISPLAY_DRIVER_ILI9341   1
#define DISPLAY_DRIVER_ST7789    2
#ifndef DISPLAY_DRIVER                // platformio.ini can override per build env
#define DISPLAY_DRIVER           DISPLAY_DRIVER_ILI9341
#endif

#define PIN_TFT_SCLK             14
#define PIN_TFT_MOSI             13
#define PIN_TFT_MISO             12
#define PIN_TFT_DC               2
#define PIN_TFT_CS               15
#define PIN_TFT_RST              -1   // tied to the ESP32 EN/reset line on the CYD
#define PIN_TFT_BACKLIGHT        21   // some CYD-family boards use 27 — check yours

#define DISPLAY_ROTATION         1    // 1 or 3 = landscape. Swap if the UI is upside down.
#define DISPLAY_INVERT_COLORS    0    // 1 if black shows as white
#define DISPLAY_SWAP_RED_BLUE    0    // 1 if red and blue are swapped
#define DISPLAY_SPI_WRITE_HZ     40000000
#define DISPLAY_SPI_READ_HZ      16000000
#define BACKLIGHT_LEVEL          200  // 0-255
#define BACKLIGHT_PWM_CHANNEL    7
#define BACKLIGHT_PWM_HZ         12000

/* Panel/touch orientation offsets. Values match LovyanGFX's own
 * auto-detect profile for the ESP32-2432S028. Touch calibration corrects
 * any remaining touch orientation error, so you normally never touch these. */
#if DISPLAY_DRIVER == DISPLAY_DRIVER_ILI9341
  #define PANEL_OFFSET_ROTATION  2
  #define TOUCH_OFFSET_ROTATION  0
#else
  #define PANEL_OFFSET_ROTATION  0
  #define TOUCH_OFFSET_ROTATION  2
#endif

/* ============================================================================
 *  3. TOUCH CONTROLLER  XPT2046 (resistive, on its own SPI pins)
 * ==========================================================================*/
#define PIN_TOUCH_SCLK           25
#define PIN_TOUCH_MOSI           32
#define PIN_TOUCH_MISO           39
#define PIN_TOUCH_CS             33
#define PIN_TOUCH_IRQ            36   // wired on the CYD; only used if TOUCH_USE_IRQ = 1
#define TOUCH_USE_IRQ            0    // 0 = poll (same as LovyanGFX's CYD profile)
#define TOUCH_SPI_HZ             1000000

/* Raw ranges used only until the first calibration has been saved. */
#define TOUCH_DEFAULT_X_MIN      300
#define TOUCH_DEFAULT_X_MAX      3900
#define TOUCH_DEFAULT_Y_MIN      3700
#define TOUCH_DEFAULT_Y_MAX      200

/* ============================================================================
 *  4. OTHER ON-BOARD CYD HARDWARE  (not used by V0.1)
 *  Listed so the compile-time check below can refuse encoder pins that
 *  collide with them.
 * ==========================================================================*/
#define PIN_SD_CS                5
#define PIN_SD_MOSI              23
#define PIN_SD_MISO              19
#define PIN_SD_SCLK              18
#define PIN_LED_RED              4    // RGB LED, active LOW
#define PIN_LED_GREEN            16
#define PIN_LED_BLUE             17
#define PIN_LDR                  34   // light sensor (input-only)
#define PIN_SPEAKER              26   // speaker amplifier
#define PIN_BOOT_BUTTON          0    // BOOT button (strapping pin)
#define PIN_UART_TX              1    // USB serial
#define PIN_UART_RX              3

/* ============================================================================
 *  5. BEHAVIOUR TUNABLES
 * ==========================================================================*/
#define SAVE_DELAY_MS            1500 // write NVS this long after the LAST change
#define SPLASH_MS                1500 // boot screen; touch/hold BOOT here to recalibrate
#define TOUCH_CAL_ON_FIRST_BOOT  1    // run calibration automatically if none saved

#define TOUCH_POLL_MS            10   // touch sampling period
#define TOUCH_PRESS_SAMPLES      2    // consecutive samples needed to accept a press
#define TOUCH_RELEASE_MS         60   // no-touch time needed to accept a release
#define TOUCH_MIN_PRESSURE       1    // raise (e.g. 3-6) if light brushes register
#define TOUCH_REPEAT_DELAY_MS    500  // hold +/- this long before auto-repeat starts
#define TOUCH_REPEAT_INTERVAL_MS 120  // auto-repeat rate while held
#define TOUCH_TAP_MAX_MS         1200 // navigation buttons ignore presses held longer
#define TOUCH_SWIPE_MIN_PX       40   // horizontal travel that counts as a swipe
#define TOUCH_MOVE_CANCEL_PX     18   // movement that stops hold-to-repeat (it's a swipe)
#define CMD_MODE_TIMEOUT_MS      10000 // commander-damage mode closes after this idle time (0 = never)
#define COMMANDER_FACE_SEATS     1    // 1 = every card faces the player at its table edge
                                      //     (top row upside down, head of table sideways)
                                      // 0 = all cards upright for someone at the bottom edge

#define ENC_BTN_DEBOUNCE_MS      25   // switch must be stable this long
#define ENC_LONG_PRESS_MS        800  // long-press event (reserved, unused in V0.1)

#define DEBUG_LOG                1    // 1 = print input events/state to Serial @115200

/* ============================================================================
 *  6. COMPILE-TIME PIN SANITY CHECKS
 *  If you get a build error here, the encoder pin you chose is unusable.
 * ==========================================================================*/
// ESP32 chip rule: GPIO 34-39 are input-only and have no internal pull-up.
#define GPIO_HAS_INTERNAL_PULLUP(p) ((p) < 34)

#ifdef __cplusplus
namespace pincheck {
constexpr bool isBoardPin(int p) {
  return p == PIN_TFT_SCLK  || p == PIN_TFT_MOSI   || p == PIN_TFT_MISO   ||
         p == PIN_TFT_DC    || p == PIN_TFT_CS     || p == PIN_TFT_RST    ||
         p == PIN_TFT_BACKLIGHT ||
         p == PIN_TOUCH_SCLK || p == PIN_TOUCH_MOSI || p == PIN_TOUCH_MISO ||
         p == PIN_TOUCH_CS   || p == PIN_TOUCH_IRQ  ||
         p == PIN_SD_CS  || p == PIN_SD_MOSI || p == PIN_SD_MISO || p == PIN_SD_SCLK ||
         p == PIN_LED_RED || p == PIN_LED_GREEN || p == PIN_LED_BLUE ||
         p == PIN_LDR || p == PIN_SPEAKER || p == PIN_BOOT_BUTTON ||
         p == PIN_UART_TX || p == PIN_UART_RX;
}
constexpr bool isValidGpio(int p) {
  return p >= 0 && p <= 39 && p != 20 && p != 24 && !(p >= 28 && p <= 31);
}
constexpr bool isFlashPin(int p) { return p >= 6 && p <= 11; }
}  // namespace pincheck

#if ENC_ENABLED
static_assert(pincheck::isValidGpio(ENC_A) && pincheck::isValidGpio(ENC_B) &&
              pincheck::isValidGpio(ENC_SW),
              "Config.h: an encoder pin is not a valid ESP32 GPIO number");
static_assert(!pincheck::isFlashPin(ENC_A) && !pincheck::isFlashPin(ENC_B) &&
              !pincheck::isFlashPin(ENC_SW),
              "Config.h: GPIO 6-11 are wired to the SPI flash chip and cannot be used");
static_assert(!pincheck::isBoardPin(ENC_A),
              "Config.h: ENC_A collides with a GPIO already used by CYD hardware");
static_assert(!pincheck::isBoardPin(ENC_B),
              "Config.h: ENC_B collides with a GPIO already used by CYD hardware");
static_assert(!pincheck::isBoardPin(ENC_SW),
              "Config.h: ENC_SW collides with a GPIO already used by CYD hardware");
static_assert(ENC_A != ENC_B && ENC_A != ENC_SW && ENC_B != ENC_SW,
              "Config.h: ENC_A, ENC_B and ENC_SW must be three different GPIOs");
static_assert(ENC_STEPS_PER_DETENT == 1 || ENC_STEPS_PER_DETENT == 2 ||
              ENC_STEPS_PER_DETENT == 4,
              "Config.h: ENC_STEPS_PER_DETENT must be 1, 2 or 4");
#endif
#endif  // __cplusplus
