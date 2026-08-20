# Node Info Page UI/UX Specification

## 1. Scope

This document defines the UI/UX specifications for the Trail Mate `Node Info` / node details page.

This document currently only restricts "the details page seen after opening a node in the contacts", and does not cover the contact list page itself, nor does it cover the main map page.

The map background, dragging, zooming, layer switching and projection capabilities in this page do not separately redefine the underlying component responsibilities in this document, but inherit the shared map viewport component specifications:

- [Firmware Visual Style Specification](../foundation/firmware_visual_style.md)
- [Shared Map Viewport Component Specification](../components/shared_map_viewport.md)
- [Shared Map Viewport Implementation Specification](../components/shared_map_viewport_impl.md)

This is an implementation constraint document, not a visual inspiration sketch. Subsequent modifications to `node_info_page_layout.*` and `node_info_page_components.*` should be reviewed in this document instead of continuing to rely on local patches to evolve.

Undefined items in this document are by default regarded as "forbidden to implement" instead of "leaving it to implementers to freely develop".

In other words:

- Only fields explicitly listed in the document can be displayed
- Only downgrade behaviors explicitly allowed by the document can appear
- Supplementary information that is not named by the document should not appear in the standard `Node Info` page by default

If you need to expand fields or add new visual elements later, you should change the specifications first, and then change the code.

---

## 2. Requirement Restatement

The goal of the node details page is not to make an information panel stacked with multiple cards and borders, but to make an immersive page with "node space position" as the main visual and "node link information" as the auxiliary information on the right side.

The currently confirmed requirements are as follows:

1. The entire node details page needs to be reconstructed, and the old card/box information layout will no longer be used.
2. The main body of the page should no longer have visual containers such as content boxes, link boxes, and information cards. The content should fall directly on the page as much as possible.
3. If the node has latitude and longitude information, the map should be used as the page background.
4. The node ID is displayed in the upper left corner.
5. The latitude and longitude are displayed in the lower left corner, and "longitude above, latitude below".
6. The information displayed in the original link box is no longer in a separate box, but is placed on the right side of the page, one item per line.
7. If the node has longitude and latitude, the location of the node needs to be marked on the map.
8. If "my latitude and longitude" and "latitude and longitude of this node" both exist, you need to mark two points on the map at the same time, display them as a line, and render the distance between the two points.
9. Two map zoom buttons, `+` and `-`, are added to the lower right corner of the page. The zoom center is always this node.
10. The zoom button is operable only when the node has latitude and longitude; if there is no latitude and longitude, the two zoom buttons are inoperable.
11. When a node has longitude and latitude, the map needs to support sliding and allow users to drag to view the area around the node.
12. If the "role" field can only display `-`, do not display it. This field is removed by default in the current specification.
13. The text description on the map needs to use a set of bright and distinguishable colors. ID, latitude and longitude, link information items, distance, etc. can use different colors, and the overall look should be as good as possible.
14. Add a `Layer` button in the middle of the bottom of the page for layer switching.
15. The function of the `Layer` button must be exactly the same as the `Layer` button in the `GPS/Map` page, including at least:
- Street Map / `OSM`
- Terrain Map / `Terrain`
- Satellite images / `Satellite`
 - Contour switch / `Contour`
16. The `Node Info` page does not allow defining its own layer switching semantics; it can only reuse the layer switching semantics defined by the shared map viewport.

---

## 3. Distinctions

### 3.1 Main object of the page

The main object of the node details page is "a remote node".

The map is not the main object, the map is just the spatial context of this node.

### 3.2 Main visual of the page

The main vision of the page is not a "table" or a "card deck", but:

- When there are coordinates: a spatial view with the map as the background
- When there are no coordinates: a degraded view with a pure background carrying text information

### 3.3 Responsibilities of the information area on the right

The area on the right is responsible for carrying the links and additional information of the node. It is an "information projection area", not a second main page, nor an independent card container.

### 3.4 Old expressions that are no longer valid

The following expressions no longer hold true in the new specification:

-The "role" field is always displayed, even if the value is meaningless
-Use link boxes to carry link information
-Use multiple content boxes to split a node into multiple visual islands
-Let the map be reduced to a vignette or a secondary component

