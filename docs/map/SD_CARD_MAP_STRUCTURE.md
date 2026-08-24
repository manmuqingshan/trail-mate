# SD card map data directory specification (Trail-Mate)

This document describes the directory structure, file naming and suffix rules required by the Trail-Mate firmware when reading SD card map data on the GPS map page.

## 1. Summary of conclusions

- The current implementation uses directory tiles (`z/x/y`) and does not support `mbtiles` single-file database.
- The root directory of the map tiles is: `/maps`
- The base map directory is: `/maps/base/{source}/{z}/{x}/{y}.{ext}`
- The contour directory is: `/maps/contour/{profile}/{z}/{x}/{y}.png`
- Supported zoom level: `z=0..18`

## 2. Required directory (basemap)

At least one basemap source (select one or more of `osm`, `terrain`, `satellite`) needs to be prepared.

```text
/
└── maps
    └── base
        ├── osm
        │   └── {z}/{x}/{y}.png
        ├── terrain
        │   └── {z}/{x}/{y}.png
        └── satellite
            └── {z}/{x}/{y}.png
```

### Basemap source and file suffix

- `osm` -> `.png`
- `terrain` -> `.png`
- `satellite` -> `.png`

Note: All basemap sources must use the `.png` suffix.

## 3. Optional directory (contour overlay)

When `Contour Overlay` is turned on in the settings, the firmware will read the contour tiles of different profiles according to the zoom level.

```text
/
└── maps
    └── contour
        ├── major-500
        │   └── {z}/{x}/{y}.png
        ├── major-200
        │   └── {z}/{x}/{y}.png
        ├── major-100
        │   └── {z}/{x}/{y}.png
        ├── major-50
        │   └── {z}/{x}/{y}.png
        └── major-25
            └── {z}/{x}/{y}.png
```

### Contour profile and zoom mapping

- `z <= 7`: Do not draw contours
- `z = 8`:`major-500`
- `z = 9`:`major-200`
- `z = 10`:`major-500`
- `z = 11`:`major-200`
- `z = 12..14`:`major-100`
- `z = 15..16`:`major-50`
- `z >= 17`:`major-25`

## 4. Other directories related to the map page (optional)

These are not "basemap required", but are common input directories for GPS/map functions.

```text
/
├── routes
│   └── *.kml
└── trackers
    └── *.gpx / *.csv / *.bin
```

- `/routes/*.kml`: used to load routes in Route mode.
- `/trackers/*`: used for track file reading and display (Track Overlay).

## 5. Notes on naming and path

- The path distinguishes the directory level and does not automatically correct errors. Must be the standard `z/x/y` hierarchy.
- `z/x/y` all use decimal directory and file names, do not add prefixes or suffixes.
- When the base map does not exist, the interface will prompt that the corresponding layer is missing (such as `Map - OSM Missing`).
- Simply placing an empty directory will not display the map, and there must be a corresponding tile file.

## 6. Minimum available example

If you only want to verify the link first, you can put a small number of tiles first (for example, a local area with `z=12`):

```text
/
└── maps
    └── base
        └── osm
            └── 12
                └── 3340
                    ├── 1788.png
                    ├── 1789.png
                    └── 1790.png
```

After entering the GPS map page, switch the base map source to `OSM`, and move to the corresponding area to verify the loading.

## 7. Code reference

- Map source and suffix mapping: `modules/ui_map_runtime/src/map_tiles/map_tile_resolver.cpp`
- Basemap path construction: `modules/ui_map_runtime/src/map_tiles/map_tile_resolver.cpp`
- Contour path construction and profile selection: `modules/ui_map_runtime/src/map_tiles/map_tile_resolver.cpp`
- Zoom range: `platform/esp/arduino_common/include/ui/screens/gps/gps_constants.h`
- Route list directory: `modules/ui_shared/src/ui/screens/tracker/tracker_page_components.cpp`
- Track/Route File entry: `modules/ui_shared/src/ui/screens/tracker/tracker_page_components.cpp`
