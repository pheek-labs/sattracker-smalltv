// Weboberfläche: Status, Einstellungen, WLAN-Einrichtung und Firmware-Update (/update).
// /update ist bewusst in jedem Modus erreichbar, damit man immer zurück kommt.

#include "web.h"
#include "settings.h"
#include "sat.h"
#include "ui.h"
#include "geo.h"
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>

static ESP8266WebServer server(80);
static ESP8266HTTPUpdateServer updater;
static bool ap = false, safe = false;

static const char HEAD[] PROGMEM =
    "<!doctype html><html lang=de><head><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'><title>SatTracker</title><style>"
    "body{font-family:system-ui,sans-serif;background:#111;color:#ddd;max-width:640px;margin:0 auto;padding:12px}"
    "h1{font-size:1.4em}h2{font-size:1.1em;margin-top:1.4em;color:#8cf}"
    "table{border-collapse:collapse;width:100%}td{padding:3px 6px;border-bottom:1px solid #333}"
    "input,select{background:#222;color:#eee;border:1px solid #555;padding:5px;border-radius:4px}"
    "button{background:#2a6;color:#fff;border:0;padding:8px 14px;border-radius:4px;margin:4px 4px 0 0}"
    ".v{color:#fd4}a{color:#8cf}label{display:inline-block;width:150px}"
    "</style></head><body><h1>&#128752; SatTracker</h1>";