---

## 4. Page Goals

### 4.1 Core Objective

- Let the user see "where is this node" at a glance
- and then quickly see "what is the relationship between it and me"
- and finally see "its link/status details"

### 4.2 Non-target

This page is not currently:

- Node configuration editor
- Complete diagnostic page
- Multi-tab container
- Multi-card information board

---

## 5. Layout Specification

## 5.1 Overall Structure

The page retains application-level public top bar capabilities, such as return, title, battery, etc.

Except for the top bar, the content area is expressed in a full-screen single canvas, and content cards, information boxes, link boxes, and outer outline boxes are no longer introduced.

The structure diagram given in this section only expresses "regional relationships" and does not express fixed pixels.

The specific geometry of `Node Info` must be determined by the current device size and `page_profile`. The `480x222` example for `pager` in other documents cannot be reversely understood as `Node Info` or a unified layout method for the entire firmware.

```text
+--------------------------------------------------+
| Top Bar                                          |
+--------------------------------------------------+
|                                                  |
|  ID                               right-side     |
|  map / background                 info lines     |
|                                                  |
|                                                  |
|  lon                                              |
|  lat                 [Layer]      +              |
|                                   -              |
+--------------------------------------------------+
```

### 5.1.1 Compact Portrait Baseline

The following baseline applies to the current `320x240` portrait screen device family and is the **normative reference layout** for `Node Info` page review, screenshot comparison and regression check:

- Root page: `320x240`
- TopBar:`320x30`
- Content area: `320x210`
- Content area margin: `10px`
- Right link column width: `122px`
- Right link column start Y: `12px`
- Right link column row height: `12px`
- Right link column row spacing: `1px`
- Bottom right zoom button: `28x28`
- Bottom middle `Layer` Button: `68x24`

This baseline only constrains the `Node Info` page under the current compact portrait profile and does not constitute a common layout method for the entire firmware.

## 5.2 Background

### With node coordinates

- The map covers the content area as the main background
- The default first frame is centered on the node
- The map needs to support the user to slide to view the surrounding area
- The map zooms around the node
- The map basemap must remain clear and readable; no persistent translucent masks, fog layers, scrims, or right-side fade masks are allowed in the standard state
- The only translucent layer allowed to cover the map can only be the background mask of the temporary modal elastic layer itself, and it must disappear completely after the elastic layer is closed

### No node coordinates

-Do not display the map basemap
-Use a solid color or lightweight texture background to carry text information
- The zoom button appears disabled

## 5.3 Left-Top: Node Identity

The node ID is displayed in the upper left corner.

Requirements:

- Stable position, high priority
- Not squeezed by the right information area
- The color should be clearly different from the background
- Accent colors can be used

 If the short name/nickname needs to be displayed at the same time later, it should also be subordinate to the ID and should not replace the first visual position of the ID.

## 5.4 Left-Bottom: Coordinates

The coordinate text is displayed in the lower left corner, and the order is fixed as:

1. Longitude
2. Latitude

Requirements:

- Longitude above, latitude below
- Text color is distinguished from ID and information items on the right
- When coordinates are missing, dummy values ​​and placeholder dashes will not be displayed

## 5.5 Right Side: Info Lines

The right side of the page is an information column, one item per line.

Requirements:

- Link boxes are no longer used
- Each line carries only one information item
- Information items are arranged from top to bottom
- Visually weaker than the main information on the left, but should still be clearly readable
- A steady rhythm and scannability need to be maintained between lines
- The entire column is processed as a right-edge reading column and must be truly right-aligned
- When the base map is a tile map or satellite image, the right reading column must prioritize ensuring contrast and instant readability; it allows the use of brighter overlay reading colors than the global text, but must not degrade into random colorful text

### 5.5.1 Standard Field Set

The information column on the right side of the standard `Node Info` page only allows the following fields to be displayed, in a fixed order:

1. `Protocol`
2. `RSSI`
3. `SNR`
4. `Seen`

These 4 items constitute the complete link information contract of the standard node details page.

