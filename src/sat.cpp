// Bahnberechnung (SGP4) für ISS und Starlink, TLE-Download von Celestrak.
//
// Starlink: Die TLEs aller Starlink-Satelliten werden einmal am Tag geladen und
// kompakt (48 Byte/Satellit) im LittleFS abgelegt. Ein Hintergrund-Durchlauf
// rechnet alle Satelliten nacheinander durch und hält die besten SL_CACHE
// Kandidaten über dem Horizont im RAM, die dann jede Sekunde genau berechnet werden.

#include "sat.h"
#include "settings.h"
#include <LittleFS.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <Sgp4.h>
#include <visible.h>

IssState iss;
StarlinkState starlink;
String satStatus;
bool satBusy = false;
void (*satProgressCb)() = nullptr;

// Bewusst HTTP: TLS (BearSSL) passt weder ins OTA-Limit noch neben den Rest in den RAM.
// Es sind öffentliche Bahndaten; jede Zeile wird per TLE-Prüfsumme kontrolliert.
static const char *URL_ISS = "http://celestrak.org/NORAD/elements/gp.php?CATNR=25544&FORMAT=tle";
static const char *URL_SL = "http://celestrak.org/NORAD/elements/gp.php?GROUP=starlink&FORMAT=tle";
static const char *ISS_PATH = "/sat_iss.tle";
static const char *SL_PATH = "/sat_starlink.bin";
static const char *SL_META = "/sat_starlink.meta";  // Download-Zeitpunkt

static const uint32_t SL_MAGIC = 0x314B4C53;  // "SLK1"
static const time_t ISS_MAX_AGE = 12 * 3600;
static const time_t SL_MAX_AGE = 24 * 3600;
static const time_t RETRY_DELAY = 30 * 60;
static const uint32_t SL_MAX_RECORDS = 15000;
static const size_t FS_RESERVE = 24 * 1024;   // Platz, der im Dateisystem frei bleiben soll

struct SlHeader {
  uint32_t magic, count, time, reserved;
};

struct SlRecord {
  uint32_t catnr;
  uint16_t slnum, flags;
  double jdEpoch;   // Julianisches Datum der Epoche
  double no;        // mittlere Bewegung in rad/min (Kozai, wie im TLE)
  float bstar, ecco, argpo, inclo, mo, nodeo;  // Winkel in rad
};
static_assert(sizeof(SlRecord) == 48, "SlRecord muss 48 Byte haben");

struct CacheEntry {
  elsetrec rec;
  SlVisible v;
  bool used;
};

static Sgp4 issSgp;
static Sgp4 work;
static CacheEntry *cache = nullptr;
static elsetrec sweepRec;   // statisch, zu groß für den Stack

static double siteLatRad, siteLonRad, siteAltKm;
static File slFile;
static uint32_t sweepIdx = 0, sweepAbove = 0, sweepVis = 0, sweepStartMs = 0;
static time_t nextIssTry = 0, nextSlTry = 0, lastIssUpdate = 0, lastTrack = 0, lastCache = 0;
static bool reloadReq = false;

static inline double jdFromUnix(time_t t) { return t / 86400.0 + 2440587.5; }

// ------------------------------------------------------------------ TLE

static double fld(const char *s, int start, int len) {
  char t[16];
  memcpy(t, s + start, len);
  t[len] = 0;
  return atof(t);
}

static bool parseTle(const char *l1, const char *l2, SlRecord &r) {
  if (strlen(l1) < 69 || strlen(l2) < 69 || l1[0] != '1' || l2[0] != '2') return false;
  if (!twolineChecksum(l1) || !twolineChecksum(l2)) return false;

  r.catnr = (uint32_t)fld(l1, 2, 5);
  int yy = (int)fld(l1, 18, 2);
  int y = yy < 57 ? 2000 + yy : 1900 + yy;
  double jan1 = 367.0 * y - floor(7.0 * y / 4.0) + 31 + 1721013.5;  // JD 1. Januar 0 Uhr
  r.jdEpoch = jan1 + fld(l1, 20, 12) - 1.0;

  char m[10] = {l1[53] == '-' ? '-' : '+', '0', '.'};
  memcpy(m + 3, l1 + 54, 5);
  m[8] = 0;
  r.bstar = atof(m) * pow(10.0, (int)fld(l1, 59, 2));

  const double d2r = PI / 180.0;
  char e[10] = {'0', '.'};
  memcpy(e + 2, l2 + 26, 7);
  e[9] = 0;
  r.inclo = fld(l2, 8, 8) * d2r;
  r.nodeo = fld(l2, 17, 8) * d2r;
  r.ecco = atof(e);
  r.argpo = fld(l2, 34, 8) * d2r;
  r.mo = fld(l2, 43, 8) * d2r;
  r.no = fld(l2, 52, 11) * 2.0 * PI / 1440.0;
  return true;
}

