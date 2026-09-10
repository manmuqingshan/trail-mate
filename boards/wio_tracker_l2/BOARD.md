# Wio Tracker L2 board facts

This package describes the Seeed Wio Tracker L2 / L2 Pro with the 3.2-inch
IPS touch display. `wio_tracker_l2` is the canonical repository spelling.
Hardware facts do not imply that a Trail Mate runtime capability is available.

## Sources

Checked on 2026-09-10:

- [Seeed introduction and pin table](https://wiki.seeedstudio.com/meshtastic_wio_tracker_l2_intro/)
- [Meshtastic board metadata](https://github.com/meshtastic/firmware/blob/develop/boards/seeed_wio_tracker_L2.json)
- [Meshtastic pin definitions](https://github.com/meshtastic/firmware/blob/develop/variants/esp32s3/seeed_wio_tracker_L2/variant.h)
- [Meshtastic board startup](https://github.com/meshtastic/firmware/blob/develop/src/platform/extra_variants/seeed_wio_tracker_l2/variant.cpp)
- [Meshtastic device-ui display, touch and backlight configuration](https://github.com/meshtastic/device-ui/blob/master/include/graphics/LGFX/LGFX_WIO_TRACKER_L2.h)
- [Meshtastic L2 battery conversion](https://github.com/meshtastic/firmware/blob/develop/src/Power.cpp)
- [AW35615 USB-C status definitions](https://github.com/mverch67/AW35615/blob/1.0.0/src/AW35615.h)

The upstream links track moving branches. Recheck the schematic and these
definitions when supporting a different PCB or display revision.

## Hardware

| Device | Facts |
| --- | --- |
| MCU | ESP32-S3, dual core, 240 MHz |
| Memory | 16 MB QSPI flash, 8 MB octal PSRAM (`qio_opi`) |
| Display | NV3031B, QSPI, native 240 x 320; landscape 320 x 240 |
| Touch | GT911 on shared I2C, upstream address `0x5D` |
| Backlight | LP5814 at I2C `0x2C`; not a direct ESP PWM output |
| GPIO expander | TCA/PCA9555-compatible 16-bit register map, I2C `0x21` |
| Radio | SX1262, dedicated SPI, DIO2 RF switch, DIO3 1.8 V TCXO |
| GNSS | L76K, UART; power and reset controlled by the expander |
| Storage | microSD, native 1-bit SDMMC; no SPI chip select |
| Battery measurement | ADS1115 at I2C `0x48`, AIN0, +/-4.096 V range, 1:2 divider; measurement enable on expander |
| Audio | ES8311 codec and speaker; separate amplifier enable |
| Wireless | Native ESP32-S3 Wi-Fi and BLE |
| Power | USB-C 5 V, Li-ion battery; solar input supported |
| L2 Pro battery | Manufacturer specifies 3000 mAh |

The optional e-paper connector does not establish an e-paper product target.
It shares display pins and requires a separate panel and refresh policy.
The microphone is described as reserved in Seeed's specification; do not
advertise microphone capture based only on the presence of audio pin definitions.

## ESP GPIO assignments

| Bus / function | GPIOs |
| --- | --- |
| Shared I2C | SDA 47, SCL 48; upstream startup uses 100 kHz |
| Radio SPI | SCK 4, MISO 5, MOSI 6, CS 21 |
| Radio control | RESET 7, BUSY 8, DIO1 9 |
| Display QSPI / SPI3 | CLK 42, IO0 41, IO1 40, IO2 39, IO3 38, CS 46 |
| GNSS UART | ESP RX 18, ESP TX 17 |
| SDMMC | CLK 2, CMD 3, D0 1 |
| I2S | MCLK 10, BCLK 11, LRCK 12, codec data input 16, output 15 |
| Expander interrupt | 45 |
| User / boot button | 0 |
| Native USB | D- 19, D+ 20 |
| Grove | 14, 13 |

## Expander numbering and startup constraints

`EXP_Pxy` in the Seeed table is a **port/bit designation**, not decimal GPIO
`xy`. The linear expander index is `8 * port + bit`. It is also not an ESP
GPIO number. For example, GNSS enable `EXP_P15` is index 13, and GNSS reset
`EXP_P11` is index 9. These agree with the upstream firmware.

| Expander pin | Linear index | Function |
| --- | --- | --- |
| P00 | 0 | Wake button input |
| P01 | 1 | Shared I2C interrupt input |
| P02 | 2 | SD card detect input |
| P03 | 3 | Touch interrupt / address selection during reset |
| P04 | 4 | Upstream additional LCD control / CS; held high during startup |
| P05 | 5 | LCD power enable |
| P06 | 6 | LCD reset |
| P07 | 7 | Grove power enable |
| P10 | 8 | Touch reset |
| P11 | 9 | GNSS reset, active high in upstream startup |
| P12 | 10 | User LED |
| P13 | 11 | USB OTG enable |
| P14 | 12 | Audio amplifier enable |
| P15 | 13 | GNSS power enable |
| P16 | 14 | SD power enable |
| P17 | 15 | Battery measurement enable |

Before implementing output control, preload the desired output latch before
changing the pin direction. Keep OTG and the audio amplifier disabled until
their respective runtime explicitly requests them. GNSS reset is released
low; display and touch resets are released high.

The display's actual QSPI chip select is GPIO46 in both Seeed's table and the
upstream display driver. Upstream startup additionally holds expander P04
high. Do not substitute the expander pin for GPIO46. Upstream's `LED_POWER=46`
definition conflicts with display CS ownership and must not be reused as a
generic status LED.

The implementation uses the upstream landscape transform: logical X is
319 minus native touch Y, and logical Y is native touch X. Touch reads inspect
the data-ready bit, validate the contact count and read one bounded contact
before acknowledging the controller.

AW35615 exposes VBUS presence; a separate charge-current or charge-complete
signal has not been established by the sources used here. WAKE is connected
through the GPIO expander, while BOOT is the separate direct GPIO0 input.
The executable adapter and its power behavior are documented under
`platform/esp/wio_tracker_l2`.

## Hardware validation still needed

- Confirm PCB/display revision, both LCD control signals, and reset timing.
- Confirm GT911 address selection, touch axes, and orientation on the device.
- Confirm battery channel, divider and charging-state source before exposing
  a battery percentage or charging status.
- Validate RF operation in the selected region. The manufacturer lists
  different maximum transmit powers for US915 and EU868; hardware capability
  is not a region-policy setting.
- Validate SD mounting, shared I2C ownership, audio, and sleep/wake separately.
