#pragma once
#include <Arduino.h>

// Ungefährer Standort über die öffentliche IP-Adresse (ip-api.com).
// Setzt bei Erfolg settings.lat/lon/locName, locSrc = LOC_AUTO und speichert.
// Die Höhe bleibt unverändert, der Dienst liefert keine.
bool geoLocate();
extern String geoStatus;  // Ergebnis des letzten Versuchs für die Weboberfläche
