# Use Case: Check GNSS satellite, positioning and time authority

Status: **confirmed**
Business boundary: map, positioning and scene awareness

## User goal

Judge whether GNSS is not enabled, starting, no satellite, no fix, whether the positioning quality is insufficient or normal; check the sky map and satellite table, and understand whether the system time comes from a valid GNSS input.

## Behavior and Rules

1. The page applies for GNSS power lease and reads `GnssStatus`, satellite snapshot and `GpsDiagnosticsSnapshot`.
2. Sky map showing satellites by constellation, azimuth, elevation, SNR and used-in-fix; table and top summary are from the same snapshot.
3. Empty state distinguishes between receiver starting, disabled, no data and no fix.
4. LocationService only accepts valid revisions/fixes; time updates are performed by the time authority port.
5. RMC time must pass date, epoch rationality and synchronization policy verification; invalid NMEA will not change the system clock.

Source code: `modules/ui_shared/src/ui/screens/gnss/gnss_skyplot_page_runtime.cpp`, `modules/core_gps/src/usecase/location_service.cpp`, `platform/esp/idf_common/src/gps_runtime.cpp`.

#

- [Activity](inspect-gnss-health/activity.md)
- [Sequence](inspect-gnss-health/sequences/sequence-inspect-gnss-health.md)
