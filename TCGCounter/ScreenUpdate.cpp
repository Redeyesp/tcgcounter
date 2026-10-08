/* ============================================================================
 *  ScreenUpdate — firmware update over Wi-Fi (OtaServer.h), so the board's
 *  BOOT button is never needed for updates. Home -> the version button.
 *
 *  INTRO                                   WAITING (hotspot on)
 *  ┌──────────────────────────────────────┐ ┌──────────────────────────────────────┐
 *  │ [< HOME]                    FIRMWARE │ │ ┌────────┐ 1 JOIN THE WI-FI           │
 *  │        UPDATE OVER WI-FI             │ │ │  QR    │ TCG-Counter-AB12           │
 *  │  This is v0.16.0 (ST7789 screen).    │ │ │ code   │ Password 12345678          │
 *  │  START turns on a Wi-Fi hotspot ...  │ │ └────────┘ 2 OPEN  192.168.4.1        │
 *  │ [            START WI-FI           ] │ │ Scan to join  3 UPLOAD st7789-app.bin │
 *  └──────────────────────────────────────┘ │ 1 device connected        [ CANCEL ] │
 *                                           └──────────────────────────────────────┘
 *  RECEIVING: progress bar (the upload keeps the loop busy, so the server's
 *  progress hook draws it) · DONE: restarts into the new firmware ·
 *  FAILED: why, TRY AGAIN (hotspot stays on) / HOME.
 *
 *  Wi-Fi is on only while WAITING / RECEIVING / FAILED is shown; HOME and
 *  CANCEL switch it off.
 *  Encoder: turn = pick a button, press = choose · waiting: press = CANCEL.
 * ==========================================================================*/
#include <Arduino.h>
#include "Screens.h"
#include "App.h"
#include "GameState.h"
#include "OtaServer.h"
#include "Theme.h"
#include "Ui.h"
#include "Config.h"
#include <stdio.h>

