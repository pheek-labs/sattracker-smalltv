// Anzeige auf dem 240x240 ST7789. Kein Vollbild-Puffer (zu wenig RAM):
// statische Teile werden einmal gezeichnet, bewegte Teile gezielt überschrieben.
// Die eingebauten Schriften kennen keine Umlaute, daher ae/oe/ue.

#include "ui.h"
#include "sat.h"
#include "settings.h"
#include "worldmap.h"
#include <TFT_eSPI.h>

static TFT_eSPI tft;

#define C_BG      TFT_BLACK
#define C_HEAD    0x10A8   // dunkelblau
#define C_GRID    0x2945
#define C_DIM     0x8410
#define C_TRACK   0xFFE0
#define C_PAST    0x6B4D
#define C_SL_SHAD 0x3A55
#define C_NEW     0xF81F

#define MAP_Y 20
#define SKY_CX 120
#define SKY_CY 130
#define SKY_R  86
#define LX 7      // linker Rand (Gehäuse verdeckt die Kante)

static uint8_t view = 0;
static bool full = true;
static time_t viewSince = 0, mapDrawn = 0;
static uint32_t passHash = 0;
static int16_t issX = -100, issY = -100;
static int16_t prevPts[SL_CACHE + 1][2];
static uint8_t prevCount = 0;

static const char *DIRS[8] = {"N", "NO", "O", "SO", "S", "SW", "W", "NW"};
static const char *WDAY[7] = {"So", "Mo", "Di", "Mi", "Do", "Fr", "Sa"};

static const char *dir(float az) { return DIRS[(int)((az + 22.5f) / 45.0f) % 8]; }

static void header(const char *title) {
  tft.fillRect(0, 0, 240, 20, C_HEAD);
  tft.setTextColor(TFT_WHITE, C_HEAD);
  tft.setTextPadding(0);
  tft.drawString(title, 4, 2, 2);
}

static void headerTime(time_t now) {
  struct tm t;
  localtime_r(&now, &t);
  char b[12];
  strftime(b, sizeof(b), "%H:%M:%S", &t);
  tft.setTextColor(TFT_WHITE, C_HEAD);
  tft.setTextDatum(TR_DATUM);
  tft.setTextPadding(60);
  tft.drawString(b, 236, 2, 2);
  tft.setTextDatum(TL_DATUM);
  tft.setTextPadding(0);
}

// Text mit Hintergrund und fester Breite, überschreibt alten Inhalt ohne Flackern
static int16_t text(const String &s, int16_t x, int16_t y, uint16_t fg, int16_t pad = 0, uint8_t font = 2) {
  tft.setTextColor(fg, C_BG);
  tft.setTextPadding(pad);
  int16_t w = tft.drawString(s, x, y, font);
  tft.setTextPadding(0);
  return w;
}

static void deg(int16_t x, int16_t y, uint16_t c) { tft.drawCircle(x + 2, y + 3, 2, c); }

// Zahl mit Gradzeichen, gibt die x-Position dahinter zurück
static int16_t numDeg(const String &s, int16_t x, int16_t y, uint16_t c) {
  int16_t w = text(s, x, y, c);
  tft.fillRect(x + w, y, 7, 16, C_BG);
  deg(x + w + 1, y, c);
  return x + w + 7;
}

static String hhmm(time_t t) {
  struct tm tm;
  localtime_r(&t, &tm);
  char b[8];
  strftime(b, sizeof(b), "%H:%M", &tm);
  return b;
}

static String dayDate(time_t t) {
  struct tm tm;
  localtime_r(&t, &tm);
  char b[16];
  snprintf(b, sizeof(b), "%s %02d.%02d.", WDAY[tm.tm_wday], tm.tm_mday, tm.tm_mon + 1);
  return b;
}

static String duration(long s) {
  if (s < 0) s = 0;
  char b[16];
  if (s >= 3600) snprintf(b, sizeof(b), "%ldh %02ldm", s / 3600, (s / 60) % 60);
  else snprintf(b, sizeof(b), "%ldm %02lds", s / 60, s % 60);
  return b;
}

