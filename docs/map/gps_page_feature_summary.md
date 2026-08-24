# GPS/Map page function point summary (current code structure and responsibilities)

This document only compares "function points and code locations". The old
`platform/esp/arduino_common/src/ui/screens/gps/*` GPS map page implementation on the ESP side has been deleted;
The current map page no longer directly controls `GPSPageState`, `TileContext`, `MapTile` or
`update_map_tiles()`.

## 1) Page entry

- Page shell: `modules/ui_shared/src/ui/screens/gps/gps_page_shell.cpp`
- Page runtime: `modules/ui_shared/src/ui/screens/gps/gps_page_runtime.cpp`
- app registration entrance: `modules/ui_shared/src/ui/app_catalog_builder.cpp`

`gps::ui::shell` is only responsible for entering/exiting the page; the real page object tree, key processing, refresh rhythm and
Map model synchronization is completed within shared `gps_page_runtime.cpp`.

## 2) Map viewport

- Shared viewport component: `modules/ui_shared/src/ui/widgets/map/map_viewport.cpp`
- Shared viewport API: `modules/ui_shared/include/ui/widgets/map/map_viewport.h`

The GPS page creates the map viewport through `ui::widgets::map::Runtime` and uses
`ui::widgets::map::Model` Enter focus, zoom, pan, basemap source, contour switch, and coordinate system.
The page no longer directly holds the tile record vector, decoded image cache, tile path helper or
LVGL tile image array.

## 3) ESP tile backend

- ESP backend implementation: `platform/esp/arduino_common/src/ui/widgets/map/map_tiles.cpp`
- ESP backend interface: `platform/esp/arduino_common/include/ui/widgets/map/map_tiles.h`
- Shared tiles runtime: `modules/ui_map_runtime/include/ui_map_runtime/map_tiles/*`

Only one platform map tile backend is reserved on the ESP side. It is responsible for:

- SD/FATFS path resolution and read adaptation
- LVGL image/tile object creation
- decoded PNG cache life cycle
- Actual rendering of base layer and contour overlay

 Both page layer and Node Info call this backend through shared `map_viewport`.

## 4) Map workspace model

- runtime source/sink:`modules/ui_shared/src/ui/presentation_sources/runtime_map_workspace_source.cpp`
- presentation model:`modules/ui_presentation/include/ui_presentation/map/map_workspace_model.h`
- overlay snapshot:`ui_presentation/map/map_overlay_snapshot.h`

The GPS map page uses `MapWorkspaceModel` to synchronize the viewport, layer, current own position and team overlay.
Layer switching, contour switching, and missing image prompts are all handled through the layer API of `map_viewport`.

## 5) Input and interaction

GPS map key/button processing is in shared `gps_page_runtime.cpp`:

- `W/A/S/D` or direction keys: pan map
- `+/-`: zoom
- `P` / `Pos`: Return to own position
- `L`: Switch base map
- `O` / `Contour`: switch contours
- `F1`: show help

Input only changes the shared map model / viewport model; no longer directly rebuilds tiles or manipulates the underlying tile object.

## 6) Route, trajectory and team overlay

- Tracker page is still responsible for route/track file entry and configuration writing:
  `modules/ui_shared/src/ui/screens/tracker/tracker_page_components.cpp`
- GPS map page displays route/team context button and overlay summary.
- The old ESP GPS route/tracker overlay drawing file has been removed, the map semantic overlay is gone
  `MapOverlaySnapshot` / `map_viewport`.

## 7) The most vulnerable boundaries

1. Do not reintroduce `TileContext`, `MapTile`, `GPSPageState` or tile path helper at the page layer.
2. The ESP platform layer is only allowed to retain the tile backend `map_tiles.cpp`.
3. Linux/uConsole GTK and CardputerZero can have product differences, but there should be no second set of GPS map page implementations on the ESP Arduino side.
4. Map missing images, layer switching, and contour switching must go through the shared `map_viewport` API, and do not touch the file system privately in the page.