static String esc(const String &s) {
  String o;
  for (char c : s) {
    if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '&') o += "&amp;";
    else if (c == '\'') o += "&#39;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

static String fmtTime(time_t t) {
  struct tm tm;
  localtime_r(&t, &tm);
  char b[24];
  strftime(b, sizeof(b), "%d.%m. %H:%M:%S", &tm);
  return b;
}

static void begin() {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");
  server.sendContent_P(HEAD);
}

static void end() {
  server.sendContent(F("<p style='color:#666;font-size:.8em'>" FW_NAME " " FW_VERSION "</p></body></html>"));
  server.sendContent("");
}

static void row(const String &k, const String &v) {
  server.sendContent("<tr><td>" + k + "</td><td>" + v + "</td></tr>");
}

static void updateBox() {
  server.sendContent(F(
      "<h2>Firmware-Update</h2><p>Neue <code>.bin</code> hochladen &ndash; auch die Original-Firmware "
      "(FW-Smalltv-Ultra-*.bin) zum Zur&uuml;ckwechseln.</p><p><a href='/update'><button>Firmware hochladen</button></a></p>"));
}

static void wifiForm() {
  server.sendContent(F("<h2>WLAN</h2><form method=post action=/wifi>"
                       "<p><label>SSID</label><input name=ssid maxlength=32></p>"
                       "<p><label>Passwort</label><input name=pass type=password maxlength=64></p>"
                       "<button>Speichern &amp; Neustart</button></form>"));
}

static void handleRoot() {
  begin();
  if (safe) {
    server.sendContent(F("<p style='color:#f84'><b>Sicherheitsmodus:</b> Die Firmware ist mehrfach abgest&uuml;rzt. "
                         "Bitte eine andere Firmware hochladen oder neu starten.</p>"));
    updateBox();
    server.sendContent(F("<form method=post action=/reboot><button>Neustart</button></form>"));
    end();
    return;
  }
  if (ap) {
    server.sendContent(F("<p>Das Ger&auml;t konnte sich nicht mit dem WLAN verbinden. Bitte Zugangsdaten eingeben.</p>"));
    wifiForm();
    updateBox();
    end();
    return;
  }

  time_t now = time(nullptr);
  server.sendContent(F("<h2>ISS</h2><table>"));
  if (iss.valid) {
    char b[64];
    snprintf(b, sizeof(b), "%.2f&deg;, %.2f&deg; &ndash; %.0f km", iss.lat, iss.lon, iss.altKm);
    row("Position", b);
    snprintf(b, sizeof(b), "Az %.0f&deg; / El %.0f&deg;", iss.az, iss.el);
    row("Von hier aus", b);
    for (int i = 0; i < iss.passCount; i++) {
      const IssPass &p = iss.passes[i];
      snprintf(b, sizeof(b), "max %.0f&deg;%s", p.maxEl, p.visible ? " <span class=v>sichtbar</span>" : "");
      row(fmtTime(p.start) + " &ndash; " + fmtTime(p.end).substring(7), b);
    }
    row("Bahndaten", fmtTime(iss.tleTime));
  } else {
    row("Status", "keine Bahndaten");
  }
  server.sendContent(F("</table><h2>Starlink</h2><table>"));
  row("Satelliten geladen", String(starlink.count));
  row("&Uuml;ber " + String(settings.minEl) + "&deg;", String(starlink.above));
  row("Optisch sichtbar", "<span class=v>" + String(starlink.visible) + "</span>");
  row("Durchlauf", String(starlink.sweepMs / 1000.0, 1) + " s");
  if (starlink.fileTime) row("Bahndaten", fmtTime(starlink.fileTime));
  for (int i = 0; i < starlink.shown; i++) {
    const SlVisible &v = starlink.list[i];
    char b[80];
    snprintf(b, sizeof(b), "Az %.0f&deg; El %.0f&deg; %.0f km %s", v.az, v.el, v.altKm,
             v.vis > 0 ? "<span class=v>sichtbar</span>" : (v.vis == 0 ? "Schatten" : "Tag"));
    row(v.slnum ? "STARLINK-" + String(v.slnum) : "#" + String(v.catnr), b);
  }
  server.sendContent(F("</table><h2>System</h2><table>"));
  row("Uhrzeit", fmtTime(now));
  row("WLAN", esc(WiFi.SSID()) + " (" + String(WiFi.RSSI()) + " dBm)");
  row("Freier Speicher", String(ESP.getFreeHeap()) + " Bytes");
  row("Dateisystem", fsOk ? "OK" : "<span style='color:#f84'>nicht eingebunden</span>");
  row("Laufzeit", String(millis() / 60000) + " min");
  row("Letzter Neustart", esc(ESP.getResetReason()));
  if (ESP.getResetInfoPtr()->reason == REASON_EXCEPTION_RST) row("Absturz-Info", esc(ESP.getResetInfo()));
  row("Starlink-Datei", esc(satFileInfo()));
  if (satStatus.length()) row("Meldung", esc(satStatus));
  server.sendContent(F("</table>"));

  server.sendContent(F("<h2>Einstellungen</h2>"));
  String loc;
  if (settings.locSrc == LOC_AUTO)
    loc = "automatisch per IP-Adresse" + (settings.locName[0] ? " (" + esc(settings.locName) + ")" : String());
  else if (settings.locSrc == LOC_MANUAL) loc = "manuell eingegeben";
  else loc = "Voreinstellung, noch nicht ermittelt";
  if (geoStatus.length() && geoStatus != "OK") loc += " &ndash; letzter Versuch: " + esc(geoStatus);
  server.sendContent("<p>Standort: " + loc + "</p>"
                     "<form method=post action=/locate><button style='background:#468'>Standort automatisch ermitteln</button></form>"
                     "<p style='color:#888;font-size:.85em'>Fragt ip-api.com nach dem ungef&auml;hren Standort deines "
                     "Internetanschlusses. Dabei wird deine IP-Adresse an den Dienst &uuml;bermittelt.</p>");

  char b[512];
  snprintf(b, sizeof(b),
           "<form method=post action=/save>"
           "<p><label>Breite (&deg;N)</label><input name=lat value='%.5f' size=10></p>"
           "<p><label>L&auml;nge (&deg;O)</label><input name=lon value='%.5f' size=10></p>"
           "<p><label>H&ouml;he (m)</label><input name=alt value='%.0f' size=6></p>"
           "<p><label>Mindesth&ouml;he (&deg;)</label><input name=minEl value='%u' size=4></p>"
           "<p><label>Helligkeit (%%)</label><input name=brightness type=range min=0 max=100 value='%u'></p>"
           "<p><label>Wechsel alle (s)</label><input name=interval value='%u' size=4></p>",
           settings.lat, settings.lon, settings.alt, settings.minEl, settings.brightness, settings.interval);
  server.sendContent(b);
  static const char *names[] = {"Automatisch wechseln", "ISS Karte", "ISS &Uuml;berfl&uuml;ge", "Starlink Himmel", "Starlink Liste"};
  server.sendContent(F("<p><label>Ansicht</label><select name=view>"));
  for (int i = 0; i <= 4; i++)
    server.sendContent("<option value=" + String(i) + (settings.view == i ? " selected>" : ">") + names[i] + "</option>");
  server.sendContent(F("</select></p><button>Speichern</button></form>"
                       "<form method=post action=/reload><button style='background:#468'>Bahndaten neu laden</button></form>"
                       "<p style='color:#888;font-size:.85em'>Celestrak bittet, Daten h&ouml;chstens alle 2 Stunden abzurufen.</p>"));
  wifiForm();
  updateBox();
  server.sendContent(F("<form method=post action=/reboot><button style='background:#844'>Neustart</button></form>"));
  end();
}

static void redirect() {
  server.sendHeader("Location", "/");
  server.send(303);
}

static void handleSave() {
  float lat = settings.lat, lon = settings.lon;
  if (server.hasArg("lat")) settings.lat = constrain(server.arg("lat").toFloat(), -90.0f, 90.0f);
  if (server.hasArg("lon")) settings.lon = constrain(server.arg("lon").toFloat(), -180.0f, 180.0f);
  if (server.hasArg("alt")) settings.alt = server.arg("alt").toFloat();
  // Nur geänderte Koordinaten zählen als manuelle Eingabe, sonst bleibt "automatisch" stehen
  if (fabs(settings.lat - lat) > 1e-4 || fabs(settings.lon - lon) > 1e-4) {
    settings.locSrc = LOC_MANUAL;
    settings.locName[0] = 0;
  }
  if (server.hasArg("minEl")) settings.minEl = constrain(server.arg("minEl").toInt(), 0, 60);
  if (server.hasArg("brightness")) settings.brightness = constrain(server.arg("brightness").toInt(), 0, 100);
  if (server.hasArg("interval")) settings.interval = constrain(server.arg("interval").toInt(), 3, 255);
  if (server.hasArg("view")) settings.view = constrain(server.arg("view").toInt(), 0, 4);
  settingsSave();
  applyBrightness();
  satSetSite();
  uiForceRedraw();
  redirect();
}

static void handleWifi() {
  strlcpy(settings.ssid, server.arg("ssid").c_str(), sizeof(settings.ssid));
  strlcpy(settings.pass, server.arg("pass").c_str(), sizeof(settings.pass));
  bool ok = settingsSave();
  begin();
  server.sendContent(ok ? F("<p>Gespeichert. Das Ger&auml;t startet neu und verbindet sich mit dem WLAN.</p>")
                        : F("<p style='color:#f84'>Speichern fehlgeschlagen (kein Dateisystem).</p>"));
  end();
  if (ok) {
    delay(500);
    ESP.restart();
  }
}

static void handleApi() {
  char b[256];
  snprintf(b, sizeof(b),
           "{\"fw\":\"" FW_VERSION "\",\"time\":%ld,\"heap\":%u,"
           "\"iss\":{\"valid\":%d,\"lat\":%.3f,\"lon\":%.3f,\"alt\":%.1f,\"az\":%.1f,\"el\":%.1f,\"vis\":%d,\"next\":%ld},"
           "\"starlink\":{\"count\":%u,\"above\":%u,\"visible\":%u,\"sweepMs\":%u}}",
           (long)time(nullptr), ESP.getFreeHeap(), iss.valid, iss.lat, iss.lon, iss.altKm, iss.az, iss.el, iss.vis,
           iss.passCount ? (long)iss.passes[0].start : 0L, starlink.count, starlink.above, starlink.visible,
           starlink.sweepMs);
  server.send(200, "application/json", b);
}

void webBegin(bool apMode, bool safeMode) {
  ap = apMode;
  safe = safeMode;
  server.on("/", handleRoot);
  server.on("/api", handleApi);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/wifi", HTTP_POST, handleWifi);
  server.on("/locate", HTTP_POST, [] {
    if (geoLocate()) {
      satSetSite();
      uiForceRedraw();
    }
    redirect();
  });
  server.on("/reload", HTTP_POST, [] {
    satRequestReload();
    redirect();
  });
  server.on("/reboot", HTTP_POST, [] {
    redirect();
    delay(300);
    ESP.restart();
  });
  updater.setup(&server, "/update");
  server.onNotFound([] {
    if (ap) redirect();  // Captive-Portal-ähnlich
    else server.send(404, "text/plain", "nicht gefunden");
  });
  server.begin();
}

void webLoop() { server.handleClient(); }