static const char *visText(int16_t v) {
  if (v == -2) return "unter Horizont";
  if (v == -1) return "Tageslicht";
  if (v == 0) return "im Erdschatten";
  return "SICHTBAR";
}

// ------------------------------------------------------------------ Weltkarte

static float colCos[WORLDMAP_W];
static float sunSin, sunCos;

static void mapPrepare(time_t now) {
  float sLat, sLon;
  sunSubpoint(now, sLat, sLon);
  sunSin = sin(sLat * DEG_TO_RAD);
  sunCos = cos(sLat * DEG_TO_RAD);
  for (int x = 0; x < WORLDMAP_W; x++) {
    float lon = -180.0f + (x + 0.5f) * 360.0f / WORLDMAP_W;
    colCos[x] = cos((lon - sLon) * DEG_TO_RAD);
  }
}

static void mapRows(int y0, int y1, int x0, int x1) {
  static uint16_t line[WORLDMAP_W];
  y0 = max(y0, 0); y1 = min(y1, WORLDMAP_H - 1);
  x0 = max(x0, 0); x1 = min(x1, WORLDMAP_W - 1);
  if (x0 > x1 || y0 > y1) return;
  for (int y = y0; y <= y1; y++) {
    float lat = (90.0f - (y + 0.5f) * 180.0f / WORLDMAP_H) * DEG_TO_RAD;
    float a = sin(lat) * sunSin, b = cos(lat) * sunCos;
    for (int x = x0; x <= x1; x++) {
      bool land = (pgm_read_byte(&WORLDMAP[y * (WORLDMAP_W / 8) + x / 8]) >> (7 - (x & 7))) & 1;
      float sunH = a + b * colCos[x];  // Sinus der Sonnenhöhe
      uint16_t c;
      if (sunH > 0.0f) c = land ? 0x3D07 : 0x0A33;          // Tag
      else if (sunH > -0.1f) c = land ? 0x2384 : 0x0909;    // Dämmerung
      else c = land ? 0x1A02 : 0x0005;                       // Nacht
      line[x] = c;
    }
    tft.pushImage(x0, MAP_Y + y, x1 - x0 + 1, 1, line + x0);
  }
}

static void mapXY(float lat, float lon, int16_t &x, int16_t &y) {
  x = (int16_t)((lon + 180.0f) / 360.0f * WORLDMAP_W);
  y = (int16_t)((90.0f - lat) / 180.0f * WORLDMAP_H);
}

static void mapOverlay() {
  int16_t x, y;
  if (iss.trackValid) {
    for (int i = 0; i < TRACK_POINTS; i++) {
      mapXY(iss.trackLat[i], iss.trackLon[i], x, y);
      tft.fillRect(x, MAP_Y + y, 2, 2, i < 20 ? C_PAST : C_TRACK);
    }
  }
  mapXY(settings.lat, settings.lon, x, y);
  tft.drawFastHLine(x - 3, MAP_Y + y, 7, TFT_GREEN);
  tft.drawFastVLine(x, MAP_Y + y - 3, 7, TFT_GREEN);
}

