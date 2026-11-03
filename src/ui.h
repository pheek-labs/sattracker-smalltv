#pragma once
#include <Arduino.h>

enum View : uint8_t { VIEW_ISS_MAP = 1, VIEW_ISS_PASSES, VIEW_SL_SKY, VIEW_SL_LIST, VIEW_COUNT = VIEW_SL_LIST };

void uiBegin();
void uiMessage(const char *title, const String &l1, const String &l2 = "", const String &l3 = "");
void uiProgress();          // Download-Fortschritt anzeigen
void uiLoop(time_t now);    // zeichnet die aktuelle Ansicht (einmal pro Sekunde)
void uiForceRedraw();
uint8_t uiCurrentView();
