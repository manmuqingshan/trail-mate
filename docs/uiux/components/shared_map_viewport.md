# Shared Map Viewport Component Specification

## 1. Scope

This document defines the specifications for the Shared Map Viewport Component in Trail Mate.

It does not constrain a certain page, but the boundaries of the map component that all pages that use the map as the main background or main content carrier layer should abide by.

The pages currently directly subject to this document include:

- `GPS / Map` page
- `Node Info / Node Details` page

 Any subsequent new pages as needed:

- Map base map rendering
- Map dragging
- Map zooming
- Map layer switching
- Map semantic overlay projection

 Priority should be given to reusing this component instead of implementing a set of map logic again within the page.

This document is a "component responsibility and boundary specification", not a specific code design draft; however, subsequent implementation must be explained by this document.

---

## 2. Current Confusions

Before entering the reconstruction, you must first admit that there are following confusions in the current system:

1. The `GPS` page already has a relatively complete set of map capabilities, but it is too deeply coupled with the page business status and cannot be directly reused as a general component.
2. The `Node Info` page currently implements an independent set of map logic. This is not reuse, but parallel implementation.
3. "Map page" and "map component" are not the same object.
4. The "tile engine" is not the "map component" itself, it is only part of the underlying capabilities.
5. The semantics of layer switching, dragging, zooming, and projection should belong to the shared map viewport, rather than private behaviors of a certain page.
6. The `Layer` button appears in different locations on different pages, which does not mean that the layer switching semantics can be defined on a per-page basis.

If these confusions are not cut through first, any subsequent implementation of "support functions first, then talk about it" will bring the system back to the dual-track map logic.

---

## 3. Distinctions

### 3.1 Map page != Map component

`GPS` page is a complete page object, which in addition to the map also contains:

- GPS fix status
- follow self strategy
-Team tags
-Route/track overlay
-Page title and status information
-Page shortcut operations

These are not shared map viewport entities.

The shared map viewport component is only responsible for "how the map is displayed, panned, zoomed, cut into layers, and projected overlays" and is not responsible for the business meaning of the page.

### 3.2 Tile engine != map viewport

The existing `map_tiles.*` is the underlying tile and projection capabilities, responsible for:

- tile calculation
- tile object management
- tile loading and caching
-Map source directory and file path
-Contour overlay
-Screen projection

It is the backend basis for sharing the map viewport and should not be used directly as a page component by the page.

### 3.3 Page coverage information != Map semantic coverage layer

The following elements belong to page coverage information:

- Top bar
- Node ID
- Longitude and latitude text in the lower left corner
- Right information column
-Page button that moves independently of the map

The following elements belong to the map semantic overlay:

- node tag
- self tag
-Team tags
- connection
- distance tag
- path/trajectory

The map semantic overlay must move synchronously with the map viewport; the page coverage information must be suspended stably and must not drift with dragging.

### 3.4 Viewport status != Page business status

The map viewport status is:

-Current zoom level
-Current translation offset
-Current base map
- Current overlay layer switch
- Whether interaction is currently allowed
- Whether there is a basemap available in the current viewport

 The business status of the page is:

- Whether the GPS page follows self
- Which node the Node Info page is currently viewing
- Which right information items are displayed on the page
- Who is the zoom anchor point of the node details page?

 The business status of the page can drive the map viewport, but it should not be mixed with the internal state of the map viewport.

### 3.5 Basic basemap != Overlay layer

The basic basemap is a "mutually exclusive choice":

- OSM
- Terrain
- Satellite

The overlay layer is an "optional layer attached to the base basemap":

- Contour Overlay

Switching the base map and switching overlay layers have different semantics and cannot be mixed into a random "layer mode".

---

## 4. Component Goals

### 4.1 Core Objectives

The shared map viewport component must provide the following stable capabilities:

1. Host the map basemap under the unified component model.
2. Support dragging and zooming under unified state semantics.
3. Support layer switching under unified rules.
4. Provide stable geographical point to screen coordinate projection capability for the page.
5. Allow the page to overlay page-private semantic elements on top of the map.
6. Let multiple pages share the same map main process instead of sharing several helpers.

### 4.2 Non-target

This component is currently not:

- Page navigation container
-Contact or node business model
-GPS page exclusive state machine
-Team page exclusive state machine
-Offline map downloader
-Map data preparation tool

---

## 5. Responsibilities

The shared map viewport component is responsible for:

