# Use Case: Load route and determine yaw and recovery

Status: **candidate; behavior exists, domain owner has not been formed**
Business boundary: map, positioning and on-site perception

## User goal

Load a route locally, see the progress of the current position relative to the route, receive a stable alarm when deviating, and automatically recover after returning to the route, and positioning jumps will not cause alarm jitter.

## Current actual behavior

1. Route storage loads route points.
2. GPS page runtime calculates the closest distance between the current position and each route segment.
3. `update_route_deviation_state` uses enter/exit dual thresholds to form hysteresis to avoid repeated switching of critical points.
4. UI projection on-route/deviated and distance; stop judging when route file or fix is ​​unavailable.

## Design gaps

`Route / NavigationSession / RouteProgress / DeviationPolicy` has no core owner; there is no unified model for route progress, target point advancement, reentrancy, route version changes and alarm throttling. Therefore, this article confirms the user goals and existing algorithms, and does not mark the complete navigation life cycle as confirmed.

Source code: `modules/ui_shared/src/ui/screens/gps/gps_page_runtime.cpp`, `modules/core_sys/include/platform/ui/route_storage.h`.

## Drill down

- [Activity](follow-route/activity.md)
- [Sequence](follow-route/sequences/sequence-follow-route.md)
