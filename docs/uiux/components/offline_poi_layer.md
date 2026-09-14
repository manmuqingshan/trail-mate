# Offline POI layer: architecture baseline

Offline POIs are spatially indexed map-layer data, not contacts or team state.
Their XYZ index is a vector tile payload, not a PNG image. The manifest is
dataset metadata, not a synthetic tile at a reserved coordinate.

The existing shared map path owns this feature:

1. `ui_shared` map viewport supplies the camera and owns the overlay host.
2. The ESP tile backend uses the existing visible tile records and generation.
3. POI tile requests use the existing map command queue, worker, event queue,
   stale-generation rejection and viewport lease lifecycle.
4. Worker-side POI source reads metadata/indexes through `IMapTileFileSystem`,
   whose ESP implementation uses the existing SD runtime. No private SPI
   ownership, direct SdFat access, or LVGL-callback file reads are introduced.
5. The worker parses JSON into a bounded typed payload. The UI never parses JSON.
6. Accepted tile records own their POI payload with RAII. Viewport snapshots are
   bounded; projection uses the map backend's existing geographical projection.
7. The shared viewport displays fixed-size markers and a limited set of labels
   with overlap checks, separately from the page's own position/team overlays.

PNG and POI share scheduling/lifecycle, but have different payload formats and
decoders. A bad or missing POI resource must not mark the PNG tile missing.
Unselected zoom levels clear POIs immediately, without reusing adjacent levels.
Manifest v2's explicit `enabled_zoom_levels` wins, including an empty array;
v1 min/max is supported only as a migration rule.

No GPS-page-only loader, second Mercator implementation, UI-side SD polling,
new POI thread, or unrelated firmware/USB behavior belongs in this change.

## Memory constraints

- No fixed 200-record object, whole-region catalog, or embedded large snapshot array.
- The worker borrows its existing PSRAM scratch for JSON and parsed records.
- A small header is followed by exactly the parsed record count. The existing
  event queue allocates that exact size in PSRAM only and transfers ownership
  directly to the tile record, without a second copy/allocation.
- Invisible tile POI buffers are evicted by the existing cache eviction stage;
  deleting tile records or leaving the map releases ownership automatically.
- Presentation metadata is small; its item span is allocated on demand in PSRAM
  and released when POIs are disabled, absent, or the viewport is destroyed.
- PSRAM failure suppresses/retries POIs; it never falls back to a large internal
  RAM allocation. Only small handles, counters and bounded transient parser
  allocations use the normal framework allocation path.
- The POI view has one LVGL object, no per-marker widgets, no label pool and no
  additional framebuffer. It borrows the viewport snapshot. Its bounded label
  layout uses bit masks instead of arrays; the view itself is at most 64 bytes.
- LVGL owns short transient label copies for asynchronous draw tasks. Clearing
  the view detaches the borrowed snapshot before its owner releases PSRAM.

## Hybrid map policy

The Kunming verification package uses baked raster text at z1–15 and independent
POIs with completely text-free raster tiles at z16–18. The firmware reads the
package's explicit level set; it does not hard-code those levels. An empty set
disables all independent POIs. Regional PBF source coverage may leave low-zoom
world context incomplete; firmware must not fabricate labels for it.
