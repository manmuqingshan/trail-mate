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
- Audio I2S: `BCLK=12`, `MCLK=13`, `WS=9`, `DOUT=10`, `DIN=11`
- SX1262 SPI: `host=SPI2`, `SCK=2`, `MISO=4`, `MOSI=3`, `CS=24`, `BUSY=6`
- XL9535 expander:
  - `IO1` = `SKY13453_VCTL`
  - `IO11` = GPS wake
  - `IO14` = C6 enable
  - `IO15` = SD enable
  - `IO16` = SX1262 reset
  - `IO17` = SX1262 DIO1
- Boot key: `35`
- Expander interrupt: `5`
- Backlight: `51`

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

- a fake “dual-firmware builder”
- a place that pretends the C6 companion firmware exists in this repo
- a fallback bucket for generic shared runtime logic

The board is dual-MCU in hardware, but this repo currently owns only the **P4-side firmware**. The C6 side is an external flashing contract, not an in-repo board runtime detail.

## Relationship To Other Layers

- `boards/t_display_p4/*`
  Own board truth and hardware arbitration.
- `platform/esp/idf_components/t_display_p4/*`
  Own display/touch/LVGL runtime.
- `platform/esp/idf_common/*`
  Consume the board contract, but should not duplicate pin truth here.
