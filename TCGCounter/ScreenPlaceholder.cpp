/* ============================================================================
 *  ScreenPlaceholder — "Coming Soon" page shared by DICE and RIFTBOUND.
 *
 *  FUTURE DICE MODULE:
 *    Create ScreenDice.cpp with its own onEnter/handleInput/render and
 *    `const ScreenModule DiceScreen = {...};`, then delete the DiceScreen
 *    line at the bottom of this file. Dice state goes in GameState.h.
 *
 *  FUTURE RIFTBOUND MODULE:
 *    Same pattern: ScreenRiftbound.cpp defines RiftboundScreen; delete the
 *    RiftboundScreen line below. Riftbound game data goes in GameState.h,
 *    its NVS keys in AppStorage.cpp.
 *
 *  When both are real, this file can be deleted.
 *
 *  Touch: tap BACK.   Encoder: press = back.
 * ==========================================================================*/
#include "Screens.h"
#include "App.h"
#include "GameState.h"
#include "Theme.h"
#include "Ui.h"
#include "Config.h"

namespace {

constexpr Rect BACK = {16, 168, 288, 56};

bool s_backPressed = false;
bool s_drawnBackPressed = false;

const char* titleFor(Screen s) {
  switch (s) {
    case SCREEN_DICE:      return "DICE";
    case SCREEN_RIFTBOUND: return "RIFTBOUND";
    default:               return "";
  }
}

void drawBack() {
  auto& g = gfx();
  const uint16_t fill = s_backPressed ? theme::BUTTON_DOWN : theme::BUTTON;
  g.fillRect(BACK.x, BACK.y, BACK.w, BACK.h, theme::BG);
  g.fillRoundRect(BACK.x, BACK.y, BACK.w, BACK.h, 12, fill);
  // Chevron + label centred as one group
  g.setFont(theme::fontTitle());
  const int textW = g.textWidth("BACK");
  const int left = BACK.cx() - (textW + 26) / 2;
  uiChevron(left + 5, BACK.cy(), 18, 4, false, theme::TEXT);
  g.setTextDatum(lgfx::textdatum_t::middle_left);
  g.setTextColor(theme::TEXT);
  g.drawString("BACK", left + 26, BACK.cy() + 1);
}

void onEnter() { s_backPressed = false; }

void handleInput(const InputEvent& e) {
  switch (e.type) {
    case InputType::EncoderClick:
      goToScreen(SCREEN_HOME);
      break;
    case InputType::TouchDown:
      s_backPressed = BACK.contains(e.x, e.y);
      break;
    case InputType::TouchUp: {
      const bool released = s_backPressed;
      s_backPressed = false;
      if (released && isTap(e, TOUCH_TAP_MAX_MS)) goToScreen(SCREEN_HOME);
      break;
    }
    default:
      break;
  }
}

void render(bool full) {
  auto& g = gfx();
  if (full) {
    g.startWrite();
    g.fillScreen(theme::BG);
    g.setTextDatum(lgfx::textdatum_t::middle_center);
    g.setFont(theme::fontTitle());
    g.setTextColor(theme::TEXT);
    g.drawString(titleFor(g_state.screen), 160, 62);
    g.setFont(theme::fontBody());
    g.setTextColor(theme::ACCENT);
    g.drawString("Coming Soon", 160, 104);
    g.setFont(theme::fontSmall());
    g.setTextColor(theme::TEXT_DIM);
    g.drawString("Not part of firmware v" FW_VERSION, 160, 134);
    drawBack();
    g.endWrite();
    s_drawnBackPressed = s_backPressed;
    return;
  }
  if (s_backPressed != s_drawnBackPressed) {
    drawBack();
    s_drawnBackPressed = s_backPressed;
  }
}

}  // namespace

const ScreenModule DiceScreen      = {"Dice", onEnter, handleInput, render, nullptr};       // FUTURE: move to ScreenDice.cpp
const ScreenModule RiftboundScreen = {"Riftbound", onEnter, handleInput, render, nullptr};  // FUTURE: move to ScreenRiftbound.cpp
