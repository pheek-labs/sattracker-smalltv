// SatTracker für GeekMagic SmallTV-Ultra: ISS- und Starlink-Tracking.
//
// Sicherheitsnetz, weil das Gerät nur per WLAN geflasht werden kann:
//  - Weboberfläche mit /update startet vor allem anderen
//  - nach 3 Abstürzen in Folge startet nur noch der Sicherheitsmodus (WLAN + /update)
//  - ohne WLAN öffnet das Gerät den Access Point "SatTracker-Setup"

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <LittleFS.h>
#include <time.h>
#include "settings.h"
#include "sat.h"
#include "ui.h"
#include "web.h"
#include "geo.h"

static const char *TZ_DE = "CET-1CEST,M3.5.0,M10.5.0/3";
static const char *AP_SSID = "SatTracker-Setup";
static const char *AP_PASS = "sattracker";
static const uint32_t RTC_MAGIC = 0x5A7B0001;
static const uint32_t RTC_SLOT = 100;  // Blöcke à 4 Byte, Anfang gehört eboot/OTA

struct RtcBoot {
  uint32_t magic, crashes;
};

static bool safeMode = false, apMode = false, crashesCleared = false;
static uint32_t apSince = 0, lastGeoTry = 0;
static const uint32_t GEO_RETRY_MS = 30UL * 60 * 1000;

static void writeCrashes(uint32_t n) {
  RtcBoot b = {RTC_MAGIC, n};
  ESP.rtcUserMemoryWrite(RTC_SLOT, (uint32_t *)&b, sizeof(b));
}

static void checkCrashes() {
  RtcBoot b;
  ESP.rtcUserMemoryRead(RTC_SLOT, (uint32_t *)&b, sizeof(b));
  if (b.magic != RTC_MAGIC) b.crashes = 0;
  uint32_t r = ESP.getResetInfoPtr()->reason;
  bool crash = r == REASON_EXCEPTION_RST || r == REASON_SOFT_WDT_RST || r == REASON_WDT_RST;
  b.crashes = crash ? b.crashes + 1 : 0;
  writeCrashes(b.crashes);
  safeMode = b.crashes >= 3;
}

static bool connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.hostname("sattracker");
  // Ohne eigene Zugangsdaten: die von der Original-Firmware gespeicherten verwenden
  if (settings.ssid[0]) WiFi.begin(settings.ssid, settings.pass);
  else WiFi.begin();
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
    uiMessage(FW_NAME " " FW_VERSION, safeMode ? "SICHERHEITSMODUS" : "Verbinde mit WLAN...",
              settings.ssid[0] ? String(settings.ssid) : WiFi.SSID(), String(i / 2) + " s");
    delay(500);
  }
  return WiFi.status() == WL_CONNECTED;
}

void setup() {
  Serial.begin(115200);
  checkCrashes();
  uiBegin();
  uiMessage(FW_NAME " " FW_VERSION, safeMode ? "SICHERHEITSMODUS" : "Starte...");

  LittleFSConfig cfg;
  cfg.setAutoFormat(false);  // niemals das Dateisystem der Original-Firmware formatieren
  LittleFS.setConfig(cfg);
  fsOk = LittleFS.begin();
  settingsLoad();
  applyBrightness();

  if (connectWifi()) {
    MDNS.begin("sattracker");
    MDNS.addService("http", "tcp", 80);
    uiMessage(safeMode ? "SICHERHEITSMODUS" : "WLAN verbunden", WiFi.localIP().toString(),
              "http://sattracker.local", safeMode ? "Bitte Firmware neu flashen" : "");
  } else {
    apMode = true;
    apSince = millis();
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    uiMessage("WLAN-Einrichtung", String("WLAN: ") + AP_SSID, String("Passwort: ") + AP_PASS, "http://192.168.4.1");
  }

  configTime(TZ_DE, "pool.ntp.org", "time.nist.gov");
  webBegin(apMode, safeMode);
  if (safeMode || apMode) return;

  if (settings.locSrc == LOC_DEFAULT) {
    lastGeoTry = millis();
    if (geoLocate()) uiMessage("Standort ermittelt", settings.locName, WiFi.localIP().toString());
  }

  satProgressCb = uiProgress;
  satBegin();
  delay(1500);  // IP-Adresse kurz stehen lassen
}

void loop() {
  webLoop();
  MDNS.update();

  if (!crashesCleared && millis() > 120000) {
    writeCrashes(0);  // 2 Minuten stabil gelaufen
    crashesCleared = true;
  }

  if (safeMode || apMode) {
    // Im Einrichtungsmodus nach 10 Minuten ohne Besucher neu versuchen (z. B. Router war aus)
    if (apMode && millis() - apSince > 600000 && WiFi.softAPgetStationNum() == 0) ESP.restart();
    delay(2);
    return;
  }

  // Standort noch nicht ermittelt (z. B. Dienst war nicht erreichbar): später erneut versuchen
  if (settings.locSrc == LOC_DEFAULT && millis() - lastGeoTry > GEO_RETRY_MS) {
    lastGeoTry = millis();
    if (geoLocate()) satSetSite();
  }

  time_t now = time(nullptr);
  satLoop(now);

  static time_t lastUi = 0;
  if (now != lastUi) {
    lastUi = now;
    if (now < 1700000000) uiMessage(FW_NAME, "Warte auf Uhrzeit (NTP)...", WiFi.localIP().toString());
    else uiLoop(now);
  }
  delay(1);
}