1. Create and maintain map basemap hosting areas.
2. Maintain unified camera / viewport status.
3. Coordinate basemap and overlay layer rendering options.
4. Drive the underlying tile backend to perform tile calculation, loading and layout.
5. Provide projection query capabilities to the page.
6. Manage the map semantic overlay host container.
7. Manage the common semantics of the three types of interactions: dragging, zooming, and layer switching.
8. Expose "current viewport status" and "current map availability status".

The shared map viewport component is not responsible for:

1. Decide which business fields should be displayed on a page.
2. Determine the content of the information column on the right side of the page.
3. Business model that directly holds contacts, nodes, GPS pages, and team pages.
4. Define "what a certain mark represents" on the page.
5. Let the page directly operate low-level details such as tile cache, tile record, file path splicing, etc.

---

## 6. Module Ownership

### 6.1 Responsibilities of `modules/ui_shared`

`modules/ui_shared` is responsible for sharing the page-independent interface and component shell of the map viewport, including at least:

-Component public API
-Viewport state model
- Page access constraints
- Interactive semantic constraints
- Map semantic overlay host abstraction

In other words, the page should rely on the shared map viewport component in `ui_shared` instead of directly assembling the underlying tile logic by itself.

### 6.2 Responsibilities of `platform/esp/*`

The platform layer is responsible for the specific back-end capabilities that the map viewport depends on, including at least:

- LVGL object-level implementation
-Tile loading and caching
-File system path and resource search
-Contour overlay rendering
-Coordinate system conversion implementation
-Platform-related memory/loading budget control

What the platform layer provides is "backend adaptation", not page semantics.

### 6.3 Responsibilities of the page layer

The page layer is only responsible for its own business usage, such as:

- The `GPS` page determines the overlay semantics of self/teammates/tracks
- The `Node Info` page determines the overlay semantics of target node/own node/connection/distance
- The page decides which fixed information columns and buttons it needs

The page layer no longer builds its own map basemap logic.

---

## 7. State Boundaries

### 7.1 Persistent configuration status

The following status belongs to the application configuration, and the component reads but does not customize it privately:

- `map_source`
- `map_contour_enabled`
- `map_coord_system`

The persistence of these statuses belongs to the application configuration system, and the component only consumes its current value or receives an explicit delivery from the page.

### 7.2 Viewport runtime status

The following status belongs to the shared map viewport component itself:

- Current zoom
- Current pan_x / pan_y
-Current base map
- Whether the current contour is on
- Whether the current viewport allows dragging
- Whether the current viewport allows zooming
- Whether the current viewport has available map data
- Current anchor / projection cache
- Summary of tile state in the current rendering

### 7.3 Page driver state

The following states are owned by the page and then fed to the viewport as input:

- Viewport focus object
- Whether the page allows follow
- Who the page wants to zoom around
- What semantic markers and line segments should be drawn on the page
- Whether the page should display fixed UI chrome above the viewport

### 7.4 Backend cache status

The following status belongs to the backend and should not be exposed across component boundaries to free page operations:

- tile records
- decoded image cache
- missing tile notice once flags
- contour overlay cache
- tile object eviction state

The page can read the summary, but cannot rewrite the internal details.

---

## 8. Layer Switching Semantics

### 8.1 Basic Rules

Shared map viewports must support "in-page layer switching", and switching should not require page reconstruction.

### 8.2 Basic basemap rules

The basic basemap always has exactly one active source:

- `0 = OSM`
- `1 = Terrain`
- `2 = Satellite`

When switching the basic basemap:

1. The page is not rebuilt.
2. The map viewport object is not rebuilt.
3. The viewport camera status should be maintained as much as possible.
4. The semantic overlay of the map is still held by the page and will not be lost due to cutting the base map.
5. The underlying tile backend should refresh the basemap rendering state.

### 8.3 Overlay layer rules

Contour Overlay is an overlay, not a type of base map.

When switching contour:

1. Do not change the active base source.
2. Do not change the semantic coverage of the page.
3. Do not change the business focus object of the page.
4. Only change the contour visibility and loading strategy above the map basemap.

### 8.4 Missing image semantics

When switching to a layer and there is no image in the current viewport:

1. The page is not allowed to collapse into a black screen.
2. Pages are not allowed to fail silently.
3. The component should maintain a stable map container structure.
4. The component should give an observable status or one-time notification of "the current layer is missing".

The page can decide how to display the notification, but it should not implement a set of missing image judgments by itself.

### 8.5 Layer switching entry semantics

