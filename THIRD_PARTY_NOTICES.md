# Lizenzen von Drittanbietern

Die SatTracker-Firmware (`.bin`) enthält neben eigenem Code (MIT, siehe `LICENSE`)
die folgenden Bestandteile. Sie werden nicht im Repository mitgeliefert, sondern
beim Build von PlatformIO heruntergeladen.

| Bestandteil | Version | Lizenz | Quelle |
|---|---|---|---|
| Arduino-Core für ESP8266 | espressif8266@4.2.1 | LGPL-2.1 | https://github.com/esp8266/Arduino |
| TFT_eSPI (Bodmer) | 2.5.43 | FreeBSD / MIT (siehe unten) | https://github.com/Bodmer/TFT_eSPI |
| Sgp4 (Hopperpop) | 1.0.3 | MIT | https://github.com/Hopperpop/Sgp4-Library |

Die Weltkarte in `src/worldmap.h` wurde mit dem Python-Paket
[global-land-mask](https://github.com/toddkarin/global-land-mask) (MIT) erzeugt.
Es basiert auf den frei verfügbaren GLOBE-Daten der NOAA.

Die Bahndaten (TLE) lädt das Gerät zur Laufzeit von [CeleStrak](https://celestrak.org).
Im Repository oder in der Firmware sind keine Bahndaten enthalten.

Den ungefähren Standort fragt das Gerät beim ersten Start bei [ip-api.com](https://ip-api.com)
ab (kostenlose Nutzung, nicht kommerziell).

## Hinweis zur LGPL (ESP8266-Arduino-Core)

Der Arduino-Core ist statisch in die Firmware gelinkt. Der vollständige Quellcode
dieses Projekts liegt in diesem Repository. Damit lässt sich die Firmware jederzeit
mit einer geänderten Version des Cores neu bauen (`pio run`).
Den Quellcode des Cores gibt es unter https://github.com/esp8266/Arduino.

---

## Sgp4 – MIT License

The MIT License (MIT)

Copyright (c) 2016 

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

---

## TFT_eSPI – license.txt

```
The original starting point for this library was the Adafruit_ILI9341
library in January 2015.

The licence for that library is MIT.

The first evolution of the library that led to TFT_eSPI is recorded here:

https://www.instructables.com/id/Arduino-TFT-display-and-font-library/

Adafruit_ILI9341 ORIGINAL LIBRARY HEADER:

vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvStartvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
  This is our library for the Adafruit  ILI9341 Breakout and Shield
  ----> http://www.adafruit.com/products/1651

  Check out the links above for our tutorials and wiring diagrams
  These displays use SPI to communicate, 4 or 5 pins are required to
  interface (RST is optional)
  Adafruit invests time and resources providing this open source code,
  please support Adafruit and open-source hardware by purchasing
  products from Adafruit!

  Written by Limor Fried/Ladyada for Adafruit Industries.
  MIT license, all text above must be included in any redistribution
  
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^End^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^


Selected functions from the Adafruit_GFX library (as it was in 2015) have
been imported into the TFT_eSPI.cpp file and modified to improve
performance, add features and make them compatible with the ESP8266 and
ESP32.

The fonts from the Adafruit_GFX and Button functions were added later.
The fonts can be found with the license.txt file in the "Fonts\GFXFF"
folder.

The Adafruit_GFX functions are covered by the BSD licence.

Adafruit_GFX ORIGINAL LIBRARY LICENSE:

vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvStartvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv

Software License Agreement (BSD License)

Copyright (c) 2012 Adafruit Industries.  All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^End^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Due to the evolution of the TFT_eSPI library the original code may no longer
be recognisable, however in most cases the function names can be used as a
reference point since the aim is to retain a level of compatibility with
the popular Adafruit_GFX graphics functions.

Contributions from other authors are recorded on GitHub:
https://github.com/Bodmer/TFT_eSPI

The major addition to the original library was the addition of fast
rendering proportional fonts of different sizes as documented here:

https://www.instructables.com/id/Arduino-TFT-display-and-font-library/

The larger fonts are "Run Length Encoded (RLE)", this was done to
reduce the font memory footprint for AVR processors that have limited
FLASH, with the added benefit of a significant improvement in rendering
speed.

In 2016 the library evolved significantly to support the ESP8266 and then
the ESP32. In 2017 new Touch Screen functions were added and a new Sprite
class called TFT_eSprite to permit "flicker free" screen updates of complex
graphics.

In 2018 anti-aliased fonts were added along with a Processing font conversion
sketch.

In 2019 the library was adapted to be able to use it with any 32-bit Arduino
compatible processor. It will run on 8-bit and 16-bit processors but will be
slow due to extensive use of 32-bit variables.

Many of the example sketches are original work that contain code created
for my own projects. For all the original code the FreeBSD licence applies
and is compatible with the GNU GPL.

vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvStartvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv
Software License Agreement (FreeBSD License)

Copyright (c) 2023 Bodmer (https://github.com/Bodmer)

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

The views and conclusions contained in the software and documentation are those
of the authors and should not be interpreted as representing official policies,
either expressed or implied, of the FreeBSD Project.
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^End^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

```
