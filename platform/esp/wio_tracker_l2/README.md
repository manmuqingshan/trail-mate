# ESP Wio Tracker L2 adapter

This package implements the Arduino ESP32-S3 providers for the hardware facts
in boards/wio_tracker_l2. It owns SDK handles, bus locks, GPIO-expander access,
NV3031B/GT911/LP5814 execution, SX1262 radio IO, GNSS UART, battery sampling and
the single ES8311/I2S playback owner.

The BoardBase/GpsBoard/LoraBoard/SdBoard implementations and retained C++ board
provider names are interface adapters. Target selection, UX selection, page
membership and business service ownership stay outside this package.

The board-runtime binding header is the platform factory/dispatcher boundary.
It supplies the existing app-context handles and LVGL driver host without
creating a second product shell or service graph.

Audio notification and Codec2 receive-playback effects share the same I2S owner
and mutex. The notification runtime decides when to request a tone. The
microphone is not enabled or advertised.

The existing charging/USB facade uses AW35615 VBUS presence as external-power
indication; it does not measure charge current or charge completion. Battery
percentage is an estimate from measured ADS1115 voltage.

Screen sleep keeps touch available and disables backlight. Software deep sleep
wakes through BOOT or reset. Expander WAKE behavior during deep sleep, RF,
display/touch calibration, SD operation and audio require real-device checks.
