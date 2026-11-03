#pragma once
#include <Arduino.h>

// apMode: Einrichtungsmodus (eigener Access Point), safeMode: nur Firmware-Update
void webBegin(bool apMode, bool safeMode);
void webLoop();
