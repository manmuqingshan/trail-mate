# Display Orientation Policy Specification

## Purpose

This specification defines the boundary between board motion-sensor facts and
product display-orientation behavior.

Tab5 and T-Display-P4 both have motion sensors. That hardware fact does not
automatically authorize portrait UI behavior.

## Core Rule

```text
Board describes sensor facts.
Target chooses orientation policy.
Display host applies orientation.
Pages do not decide device rotation.
```

## Current Policy

For the current ESP32-P4 targets:

```text
tab5: sensor-aware, landscape locked
t_display_p4_tft: sensor-aware, landscape locked
t_display_p4_amoled: sensor-aware, landscape locked
```

The product may observe that a motion sensor exists, but the active screen
orientation must remain landscape. Portrait support is false in this release.

Sensor-derived requests that would require portrait UI are deferred until the
page set and layout profiles explicitly support portrait. They must not be
implemented by patching individual pages.

The current implementation may initialize sensor/heading runtime so that board
facts and future rotation signals are observable. It must still clamp the
effective orientation to landscape for `tab5`, `t_display_p4_tft`, and
`t_display_p4_amoled`.

The six-axis sensor on each P4+C6 target is a board capability, not an
orientation policy by itself:

```text
tab5: BMI270+BMM150, fixed landscape in this release
t_display_p4_tft: ICM20948, fixed landscape in this release
t_display_p4_amoled: ICM20948, fixed landscape in this release
```

Sensor startup may be used for diagnostics and future orientation decisions.
It must not rotate any page into portrait in this release.

## Runtime Contract

`platform::ui::orientation::get_screen_orientation()` reports:

- the selected policy,
- the active orientation,
- whether a motion sensor is available,
- whether the sensor runtime is ready,
- whether portrait is supported,
- whether a sensor request was ignored.

The display host may set the physical LVGL/display rotation needed to satisfy
the active orientation. UI pages consume the resulting viewport and must not
make board-specific rotation decisions.

For this release, any portrait request sourced from an accelerometer,
magnetometer, IMU fusion result, or future heading runtime must be represented
as ignored/deferred state. It must not rotate the display into portrait.

## Forbidden Shortcuts

Do not:

- infer portrait support from a six-axis sensor.
- change page layouts in response to raw accelerometer values.
- add target-specific rotation branches inside page code.
- let board profiles choose UX or orientation policy.
- use a motion sensor driver as the product orientation authority.

## Future Auto-Rotation Gate

Sensor auto-rotation may be introduced only after:

- portrait layout profiles exist for the affected targets,
- page-level overflow and navigation behavior are validated in portrait,
- touch coordinate transforms are verified per display host,
- the target profile changes from `SensorLandscapeOnly` to `SensorAuto`.