If an item is missing, it will be omitted directly, and subsequent items will be moved up; **No other fields are allowed to be filled in**.

### 5.5.2 Field Text Templates

The right information column text template is fixed as follows:

1. `Protocol`
 - Allowed values: `Meshtastic` / `MeshCore` / `RNode` / `LXMF`
 - Abbreviations to `MT` / `MC` / `RN` / `LX`
2. `RSSI`
 are not allowed - Template: `RSSI -49 dBm`
 - Values rounded to 1 dBm
3. `SNR`
 - Template: `SNR +6.2 dB`
 - Keep 1 decimal place, positive numbers must contain `+`
4. `Seen`
 - Template: `Seen 12s` / `Seen 3m` / `Seen 2h` / `Seen 1d`
 - Use relative duration short format

### 5.5.3 Readout Color Contract

The right reading column uses a fixed high readable color contract on the map background, the current version is defined as follows:

1. `Protocol`
 - Use bright and warm amber
 - Aim to be scannable at first glance
2. `RSSI`
 - Use bright info blue / cyan
 - Must be significantly lighter than normal text brown
3. `SNR`
 - Use bright ok green
4. `Seen`
 - Use light warm bright color, not dull sub-text brown
5. `Zoom`
 - Use with `Seen` Light bright color of the same level or slightly better

Prohibited practices:

-Continue to use the global normal text dark brown to directly cover the map tiles
-For the sake of "unification", push `Seen` / `Zoom` back to low brightness taupe
- Randomly specify new unconverged colors for each line

### 5.5.4 Forbidden Fields

The following fields are in the standard `Node Info` **Explicitly prohibited** on the page:

- `LoRa`
- `MQTT`
- `FREQ`
- `SF`
- `BW`
- `CH`
- `HOPS`
- `NEXT`
- `Role`
- Any debug fields
- Any derived fields temporarily added to "fill in the right area"

If this information is indeed valuable in the future, it should be entered into a separate diagnostic page or advanced details mode instead of being stuffed back into the standard node details page.

### 5.5.5 Line Count Contract

The maximum number of rows in the information column on the right side of the standard `Node Info` page is `4`.

This is not a "current implementation coincidence", but part of the specification itself.

Any implementation that displays link information on line 5 and above in the standard node details page shall be considered a specification violation.

### 5.5.6 Viewport Status Line

The standard `Node Info` page allows an additional line of **viewport status line** to be displayed below `Seen`. The current version is only used to display the current zoom level.

It is not part of the link information and therefore does not count against the `4` maximum number of rows in the link information column in 5.5.4.

Requirements:

- Fixed to be displayed below `Seen`
- Still belongs to the right reading column and must be right-aligned
- Only displayed when the node has valid longitude and latitude
- The current version text template is fixed to `Zoom 12`
- It is not allowed to add other debugging fields, tile status, coordinate system, base map source abbreviation and other information in this line

### 5.5.7 Readout Backdrop

The right link reading column allows adding a *content-wrapped translucent bottom plate*, which has only one purpose: to improve the instant readability of the `Protocol / RSSI / SNR / Seen / Zoom` set of readings on the tile map or satellite image background.

Requirements:
- The base plate can only wrap the currently visible reading item on the right side, and the size must be determined by "the actual bounding box of the visible reading text + a small amount of padding"
- The current version transparency is fixed at `60%`
- The current version of the base plate color is fixed and inherits the firmware warm panel background color semantics, that is, `PanelBG`
- The bottom plate must follow changes in the number of visible reading items and text width, and cannot degenerate into a fixed half column, fixed full column, or half-screen mask
- The bottom plate only serves the right reading cluster and must not be extended to the main map area, nor must it affect the visual hierarchy of the upper left ID, lower left coordinate, map mark, or distance label
- The base plate is the supporting layer of the reading cluster, not a new "link box" or "information card"; it is forbidden to reintroduce title bars, group borders, dividers or secondary card semantics
- When there are no visible read items on the right, the base plate must be completely hidden