static bool initRec(const SlRecord &r, elsetrec &s) {
  memset(&s, 0, sizeof(s));
  s.jdsatepoch = r.jdEpoch;
  sgp4init(wgs84, 'i', r.catnr, r.jdEpoch - 2433281.5, r.bstar, r.ecco, r.argpo,
           r.inclo, r.mo, r.no, r.nodeo, s);
  s.jdsatepoch = r.jdEpoch;
  return s.error == 0;
}

// Schnelle Höhenberechnung ohne Sonnenstand
static bool quickEl(elsetrec &s, double jd, double &el) {
  double ro[3], vo[3], razel[3];
  sgp4(wgs84, s, (jd - s.jdsatepoch) * 1440.0, ro, vo);
  if (s.error) return false;
  rv2azel(ro, siteLatRad, siteLonRad, siteAltKm, jd, razel);
  el = razel[2] * 180.0 / PI;
  return true;
}

// ------------------------------------------------------------------ Download

typedef void (*LineCb)(char *line);

// Lädt eine URL und ruft cb für jede Zeile auf. Rückgabe: HTTP-Status oder <0
static int fetchLines(const char *url, LineCb cb) {
  WiFiClient client;
  HTTPClient http;
  http.useHTTP10(true);  // kein chunked encoding
  http.setTimeout(20000);
  http.setUserAgent(FW_NAME "/" FW_VERSION " (ESP8266)");
  if (!http.begin(client, url)) return -1;
  int code = http.GET();
  if (code != 200) {
    http.end();
    return code;
  }

  Stream *s = http.getStreamPtr();
  char buf[100];
  size_t len = 0;
  uint32_t last = millis();
  for (;;) {
    while (s->available()) {
      char c = s->read();
      last = millis();
      if (c == '\r') continue;
      if (c == '\n') {
        buf[len] = 0;
        cb(buf);
        len = 0;
        yield();  // Flash-Schreibzugriffe dauern, Watchdog nicht verhungern lassen
        continue;
      }
      if (len < sizeof(buf) - 1) buf[len++] = c;
    }
    if (!http.connected()) break;
    if (millis() - last > 20000) break;
    delay(1);
  }
  if (len) {
    buf[len] = 0;
    cb(buf);
  }
  http.end();
  return 200;
}

static void freeCache() {
  free(cache);
  cache = nullptr;
  starlink.shown = 0;
}

static void allocCache() {
  if (!cache) cache = (CacheEntry *)calloc(SL_CACHE, sizeof(CacheEntry));
}

static void progress() {
  if (satProgressCb) satProgressCb();
}

// --- ISS
static char dlName[25], dlL1[72], dlL2[72];

static void trimRight(char *s) {
  for (int i = strlen(s) - 1; i >= 0 && s[i] == ' '; i--) s[i] = 0;
}

static void issLine(char *line) {
  if (line[0] == '1' && line[1] == ' ') strlcpy(dlL1, line, sizeof(dlL1));
  else if (line[0] == '2' && line[1] == ' ') strlcpy(dlL2, line, sizeof(dlL2));
  else if (line[0]) {
    strlcpy(dlName, line, sizeof(dlName));
    trimRight(dlName);
  }
}

static bool issApply(const char *name, char *l1, char *l2, time_t t) {
  SlRecord dummy;
  if (!parseTle(l1, l2, dummy)) return false;
  issSgp.line1[0] = 0;  // erzwingt Neuinitialisierung
  issSgp.init(name, l1, l2);
  iss.valid = true;
  iss.tleTime = t;
  iss.passCount = 0;
  iss.trackValid = false;
  lastTrack = 0;
  return true;
}