namespace {

#if DISPLAY_DRIVER == DISPLAY_DRIVER_ST7789
const char* const VARIANT = "ST7789";
#else
const char* const VARIANT = "ILI9341";
#endif
constexpr uint32_t RESTART_DELAY_MS = 1500;  // DONE stays on screen this long
constexpr uint16_t GOOD = rgb565(90, 220, 110);

// ---------------------------------------------------------------- geometry
constexpr Rect BACK_BTN   = {12, 6, 88, 34};
constexpr Rect START_BTN  = {12, 180, 296, 50};
constexpr Rect QR_BOX     = {8, 8, 134, 134};
constexpr Rect CANCEL_BTN = {196, 194, 112, 40};
constexpr Rect STATUS_BAR = {20, 104, 280, 24};
constexpr Rect RETRY_BTN  = {12, 184, 190, 48};
constexpr Rect HOME_BTN   = {210, 184, 98, 48};

enum class Page : uint8_t { Intro, Waiting, Receiving, Done, Failed };

Page pageNow() {
  switch (otaStatus().state) {
    case OtaState::Waiting:   return Page::Waiting;
    case OtaState::Receiving: return Page::Receiving;
    case OtaState::Done:      return Page::Done;
    case OtaState::Failed:    return Page::Failed;
    default:                  return Page::Intro;
  }
}

// ---------------------------------------------------------------- UI-only state
int8_t   s_focus = 1;      // intro: 0 HOME, 1 START · failed: 0 TRY AGAIN, 1 HOME
int8_t   s_pressed = -1;
uint32_t s_doneAt = 0;

// what is on screen
bool     s_needFull = true;
Page     s_drawnPage = Page::Intro;
int8_t   s_drawnFocus = -1, s_drawnPressed = -1;
uint8_t  s_drawnClients = 255;
uint32_t s_drawnReceived = 0xFFFFFFFF;

// ---------------------------------------------------------------- drawing
void text(int x, int y, const char* t, const lgfx::IFont* f, uint16_t col,
          lgfx::textdatum_t datum = lgfx::textdatum_t::middle_center) {
  auto& g = gfx();
  g.setFont(f);
  g.setTextDatum(datum);
  g.setTextColor(col);
  g.drawString(t, x, y);
}

void button(const Rect& r, const char* label, bool pressed, bool focus) {
  auto& g = gfx();
  g.fillRect(r.x, r.y, r.w, r.h, theme::BG);
  g.fillRoundRect(r.x, r.y, r.w, r.h, 10, pressed ? theme::BUTTON_DOWN : theme::BUTTON);
  if (focus) uiRoundFrame(g, r.x, r.y, r.w, r.h, 10, 3, theme::ACCENT);
  text(r.cx(), r.cy() + 1, label, theme::fontButton(), theme::TEXT);
}

void backButton(bool pressed, bool focus) {
  auto& g = gfx();
  const Rect& r = BACK_BTN;
  g.fillRect(r.x, r.y, r.w, r.h, theme::BG);
  g.fillRoundRect(r.x, r.y, r.w, r.h, 10, pressed ? theme::BUTTON_DOWN : theme::BUTTON);
  if (focus) uiRoundFrame(g, r.x, r.y, r.w, r.h, 10, 3, theme::ACCENT);
  uiChevron(g, r.x + 18, r.cy(), 14, 3, false, theme::TEXT);
  text(r.x + 32, r.cy() + 1, "HOME", theme::fontLabel(), theme::TEXT, lgfx::textdatum_t::middle_left);
}

void drawIntro() {
  auto& g = gfx();
  g.fillScreen(theme::BG);
  backButton(s_pressed == 0, s_focus == 0);
  text(308, 23, "FIRMWARE", theme::fontLabel(), theme::TEXT_DIM, lgfx::textdatum_t::middle_right);
  text(160, 64, "UPDATE OVER WI-FI", theme::fontButton(), theme::TEXT);
  char buf[160];
  snprintf(buf, sizeof(buf),
           "This is v%s (%s screen). START turns on a Wi-Fi hotspot: join it with a phone or "
           "computer and upload %s. Saved games are kept.",
           FW_VERSION, VARIANT, otaAppFileName());
  uiWrappedText(g, 18, 86, 284, 18, buf, theme::fontSmall(), theme::TEXT_DIM);
  button(START_BTN, "START WI-FI", s_pressed == 1, s_focus == 1);
}

void drawClients() {
  auto& g = gfx();
  const uint8_t n = otaStatus().clients;
  g.fillRect(0, 196, CANCEL_BTN.x - 2, 40, theme::BG);
  char buf[32];
  if (n == 0) snprintf(buf, sizeof(buf), "Waiting for a phone...");
  else        snprintf(buf, sizeof(buf), "%u device%s connected", (unsigned)n, n == 1 ? "" : "s");
  text(14, 214, buf, theme::fontSmall(), n ? GOOD : theme::TEXT_DIM, lgfx::textdatum_t::middle_left);
}

void drawWaiting() {
  auto& g = gfx();
  const OtaStatus& st = otaStatus();
  g.fillScreen(theme::BG);
  // a QR code that joins the hotspot (phones: point the camera at it)
  char qr[96];
  snprintf(qr, sizeof(qr), "WIFI:T:WPA;S:%s;P:%s;;", st.ssid, st.password);
  const Rect& q = QR_BOX;
  g.fillRoundRect(q.x, q.y, q.w, q.h, 8, TFT_WHITE);
  g.qrcode(qr, q.x + 7, q.y + 7, q.w - 14, 2);
  text(q.cx(), q.y + q.h + 12, "Scan to join", theme::fontSmall(), theme::TEXT_DIM);

  const int x = 154;
  text(x, 16, "1  JOIN THE WI-FI", theme::fontLabel(), theme::ACCENT, lgfx::textdatum_t::middle_left);
  text(x, 38, st.ssid, theme::fontLabel(), theme::TEXT, lgfx::textdatum_t::middle_left);
  text(x, 60, "Password", theme::fontSmall(), theme::TEXT_DIM, lgfx::textdatum_t::middle_left);
  text(x + 80, 61, st.password, theme::fontLabel(), theme::TEXT, lgfx::textdatum_t::middle_left);
  text(x, 90, "2  OPEN", theme::fontLabel(), theme::ACCENT, lgfx::textdatum_t::middle_left);
  text(x, 113, st.address, theme::fontButton(), theme::TEXT, lgfx::textdatum_t::middle_left);
  text(x, 142, "3  UPLOAD", theme::fontLabel(), theme::ACCENT, lgfx::textdatum_t::middle_left);
  text(x, 164, otaAppFileName(), theme::fontSmall(), theme::TEXT, lgfx::textdatum_t::middle_left);
  drawClients();
  button(CANCEL_BTN, "CANCEL", s_pressed == 0, true);
}

void drawProgress() {
  auto& g = gfx();
  const OtaStatus& st = otaStatus();
  const Rect& b = STATUS_BAR;
  const uint32_t pct = st.total ? (uint32_t)((uint64_t)st.received * 100 / st.total) : 0;
  const int fill = (int)((b.w - 4) * (pct > 100 ? 100 : pct) / 100);
  g.fillRoundRect(b.x + 2, b.y + 2, b.w - 4, b.h - 4, 6, theme::PANEL);
  if (fill > 0) g.fillRoundRect(b.x + 2, b.y + 2, fill, b.h - 4, 6, theme::ACCENT);
  char buf[48];
  if (st.total) snprintf(buf, sizeof(buf), "%u%%   %u / %u KB", (unsigned)pct, (unsigned)(st.received / 1024),
                         (unsigned)(st.total / 1024));
  else snprintf(buf, sizeof(buf), "%u KB", (unsigned)(st.received / 1024));
  g.fillRect(0, b.y + b.h + 6, 320, 26, theme::BG);
  text(160, b.y + b.h + 19, buf, theme::fontSmall(), theme::TEXT);
}

void drawReceiving() {
  auto& g = gfx();
  g.fillScreen(theme::BG);
  text(160, 60, "RECEIVING FIRMWARE", theme::fontButton(), theme::TEXT);
  const Rect& b = STATUS_BAR;
  uiRoundFrame(g, b.x, b.y, b.w, b.h, 8, 2, theme::PANEL_EDGE);
  drawProgress();
  text(160, 190, "Keep the device powered.", theme::fontSmall(), theme::TEXT_DIM);
}

void drawDone() {
  auto& g = gfx();
  g.fillScreen(theme::BG);
  text(160, 96, "UPDATE COMPLETE", theme::fontButton(), GOOD);
  text(160, 128, "Restarting into the new firmware...", theme::fontSmall(), theme::TEXT);
}

void drawFailed() {
  auto& g = gfx();
  g.fillScreen(theme::BG);
  text(160, 44, "UPDATE FAILED", theme::fontButton(), theme::DANGER);
  uiWrappedText(g, 18, 70, 284, 18, otaStatus().error, theme::fontSmall(), theme::TEXT);
  uiWrappedText(g, 18, 128, 284, 18,
                "Nothing was changed: the current firmware is still installed. The hotspot stays on.",
                theme::fontSmall(), theme::TEXT_DIM);
  button(RETRY_BTN, "TRY AGAIN", s_pressed == 0, s_focus == 0);
  button(HOME_BTN, "HOME", s_pressed == 1, s_focus == 1);
}

// ---------------------------------------------------------------- actions
void leave() {
  otaStop();
  goToScreen(SCREEN_HOME);
}

void start() {
  otaStart();
  s_pressed = -1;
  s_needFull = true;
}

// ---------------------------------------------------------------- module functions
void render(bool full);
void progressHook() { render(false); }  // the server is busy receiving: draw from inside it

void onEnter() {
  otaSetProgressHook(progressHook);
  s_focus = 1;
  s_pressed = -1;
  s_doneAt = 0;
  s_needFull = true;
}

void handleInput(const InputEvent& e) {
  const Page page = pageNow();
  switch (page) {
    case Page::Intro:
      switch (e.type) {
        case InputType::EncoderTurn:  s_focus = (int8_t)(((s_focus + e.delta) % 2 + 2) % 2); break;
        case InputType::EncoderClick: if (s_focus == 0) leave(); else start(); break;
        case InputType::TouchDown:
          s_pressed = BACK_BTN.contains(e.x, e.y) ? 0 : (START_BTN.contains(e.x, e.y) ? 1 : -1);
          if (s_pressed >= 0) s_focus = s_pressed;
          break;
        case InputType::TouchUp: {
          const int8_t r = s_pressed;
          s_pressed = -1;
          if (r < 0 || !isTap(e, TOUCH_TAP_MAX_MS)) break;
          if (r == 0) leave(); else start();
          break;
        }
        default: break;
      }
      break;
    case Page::Waiting:
      switch (e.type) {
        case InputType::EncoderClick:
        case InputType::EncoderLongPress: leave(); break;
        case InputType::TouchDown: s_pressed = CANCEL_BTN.contains(e.x, e.y) ? 0 : -1; break;
        case InputType::TouchUp: {
          const bool on = s_pressed == 0;
          s_pressed = -1;
          if (on && isTap(e, TOUCH_TAP_MAX_MS)) leave();
          break;
        }
        default: break;
      }
      break;
    case Page::Failed:
      switch (e.type) {
        case InputType::EncoderTurn:  s_focus = (int8_t)(((s_focus + e.delta) % 2 + 2) % 2); break;
        case InputType::EncoderClick: if (s_focus == 0) otaRetry(); else leave(); break;
        case InputType::TouchDown:
          s_pressed = RETRY_BTN.contains(e.x, e.y) ? 0 : (HOME_BTN.contains(e.x, e.y) ? 1 : -1);
          if (s_pressed >= 0) s_focus = s_pressed;
          break;
        case InputType::TouchUp: {
          const int8_t r = s_pressed;
          s_pressed = -1;
          if (r < 0 || !isTap(e, TOUCH_TAP_MAX_MS)) break;
          if (r == 0) otaRetry(); else leave();
          break;
        }
        default: break;
      }
      break;
    default:  // receiving / done: hands off
      break;
  }
}

void tick(uint32_t now) {
  otaPoll();
  if (otaStatus().state != OtaState::Done) { s_doneAt = 0; return; }
  if (s_doneAt == 0) s_doneAt = now ? now : 1;
  else if (now - s_doneAt >= RESTART_DELAY_MS) otaRestart();
}

void render(bool full) {
  auto& g = gfx();
  const Page page = pageNow();
  const OtaStatus& st = otaStatus();
  if (page != s_drawnPage) {
    full = true;
    if (page == Page::Failed) s_focus = 0;  // TRY AGAIN first
  }
  full = full || s_needFull;
  const bool changed = full || s_focus != s_drawnFocus || s_pressed != s_drawnPressed ||
                       (page == Page::Waiting && st.clients != s_drawnClients) ||
                       (page == Page::Receiving && st.received != s_drawnReceived);
  if (!changed) return;
  g.startWrite();
  if (full) {
    switch (page) {
      case Page::Intro:     drawIntro(); break;
      case Page::Waiting:   drawWaiting(); break;
      case Page::Receiving: drawReceiving(); break;
      case Page::Done:      drawDone(); break;
      case Page::Failed:    drawFailed(); break;
    }
  } else {
    switch (page) {
      case Page::Intro:
        backButton(s_pressed == 0, s_focus == 0);
        button(START_BTN, "START WI-FI", s_pressed == 1, s_focus == 1);
        break;
      case Page::Waiting:
        if (st.clients != s_drawnClients) drawClients();
        button(CANCEL_BTN, "CANCEL", s_pressed == 0, true);
        break;
      case Page::Receiving:
        drawProgress();
        break;
      case Page::Failed:
        button(RETRY_BTN, "TRY AGAIN", s_pressed == 0, s_focus == 0);
        button(HOME_BTN, "HOME", s_pressed == 1, s_focus == 1);
        break;
      default: break;
    }
  }
  g.endWrite();
  s_needFull = false;
  s_drawnPage = page;
  s_drawnFocus = s_focus;
  s_drawnPressed = s_pressed;
  s_drawnClients = st.clients;
  s_drawnReceived = st.received;
}

}  // namespace

const ScreenModule UpdateScreen = {"Update", onEnter, handleInput, render, tick};