Prohibited practices:
-Replace this base plate with the entire right half-screen mask, fade mask, scrim, fog layer
- Re-grey, fog or overall reduce the contrast of the map in order to achieve readability
- Make the base plate a fixed rectangle permanently bound to the right column width, rather than a content-driven rectangle

## 5.6 Bottom-Right: Zoom Controls

Place two buttons in the lower right corner:

- `+`
- `-`

Requirements:

-Always fixed in the lower right corner area
-Only control the map zoom
- The zoom center is always the current node
- Fixed inheritance of the zoom level contract shared map viewport/map page contract: minimum `0`, maximum `18`, default `12`
- When entering the page for the first time, the viewport defaults to `12` as the preferred zoom level; if the center tile at this level is not available, only `0..18` is allowed. Find the nearest available level within the range
- After the user clicks `+` or `-`, the zoom action must return to the camera semantics of "using the current node as the anchor point", and cannot continue to use the screen center of the node after dragging
- The zoom request triggered by the user is only constrained by the zoom range, and must not be additionally intercepted by "whether the current higher/lower level center tile exists". no-op; missing tiles are a map data availability issue, not a zoom semantic issue
- When the minimum or maximum zoom level has been reached, the corresponding button must be displayed as disabled

When the node has no latitude and longitude:

- The two buttons remain visible but inoperable
- They must appear visually disabled
- No map behavior is performed after clicking

## 5.7 Bottom-Center: Layer Button

 A `Layer` button must be placed in the middle of the bottom of the page.

Requirements:

- The position is fixed in the bottom middle area
- It belongs to the page fixed chrome and does not follow the map dragging
- The layer switching capability it opens must be exactly the same as the `Layer` button function of the `GPS/Map` page
- It does not allow the introduction of a set of `Node Info` Private layer definition, layer naming or layer switching consequences

The `Layer` button controls the shared map configuration, not the private state of the current page.

This means:

- When switching `OSM / Terrain / Satellite`, what changes is the shared base map selection
- When switching `Contour`, what changes is the shared contour overlay switch
- The semantics of these changes should be consistent with the map page

### `Layer` in the non-coordinate state Buttons

The `Layer` button remains visible and operable even if the current node has no coordinates.

The reason is:

- It switches the shared map layer preference
- It does not depend on whether the current node can be projected
- No coordinates only means that the current page does not display map content, it does not mean that the layer switching ability is invalid

But in the non-coordinate state:

- The page still must not pretend to be a browsable map
- The map background will not be forcibly displayed after switching layers
- The primary and secondary status of the page remains unchanged

---

## 6. Map Semantics

## 6.1 Node Marker

When the node has longitude and latitude:

- The node location must be marked on the map
- The node marking should be clear, eye-catching, and easy to locate
- The mark color should be distinguished from the "My Location" mark color

## 6.2 Self Marker

The "My Location" mark will be displayed only when the local machine also has valid longitude and latitude.

## 6.3 Connection Line

The connection is only displayed when both "Node Position" and "My Position" exist.

Requirements:

- The connection should be clear but not overwhelming
- The color should be distinguishable from the base map and both markers

## 6.4 Distance Label

The distance is only displayed if both "Node Position" and "My Position" exist.

Requirements:

- The distance label must be semantically consistent with the connection
- Distance labels prioritize spatial understanding and should not obscure the main text

## 6.5 Panning Semantics

When the node has latitude and longitude, the map content layer must support sliding.

Requirements:

- Users can view the area around the node by dragging the map
- What slides is the map viewport, not the entire page layout
- The positions of UI overlays such as ID, longitude and latitude text, right information column, zoom button, etc. must remain stable and cannot drift with the map
- Map semantic layers such as node markers, self-marks, connections, distances, etc. must move synchronously with the base map
- If the node does not have longitude and latitude, the map sliding ability is not provided
- The default view of the first frame must place the current node in the geometric center of the visual area, rather than deviating to the left or right of the composition
- Dragging only represents "temporarily browsing the surrounding area"; it does not change the main object of this page, nor does it change the zoom anchor semantics
- Therefore, when the user performs zoom again after dragging, the viewport must reconverge to the "node-centered" anchor state

