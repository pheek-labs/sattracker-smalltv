#include "settings.h"
#include <LittleFS.h>

Settings settings;
bool fsOk = false;

static const char *CFG_PATH = "/sattracker.cfg";

static void copyStr(char *dst, size_t n, const String &v) {
  strlcpy(dst, v.c_str(), n);
}

void settingsLoad() {
  if (!fsOk) return;
  File f = LittleFS.open(CFG_PATH, "r");
  if (!f) return;
  bool hasSrc = false;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    int eq = line.indexOf('=');
    if (eq <= 0) continue;
    String k = line.substring(0, eq);
    String v = line.substring(eq + 1);
    if (k == "lat") settings.lat = v.toFloat();
    else if (k == "lon") settings.lon = v.toFloat();
    else if (k == "alt") settings.alt = v.toFloat();
    else if (k == "minEl") settings.minEl = constrain(v.toInt(), 0, 60);
    else if (k == "brightness") settings.brightness = constrain(v.toInt(), 0, 100);
    else if (k == "view") settings.view = constrain(v.toInt(), 0, 4);
    else if (k == "interval") settings.interval = constrain(v.toInt(), 3, 255);
    else if (k == "locSrc") settings.locSrc = constrain(v.toInt(), 0, 2), hasSrc = true;
    else if (k == "locName") copyStr(settings.locName, sizeof(settings.locName), v);
    else if (k == "ssid") copyStr(settings.ssid, sizeof(settings.ssid), v);
    else if (k == "pass") copyStr(settings.pass, sizeof(settings.pass), v);
  }
  f.close();
  // Konfiguration von vor 1.0.3: gespeicherter Standort gilt als manuell eingegeben
  if (!hasSrc) settings.locSrc = LOC_MANUAL;
}

bool settingsSave() {
  if (!fsOk) return false;
  File f = LittleFS.open(CFG_PATH, "w");
  if (!f) return false;
  f.printf("lat=%.5f\nlon=%.5f\nalt=%.0f\n", settings.lat, settings.lon, settings.alt);
  f.printf("locSrc=%u\nlocName=%s\n", settings.locSrc, settings.locName);
  f.printf("minEl=%u\nbrightness=%u\nview=%u\ninterval=%u\n",
           settings.minEl, settings.brightness, settings.view, settings.interval);
  f.printf("ssid=%s\npass=%s\n", settings.ssid, settings.pass);
  f.close();
  return true;
}

void applyBrightness() {
  // Hintergrundbeleuchtung ist invertiert angeschlossen
  analogWrite(PIN_BACKLIGHT, 255 - map(settings.brightness, 0, 100, 0, 255));
}
