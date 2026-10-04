#include "AppDisplay.h"
#include "Theme.h"
#include "Log.h"

static LGFX s_lcd;

LGFX& lcd() { return s_lcd; }
lgfx::LovyanGFX& gfx() { return s_lcd; }

/* Boot screen doubles as a hardware check:
 *  - text must read the right way up      -> otherwise change DISPLAY_ROTATION
 *  - the RED swatch must look red, etc.   -> otherwise DISPLAY_SWAP_RED_BLUE
 *  - background must be black             -> otherwise DISPLAY_INVERT_COLORS  */
static void drawBootScreen() {
  auto& g = s_lcd;
  const int cx = g.width() / 2;

  g.fillScreen(theme::BG);
  g.setTextDatum(lgfx::textdatum_t::middle_center);

  g.setFont(theme::fontTitle());
  g.setTextColor(theme::TEXT);
  g.drawString("TCG COUNTER", cx, 62);

  g.setFont(theme::fontSmall());
  g.setTextColor(theme::TEXT_DIM);
  g.drawString("Firmware v" FW_VERSION, cx, 98);

  // Colour-order check swatches
  struct { const char* name; uint16_t col; } sw[3] = {
    {"RED", rgb565(255, 0, 0)}, {"GREEN", rgb565(0, 255, 0)}, {"BLUE", rgb565(0, 0, 255)}};
  for (int i = 0; i < 3; ++i) {
    int x = cx - 141 + i * 96;
    g.fillRoundRect(x, 124, 90, 30, 6, sw[i].col);
    g.setTextColor(i == 2 ? theme::TEXT : theme::TEXT_ON_ACCENT);
    g.drawString(sw[i].name, x + 45, 139);
  }

  g.setTextColor(theme::TEXT_DIM);
  g.drawString("Touch & hold now to calibrate", cx, 196);
}

void setupDisplay() {
  s_lcd.init();
  s_lcd.setRotation(DISPLAY_ROTATION);
  s_lcd.setBrightness(BACKLIGHT_LEVEL);
  drawBootScreen();
  LOGF("[display] %dx%d, driver=%s, rotation=%d\n", (int)s_lcd.width(), (int)s_lcd.height(),
       DISPLAY_DRIVER == DISPLAY_DRIVER_ILI9341 ? "ILI9341" : "ST7789", DISPLAY_ROTATION);
}
