#pragma once
#include <Arduino.h>

// apMode: Einrichtungsmodus (eigener Access Point), safeMode: nur Firmware-Update
void webBegin(bool apMode, bool safeMode);
void webLoop();
// Neustart mit sicheren Pegeln an den Boot-Pins (GPIO0/GPIO2 hängen am Display)
void webRestart();
