#include "platform/ui/lora_runtime.h"

#include <limits>

#if defined(ARDUINO)
#include <RadioLib.h>
#endif

#include "app/app_facade_access.h"
#include "board/LoraBoard.h"
#include "platform/esp/arduino_common/exclusive_lora_runtime.h"

namespace platform::ui::lora
{

namespace
{

using Session = ::platform::esp::arduino_common::exclusive_lora_runtime::Session;

Session s_session{};
LoraBoard* s_lora = nullptr;

constexpr uint32_t radio_rx_done_mask()
{
#if defined(ARDUINO_LILYGO_LORA_LR1121) && defined(RADIOLIB_LR11X0_IRQ_RX_DONE)
    return RADIOLIB_LR11X0_IRQ_RX_DONE;
#elif defined(RADIOLIB_SX126X_IRQ_RX_DONE)
    return RADIOLIB_SX126X_IRQ_RX_DONE;
#else
    return 0x0002u;
#endif
}

} // namespace

bool is_supported()
{
    return true;
}

bool acquire()
{
    if (s_lora)
    {
        return true;
    }
    s_session = ::platform::esp::arduino_common::exclusive_lora_runtime::acquire();
    s_lora = s_session.lora;
    return s_lora != nullptr;
}

bool is_online()
{
    return s_lora && s_lora->isRadioOnline();
}

bool configure_receive(float freq_mhz, const ReceiveConfig& config)
{
    if (!s_lora)
    {
        return false;
    }

    s_lora->configureLoraRadio(freq_mhz,
                               config.bw_khz,
                               config.sf,
                               config.cr,
                               config.tx_power,
                               config.preamble_len,
                               config.sync_word,
                               config.crc_len);
    s_lora->startRadioReceive();
    return true;
}

float read_instant_rssi()
{
    return s_lora ? s_lora->getRadioInstantRSSI() : std::numeric_limits<float>::quiet_NaN();
}

bool poll_received_packet(uint8_t* buffer, std::size_t capacity, ReceivedPacket* out_packet)
{
    if (!s_lora || !buffer || capacity == 0 || !out_packet)
    {
        return false;
    }

    const uint32_t irq = s_lora->getRadioIrqFlags();
    if ((irq & radio_rx_done_mask()) == 0u)
    {
        if (irq != 0u)
        {
            s_lora->clearRadioIrqFlags(irq);
            (void)s_lora->startRadioReceive();
        }
        return false;
    }

    const int packet_size = s_lora->getRadioPacketLength(true);
    const bool packet_fits = packet_size > 0 &&
                             static_cast<std::size_t>(packet_size) <= capacity;
    const int read_state = packet_fits
                               ? s_lora->readRadioData(buffer, static_cast<std::size_t>(packet_size))
                               : -1;
    const float rssi_dbm = packet_fits ? s_lora->getRadioRSSI() : 0.0f;
    const float snr_db = packet_fits ? s_lora->getRadioSNR() : 0.0f;

    s_lora->clearRadioIrqFlags(irq);
    (void)s_lora->startRadioReceive();

    if (!packet_fits || read_state != 0)
    {
        return false;
    }

    out_packet->size = static_cast<std::size_t>(packet_size);
    out_packet->rssi_dbm = rssi_dbm;
    out_packet->snr_db = snr_db;
    return true;
}

void release()
{
    if (!s_session.lora)
    {
        s_lora = nullptr;
        return;
    }

    app::configFacade().applyMeshConfig();
    ::platform::esp::arduino_common::exclusive_lora_runtime::release(&s_session);
    s_lora = nullptr;
}

} // namespace platform::ui::lora
