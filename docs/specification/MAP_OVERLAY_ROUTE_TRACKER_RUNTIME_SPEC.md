# Map Overlay / Route / Tracker Runtime Spec

## Ownership Rule

Map overlay state is presentation projection state. It is not tile source state,
not renderer widget state, and not filesystem state.

The required flow is:

runtime source -> presentation source / projector -> `MapOverlaySnapshot` -> renderer

The forbidden flow is:

renderer -> GPS runtime / Team store / route store / tracker store / filesystem

## Types

`MapOverlayKind` describes what a map item means:

- `CurrentPosition`
- `TeamMember`
- `RoutePoint`
- `TrackPoint`
- `MeasurementPoint`
- `SelectedTarget`
- `Warning`

`MapOverlayStyle` describes renderer styling intent:

- `Default`
- `OwnPosition`
- `Team`
- `Route`
- `Track`
- `Warning`

`MapOverlayItem` is a view DTO with:

- kind/style
- `MapGeoPoint`
- label/detail fixed text
- stable id
- icon id
- selected/visible flags

`MapOverlaySnapshot` is a fixed-capacity snapshot with `kMaxItems = 64`.

It must not contain:

- `lv_obj_t*`
- tile paths
- bitmap bytes
- route store pointers
- GPS source pointers
- Team store pointers
- filesystem paths

## Source Port

`IMapOverlayPresentationSource` owns read projection:

```cpp
virtual bool buildMapOverlaySnapshot(MapOverlaySnapshot& out) const = 0;
```

It may be implemented by a legacy adapter, but renderers must depend on the port
or on an already-built snapshot, not on concrete runtime sources.

## Projector

`MapOverlayProjector` maps already-read runtime facts to overlay rows:

- `projectCurrentPosition(...)`
- `projectTeamMember(...)`

It validates coordinates, assigns kind/style/stable id, and appends to the fixed
snapshot.

It must not:

- read GPS runtime
- read Team store
- render LVGL widgets
- access tile source/cache
- access filesystem

## Legacy Adapter

`LegacyMapOverlaySource` is an anti-corruption adapter. It may depend on:

- `IMapOverlayGpsSource`
- `IMapOverlayTeamSource`
- `MapOverlayProjector`

The adapter exists to contain legacy GPS/team fact gathering. It does not render,
open files, or mutate map viewport state.

## Team Location Source

`TeamMapOverlaySource` owns Team member position projection for map consumers.

It may depend on:

- `ITeamUiSnapshotStore`
- Team posring storage API
- `IMapOverlayTeamSource`

It exposes fixed-capacity map facts through:

- `latestTeamPoints(...)`
- `latestTeamLocations(...)`
- `loadMemberLocation(...)`

GPS pages, map runtimes, dashboards, and renderers must not call
`team_ui_posring_load_latest(...)` directly. They consume `TeamMapOverlaySource`
or a built `MapOverlaySnapshot`.

## Runtime Wiring

Map page runtime or composition root owns:

- concrete legacy GPS/team overlay sources
- `LegacyMapOverlaySource`
- built overlay snapshot

Renderer consumes the overlay snapshot. During migration, a runtime may build
the snapshot before render and keep renderer changes small.

## Deferred Items

Route/tracker/breadcrumb overlays are deferred until route and tracker stores
have presentation source ports. Exit condition:

route/tracker facts are projected into `MapOverlaySnapshot` or a dedicated route
overlay snapshot, and renderer code no longer reads concrete route/tracker
stores.

Measurement point overlays are deferred until the measurement tool has a
runtime/source boundary. Exit condition:

measurement state is projected as `MeasurementPoint` items and renderers consume
the same overlay snapshot path.

## Route / Track File Streaming Rule

GPX, KML, and CSV route or track files are untrusted external files and may be
larger than the available embedded RAM budget. Runtime code must never load a
complete GPX/KML route or track file into a `String`, `std::string`, `std::vector`,
or equivalent heap buffer before parsing it.

Required behavior:

- read route/track files in bounded chunks from the platform filesystem;
- for GPX and CSV, line parsing may be layered on top of chunk reads, but line
  buffers must have a fixed maximum and overlong lines must be discarded or
  bounded;
- for KML, `<coordinates>` contents must be tokenized while streaming, because a
  single coordinates element may contain a large route;
- parsed overlay points must be downsampled into the fixed presentation budget
  before reaching `MapOverlaySnapshot`;
- the same streaming loader must be used by explicit track loading and by active
  route loading from `AppConfig::route_path`.

Forbidden behavior:

- `readString()` / `String(file.read...)` style reads for GPX/KML;
- `istreambuf_iterator` or `rdbuf()` slurping for GPX/KML;
- `file.size()` followed by allocating a file-sized buffer for GPX/KML;
- separate ESP/Linux/nRF parsers with different route/track memory rules.

`scripts/check_track_file_streaming.py` is the CI guard for this rule. If a new
route/track format is added, extend the streaming loader and the guard together.

## Non-goals

- No map overlay visual redesign.
- No online tile download.
- No route/tracker engine rewrite.
- No measurement tool rewrite.
- No UX Pack work.
- No repository layout work.
