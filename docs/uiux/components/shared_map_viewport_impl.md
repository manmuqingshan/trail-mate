# Shared Map Viewport Implementation Specification

## 1. Scope

This document defines the implementation specifications of the "Shared Map Viewport Component".

It is not to lock a specific class name, but to fix the implementation boundaries that are most likely to drift during subsequent refactoring, so that the code structure can be continuously interpreted:

-Who owns the viewport state
-Who owns the business state
-Who owns the tile backend
-Who is responsible for the overlay layer
- Which states are retained and which states are refreshed when switching layers?

This file is used in conjunction with [shared_map_viewport.md](./shared_map_viewport.md):

- The former defines "what the component is"
- This file defines "how the component should be implemented"

---

## 2. Implementation Goal

The goal is not to convert the existing The `GPS` page code is moved out as a whole, and the main map process that already exists in the current system but is wrapped by the page's private code is extracted into a shared implementation layer.

In other words, the refactoring goal is:

- The page continues to express its own business semantics
- Only one copy of the main map process is kept
- The underlying tile backend continues to reuse existing capabilities

---

## 3. Target Decomposition

The shared map capability should be split into at least three layers in implementation.

### 3.1 Layer A: Page-Neutral Viewport Facade

This layer is the public entrance of shared components and is responsible for:

- Create/destroy map viewport objects
- Receive the viewport model passed in by the page
-Manage interaction and life cycle
-Provide projection and status query
-Provide shared layer state reading and writing core
-Host page semantic overlay layer

This layer should not directly hold contacts, node details, and GPS page private status.

### 3.2 Layer B: Map Runtime / Camera State

This layer is responsible for:

- zoom / pan
- active base layer
- contour toggle
- focus anchor
- interaction enabled flags
- viewport availability flags
- render dirty / refresh scheduling

This layer should be a page-independent runtime state machine.

### 3.3 Layer C: Platform Tile Backend

This layer is responsible for:

- tile calculation
- tile cache
- tile object creation
- contour overlay loading
- file path resolution
- coordinate transform helpers
- LVGL object level rendering

Currently `map_tiles.*` has taken on a lot of Layer C responsibilities, and should be retained as a backend for shared map viewports, rather than a public page API for direct consumption by the page.

---

## 4. Proposed Module Ownership

### 4.1 Location of the main entrance of the component

The main entrance of the shared map viewport component should belong to the page sharing layer, and the target ownership suggestion is:

- `modules/ui_shared/include/ui/widgets/map/...`
- `modules/ui_shared/src/ui/widgets/map/...`

The reason is that the page code should rely on the "shared component interface" rather than directly relying on a specific page or a specific board-level page implementation.

### 4.2 Backend adaptation location

The specific tile/LVGL/file system backend continues to be placed on the platform layer. It is recommended to belong to:

- `platform/esp/.../ui/widgets/map/...`

This layer is responsible for ESP + LVGL + local file system related implementation.

### 4.3 Page usage location

The page layer only references the shared map viewport component and does not directly reference the back-end private details.

If the page still directly contains and manipulates objects such as `TileContext`, `MapTile`, tile path helper, etc., it means that the component boundaries have not been converged yet.

---

## 5. Public Contract

The implementation of the shared map viewport component should expose the following capability types to the page.

### 5.1 Input model

The page input to the component should be the "page intent" rather than the back-end details, including at least:

- Map container size or mounting parent object
- Current geographical focus object
- initial or current zoom
- current layer selection
- contour enabled
- whether dragging is allowed
- whether zooming is allowed
-zoom anchor strategy
-page private overlay model

### 5.2 Output capabilities

What the component outputs to the page should be "controlled capabilities", which at least include:

-Request for re-rendering
-Coordinate projection query
-Current viewport status snapshot
-Current shared layer status snapshot
- Shared layer status modification entry
- Whether the current map is available
- Missing map event/one-time notification
- Page gesture callback or status change callback

### 5.3 Content that should not be exposed

The following content should not be exposed as a page API:

- tile record vector
- decoded image cache entry
- contour object pointer
- tile placeholder object
- file path splicing details

These are all back-end internal implementations.

---

## 6. UI Object Tree

The shared map viewport component should maintain at least the following object hierarchy:

```text
MapViewportRoot
├─ TileLayer
├─ SemanticOverlayLayer
└─ GestureSurface
```

The description is as follows:

- `TileLayer` carries the underlying image objects of maps such as basic basemaps and contours
- `SemanticOverlayLayer` carries semantic objects that move with the map
- `GestureSurface` is used to receive map gestures and does not carry page fixed chrome

Page fixed chrome, for example:

- Top bar
- Node ID
- Longitude and latitude text
- right information column
- Page buttons

 should not be built inside the shared map viewport, but should be placed outside or above the component by the page.

---

## 7. Camera Model

### 7.1 Must exist status

The component should explicitly hold at least:

- `zoom`
- `pan_x`
- `pan_y`
- `active_base_layer`
- `contour_enabled`
- `interaction_enabled`
- `drag_enabled`
- `zoom_enabled`
- `viewport_has_map_data`
- `viewport_has_visible_map_data`

### 7.1.1 Zoom Contract

The shared map viewport implementation must only retain a set of zoom level contracts:

- `default_zoom = 12`
- `min_zoom = 0`
- `max_zoom = 18`

If a page needs to select a different first frame due to missing images, weak grids, or insufficient offline tile coverage zoom, it can search for the "most recently available level" within this set of contracts, but it is not allowed to overwrite the minimum, maximum or default values without permission.

### 7.2 Focus and Anchor

Two concepts must be distinguished in implementation:

- `focus object`
- `zoom anchor`

The two usually overlap, but are not synonyms.

For example:

