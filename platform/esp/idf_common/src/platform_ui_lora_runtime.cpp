#include "platform/ui/lora_runtime.h"

#include <cmath>
#include <limits>

#include "boards/t_display_p4/board_profile.h"
#include "boards/tab5/tab5_board.h"
#include "platform/esp/idf_common/sx126x_radio.h"

namespace
{

bool board_has_lora_capability()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    return ::boards::tab5::Tab5Board::hasLora() ||
           ::boards::tab5::Tab5Board::hasM5BusLoraRouting();
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return ::boards::t_display_p4::kBoardProfile.has_lora;
#else
    return false;
#endif
}

platform::esp::idf_common::Sx126xRadio& radio()
{
    return platform::esp::idf_common::Sx126xRadio::instance();
}

} // namespace

namespace platform::ui::lora
{

bool is_supported()
{
    return board_has_lora_capability();
}

bool acquire()
{
    return is_supported() && radio().acquire();
}

bool is_online()
{
    return radio().isOnline();
}

bool configure_receive(float freq_mhz, const ReceiveConfig& config)
{
    return radio().configureLoRaReceive(freq_mhz,
                                        config.bw_khz,
                                        config.sf,
                                        config.cr,
                                        config.tx_power,
                                        config.preamble_len,
                                        config.sync_word,
                                        config.crc_len);
}

float read_instant_rssi()
{
    const float rssi = radio().readRssi();
    return std::isfinite(rssi) ? rssi : std::numeric_limits<float>::quiet_NaN();
}

bool poll_received_packet(uint8_t* buffer, std::size_t capacity, ReceivedPacket* out_packet)
{
    constexpr uint32_t kIrqRxDone = 0x0002u;
    if (!buffer || capacity == 0 || !out_packet || !radio().isOnline())
    {
        return false;
    }

    const uint32_t irq = radio().getIrqFlags();
    if ((irq & kIrqRxDone) == 0u)
    {
        if (irq != 0u)
        {
            radio().clearIrqFlags(irq);
            (void)radio().startReceive();
        }
        return false;
    }

    const int packet_size = radio().getPacketLength(true);
    const bool packet_fits = packet_size > 0 &&
                             static_cast<std::size_t>(packet_size) <= capacity;
    const int read_state = packet_fits
                               ? radio().readPacket(buffer, static_cast<std::size_t>(packet_size))
                               : -1;
    const float rssi_dbm = packet_fits ? radio().readRssi() : 0.0f;

    radio().clearIrqFlags(irq);
    (void)radio().startReceive();

    if (!packet_fits || read_state != 0)
    {
        return false;
    }

    out_packet->size = static_cast<std::size_t>(packet_size);
    out_packet->rssi_dbm = rssi_dbm;
    out_packet->snr_db = std::numeric_limits<float>::quiet_NaN();
    return true;
}

void release()
{
    radio().release();
}

} // namespace platform::ui::lora
