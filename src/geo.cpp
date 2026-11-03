// Standortbestimmung über die IP-Adresse. Genau genug für Satelliten (Stadt/Region),
// HTTP wie bei den Bahndaten, weil TLS nicht in den Speicher passt.

#include "geo.h"
#include "settings.h"
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

String geoStatus;

// Kostenlos ohne Schlüssel, nur über HTTP, max. 45 Anfragen/min
static const char *URL_GEO = "http://ip-api.com/json/?fields=status,lat,lon,city";

// Wert hinter "key": aus einer flachen JSON-Antwort, ohne JSON-Bibliothek
static bool jsonValue(const String &js, const char *key, String &out) {
  String k = String('"') + key + "\":";
  int i = js.indexOf(k);
  if (i < 0) return false;
  i += k.length();
  if (js[i] == '"') {
    int e = js.indexOf('"', i + 1);
    if (e < 0) return false;
    out = js.substring(i + 1, e);
  } else {
    int e = i;
    while (e < (int)js.length() && js[e] != ',' && js[e] != '}') e++;
    out = js.substring(i, e);
  }
  return true;
}

bool geoLocate() {
  if (WiFi.status() != WL_CONNECTED) {
    geoStatus = "kein WLAN";
    return false;
  }
  WiFiClient client;
  HTTPClient http;
  http.setTimeout(10000);
  http.setUserAgent(FW_NAME "/" FW_VERSION " (ESP8266)");
  if (!http.begin(client, URL_GEO)) {
    geoStatus = "Verbindung fehlgeschlagen";
    return false;
  }
  int code = http.GET();
  String js = code == 200 ? http.getString() : String();
  http.end();
  if (code != 200) {
    geoStatus = "HTTP " + String(code);
    return false;
  }

  String status, lat, lon, city;
  if (!jsonValue(js, "status", status) || status != "success" || !jsonValue(js, "lat", lat) ||
      !jsonValue(js, "lon", lon)) {
    geoStatus = "ungültige Antwort";
    return false;
  }
  float la = lat.toFloat(), lo = lon.toFloat();
  if (la < -90 || la > 90 || lo < -180 || lo > 180 || (la == 0 && lo == 0)) {
    geoStatus = "ungültige Koordinaten";
    return false;
  }
  jsonValue(js, "city", city);

  settings.lat = la;
  settings.lon = lo;
  settings.locSrc = LOC_AUTO;
  strlcpy(settings.locName, city.c_str(), sizeof(settings.locName));
  settingsSave();
  geoStatus = "OK";
  return true;
}
