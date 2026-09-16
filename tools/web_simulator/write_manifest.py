"""Version the complete native asset set so cached JS/WASM/data never mix builds."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys

assets = Path(sys.argv[1]).resolve()
repository = Path(__file__).resolve().parents[2]
manifest = {"lvgl": "9.4.0", "devices": {}}
for device in ("pager", "tdeck", "wio-tracker-l2"):
    digest = hashlib.sha256()
    for suffix in ("js", "wasm", "data"):
        digest.update((assets / f"{device}.{suffix}").read_bytes())
    manifest["devices"][device] = {"version": digest.hexdigest()[:20]}
manifest["guide"] = hashlib.sha256((repository / "site/explorer-data.js").read_bytes()).hexdigest()[:20]
manifest["sourceCommit"] = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], cwd=repository, text=True).strip()
output = json.dumps(manifest, indent=2) + "\n"
target = assets / "manifest.json"
if not target.exists() or target.read_text(encoding="utf-8") != output:
    target.write_text(output, encoding="utf-8")
print("Native manifest: Pager + T-Deck, JS/WASM/data versioned together")