The responsibility of the sliding ability is to "browse around the node", not to rewrite the main object of the page. Even if the user drags the viewport away from the node, the node remains the main reference object of the page.

---

## 7. Data Presence States

## 7.1 State A: Node Has No Coordinates

Performance:

-Do not display the map basemap
-Do not display node map markers
-Do not display self-marks
-Do not display connections
- Do not show distance
- Zoom button disabled
- Still show upper left ID and right information column

## 7.2 State B: Node Has Coordinates, Self Has No Coordinates

Performance:

- Show map background
- Show node markers
-Do not display self-marks
-Do not display connections
- Do not show distance
- Support map sliding
- Zoom button available

## 7.3 State C: Node Has Coordinates, Self Also Has Coordinates

Performance:

- Show map background
- Show node markers
-Display self-mark
-Display a line connecting two points
-Display distance
- Support map sliding
- Zoom button available

---

## 8. Visual Style

## 8.1 General Style Direction

`Node Info` page must inherit [Firmware Visual Style Specification](../foundation/firmware_visual_style.md).

This means:

- The overall page must maintain a warm engineering instrument style
- TopBar must use the same shared chrome as other standard pages
- Private introduction of dark HUD/cyber color schemes on this page is not allowed
- The map semantic layer can use limited semantic colors, but the page chrome is still dominated by warm-color tokens

The page should get rid of the "debug panel feel" and "form feel", and be closer to a warm-color engineering map details page with a sense of space.

Requirements:

- Don't stack boxes
- Don't stack edges
- Don't make the page look like a report
- Vivid colors but not clutter

## 8.2 Color Strategy

The following elements allow for color differentiation with limited semantics:

- ID
- Longitude
- Latitude
- Distance
- Node markers
- Self tag
- Connection

The information column on the right side should be mainly `Text / TextDim` by default, and only use `Info / Ok / Warn / AmberDark` for semantic emphasis when necessary.

Color should express hierarchy and role, not random coloring.

For the information column on the right, color allocation should also be fixed and converged:

- `Protocol`:`Text`
- `RSSI`:`Text`
- `SNR`:`Ok`
- `Seen`:`TextDim`

The standard node details page does not allow different link items to randomly invent a set of colors.

## 8.3 Typography

Requirements:

- ID is one of the highest priority texts on the page
- Longitude and latitude are secondary, but still obvious
- Information items on the right are prioritized according to scannability, rather than being squeezed into dense small characters
- Do not make the font size too small to accommodate more fields

---

## 9. Interaction Specification

## 9.1 Enter Page

After entering the node details page:

- The page should directly display the current information of the node
- If there are coordinates, it should directly enter the map background state
- If there are coordinates, the first frame will be centered on the node by default
- No additional clicks are required to expand the map

## 9.2 Drag / Pan

 When the node has longitude and latitude, the map must support dragging.

Requirements:

- The dragging gesture only works on the map viewport
- After dragging, the user can view the area near the node
- Dragging does not change the semantic status of the node as the main object of the page
- Dragging should not cause the upper left ID, lower left coordinate, right information column and lower right zoom button to be misaligned

## 9.3 Zoom In / Out

- `+` Enlarge the map
- `-` Zoom out the map
- The zoom center is fixed to the node, not the center of the screen to roam freely
- If the user has previously dragged the map away from the node, the view should return to the state of using the node as the zoom anchor after triggering zoom

## 9.4 Disabled Interaction

When the node has no coordinates:

-The zoom button is inoperable
-The map cannot be slid
- The page cannot pretend to have map capabilities
- There should be no vague feedback of "it looks like it clicked but nothing happened" after clicking it

## 9.5 Layer Switching

The layer switching interaction triggered by the `Layer` button obeys the following rules:

