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

/* Reads the controller's ID register (RDDID, 0x04) by toggling the SPI pins
 * by hand, before the SPI driver owns them. Diagnostic only: the result is
 * printed so the serial log shows which controller this board really has.
 *   ST7789  answers 0x85 0x85 0x52  -> use the cyd_st7789 build
 *   ILI9341 answers 0x00 0x00 0x00  -> use the cyd build                    */
static uint32_t probePanelId() {
  pinMode(PIN_TFT_CS, OUTPUT);
  digitalWrite(PIN_TFT_CS, HIGH);
  pinMode(PIN_TFT_DC, OUTPUT);
  pinMode(PIN_TFT_SCLK, OUTPUT);
  digitalWrite(PIN_TFT_SCLK, LOW);
  pinMode(PIN_TFT_MOSI, OUTPUT);
  pinMode(PIN_TFT_MISO, INPUT);
  delayMicroseconds(10);

  digitalWrite(PIN_TFT_CS, LOW);
  digitalWrite(PIN_TFT_DC, LOW);  // command
  const uint8_t cmd = 0x04;
  for (int i = 7; i >= 0; --i) {
    digitalWrite(PIN_TFT_MOSI, (cmd >> i) & 1);
    delayMicroseconds(1);
    digitalWrite(PIN_TFT_SCLK, HIGH);
    delayMicroseconds(1);
    digitalWrite(PIN_TFT_SCLK, LOW);
  }
  digitalWrite(PIN_TFT_DC, HIGH);  // data phase: clock in 32 bits (incl. dummy bits)
  uint32_t v = 0;
  for (int i = 0; i < 32; ++i) {
    digitalWrite(PIN_TFT_SCLK, HIGH);
    delayMicroseconds(1);
    v = (v << 1) | (digitalRead(PIN_TFT_MISO) ? 1u : 0u);
    digitalWrite(PIN_TFT_SCLK, LOW);
    delayMicroseconds(1);
  }
  digitalWrite(PIN_TFT_CS, HIGH);
  return v;
}

static const char* guessController(uint32_t id) {
  // The reply may be preceded by 1-8 dummy bits depending on the chip.
  for (int s = 0; s <= 8; ++s) {
    const uint32_t w = id << s;
    if (((w >> 16) & 0xFFFF) == 0x8585) return "ST7789 -> flash the st7789 build";
  }
  if (id == 0 || id == 0xFFFFFFFF) return "no ID (typical for ILI9341) -> flash the ili9341 build";
  return "unknown controller";
}

void setupDisplay() {
  const uint32_t panelId = probePanelId();
  LOGF("[display] controller ID read (0x04): 0x%08lX = %s\n", (unsigned long)panelId,
       guessController(panelId));

  s_lcd.init();
  s_lcd.setRotation(DISPLAY_ROTATION);
  s_lcd.setBrightness(BACKLIGHT_LEVEL);
  drawBootScreen();
  LOGF("[display] %dx%d, driver=%s, rotation=%d\n", (int)s_lcd.width(), (int)s_lcd.height(),
       DISPLAY_DRIVER == DISPLAY_DRIVER_ILI9341 ? "ILI9341" : "ST7789", DISPLAY_ROTATION);
}
