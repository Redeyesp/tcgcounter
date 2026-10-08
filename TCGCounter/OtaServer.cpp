#include "OtaServer.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <esp_system.h>
#include "Config.h"
#include "Log.h"

namespace {

WebServer* s_server = nullptr;
OtaStatus  s_st = {OtaState::Off, 0, 0, 0, "", "", "", ""};
void     (*s_hook)() = nullptr;
uint32_t   s_hookAt = 0;

#if DISPLAY_DRIVER == DISPLAY_DRIVER_ST7789
const char* const APP_FILE = "st7789-app.bin";
const char* const OTHER_VARIANT = "ili9341";
const char* const VARIANT = "ST7789";
#else
const char* const APP_FILE = "ili9341-app.bin";
const char* const OTHER_VARIANT = "st7789";
const char* const VARIANT = "ILI9341";
#endif

// The upload page. %VERSION%, %VARIANT%, %FILE%, %OTHER% are filled in.
const char PAGE[] = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>TCG Counter update</title>
<style>
body{font-family:system-ui,sans-serif;background:#111;color:#eee;max-width:30rem;margin:1.5rem auto;padding:0 1rem}
h1{font-size:1.3rem;margin-bottom:.2rem}.v{color:#999;margin-top:0}
input,button{font-size:1rem;width:100%;box-sizing:border-box;margin:.4rem 0}
input{padding:.6rem;background:#222;color:#eee;border:1px solid #444;border-radius:.5rem}
button{padding:.9rem;background:#ffc400;color:#111;border:0;border-radius:.6rem;font-weight:700}
button:disabled{opacity:.5}progress{width:100%;height:1.1rem;margin-top:.6rem}
#s{margin-top:.8rem;min-height:1.5rem}.ok{color:#5d5}.bad{color:#f66}code{background:#222;padding:.1rem .3rem;border-radius:.3rem}
</style></head><body>
<h1>TCG Counter &mdash; firmware update</h1>
<p class="v">This device: v%VERSION% &middot; %VARIANT% screen</p>
<p>Choose <code>%FILE%</code> and press <b>Upload</b>. (Get the file first from the
project's web flasher page or a GitHub build &mdash; this Wi-Fi has no internet.)
Saved games and the touch calibration are kept.</p>
<input type="file" id="f" accept=".bin">
<button id="b">Upload</button>
<progress id="p" max="100" value="0"></progress>
<div id="s"></div>
<script>
const want='%FILE%',other='%OTHER%',f=document.getElementById('f'),b=document.getElementById('b'),
p=document.getElementById('p'),s=document.getElementById('s');
function say(t,c){s.textContent=t;s.className=c||''}
b.onclick=function(){
  const file=f.files[0];
  if(!file){say('Choose a file first.','bad');return}
  const n=file.name.toLowerCase();
  if(n.indexOf('fresh')>=0||n.indexOf('bootloader')>=0||n.indexOf('partitions')>=0||n.indexOf('boot_app0')>=0){
    say('That file is not the app. Choose '+want+'.','bad');return}
  if(n.indexOf(other)>=0&&!confirm('This looks like the '+other.toUpperCase()+' build, but this device needs '+want+'. Upload anyway?'))return;
  const x=new XMLHttpRequest();
  x.open('POST','/update?size='+file.size);
  x.upload.onprogress=function(e){if(e.lengthComputable)p.value=Math.round(e.loaded*100/e.total)};
  x.onload=function(){
    if(x.status==200){p.value=100;say('Done! The device restarts with the new firmware.','ok')}
    else{say('Failed: '+x.responseText,'bad');b.disabled=false}};
  x.onerror=function(){say('Connection lost. Check the screen, then try again.','bad');b.disabled=false};
  const d=new FormData();d.append('firmware',file,file.name);
  b.disabled=true;say('Uploading... keep this page open.');x.send(d);
};
</script></body></html>)HTML";

void fail(const char* why) {
  s_st.state = OtaState::Failed;
  snprintf(s_st.error, sizeof(s_st.error), "%s", why);
  LOGF("[ota] failed: %s\n", why);
}

void handleRoot() {
  String page(PAGE);
  page.replace("%VERSION%", FW_VERSION);
  page.replace("%VARIANT%", VARIANT);
  page.replace("%FILE%", APP_FILE);
  page.replace("%OTHER%", OTHER_VARIANT);
  s_server->send(200, "text/html", page);
}

void handleNotFound() {  // anything else: the upload page
  s_server->sendHeader("Location", "/");
  s_server->send(302, "text/plain", "");
}

void callHook(bool force) {
  if (!s_hook) return;
  const uint32_t now = millis();
  if (!force && now - s_hookAt < 150) return;  // a few frames a second is plenty
  s_hookAt = now;
  s_hook();
}

void handleUpload() {
  HTTPUpload& u = s_server->upload();
  switch (u.status) {
    case UPLOAD_FILE_START: {
      if (s_st.state == OtaState::Receiving) Update.abort();
      s_st.received = 0;
      s_st.total = (uint32_t)s_server->arg("size").toInt();
      s_st.error[0] = 0;
      LOGF("[ota] receiving %s (%u bytes)\n", u.filename.c_str(), (unsigned)s_st.total);
      if (!Update.begin(s_st.total ? s_st.total : UPDATE_SIZE_UNKNOWN, U_FLASH)) {
        fail(Update.errorString());
        break;
      }
      s_st.state = OtaState::Receiving;
      callHook(true);
      break;
    }
    case UPLOAD_FILE_WRITE:
      if (s_st.state != OtaState::Receiving) break;
      if (Update.write(u.buf, u.currentSize) != u.currentSize) {
        fail(Update.errorString());
        Update.abort();
        callHook(true);
        break;
      }
      s_st.received += u.currentSize;
      callHook(false);
      break;
    case UPLOAD_FILE_END:
      if (s_st.state != OtaState::Receiving) break;
      if (Update.end(true)) {
        s_st.state = OtaState::Done;
        LOGF("[ota] done: %u bytes\n", (unsigned)s_st.received);
      } else {
        fail(Update.errorString());
      }
      callHook(true);
      break;
    case UPLOAD_FILE_ABORTED:
      if (s_st.state == OtaState::Receiving) Update.abort();
      fail("The upload stopped halfway");
      callHook(true);
      break;
    default:
      break;
  }
}

void handleUpdateDone() {
  s_server->sendHeader("Connection", "close");
  if (s_st.state == OtaState::Done) s_server->send(200, "text/plain", "OK");
  else s_server->send(500, "text/plain", s_st.error[0] ? s_st.error : "Update failed");
}

}  // namespace

void otaStart() {
  if (s_server) return;
  const uint64_t mac = ESP.getEfuseMac();
  snprintf(s_st.ssid, sizeof(s_st.ssid), "TCG-Counter-%02X%02X", (unsigned)((mac >> 32) & 0xFF),
           (unsigned)((mac >> 40) & 0xFF));
  snprintf(s_st.password, sizeof(s_st.password), "%08lu", (unsigned long)(esp_random() % 100000000UL));
  WiFi.mode(WIFI_AP);
  WiFi.softAP(s_st.ssid, s_st.password);
  snprintf(s_st.address, sizeof(s_st.address), "%s", WiFi.softAPIP().toString().c_str());
  s_server = new WebServer(80);
  s_server->on("/", HTTP_GET, handleRoot);
  s_server->on("/update", HTTP_POST, handleUpdateDone, handleUpload);
  s_server->onNotFound(handleNotFound);
  s_server->begin();
  s_st.state = OtaState::Waiting;
  s_st.received = s_st.total = 0;
  s_st.clients = 0;
  s_st.error[0] = 0;
  LOGF("[ota] hotspot %s up at %s\n", s_st.ssid, s_st.address);
}

void otaStop() {
  if (!s_server) return;
  if (s_st.state == OtaState::Receiving) Update.abort();
  s_server->stop();
  delete s_server;
  s_server = nullptr;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  s_st.state = OtaState::Off;
  s_st.clients = 0;
  LOGF("[ota] hotspot off\n");
}

void otaPoll() {
  if (!s_server) return;
  s_server->handleClient();
  s_st.clients = (uint8_t)WiFi.softAPgetStationNum();
}

const OtaStatus& otaStatus() { return s_st; }

void otaRetry() {
  if (s_server && s_st.state == OtaState::Failed) {
    s_st.state = OtaState::Waiting;
    s_st.received = s_st.total = 0;
    s_st.error[0] = 0;
  }
}

void otaSetProgressHook(void (*hook)()) { s_hook = hook; }

void otaRestart() { ESP.restart(); }

const char* otaAppFileName() { return APP_FILE; }
