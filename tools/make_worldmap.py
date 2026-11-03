"""Erzeugt src/worldmap.h: 240x120 Land/Wasser-Bitmap (equirektangulär, 1 Bit/Pixel).

Aufruf:  python tools/make_worldmap.py
Benötigt: pip install global-land-mask numpy
"""
import os
import numpy as np
from global_land_mask import globe

W, H = 240, 120
SUB = 3  # Unterabtastung pro Pixel, Land wenn >= 1/3 der Stichproben Land

out = os.path.join(os.path.dirname(__file__), "..", "src", "worldmap.h")

rows = []
for y in range(H):
    lats = 90 - (y + (np.arange(SUB) + 0.5) / SUB) * 180 / H
    row = 0
    bits = []
    for x in range(W):
        lons = -180 + (x + (np.arange(SUB) + 0.5) / SUB) * 360 / W
        la, lo = np.meshgrid(lats, lons)
        land = globe.is_land(la.ravel(), lo.ravel()).mean() >= 1 / 3
        bits.append(1 if land else 0)
    rows.append(bits)

data = bytearray()
for bits in rows:
    for i in range(0, W, 8):
        b = 0
        for j in range(8):
            b |= bits[i + j] << (7 - j)
        data.append(b)

with open(out, "w", encoding="utf-8") as f:
    f.write("// Automatisch erzeugt von tools/make_worldmap.py - nicht von Hand bearbeiten\n")
    f.write("#pragma once\n#include <pgmspace.h>\n\n")
    f.write(f"#define WORLDMAP_W {W}\n#define WORLDMAP_H {H}\n\n")
    f.write("// Zeilenweise, MSB = linkes Pixel, 1 = Land\n")
    f.write(f"static const uint8_t WORLDMAP[{len(data)}] PROGMEM = {{\n")
    for i in range(0, len(data), 30):
        f.write("  " + ",".join(f"0x{b:02x}" for b in data[i:i + 30]) + ",\n")
    f.write("};\n")

print(f"{out}: {len(data)} Bytes")
