# Node Info Layer Popup Addendum

This file is an additional constraint of [node_info_page.md](C:/Users/VicLi/Documents/Projects/trail-mate/docs/uiux/pages/node_info_page.md). It only converges the layer pop-ups in the `Node Info` page and does not redefine the entire page layout.

## 1. Scope

1. This supplement only constrains the layer pop-up window opened by the `Layer` button at the bottom of the `Node Info` page.
2. The page shell can determine the pop-up window's location, container, closing trigger method and focus switching.
3. The page shell does not own the layer semantics itself and cannot redefine the layer name, status summary or missing image prompt.

## 2. Copy Contract

1. The map-specific copy in the `Node Info` page layer pop-up must reuse the semantics of the shared map component, and cannot directly write another set of English or page-private names in the page.
2. At least the following text must remain synonymous with the shared map component and be localizable:
   - `Map Layer`
   - `Base: <Source>`
   - `OSM / Terrain / Satellite`
   - `Contour: ON / OFF`
 - Layer missing prompt
 - Contour data missing prompt
3. The universal closing action allows the continued reuse of global public text keys, such as `Close`, but must still be processed by i18n.

## 3. Small-Screen Operability

1. Under the `320x240` baseline, the five action items of `OSM / Terrain / Satellite / Contour / Close` must be fully visible when the layer pop-up window is opened for the first time.
2. The `Close` action item must be completely visible and clickable, and is not allowed to be clipped by the edge of the screen, mask, parent container or super-tall button.
3. If the small screen space is insufficient, priority should be given to tightening the internal spacing, button height and positioning strategy of the pop-up window. It is not allowed to squeeze the last action item into half and expose it.

## 4. Consequence

1. If the `GPS/Map` page adjusts the layer name, summary format or missing image prompt in the future, the `Node Info` page must be adjusted along with the shared semantics.
2. In the future, if the `Node Info` page only changes the pop-up shell without changing the shared semantic layer, it will be regarded as a legal page implementation.
