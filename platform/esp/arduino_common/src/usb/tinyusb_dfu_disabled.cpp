// Arduino-ESP32 precompiles both TinyUSB DFU classes for every ESP32-S3 memory
// profile. Release Pager and T-Deck firmware exposes neither a USB CDC log
// port nor an application DFU endpoint. Updating is intentionally limited to
// the ESP32-S3 ROM downloader entered with the physical BOOT + RESET sequence.
//
// TinyUSB's precompiled usbd.c object has direct references to the class
// driver hooks. Supplying inert hooks here prevents the linker from extracting
// dfu_device.c and dfu_rt_device.c from libarduino_tinyusb.a. That removes the
// full DFU driver's 4 KiB transfer buffer (_dfu_ctx) and all DFU Runtime code.
// No Arduino TinyUSB DFU descriptor is registered in a release image.
//
// This file must remain guarded by the environment-specific macro injected in
// scripts/platformio-pre.py. nRF52 targets build a different TinyUSB stack and
// intentionally retain their DFU support.

#if defined(TRAIL_MATE_DISABLE_ARDUINO_TINYUSB_DFU)

#include <tusb.h>

extern "C"
{
    void dfu_moded_init(void) {}

    void dfu_moded_reset(uint8_t) {}

    uint16_t dfu_moded_open(uint8_t, tusb_desc_interface_t const*, uint16_t)
    {
        return 0;
    }

    bool dfu_moded_control_xfer_cb(uint8_t, uint8_t, tusb_control_request_t const*)
    {
        return false;
    }

    void dfu_rtd_init(void) {}

    void dfu_rtd_reset(uint8_t) {}

    uint16_t dfu_rtd_open(uint8_t, tusb_desc_interface_t const*, uint16_t)
    {
        return 0;
    }

    bool dfu_rtd_control_xfer_cb(uint8_t, uint8_t, tusb_control_request_t const*)
    {
        return false;
    }
}

#endif
