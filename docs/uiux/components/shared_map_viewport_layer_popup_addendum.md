# Shared Map Viewport Layer Popup Addendum

This file is [shared_map_viewport.md](C:/Users/VicLi/Documents/Projects/trail-mate/docs/uiux/components/shared_map_viewport.md) and A minimal addition to [shared_map_viewport_impl.md](C:/Users/VicLi/Documents/Projects/trail-mate/docs/uiux/components/shared_map_viewport_impl.md), which is used to completely separate the "copy ownership" and "page shell responsibility" in the layer pop-up window.

## 1. Distinction

1. The layer pop-up window is not a private business object of a certain page, but a visual portal that shares the semantics of map layers.
2. The page has a "pop-up shell".
3. The shared map component has "layer semantics and layer-specific copywriting".

## 2. Component Ownership

Shared map components must uniformly possess and output the following map-specific visible semantics:

1. Layer pop-up title key, such as `Map Layer`.
2. Basemap name key, such as `OSM/Terrain/Satellite`.
3. Status summary format, such as `Base: <Source>`.
4. Contour status text, such as `Contour: ON / OFF`.
5. Map-related missing prompts, such as missing layers or missing contour data.

Pages are not allowed to reinvent these strings in their own files, nor are they allowed to give the same layer state a second set of names.

## 3. Page-Shell Ownership

The page shell can be determined independently:

1. Which parent container is the pop-up window mounted to?
2. How to position the pop-up window relative to the trigger button or safe area.
3. Background mask, close button wiring, focus group switching and exit animation.

However, the page shell shall not be determined independently:

1. Layer name.
2. Status summary format.
3. Wording for missing pictures.
4. The set of legal values ​​for the layer.

## 4. Localization Rule

1. The map-specific copy output by the shared map component must be provided to the page in the form of a localizable key or formatted localized text.
2. If the page needs to display these copywriting, it can only consume the key or result given by the shared component. Words such as `Terrain`, `Satellite`, and `Contour` must not be rewritten into the page implementation.
3. Universal action words such as `Close` can continue to use the public i18n key, but they must not bypass localization.

## 5. Consequence

The purpose of this addition is not to increase the abstraction layer, but to prevent the shadow implementation of "the page shell also serves as the layer semantic owner" from drifting back again in the future.
