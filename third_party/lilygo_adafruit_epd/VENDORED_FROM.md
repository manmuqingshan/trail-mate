Vendored from `C:\Users\vicliu\Projects\T-Echo-Lite\libraries\Adafruit_EPD-4.5.5`.

This is the LILYGO T-Echo Lite demo copy of Adafruit EPD 4.5.5. It carries
T-Echo Lite specific SSD1681 changes that are not present in the upstream
PlatformIO Adafruit EPD package, including:

- `Adafruit_EPD::Update_Mode`
- SSD1681 fast and partial refresh paths
- `setRAMValueBaseMap()`
- an SPI speed parameter used by the official demo

Local compatibility note:

- `Adafruit_EPD::end()` does not call `Adafruit_SPIDevice::end()` because newer
  PlatformIO Adafruit BusIO releases no longer expose that method.
