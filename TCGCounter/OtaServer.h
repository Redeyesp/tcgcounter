#pragma once
/* ============================================================================
 *  OtaServer — firmware update over Wi-Fi, no BOOT button needed.
 *
 *  otaStart() opens a Wi-Fi hotspot (TCG-Counter-XXXX, a fresh 8-digit
 *  password each time) and a small web page at http://192.168.4.1 where a
 *  phone or computer uploads the new app file (st7789-app.bin /
 *  ili9341-app.bin). It is written to the spare app slot of the flash and
 *  checked; only a complete, valid image is switched to, so a broken upload
 *  leaves the running firmware alone. The saved games (NVS) are not touched.
 *
 *  The update screen (ScreenUpdate.cpp) calls otaPoll() every loop and shows
 *  otaStatus(). While a file is coming in, the web server keeps the loop busy
 *  until the upload ends, so it calls the progress hook to let the screen
 *  draw the progress bar.
 *
 *  Wi-Fi is only on between otaStart() and otaStop().
 * ==========================================================================*/
#include <stdint.h>

enum class OtaState : uint8_t {
  Off,        // Wi-Fi off
  Waiting,    // hotspot up, waiting for a file
  Receiving,  // a file is coming in
  Done,       // new firmware written and checked: restart into it
  Failed      // the upload went wrong (error says why); the old firmware stays
};

struct OtaStatus {
  OtaState state;
  uint32_t received;     // bytes so far
  uint32_t total;        // file size (0 = not known)
  uint8_t  clients;      // devices joined to the hotspot
  char     ssid[24];
  char     password[12];
  char     address[16];  // "192.168.4.1"
  char     error[48];
};

void otaStart();
void otaStop();
void otaPoll();                      // call every loop while the update screen is open
const OtaStatus& otaStatus();
void otaRetry();                     // after Failed: wait for another file
void otaSetProgressHook(void (*hook)());
void otaRestart();                   // reboot (into the new firmware after Done)

// The app file this board takes ("st7789-app.bin" / "ili9341-app.bin").
const char* otaAppFileName();
