#pragma once
#include <Arduino.h>

#define FW_NAME    "SatTracker"
#define FW_VERSION "1.0.3"

// Hardware GeekMagic SmallTV-Ultra
#define PIN_BACKLIGHT 5   // PWM, invertiert (LOW = hell)

// Herkunft des Standorts
#define LOC_DEFAULT 0  // Voreinstellung, wird beim ersten WLAN-Kontakt per IP ermittelt
#define LOC_AUTO    1  // per IP-Adresse ermittelt
#define LOC_MANUAL  2  // in der Weboberfläche eingegeben, wird nie überschrieben

struct Settings {
  float lat = 50.111f;      // Frankfurt am Main
  float lon = 8.682f;
  float alt = 112.0f;       // Meter
  uint8_t locSrc = LOC_DEFAULT;
  char locName[33] = "";    // Ortsname bei automatischer Ermittlung
  uint8_t minEl = 10;       // Mindesthöhe über Horizont in Grad
  uint8_t brightness = 70;  // 0..100 %
  uint8_t view = 0;         // 0 = automatisch wechseln, 1..4 = feste Ansicht
  uint8_t interval = 15;    // Sekunden pro Ansicht im Automatikmodus
  char ssid[33] = "";       // leer = vom Gerät gespeichertes WLAN verwenden
  char pass[65] = "";
};

extern Settings settings;
extern bool fsOk;           // LittleFS der Original-Firmware eingebunden

void settingsLoad();
bool settingsSave();
void applyBrightness();
