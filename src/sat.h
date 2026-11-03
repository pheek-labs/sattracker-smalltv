#pragma once
#include <Arduino.h>
// Sgp4.h bewusst nicht hier einbinden: dessen globales enum {none, ...} kollidiert mit ESP8266WebServer

// ---- ISS ----
struct IssPass {
  time_t start, top, end;
  float maxEl, azStart, azEnd;
  bool visible;  // angestrahlt bei dunklem Himmel
};

#define ISS_MAX_PASSES 4
#define TRACK_POINTS 61

struct IssState {
  bool valid = false;        // TLE geladen
  time_t tleTime = 0;        // Zeitpunkt des Downloads
  float lat, lon, altKm, az, el;
  int16_t vis;               // -2 unter Horizont, -1 Tag, 0 Erdschatten, >0 sichtbar
  IssPass passes[ISS_MAX_PASSES];
  uint8_t passCount = 0;
  float trackLat[TRACK_POINTS], trackLon[TRACK_POINTS];
  bool trackValid = false;
};

// ---- Starlink ----
struct SlVisible {
  uint32_t catnr;
  uint16_t slnum;   // Nummer aus "STARLINK-1234"
  float az, el, altKm;
  int16_t vis;
};

#define SL_CACHE 16

struct StarlinkState {
  uint32_t count = 0;        // Satelliten in der Datei
  time_t fileTime = 0;
  uint16_t above = 0;        // im letzten Durchlauf über minEl
  uint16_t visible = 0;      // davon optisch sichtbar
  uint32_t sweepMs = 0;      // Dauer eines kompletten Durchlaufs
  uint8_t shown = 0;         // Einträge in list[]
  SlVisible list[SL_CACHE];  // sortiert: sichtbar zuerst, dann nach Höhe
};

extern IssState iss;
extern StarlinkState starlink;
extern String satStatus;     // Text für Anzeige/Web (Download-Fortschritt, Fehler)
extern bool satBusy;         // Download läuft
extern void (*satProgressCb)();  // wird während Downloads aufgerufen (Anzeige aktualisieren)

void satBegin();
void satSetSite();                // nach Änderung des Standorts aufrufen
void satLoop(time_t now);         // regelmäßig aufrufen
void satRequestReload();          // Daten beim nächsten Durchlauf neu laden
String satFileInfo();             // Diagnose: Größe/Kopf der Starlink-Datei, freier Platz

// Subsolarpunkt für die Tag/Nacht-Grenze der Karte
void sunSubpoint(time_t now, float &lat, float &lon);