static void issLoadFile() {
  if (!fsOk) return;
  File f = LittleFS.open(ISS_PATH, "r");
  if (!f) return;
  time_t t = f.readStringUntil('\n').toInt();
  String n = f.readStringUntil('\n'), a = f.readStringUntil('\n'), b = f.readStringUntil('\n');
  f.close();
  n.trim(); a.trim(); b.trim();
  strlcpy(dlL1, a.c_str(), sizeof(dlL1));
  strlcpy(dlL2, b.c_str(), sizeof(dlL2));
  issApply(n.c_str(), dlL1, dlL2, t);
}

static void downloadIss(time_t now) {
  satBusy = true;
  satStatus = "Lade ISS-Bahndaten...";
  progress();
  freeCache();
  dlName[0] = dlL1[0] = dlL2[0] = 0;
  int code = fetchLines(URL_ISS, issLine);
  if (code == 200 && issApply(dlName[0] ? dlName : "ISS (ZARYA)", dlL1, dlL2, now)) {
    if (fsOk) {
      File f = LittleFS.open(ISS_PATH, "w");
      if (f) {
        f.printf("%ld\n%s\n%s\n%s\n", (long)now, dlName, dlL1, dlL2);
        f.close();
      }
    }
    satStatus = "";
    nextIssTry = 0;
  } else {
    satStatus = "ISS-Download fehlgeschlagen (" + String(code) + ")";
    nextIssTry = now + RETRY_DELAY;
  }
  allocCache();
  satBusy = false;
}

// --- Starlink
static File dlOut;
static uint32_t dlCount, dlMax;

static void slOpenOut() {
  // Erst wenn echte TLE-Daten ankommen, wird die alte Datei ersetzt
  if (slFile) slFile.close();
  LittleFS.remove(SL_PATH);
  FSInfo info;
  LittleFS.info(info);
  size_t freeB = info.totalBytes > info.usedBytes ? info.totalBytes - info.usedBytes : 0;
  dlMax = freeB > FS_RESERVE ? (freeB - FS_RESERVE) / sizeof(SlRecord) : 0;
  if (dlMax > SL_MAX_RECORDS) dlMax = SL_MAX_RECORDS;
  dlOut = LittleFS.open(SL_PATH, "w");
  SlHeader h = {SL_MAGIC, 0, 0, 0};
  if (dlOut) dlOut.write((const uint8_t *)&h, sizeof(h));
}

static void slLine(char *line) {
  if (line[0] == '1' && line[1] == ' ') {
    strlcpy(dlL1, line, sizeof(dlL1));
    return;
  }
  if (line[0] == '2' && line[1] == ' ') {
    SlRecord r;
    if (dlL1[0] && parseTle(dlL1, line, r)) {
      r.slnum = strncmp(dlName, "STARLINK-", 9) == 0 ? atoi(dlName + 9) : 0;
      r.flags = 0;
      if (!dlOut) slOpenOut();
      if (dlOut && dlCount < dlMax) {
        dlOut.write((const uint8_t *)&r, sizeof(r));
        dlCount++;
        if (dlCount % 250 == 0) {
          satStatus = "Lade Starlink... " + String(dlCount);
          progress();
        }
      }
    }
    dlL1[0] = 0;
    return;
  }
  if (line[0]) {
    strlcpy(dlName, line, sizeof(dlName));
    trimRight(dlName);
  }
}

static bool slOpenSweep() {
  if (!fsOk) return false;
  slFile = LittleFS.open(SL_PATH, "r");
  if (!slFile) return false;
  SlHeader h;
  if (slFile.read((uint8_t *)&h, sizeof(h)) != sizeof(h) || h.magic != SL_MAGIC) {
    slFile.close();
    return false;
  }
  // Anzahl aus der Dateigröße: der Kopf wird nach dem Download nicht mehr angefasst,
  // so bleiben auch abgebrochene Downloads nutzbar
  starlink.count = (slFile.size() - sizeof(SlHeader)) / sizeof(SlRecord);
  if (starlink.count == 0) {
    slFile.close();
    return false;
  }
  File m = LittleFS.open(SL_META, "r");
  // Ohne Meta-Datei (ältere Version): Änderungszeit der Datei aus dem LittleFS
  starlink.fileTime = m ? m.readStringUntil('\n').toInt() : slFile.getLastWrite();
  if (m) m.close();
  sweepIdx = sweepAbove = sweepVis = 0;
  sweepStartMs = millis();
  return true;
}