- In the `Node Info` page, the focus object and zoom anchor are both target nodes
- In the `GPS` page, the focus object may be the current position, but after dragging, the camera center can deviate from the focus object

This also means:

- After dragging, the `camera center` can temporarily deviate from the `focus object`
- But if the page declares that "the zoom anchor point is always the focus object", then the camera must be resolved according to the anchor point during the next zoom commit

### 7.3 Follow Does not belong to the underlying default logic

Shared map viewports should not have "follow self" built in by default.

The correct implementation is:

- The page declares whether to follow
- The component only executes the camera policy given by the page

---

## 8. Render Pipeline

The main rendering process of the component should be interpreted as the following sequence:

1. The page passes in the current model.
2. Component normalization layer selection.
3. The component calculates geographical focus and coordinate transformation.
4. The component updates anchor / camera state.
5. Component-driven backend calculation required tiles.
6. Tiles are visible in the backend layout.
7. The component refreshes the map semantic overlay.
8. Page fixed chrome remains stationary.

Note:

- The semantic overlay update in step 7 should be based on the unified projection capability, rather than the page itself doing a set of derivation of latitude and longitude to screen coordinates.
 - Layer switching should re-walk 2 to 7, but should not require a page rebuild.

---

## 9. Layer Switching Implementation Rules

### 9.0 Separation of shared core and page entry

The implementation must explicitly distinguish between two layers:

1. Layer switching shared core
2. Page trigger entry chrome

The layer switching shared core is responsible for:

-`map_source` legal value normalization
-`Contour` switch semantics
-Configuration persistence
-One-time notification generation of missing map/missing SD/missing contour data

Page trigger entry chrome Responsible for:

- Where to place the button
- How to open the pop-up layer
- How to focus on the pop-up layer button

 The page entrance can be different, but the shared core must be unique.

### 9.1 Basic basemap switching

The implementation should comply with:

1. Modify active base layer.
2. Notify the backend to refresh render options.
3. Retain the current camera semantic state.
4. Keep the page overlay model.
5. Let the semantic overlay be repositioned according to the new basemap projection.

Prohibited practices:

- Directly destroy the entire page when cutting the layer
- Reset the page business status to the initial value when cutting the layer
- Discard the overlay host when cutting the layer and let the page rebuild everything by itself

### 9.2 Contour switch

 The Contour switch should only change the basemap rendering option.

It should not:

- Change focus object
- Change zoom
- Change pan
- Change page overlay data

### 9.3 Missing image handling

Component implementation must model "missing image" as an explicit state, rather than hiding the failure.

What the page consumes is:

- Whether the current layer is available
- Whether to trigger a one-time missing image notification

 instead of touching the file system to judge by yourself.

### 9.4 Implementation constraints of `Node Info`

The `Node Info` page can have its own `Layer` button position and elastic layer carrying shell, but it must not redefine itself:

- `OSM / Terrain / Satellite` enumeration semantics
- `Contour` switch semantics
- Layer configuration write-back logic
- Missing image prompt determination

In other words:

- `Node Info` page allows to have its own entrance chrome
- `Node Info` pages are not allowed to have their own layer state core

---

## 10. Overlay Contract

Page semantic overlays should be rendered through the host provided by the shared viewport component.

The page is only responsible for:

-Describing which objects to draw
-Describing their styles and labels
-Whether to update the model after responding to the interaction

The component is responsible for:

-Provide geographical point to screen coordinate projection
-Provide overlay mounting container
-Trigger repositioning when the camera changes

This means that in the `Node Info` page:

-Node position
- The own points
- Connection
- Distance

 should be the page overlay above the shared map viewport, rather than a set of "pseudo tile overlays" maintained by the page itself.

---

## 11. Logging Contract

In order to avoid the situation of "the interface is black but you don't know what happened" in the future, the shared map viewport component must have a unified log prefix. It is recommended that:

- `[MapViewport]`

 Logs should be logged at least on the following nodes:

1. create / destroy
2. attach / detach parent
3. model apply
4. layer switch
5. contour toggle
6. drag begin / drag update / drag end
7. zoom request / zoom commit
8. anchor update
9. required tile summary
10. missing tile notice
11. overlay projection refresh
12. gesture enable / disable

Page logs may still be retained, but page logs should not replace component logs.

---

## 12. Refactor Obligations

After accepting this implementation specification, subsequent code refactoring must at least complete the following convergence:

1. The map source normalization, tile path splicing, world pixel conversion, independent tile image array and other logic in the `Node Info` page must be deleted.
2. The capabilities in the `GPS` page that only belong to the main process of sharing the map must be separated from the private logic of the page.
3. Common map capabilities such as coordinate system conversion must not continue to be hung in page files such as `gps_page_map.cpp` to serve as de facto shared libraries.
4. The page should instead obtain projection and interaction capabilities through the shared map viewport component API.

---

## 13. File Layout Baseline

When subsequent implementation is implemented, it is recommended to form at least the following structure:

```text
modules/ui_shared/include/ui/widgets/map/
  map_viewport.h
  map_viewport_types.h
  map_viewport_overlay.h

modules/ui_shared/src/ui/widgets/map/
  map_viewport.cpp

platform/esp/.../include/ui/widgets/map/
  map_viewport_backend.h
  map_tiles.h

platform/esp/.../src/ui/widgets/map/
  map_viewport_backend.cpp
  map_tiles.cpp
```

This is the implementation layout baseline, not the file name that must be copied character by character; but the structural meaning of "the shared entry is in `ui_shared`, and the platform backend is in the platform layer" should remain stable.

---

## 14. Summary Baseline

Summary in one sentence:

The correct implementation of the shared map viewport component is not to extract a certain page into public code, but to separate the "map main process" from the page business, so that the page only retains its own semantics and overlay.