static void drawIssMap(time_t now) {
  if (full) {
    header("ISS - Position");
    tft.fillRect(0, MAP_Y + WORLDMAP_H, 240, 240 - MAP_Y - WORLDMAP_H, C_BG);
  }
  if (!iss.valid) {
    text("Keine ISS-Bahndaten", 10, 160, TFT_ORANGE, 220);
    text(satStatus, 10, 180, C_DIM, 220);
    return;
  }
  if (full || now - mapDrawn >= 60) {
    mapPrepare(now);
    mapRows(0, WORLDMAP_H - 1, 0, WORLDMAP_W - 1);
    mapDrawn = now;
    issX = -100;
  } else if (issX > -100) {
    mapRows(issY - 6, issY + 6, issX - 6, issX + 6);  // alten Punkt löschen
  }
  mapOverlay();
  mapXY(iss.lat, iss.lon, issX, issY);
  tft.fillCircle(issX, MAP_Y + issY, 3, TFT_RED);
  tft.drawCircle(issX, MAP_Y + issY, 5, TFT_WHITE);

  char b[48];
  int16_t x = numDeg(String(fabs(iss.lat), 1), LX, 143, TFT_WHITE);
  x += text(iss.lat >= 0 ? "N   " : "S   ", x, 143, TFT_WHITE);
  x = numDeg(String(fabs(iss.lon), 1), x, 143, TFT_WHITE);
  x += text(iss.lon >= 0 ? "O" : "W", x, 143, TFT_WHITE);
  text("   " + String((int)iss.altKm) + " km", x, 143, TFT_WHITE, 240 - x);
  snprintf(b, sizeof(b), "Az %3.0f  El %3.0f", iss.az, iss.el);
  int16_t w = text(b, LX, 159, TFT_WHITE, 110);
  deg(LX + w + 1, 159, TFT_WHITE);
  text(visText(iss.vis), 124, 159, iss.vis > 0 ? TFT_YELLOW : C_DIM, 112);

  if (iss.passCount == 0) {
    text("Kein Ueberflug gefunden", LX, 177, C_DIM, 226);
    return;
  }
  const IssPass &p = iss.passes[0];
  bool now_in = now >= p.start && now <= p.end;
  static time_t shownStart = 0;
  if (full || p.start != shownStart) {
    tft.fillRect(0, 193, 240, 16, C_BG);
    shownStart = p.start;
  }
  text(now_in ? "Ueberflug JETZT:" : "Naechster Ueberflug:", LX, 177, now_in ? TFT_GREEN : TFT_CYAN, 226);
  snprintf(b, sizeof(b), "%s %s  max %.0f", dayDate(p.start).c_str(), hhmm(p.start).c_str(), p.maxEl);
  w = text(b, LX, 193, TFT_WHITE, 0);
  deg(LX + w + 1, 193, TFT_WHITE);
  text(p.visible ? " *" : "", LX + w + 7, 193, TFT_YELLOW, 20);
  String c = now_in ? String("noch ") + duration(p.end - now) : String("in ") + duration(p.start - now);
  c += String("  ") + dir(p.azStart) + " > " + dir(p.azEnd);
  text(c, LX, 209, now_in ? TFT_GREEN : TFT_WHITE, 226);
}

// ------------------------------------------------------------------ ISS-Überflüge

static void drawIssPasses(time_t now) {
  uint32_t h = iss.passCount ? (uint32_t)iss.passes[0].start + iss.passCount : 1;
  if (full || h != passHash) {
    header("ISS - Ueberfluege");
    tft.fillRect(0, 20, 240, 220, C_BG);
    passHash = h;
    if (!iss.valid || iss.passCount == 0) {
      text(iss.valid ? "Kein Ueberflug gefunden" : "Keine ISS-Bahndaten", 10, 100, TFT_ORANGE);
      return;
    }
    for (int i = 0; i < iss.passCount; i++) {
      const IssPass &p = iss.passes[i];
      int16_t y = 24 + i * 42;
      uint16_t c = p.visible ? TFT_YELLOW : TFT_WHITE;
      text(dayDate(p.start) + "  " + hhmm(p.start) + " - " + hhmm(p.end), LX, y, c);
      char b[40];
      snprintf(b, sizeof(b), "max %.0f", p.maxEl);
      int16_t w = text(b, LX, y + 18, C_DIM);
      deg(LX + w + 1, y + 18, C_DIM);
      text(String(dir(p.azStart)) + " > " + dir(p.azEnd), 90, y + 18, C_DIM);
      text(p.visible ? "sichtbar" : "nicht sichtbar", 150, y + 18, p.visible ? TFT_YELLOW : C_DIM);
    }
  }
  if (iss.passCount) {
    const IssPass &p = iss.passes[0];
    String s = now < p.start ? "Start in " + duration(p.start - now) : "laeuft noch " + duration(p.end - now);
    text(s, LX, 193, TFT_CYAN, 226);
  }
  long age = (now - iss.tleTime) / 3600;
  text("Bahndaten vor " + String(age) + " h  |  min " + String(settings.minEl), LX, 212, C_DIM, 226, 1);
}