static void downloadStarlink(time_t now) {
  if (!fsOk) {
    satStatus = "Kein Dateisystem - Starlink aus";
    nextSlTry = now + 24 * 3600;
    return;
  }
  satBusy = true;
  satStatus = "Lade Starlink-Bahndaten...";
  progress();
  freeCache();
  dlName[0] = dlL1[0] = 0;
  dlCount = 0;
  int code = fetchLines(URL_SL, slLine);
  if (dlOut) {
    dlOut.close();
    File m = LittleFS.open(SL_META, "w");
    if (m) {
      m.printf("%ld\n", (long)now);
      m.close();
    }
  }
  if (code == 200 && dlCount > 0) {
    satStatus = "";
    nextSlTry = 0;
  } else {
    satStatus = "Starlink-Download fehlgeschlagen (" + String(code) + ")";
    nextSlTry = now + RETRY_DELAY;
  }
  starlink.count = 0;
  slOpenSweep();
  allocCache();
  satBusy = false;
}

// ------------------------------------------------------------------ ISS

static void issCompute(time_t now) {
  issSgp.findsat((unsigned long)now);
  iss.lat = issSgp.satLat;
  iss.lon = issSgp.satLon;
  iss.altKm = issSgp.satAlt;
  iss.az = issSgp.satAz;
  iss.el = issSgp.satEl;
  iss.vis = issSgp.satVis;
}

static void issPasses(time_t now) {
  iss.passCount = 0;
  // 15 min zurück starten, damit ein laufender Überflug mitgezählt wird
  if (!issSgp.initpredpoint((unsigned long)(now - 900), settings.minEl)) return;
  passinfo p;
  for (int i = 0; i < 8 && iss.passCount < ISS_MAX_PASSES; i++) {
    if (!issSgp.nextpass(&p, 20)) break;
    IssPass &o = iss.passes[iss.passCount];
    o.start = getUnixFromJulian(p.jdstart);
    o.top = getUnixFromJulian(p.jdmax);
    o.end = getUnixFromJulian(p.jdstop);
    if (o.end < now) continue;
    o.maxEl = p.maxelevation;
    o.azStart = p.azstart;
    o.azEnd = p.azstop;
    o.visible = p.sight == lighted;
    iss.passCount++;
    yield();
  }
}

static void issTrack(time_t now) {
  // Bodenspur von -30 bis +60 Minuten
  for (int i = 0; i < TRACK_POINTS; i++) {
    issSgp.findsat((unsigned long)(now + (i - 20) * 90));
    iss.trackLat[i] = issSgp.satLat;
    iss.trackLon[i] = issSgp.satLon;
  }
  iss.trackValid = true;
}

// ------------------------------------------------------------------ Starlink-Cache

static float score(const SlVisible &v) { return (v.vis > 0 ? 1000.0f : 0.0f) + v.el; }

static void cacheOffer(const elsetrec &rec, const SlVisible &v) {
  if (!cache) return;
  int freeSlot = -1, worst = -1;
  for (int i = 0; i < SL_CACHE; i++) {
    if (!cache[i].used) {
      if (freeSlot < 0) freeSlot = i;
      continue;
    }
    if (cache[i].v.catnr == v.catnr) {
      cache[i].v = v;
      return;
    }
    if (worst < 0 || score(cache[i].v) < score(cache[worst].v)) worst = i;
  }
  int slot = freeSlot >= 0 ? freeSlot : (score(v) > score(cache[worst].v) ? worst : -1);
  if (slot < 0) return;
  memcpy(&cache[slot].rec, &rec, sizeof(rec));
  cache[slot].v = v;
  cache[slot].used = true;
}

static void cacheRefresh(time_t now) {
  if (!cache) return;
  uint8_t n = 0;
  for (int i = 0; i < SL_CACHE; i++) {
    CacheEntry &c = cache[i];
    if (!c.used) continue;
    memcpy(&work.satrec, &c.rec, sizeof(c.rec));
    work.findsat((unsigned long)now);
    c.v.az = work.satAz;
    c.v.el = work.satEl;
    c.v.altKm = work.satAlt;
    c.v.vis = work.satVis;
    if (c.v.el < settings.minEl - 1) {
      c.used = false;
      continue;
    }
    starlink.list[n++] = c.v;
  }
  // Einfügesortierung, höchstens 16 Einträge
  for (int i = 1; i < n; i++) {
    SlVisible t = starlink.list[i];
    int j = i - 1;
    while (j >= 0 && score(starlink.list[j]) < score(t)) {
      starlink.list[j + 1] = starlink.list[j];
      j--;
    }
    starlink.list[j + 1] = t;
  }
  starlink.shown = n;
}