1. Do not rebuild the `Node Info` page.
2. Do not change the current viewing object.
3. Do not change the semantics of the information on the right side of the page.
4. If the current node has coordinates, the basemap will be refreshed according to the new layer configuration.
5. If the current node has no coordinates, only the shared map layer preference will be updated and the map display will not be forced.
6. The missing image prompt, layer normalization, and contour switch semantics must be consistent with the shared map viewport.
7. The layer pop-up window/pop-up layer must inherit the global warm color pop-up layer style and remain operable on small screens. It is not allowed to be made into dark modal blocks that are severely blocked and difficult to operate.
8. Under the `320x240` baseline, the vertical positioning of the layer pop-up window must be prioritized close to the top safe area or close to the top of the trigger button; if there is insufficient space above the button, it is not allowed to degenerate into a low-position pop-up window that "presses as hard as possible on the bottom edge of the screen."
9. The status summary of the layer pop-up window must be compressed into a single line for display, fixed to `Base: <Source>` and `Contour: <ON/OFF>` in the same line; it is not allowed to be split into two lines again.
10. The layer pop-up window must ensure that the five action items of `OSM / Terrain / Satellite / Contour / Close` are fully visible at one time under the `320x240` baseline; it is not allowed to rely on cropping, scrolling or super-high buttons to occupy the operable space.

---

## 10. Content Rules

## 10.1 Role Field

The "Role" field is not displayed by default.

Reintroduction is allowed only when role semantics that are meaningful, stable, and valuable to users are truly defined on subsequent products, and the value is not a placeholder.

## 10.2 Empty Values

Do not use `-`, `N/A`, or empty tags to fill the page.

The rules are:

- Display only if there is a value
- Omit if there is no value

## 10.3 Information Priority

The information priority of the page is as follows:

1. Node identity
2. Node spatial location
3. My spatial relationship with the node
4. Node links and additional information

The information items on the right must not in turn suppress the map and identity information.

### 10.4 Standard Page Contract

The standard `Node Info` page is not a diagnostic page.

So its information contract is fixed as:

-Upper left: node ID
-Bottom left: longitude, latitude
-Map layer: node position, own position, connection, distance
-right side: `Protocol / RSSI / SNR / Seen`

Other than this, no other link details will be shown.

The purpose of this contract is to compress the degree of freedom of implementation and prevent different developers or AI from continuing to diverge under the judgment of "maybe these fields are useful".

---

## 11. Implementation Guardrails

To avoid drifting again, the following constraints must be observed during subsequent modifications:

1. Reintroduction of the "link box" is not allowed.
2. It is not allowed to restore the card layout for filling fields.
3. Role fields that only display `-` are not allowed to be retained.
4. It is not allowed to reduce the map to a small component in the page. As long as the node has coordinates, the map is the background main canvas.
5. The zoom is not allowed to drift around any center point. The zoom center must be a node.
6. Pretending to be a zoomable map page without coordinates is not allowed.
7. "The map supports sliding" is not allowed to be implemented so that the entire details page scrolls, and the fixed information layer must remain stable.
8. It is not allowed to degenerate the color strategy into "all text of the same color".
9. It is not allowed to make the right information column into a "left-aligned paragraph on the right side".
10. Recompression of protocol object names into abbreviated expressions such as `MT/MC/RN/LX` is not allowed.
11. Re-adding `LoRa / MQTT / FREQ / SF / BW / CH / HOPS / NEXT` to the standard `Node Info` page is not allowed.
12. It is not allowed to extend the right information column to more than 4 lines.
13. It is not allowed to implement an independent map main process again in the `Node Info` page; map basemap, projection, dragging, zooming, and layer switching must be connected to the shared map viewport component.
14. It is not allowed to define different layer switching semantics in the `Node Info` page than in the `GPS/Map` page.
15. It is not allowed to implement the `Layer` button as a pseudo-entry that only changes the visual but not the shared layer configuration.

---

## 12. Open Items

The following issues can currently be left to the implementation stage, but they cannot violate the aforementioned structural constraints:

- The specific graphic style of the map marker
- The distance label is placed at the midpoint of the connection, close to the node, or close to the right information column
- Whether to use a solid color background or a lightweight texture background in the coordinate-free state
- Fine-tuning of font size and margins on different device sizes

These are fine-tuning of implementation details and should not overturn the page structure of this document.

---

## 13. Summary Baseline

Summary the specifications of this page in one sentence:

The node details page should be a "full-screen map details page centered on the node location" rather than a "node information panel with multiple boxes".
