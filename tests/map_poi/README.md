# Offline POI contracts

These host tests exercise the production cJSON adapter, bounded tile source,
snapshot selection, and the existing map worker/generation regression suite.
Supply cJSON 1.7.17's source and header (the version used by the ESP32 SDK).

```sh
cmake -S tests/map_poi -B build/poi-host -DCJSON_DIR=/path/to/cJSON
cmake --build build/poi-host --config Debug
ctest --test-dir build/poi-host -C Debug --output-on-failure
```

Run `map_poi_contract` with a generated `maps/poi` directory to additionally
validate every JSONL row and the z16–18 hybrid package manifest. Use a Debug
build: these tests deliberately use assertions, like the existing map tests.

The companion `tests/map_poi_view` target builds the production `PoiOverlay`
against LVGL 9.4.0 and a deterministic host font adapter. It verifies actual
marker pixels, repeated refresh, clearing, and parent-first destruction, while
asserting that no marker/label child widgets are ever created.

```sh
cmake -S tests/map_poi_view -B build/poi-view -DLVGL_DIR=/path/to/lvgl
cmake --build build/poi-view --config Debug
ctest --test-dir build/poi-view -C Debug --output-on-failure
```

Optionally pass `font.bin` and its comma-separated single-codepoint `ranges.txt`
to `map_poi_view_contract`. It loads the actual binary through LVGL and checks
each declared glyph before running the view tests. This verifies the map-specific
font subset; it does not replace a device test of the shared font-pack manager.