static void sweepStep(time_t now) {
  if (!slFile && !slOpenSweep()) return;
  double jd = jdFromUnix(now);
  uint32_t t0 = millis();
  SlRecord r;
  while (millis() - t0 < 25) {
    if (sweepIdx >= starlink.count) {
      starlink.above = sweepAbove;
      starlink.visible = sweepVis;
      starlink.sweepMs = millis() - sweepStartMs;
      sweepStartMs = millis();
      sweepIdx = sweepAbove = sweepVis = 0;
      slFile.seek(sizeof(SlHeader));
      break;
    }
    sweepIdx++;
    if (slFile.read((uint8_t *)&r, sizeof(r)) != sizeof(r)) {
      sweepIdx = starlink.count;
      continue;
    }
    double el;
    if (!initRec(r, sweepRec) || !quickEl(sweepRec, jd, el) || el < settings.minEl) continue;
    memcpy(&work.satrec, &sweepRec, sizeof(sweepRec));
    work.findsat((unsigned long)now);
    SlVisible v = {r.catnr, r.slnum, (float)work.satAz, (float)work.satEl, (float)work.satAlt, work.satVis};
    sweepAbove++;
    if (v.vis > 0) sweepVis++;
    cacheOffer(sweepRec, v);
  }
}

// ------------------------------------------------------------------ öffentlich

void satSetSite() {
  siteLatRad = settings.lat * PI / 180.0;
  siteLonRad = settings.lon * PI / 180.0;
  siteAltKm = settings.alt / 1000.0;
  issSgp.site(settings.lat, settings.lon, settings.alt);
  work.site(settings.lat, settings.lon, settings.alt);
  iss.passCount = 0;
  lastIssUpdate = 0;
  if (cache) memset(cache, 0, SL_CACHE * sizeof(CacheEntry));
  starlink.shown = 0;
}

void satBegin() {
  allocCache();
  satSetSite();
  issLoadFile();
  slOpenSweep();
}

void satRequestReload() { reloadReq = true; }

String satFileInfo() {
  if (!fsOk) return "kein Dateisystem";
  File f = LittleFS.open(SL_PATH, "r");
  if (!f) return "keine Starlink-Datei";
  String s = String(f.size()) + " Bytes = " + String((f.size() - sizeof(SlHeader)) / sizeof(SlRecord)) + " Sat.";
  f.close();
  FSInfo info;
  LittleFS.info(info);
  return s + ", frei " + String((info.totalBytes - info.usedBytes) / 1024) + " KB";
}

void satLoop(time_t now) {
  if (now < 1700000000) return;  // noch keine gültige Uhrzeit
  bool reload = reloadReq;
  reloadReq = false;

  if (WiFi.status() == WL_CONNECTED) {
    if (reload || ((!iss.valid || now - iss.tleTime > ISS_MAX_AGE) && now >= nextIssTry)) {
      downloadIss(now);
      return;
    }
    if (reload || ((starlink.count == 0 || now - starlink.fileTime > SL_MAX_AGE) && now >= nextSlTry)) {
      downloadStarlink(now);
      return;
    }
  }

  if (iss.valid && now != lastIssUpdate) {
    lastIssUpdate = now;
    if (iss.passCount == 0 || now > iss.passes[0].end) issPasses(now);
    if (now - lastTrack >= 60) {
      issTrack(now);
      lastTrack = now;
    }
    issCompute(now);
  }

  if (starlink.count) {
    sweepStep(now);
    if (now != lastCache) {
      lastCache = now;
      cacheRefresh(now);
    }
  }
}

void sunSubpoint(time_t now, float &lat, float &lon) {
  double jd = jdFromUnix(now), r[3];
  sun(jd, r);
  lat = atan2(r[2], sqrt(r[0] * r[0] + r[1] * r[1])) * 180.0 / PI;
  double l = (atan2(r[1], r[0]) - gstime(jd)) * 180.0 / PI;
  while (l < -180) l += 360;
  while (l > 180) l -= 360;
  lon = l;
}
