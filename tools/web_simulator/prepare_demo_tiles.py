"""Fetch one small attributed OSM fixture region; never run during normal builds."""
from pathlib import Path
import math
import urllib.request

ROOT = Path(__file__).resolve().parent / "data" / "maps" / "base" / "osm"
zoom = 12
latitude, longitude = 31.2304, 121.4737
scale = 2**zoom
x = math.floor((longitude + 180) / 360 * scale)
y = math.floor((1 - math.asinh(math.tan(math.radians(latitude))) / math.pi) / 2 * scale)
for tx in range(x - 1, x + 2):
    for ty in range(y - 1, y + 2):
        target = ROOT / str(zoom) / str(tx) / f"{ty}.png"
        if target.exists():
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        url = f"https://tile.openstreetmap.org/{zoom}/{tx}/{ty}.png"
        request = urllib.request.Request(url, headers={"User-Agent": "TrailMateNativePreview/1.0 (https://github.com/vicliu624/trail-mate)"})
        with urllib.request.urlopen(request, timeout=30) as response:
            target.write_bytes(response.read())
        print(target.relative_to(ROOT))