// ------------------------------------------------------------------ Starlink

static void slCounts(int16_t y) {
  String s = "ueber " + String(settings.minEl) + ": " + String(starlink.above);
  int16_t w = text(s, LX, y, TFT_WHITE, 0);
  deg(LX + w + 1, y, TFT_WHITE);
  text("  sichtbar: " + String(starlink.visible) + "   ", LX + w + 6, y, TFT_YELLOW, 0);
}

static bool slNoData() {
  if (starlink.count) return false;
  text("Keine Starlink-Daten", 10, 100, TFT_ORANGE, 220);
  text(satStatus, 10, 120, C_DIM, 220);
  return true;
}

static void skyXY(float az, float el, int16_t &x, int16_t &y) {
  float r = SKY_R * (90.0f - max(el, 0.0f)) / 90.0f;
  x = SKY_CX + (int16_t)(r * sin(az * DEG_TO_RAD));
  y = SKY_CY - (int16_t)(r * cos(az * DEG_TO_RAD));
}

static void skyGrid() {
  tft.drawCircle(SKY_CX, SKY_CY, SKY_R, C_DIM);
  tft.drawCircle(SKY_CX, SKY_CY, SKY_R * 2 / 3, C_GRID);
  tft.drawCircle(SKY_CX, SKY_CY, SKY_R / 3, C_GRID);
  tft.drawFastVLine(SKY_CX, SKY_CY - SKY_R, 2 * SKY_R, C_GRID);
  tft.drawFastHLine(SKY_CX - SKY_R, SKY_CY, 2 * SKY_R, C_GRID);
  tft.setTextColor(TFT_WHITE);
  tft.drawString("N", SKY_CX + 4, SKY_CY - SKY_R + 2, 2);
  tft.drawString("S", SKY_CX + 4, SKY_CY + SKY_R - 16, 2);
  tft.drawString("W", SKY_CX - SKY_R + 3, SKY_CY + 2, 2);
  tft.drawString("O", SKY_CX + SKY_R - 12, SKY_CY + 2, 2);
}

static void drawSky(time_t now) {
  if (full) {
    header("Starlink - Himmel");
    tft.fillRect(0, 20, 240, 220, C_BG);
    prevCount = 0;
    if (starlink.count) {
      tft.setTextColor(TFT_YELLOW);
      tft.drawString("gelb: sichtbar", LX, 220, 1);
      tft.setTextColor(C_NEW);
      tft.setTextDatum(TR_DATUM);
      tft.drawString("lila: neu/Zug", 240 - LX, 220, 1);
      tft.setTextDatum(TL_DATUM);
    }
  }
  if (slNoData()) return;
  slCounts(22);

  for (int i = 0; i < prevCount; i++) tft.fillCircle(prevPts[i][0], prevPts[i][1], 5, C_BG);
  prevCount = 0;
  skyGrid();

  int16_t x, y;
  for (int i = starlink.shown - 1; i >= 0; i--) {  // hellste zuletzt, liegen oben
    const SlVisible &v = starlink.list[i];
    skyXY(v.az, v.el, x, y);
    if (v.altKm < 450) tft.drawCircle(x, y, 5, C_NEW);
    if (v.vis > 0) tft.fillCircle(x, y, 3, TFT_YELLOW);
    else tft.fillCircle(x, y, 2, v.vis == 0 ? C_SL_SHAD : C_DIM);
    prevPts[prevCount][0] = x;
    prevPts[prevCount][1] = y;
    prevCount++;
  }
  if (iss.valid && iss.el > 0) {
    skyXY(iss.az, iss.el, x, y);
    tft.fillCircle(x, y, 4, TFT_RED);
    prevPts[prevCount][0] = x;
    prevPts[prevCount][1] = y;
    prevCount++;
  }
}

