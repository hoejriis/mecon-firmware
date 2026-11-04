#ifdef ESP_PLATFORM

#include "ESP32Board.h"
#include <target.h>

#if defined(ADMIN_PASSWORD) && !defined(DISABLE_WIFI_OTA) && defined(ENABLE_WIFI_OTA)   // Repeater or Room Server, opted in
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncElegantOTA.h>
#include <Update.h>
#include <freertos/timers.h>
#include <helpers/BleDfuPolicy.h>

#include <SPIFFS.h>

// The updater installs any image it is given, so it is never left open: each start draws a credential that is both the
// access point's WPA2 passphrase and the login of every page (the upload, the packet log, the home page). Only the
// console can start it (CliRegistry: destructive, local only), and the reply that shows the credential goes there.
static char ota_pass[13];
static const char OTA_USER[] = "ota";

// The running session (#164): one server at most, stopped after an idle period with no page request and no upload.
static AsyncWebServer* ota_server = nullptr;
static TimerHandle_t ota_timer = nullptr;
static volatile uint32_t ota_activity_ms = 0;
static char ota_reply[120];

static void otaIdleCheck(TimerHandle_t t) {
  if (!BleDfuPolicy::otaIdleStop(millis(), ota_activity_ms, Update.isRunning())) return;
  xTimerStop(t, 0);
  ota_server->end();
  delete ota_server;
  ota_server = nullptr;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  memset(ota_pass, 0, sizeof(ota_pass));
  memset(ota_reply, 0, sizeof(ota_reply));
  static_cast<ESP32Board*>(pvTimerGetTimerID(t))->setInhibitSleep(false);
  Serial.println("OTA updater stopped: idle");
}

bool ESP32Board::startOTAUpdate(const char* id, char reply[]) {
  ota_activity_ms = millis();
  if (!BleDfuPolicy::otaStartNewSession(ota_server != nullptr)) {   // already open: the same session, counted as activity
    strcpy(reply, ota_reply);
    return true;
  }
  inhibit_sleep = true;   // prevent sleep during OTA
  // The radio first: esp_random() draws from the hardware RNG only while Wi-Fi or BT RF is on, and a repeater runs with
  // Wi-Fi off, so a draw made before this could come from a weak source.
  WiFi.mode(WIFI_AP);
  static const char A[] = "abcdefghjkmnpqrstuvwxyz23456789";   // no look-alikes: it is read off a console and typed
  for (int i = 0; i < 12; i++) ota_pass[i] = A[esp_random() % (sizeof(A) - 1)];
  ota_pass[12] = 0;
  WiFi.softAP("MeshCore-OTA", ota_pass);

  sprintf(ota_reply, "Started: http://%s/update user=%s pass=%s (Wi-Fi MeshCore-OTA, the same pass)",
          WiFi.softAPIP().toString().c_str(), OTA_USER, ota_pass);
  strcpy(reply, ota_reply);
  MESH_DEBUG_PRINTLN("startOTAUpdate: started");   // never the reply: it holds the credential

  static char id_buf[60];
  sprintf(id_buf, "%s (%s)", id, getManufacturerName());
  static char home_buf[90];
  sprintf(home_buf, "<H2>Hi! I am a MeshCore Repeater. ID: %s</H2>", id);

  AsyncWebServer* server = new AsyncWebServer(80);
  ota_server = server;

  server->on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(OTA_USER, ota_pass)) return request->requestAuthentication();
    ota_activity_ms = millis();
    request->send(200, "text/html", home_buf);
  });
  server->on("/log", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->authenticate(OTA_USER, ota_pass)) return request->requestAuthentication();   // the packet log
    ota_activity_ms = millis();
    request->send(SPIFFS, "/packet_log", "text/plain");
  });

  AsyncElegantOTA.setID(id_buf);
  AsyncElegantOTA.begin(server, OTA_USER, ota_pass);    // every /update handler, the upload chunks included, logs in
  server->begin();

  if (!ota_timer) ota_timer = xTimerCreate("ota-idle", pdMS_TO_TICKS(30000), pdTRUE, this, otaIdleCheck);
  if (ota_timer) xTimerStart(ota_timer, 0);
  return true;
}

#else
bool ESP32Board::startOTAUpdate(const char* id, char reply[]) {
  return false; // not supported
}
#endif

void ESP32Board::powerOff() {
  enterDeepSleep(0); // Do not wakeup
}

void ESP32Board::enterDeepSleep(uint32_t secs) {
  // Power off the display if any
#ifdef DISPLAY_CLASS
  display.turnOff();
#endif

  // Power off LoRa
  radio_driver.powerOff();

  // Keep LoRa inactive during deepsleep
  digitalWrite(P_LORA_NSS, HIGH);
#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)
  gpio_hold_en((gpio_num_t)P_LORA_NSS);
#else
  rtc_gpio_hold_en((gpio_num_t)P_LORA_NSS);
#endif

  // Power off GPS if any
  if (sensors.getLocationProvider() != NULL) {
    sensors.getLocationProvider()->stop();
  }

  // Flush serial buffers
  Serial.flush();
  delay(100);

  // Clear stale wakeup sources to avoid ghost wakeup
  // This is required when Power Management and automatic lightsleep are enabled
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

  if (secs > 0) {
    esp_sleep_enable_timer_wakeup(secs * 1000000ULL);
  }

  // Finally set ESP32 into deepsleep
  esp_deep_sleep_start(); // CPU halts here and never returns!
}
#endif
