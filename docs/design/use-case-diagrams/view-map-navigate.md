# Use Case: Use offline maps to establish on-site situation

Status: **confirmed**
Business boundary: map, positioning and on-site awareness

## User Goals

View the current location, offline base map, contacts/nearby nodes, team location, waypoints, routes and trajectories when there is no public network, and be able to determine where each overlay object comes from, when it was updated, and whether it is stale.

## Main scene

1. Map workspace restores the last viewport; if there is a credible fix, the user can choose to center instead of automatically grabbing the gesture viewport.
2. Tile source reads the tiles required by the current zoom/viewport from local storage; missing tiles still retain the coordinate grid and existing overlays.
3. LocationService, ContactService, Team events, and Route/Track storage provide projections with source and time respectively.
4. Map model renders protocol nodes, contacts, team members, waypoints, routes, and trajectories as different types; click on the object to enter the corresponding details without changing the source owner.
5. Incremental refresh when new revision arrives; expired position reduces credibility and does not risk full time.

## Failure and recovery

 - no fix: display last-known or unknown, do not generate `(0,0)`.
 - No tiles: retain overlay and coordinate semantics.
- SD/storage busy: UI does not block radio; display of base map/route/track is temporarily unavailable.
- Cross-protocol NodeIds are not directly merged into the same map object.

Source code: `modules/ui_shared/src/ui/screens/gps/gps_page_runtime.cpp`, `platform/linux/uconsole/src/uconsole_map_workspace_model.cpp`, `modules/core_gps/src/usecase/location_service.cpp`.

## Drill down

- [Activity: Offline situation assembly](view-map-navigate/activity.md)
- [Sequence: Data source to Map workspace](view-map-navigate/sequences/sequence-view-map-navigate.md)