static void drawSlList(time_t now) {
  const int ROWS = 9;
  if (full) {
    header("Starlink - Liste");
    tft.fillRect(0, 20, 240, 220, C_BG);
  }
  if (slNoData()) return;
  slCounts(22);
  for (int i = 0; i < ROWS; i++) {
    int16_t y = 40 + i * 19;
    if (i >= starlink.shown) {
      tft.fillRect(0, y, 240, 18, C_BG);
      continue;
    }
    const SlVisible &v = starlink.list[i];
    uint16_t c = v.vis > 0 ? TFT_YELLOW : (v.vis == 0 ? C_SL_SHAD : C_DIM);
    text(v.slnum ? "SL-" + String(v.slnum) : "#" + String(v.catnr), LX, y, c, 69);
    text(String(dir(v.az)), 76, y, TFT_WHITE, 28);
    int16_t w = text("El " + String((int)v.el), 106, y, TFT_WHITE, 0);
    deg(106 + w + 1, y, TFT_WHITE);
    tft.fillRect(106 + w + 6, y, 10, 16, C_BG);
    String st = v.vis > 0 ? "hell" : (v.vis == 0 ? "Schatten" : "Tag");
    if (v.altKm < 450) st += " neu";
    text(st, 160, y, v.altKm < 450 ? C_NEW : c, 80);
  }
  String more = starlink.above > starlink.shown ? "+" + String(starlink.above - starlink.shown) + " weitere, Durchlauf " +
                                                      String(starlink.sweepMs / 1000) + " s"
                                                : "Durchlauf " + String(starlink.sweepMs / 1000) + " s";
  text(more, LX, 213, C_DIM, 226, 1);
}

// ------------------------------------------------------------------ öffentlich

void uiBegin() {
  tft.init();
  tft.setRotation(0);
  tft.setSwapBytes(true);
  tft.fillScreen(C_BG);
  analogWriteRange(255);
  analogWriteFreq(1000);
  applyBrightness();
}

void uiMessage(const char *title, const String &l1, const String &l2, const String &l3) {
  tft.fillScreen(C_BG);
  header(title);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_WHITE, C_BG);
  tft.drawString(l1, 120, 80, 2);
  tft.setTextColor(TFT_CYAN, C_BG);
  tft.drawString(l2, 120, 110, 2);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString(l3, 120, 140, 2);
  tft.setTextDatum(TL_DATUM);
  full = true;
}

static bool progressShown = false;

void uiProgress() {
  if (!progressShown) {
    tft.fillScreen(C_BG);
    header("Daten laden");
    text("Bahndaten von celestrak.org", 10, 80, TFT_WHITE);
    progressShown = true;
  }
  text(satStatus, 10, 120, TFT_CYAN, 225);
  text("Heap frei: " + String(ESP.getFreeHeap()), 10, 150, C_DIM, 225);
  full = true;  // danach Ansicht neu aufbauen
  yield();
}

void uiForceRedraw() { full = true; }

uint8_t uiCurrentView() { return view; }

static bool viewUsable(uint8_t v) {
  if (v == VIEW_ISS_MAP || v == VIEW_ISS_PASSES) return iss.valid;
  return starlink.count > 0;
}

void uiLoop(time_t now) {
  progressShown = false;
  uint8_t want = view;
  if (settings.view) want = settings.view;
  else if (view == 0 || now - viewSince >= settings.interval) {
    want = view;
    for (int i = 0; i < VIEW_COUNT; i++) {
      want = want % VIEW_COUNT + 1;
      if (viewUsable(want)) break;
    }
  }
  if (want != view) {
    view = want;
    viewSince = now;
    full = true;
  }

  switch (view) {
    case VIEW_ISS_MAP: drawIssMap(now); break;
    case VIEW_ISS_PASSES: drawIssPasses(now); break;
    case VIEW_SL_SKY: drawSky(now); break;
    case VIEW_SL_LIST: drawSlList(now); break;
  }
  headerTime(now);
  full = false;
}
