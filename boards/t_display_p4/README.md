# `boards/t_display_p4`

Board-owned runtime and hardware truth for `LILYGO T-Display-P4`.

This board family now maps to two explicit ESP-IDF target environments:

- `t_display_p4_tft`
- `t_display_p4_amoled`

The family-level code here stays shared. Variant selection belongs to the target descriptor layer, not to ad-hoc build commands or undocumented menuconfig toggles.

Primary source reference:

- `.tmp/T-Display-P4`

## Extracted Board Facts

- System I2C: `port=0`, `SDA=7`, `SCL=8`
- External I2C: `port=1`, `SDA=20`, `SCL=21`
- GPS UART: `UART1`, `TX=22`, `RX=23`
- SDMMC: `D0=39`, `D1=40`, `D2=41`, `D3=42`, `CMD=44`, `CLK=43`
- C6 SDIO link: `CLK=18`, `CMD=19`, `D0=14`, `D1=15`, `D2=16`, `D3=17`
- C6 companion module: present, ESP32-C6, P4-C6 transport target is SDIO
- C6 reset/release control: XL9535 `IO14`, release high / assert low
- IMU: `ICM20948`, I2C address `0x68`
- Audio I2S: `BCLK=12`, `MCLK=13`, `WS=9`, `DOUT=10`, `DIN=11`
- SX1262 SPI: `host=SPI2`, `SCK=2`, `MISO=4`, `MOSI=3`, `CS=24`, `BUSY=6`
- XL9535 expander:
  - `IO1` = `SKY13453_VCTL`
  - `IO11` = GPS wake
  - `IO14` = C6 reset/release control
  - `IO15` = SD enable
  - `IO16` = SX1262 reset
  - `IO17` = SX1262 DIO1
- Boot key: `35`
- Expander interrupt: `5`
- Backlight: `51`
- Optional keyboard module:
  - software I2C: `SDA=46`, `SCL=45`
  - TCA8418: `0x34`, 10 x 7 scan window, interrupt `48`
  - keyboard backlight: `47`

## Display Runtime Baseline

The official `.tmp/T-Display-P4` LVGL demo uses a full-screen RGB565 draw
buffer allocated from `SPIRAM | 8BIT | DMA`, a 1 ms LVGL tick, and direct DPI
panel flush callbacks. The TrailMate IDF runtime keeps the panel timing and pin
facts aligned with that demo, uses a 1 ms LVGL tick, allocates RGB565 draw
buffers from PSRAM with DMA capability, enables the ESP32-P4 PPA rotation path,
and defaults `CONFIG_TRAIL_MATE_T_DISPLAY_P4_LVGL_BUFFER_LINES` to 200 for both
TFT and AMOLED targets. If real-device animation still feels slower than the
official demo after functional bring-up, tune this display-runtime policy in
`platform/esp/idf_components/t_display_p4` rather than changing board pin facts.

## Keyboard Runtime Policy

T-Display-P4 Keyboard is an accessory, not a base-board invariant. Runtime
probing owns the distinction:

- TCA8418 probe succeeds: register an LVGL keypad input device and suppress the
  large touch IME keyboard.
- TCA8418 probe fails or the module is absent: keep touch input active and show
  the virtual keyboard on large-touch compose surfaces.

## What Lives Here

This directory should own the hardware facts that stay true even if higher-level runtime code changes:

- board profile data
- board bootstrap and power sequencing
- SYS/EXT I2C ownership
- XL9535 expander access
- RTC access and RTC-to-system-time sync
- GPS UART wake/setup/teardown
- SDMMC power + mount contract
- LoRa-side expander control and verified RF-path assumptions

## What Must Not Drift Here

This directory should **not** become:

- a dual-firmware builder
- the C6 companion firmware implementation
- a fallback bucket for generic shared runtime logic

The board is dual-MCU in hardware. The board package records the C6 hardware
facts, while the in-repository C6 firmware project lives at
`firmware/c6_companion`. The board package must not own C6 wireless facade
business policy or P4-C6 protocol semantics.

## Relationship To Other Layers

- `boards/t_display_p4/*`
  Own board truth and hardware arbitration.
- `platform/esp/idf_components/t_display_p4/*`
  Own display/touch/LVGL runtime.
- `platform/esp/idf_common/*`
  Consume the board contract, but should not duplicate pin truth here.