The shared map viewport constraint is "layer switching semantics", not "buttons must be at the same coordinates".

Allow:

- The `GPS/Map` page places the `Layer` button in its own control area
- The `Node Info` page places the `Layer` button in the bottom middle

Not allowed:

- Different pages have different basic basemap enumerations
- Different pages have different meanings for `Contour`
- Different pages implement different missing image judgment and layer normalization logic

Therefore, the page can have its own trigger chrome, but the state changes, legal value sets, missing image semantics and persistence consequences after layer switching must be completely consistent.

---

## 9. Camera and Interaction Semantics

### 9.1 Drag

When the page allows dragging:

- Dragging is applied to the map viewport
-The map semantic overlay moves accordingly
-The page is fixed in chrome and does not move

### 9.2 Zoom

Shared map viewports must support "page-specified zoom anchor semantics".

The reason is that different pages have different zoom anchors:

- The `GPS` page may be around self / screen center / follow target
- The `Node Info` page must be around the target node

So the zoom behavior cannot be hard-coded as a page-private rule.

The shared scaling level contract is fixed at:

-Default scaling: `12`
-Minimum zoom: `0`
-Maximum zoom: `18`

The page can decide "whether the first frame the user sees is downgraded to the other nearest available level due to missing images", but **cannot** privatize a different set of min/max zoom ranges again within the page.

Additional constraints:

- "User requests to change zoom" and "Whether the current zoom has a central tile" must be two separate judgments
- The first frame or automatic selection can refer to tile availability
- But interactive zooming must not be silently blocked as a no-op due to missing center tile

### 9.3 Follow

`follow` is not a universal default behavior for shared map viewports, but a page policy.

The shared map viewport only provides:

- camera movement capability
- anchor calculation capability
- camera offset retention capability after dragging

Whether to "automatically follow an object" is declared by the page itself.

### 9.4 When there is no geographical target

When the page has no available geographical target, the component must be allowed to enter the degraded state of "no map semantic capability".

In this state:

- The map base can be empty or just a background
- Dragging can be disabled
- Zoom can be disabled
- Page fixed information can still be displayed normally

---

## 10. Page Integration Contracts

### 10.1 `Node Info` page access requirements

The `Node Info` page is obtained through the shared map viewport component:

- Map base map
- Drag ability
- Zoom ability
- Layer switching ability
- Coordinate projection ability

`Node Info` page itself provides:

- Target node mark
- self tag
- Two-point connection
- distance tag
- Fixed chrome for the upper left ID, lower left longitude and latitude, right information column, lower right zoom button, bottom middle `Layer` button, etc.

Additional constraints:

- The `Node Info` page allows fixed chrome to be overlaid on top of the map, but it must no longer cover the persistent translucent fog layer or the right mask to "dark" the base map
- Dragging on the `Node Info` page only changes the camera offset; its zoom anchor is always the target node

### 10.2 `GPS` page access requirements

The `GPS` page is obtained through the shared map viewport component:

- Map base map
-Drag/zoom
-Layer switching
-Coordinate projection

`GPS` page itself provides:

- self marker
- team markers
- Track/route overlay
- follow policy
- Page status information

---

## 11. Illegal Implementations

The following implementations are considered illegal under this specification:

1. Implement `map_source` normalization again in the page file.
2. Implement the basic tile path splicing rules in the page file again.
3. Implement the main process of world pixel projection again in the page file.
4. Maintain an independent tile image array and tile life cycle in the page file again.
5. Directly manipulate the underlying tile cache details in the page.
6. Treat the entire `GPS` page as a component and hard-reuse it to other pages.
7. Mix the page fixed chrome into the map semantic overlay and drag it together.
8. Implement a set of page-private layer normalization, contour switching semantics or missing image determination in the page again.

---

## 12. Consequences

Once this specification is accepted, subsequent refactoring will be explicitly constrained to:

1. `Node Info` The current set of parallel map implementations must be removed rather than continued to be expanded.
2. The current main map process of the `GPS` page must be stripped out of the page-specific state.
3. The shared map viewport component will become the only main map entrance that multiple pages depend on.
4. Any subsequent enhancements to map capabilities, such as more layers, more markers, and more interactions, will be prioritized in shared components rather than in private branches of the page.

---

## 13. Summary Baseline

Summary in one sentence:

The shared map viewport component is not "another map page", nor is it "several shared helpers", but the only map main process carrying layer shared by all map-type pages in Trail Mate.
