"""Prüft den kompakten TLE-Parser der Firmware (parseTle in src/sat.cpp) gegen
die Referenz-Implementierung des Python-Pakets sgp4.

Aufruf:  python tools/check_parser.py starlink.tle
Benötigt: pip install sgp4 numpy
"""
import math
import struct
import sys
from sgp4.api import Satrec, WGS84

D2R = math.pi / 180


def f32(x):
    return struct.unpack("f", struct.pack("f", x))[0]


def parse_tle(l1, l2):
    """1:1 nachgebaut aus parseTle() inkl. float-Rundung wie auf dem ESP8266."""
    fld = lambda s, a, n: float(s[a:a + n])
    yy = int(fld(l1, 18, 2))
    y = 2000 + yy if yy < 57 else 1900 + yy
    jan1 = 367.0 * y - math.floor(7.0 * y / 4.0) + 31 + 1721013.5
    jd = jan1 + fld(l1, 20, 12) - 1.0
    m = ("-" if l1[53] == "-" else "+") + "0." + l1[54:59]
    bstar = float(m) * 10 ** int(fld(l1, 59, 2))
    return dict(
        catnr=int(fld(l1, 2, 5)), jd=jd, bstar=f32(bstar),
        inclo=f32(fld(l2, 8, 8) * D2R), nodeo=f32(fld(l2, 17, 8) * D2R),
        ecco=f32(float("0." + l2[26:33])), argpo=f32(fld(l2, 34, 8) * D2R),
        mo=f32(fld(l2, 43, 8) * D2R), no=fld(l2, 52, 11) * 2 * math.pi / 1440.0,
    )


def main(path):
    lines = [l.rstrip("\n\r") for l in open(path, encoding="ascii")]
    worst, n = 0.0, 0
    for i in range(len(lines) - 2):
        l1, l2 = lines[i + 1], lines[i + 2]
        if not (l1.startswith("1 ") and l2.startswith("2 ")):
            continue
        ref = Satrec.twoline2rv(l1, l2, WGS84)
        r = parse_tle(l1, l2)
        mine = Satrec()
        mine.sgp4init(WGS84, "i", r["catnr"], r["jd"] - 2433281.5, r["bstar"], 0.0, 0.0,
                      r["ecco"], r["argpo"], r["inclo"], r["mo"], r["no"], r["nodeo"])
        jd = ref.jdsatepoch + ref.jdsatepochF + 1.0  # einen Tag nach Epoche
        e1, p1, _ = ref.sgp4(math.floor(jd), jd % 1)
        e2, p2, _ = mine.sgp4(math.floor(jd), jd % 1)
        if e1 or e2:
            continue
        d = math.dist(p1, p2)
        worst = max(worst, d)
        n += 1
    print(f"{n} Satelliten geprüft, größte Abweichung nach 1 Tag: {worst * 1000:.0f} m")


if __name__ == "__main__":
    main(sys.argv[1])
