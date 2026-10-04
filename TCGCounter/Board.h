#pragma once
/* ============================================================================
 *  Board.h — LovyanGFX device description for the ESP32-2432S028R (CYD).
 *
 *  Builds the display + backlight + touch driver entirely from the constants
 *  in Config.h, so no LovyanGFX library files ever need editing.
 *  Settings mirror LovyanGFX's own auto-detect profile for this board.
 * ==========================================================================*/
#include "Gfx.h"
#include "Config.h"

class LGFX : public lgfx::LGFX_Device {
#if DISPLAY_DRIVER == DISPLAY_DRIVER_ILI9341
  lgfx::Panel_ILI9341 _panel;
#elif DISPLAY_DRIVER == DISPLAY_DRIVER_ST7789
  lgfx::Panel_ST7789 _panel;
#else
  #error "Config.h: DISPLAY_DRIVER must be DISPLAY_DRIVER_ILI9341 or DISPLAY_DRIVER_ST7789"
#endif
  lgfx::Bus_SPI       _bus;
  lgfx::Light_PWM     _light;
  lgfx::Touch_XPT2046 _touch;

 public:
  LGFX() {
    {  // ---- SPI bus shared by nothing else (HSPI) ----
      auto cfg = _bus.config();
      cfg.spi_host   = SPI2_HOST;            // SPI2 == HSPI on the classic ESP32
      cfg.spi_mode   = 0;
      cfg.freq_write = DISPLAY_SPI_WRITE_HZ;
      cfg.freq_read  = DISPLAY_SPI_READ_HZ;
      cfg.spi_3wire  = false;                // CYD has a separate MISO line
      cfg.use_lock   = true;
      cfg.pin_sclk   = PIN_TFT_SCLK;
      cfg.pin_mosi   = PIN_TFT_MOSI;
      cfg.pin_miso   = PIN_TFT_MISO;
      cfg.pin_dc     = PIN_TFT_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {  // ---- Panel ----
      auto cfg = _panel.config();
      cfg.pin_cs          = PIN_TFT_CS;
      cfg.pin_rst         = PIN_TFT_RST;
      cfg.pin_busy        = -1;
      cfg.panel_width     = 240;             // native portrait size
      cfg.panel_height    = 320;
      cfg.memory_width    = 240;
      cfg.memory_height   = 320;
      cfg.offset_x        = 0;
      cfg.offset_y        = 0;
      cfg.offset_rotation = PANEL_OFFSET_ROTATION;
      cfg.readable        = true;
      cfg.invert          = DISPLAY_INVERT_COLORS;
      cfg.rgb_order       = DISPLAY_SWAP_RED_BLUE;
      cfg.dlen_16bit      = false;
      cfg.bus_shared      = false;           // set true if the SD card shares this bus later
      _panel.config(cfg);
    }
    {  // ---- Backlight (PWM) ----
      auto cfg = _light.config();
      cfg.pin_bl      = PIN_TFT_BACKLIGHT;
      cfg.invert      = false;
      cfg.freq        = BACKLIGHT_PWM_HZ;
      cfg.pwm_channel = BACKLIGHT_PWM_CHANNEL;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    {  // ---- Touch: XPT2046 on its own pins, bit-banged SPI ----
      auto cfg = _touch.config();
      cfg.x_min           = TOUCH_DEFAULT_X_MIN;
      cfg.x_max           = TOUCH_DEFAULT_X_MAX;
      cfg.y_min           = TOUCH_DEFAULT_Y_MIN;
      cfg.y_max           = TOUCH_DEFAULT_Y_MAX;
      cfg.pin_int         = TOUCH_USE_IRQ ? PIN_TOUCH_IRQ : -1;
      cfg.bus_shared      = false;
      cfg.offset_rotation = TOUCH_OFFSET_ROTATION;
      cfg.spi_host        = -1;              // -1 = software SPI (as LovyanGFX's CYD profile)
      cfg.freq            = TOUCH_SPI_HZ;
      cfg.pin_sclk        = PIN_TOUCH_SCLK;
      cfg.pin_mosi        = PIN_TOUCH_MOSI;
      cfg.pin_miso        = PIN_TOUCH_MISO;
      cfg.pin_cs          = PIN_TOUCH_CS;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};
