// Debug Pager and T-Deck images intentionally expose an Arduino TinyUSB
// composite device: CDC for logs and a full DFU interface for firmware
// transfer. Arduino-ESP32 provides only a weak empty full-DFU descriptor by
// default, so supply the OTA descriptor and callbacks here.
//
// The release-only DFU-removal macro is never defined in debug environments.
// nRF52 uses a separate TinyUSB stack and does not compile this file.

#if defined(TRAIL_MATE_ENABLE_ARDUINO_TINYUSB_DFU)

#include <cstring>

#include <Arduino.h>
#include <Update.h>
#include <esp32-hal-tinyusb.h>
#include <esp_system.h>

#define TRAIL_MATE_DFU_ALT_COUNT 1

// This symbol is force-referenced by debug environment linker flags. Arduino's
// framework ships weak no-op DFU callbacks in a separate static archive; the
// explicit reference makes the linker extract this object so these strong
// callbacks and descriptor take precedence over those no-ops.
extern "C" void trail_mate_debug_tinyusb_dfu_link_anchor(void) {}

uint16_t load_dfu_ota_descriptor(uint8_t* destination, uint8_t* interface_number)
{
    constexpr uint8_t kDfuAttributes =
        DFU_ATTR_CAN_DOWNLOAD | DFU_ATTR_CAN_UPLOAD | DFU_ATTR_MANIFESTATION_TOLERANT;

    const uint8_t string_index = tinyusb_add_string_descriptor("Trail Mate DFU");
    const uint8_t descriptor[TUD_DFU_DESC_LEN(TRAIL_MATE_DFU_ALT_COUNT)] = {
        TUD_DFU_DESCRIPTOR(*interface_number,
                           TRAIL_MATE_DFU_ALT_COUNT,
                           string_index,
                           kDfuAttributes,
                           100,
                           CFG_TUD_DFU_XFER_BUFSIZE),
    };
    *interface_number += 1;
    memcpy(destination, descriptor, TUD_DFU_DESC_LEN(TRAIL_MATE_DFU_ALT_COUNT));
    return TUD_DFU_DESC_LEN(TRAIL_MATE_DFU_ALT_COUNT);
}

uint32_t tud_dfu_get_timeout_cb(uint8_t, uint8_t state)
{
    if (state == DFU_DNBUSY)
    {
        return 10;
    }
    if (state == DFU_MANIFEST)
    {
        return 100;
    }
    return 0;
}

void tud_dfu_download_cb(uint8_t, uint16_t, uint8_t const* data, uint16_t length)
{
    if (!Update.isRunning() && !Update.begin())
    {
        tud_dfu_finish_flashing(DFU_STATUS_ERR_TARGET);
        return;
    }

    const size_t written = Update.write(const_cast<uint8_t*>(data), length);
    tud_dfu_finish_flashing(written == length ? DFU_STATUS_OK : DFU_STATUS_ERR_WRITE);
}

void tud_dfu_manifest_cb(uint8_t)
{
    tud_dfu_finish_flashing(Update.end(true) ? DFU_STATUS_OK : DFU_STATUS_ERR_VERIFY);
}

uint16_t tud_dfu_upload_cb(uint8_t, uint16_t, uint8_t*, uint16_t)
{
    return 0;
}

void tud_dfu_abort_cb(uint8_t) {}

void tud_dfu_detach_cb(void)
{
    esp_restart();
}

#undef TRAIL_MATE_DFU_ALT_COUNT

#endif
