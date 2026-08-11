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

constexpr uint32_t radio_terminal_irq_mask()
{
    uint32_t mask = radio_rx_done_mask();
#if defined(ARDUINO_LILYGO_LORA_LR1121)
#if defined(RADIOLIB_LR11X0_IRQ_CRC_ERR)
    mask |= RADIOLIB_LR11X0_IRQ_CRC_ERR;
#endif
#if defined(RADIOLIB_LR11X0_IRQ_HEADER_ERR)
    mask |= RADIOLIB_LR11X0_IRQ_HEADER_ERR;
#endif
#if defined(RADIOLIB_LR11X0_IRQ_TIMEOUT)
    mask |= RADIOLIB_LR11X0_IRQ_TIMEOUT;
#endif
#else
#if defined(RADIOLIB_SX126X_IRQ_CRC_ERR)
    mask |= RADIOLIB_SX126X_IRQ_CRC_ERR;
#endif
#if defined(RADIOLIB_SX126X_IRQ_HEADER_ERR)
    mask |= RADIOLIB_SX126X_IRQ_HEADER_ERR;
#endif
#if defined(RADIOLIB_SX126X_IRQ_TIMEOUT)
    mask |= RADIOLIB_SX126X_IRQ_TIMEOUT;
#endif
#endif
    return mask;
}

#if defined(ARDUINO_LILYGO_LORA_LR1121) &&            \
    defined(RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED) && \
    defined(RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID)
static_assert((radio_terminal_irq_mask() &
               (RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED |
                RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID)) == 0u,
              "LR1121 receive progress IRQs must not restart the radio");
#elif defined(RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED) && \
    defined(RADIOLIB_SX126X_IRQ_SYNC_WORD_VALID) &&     \
    defined(RADIOLIB_SX126X_IRQ_HEADER_VALID)
static_assert((radio_terminal_irq_mask() &
               (RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED |
                RADIOLIB_SX126X_IRQ_SYNC_WORD_VALID |
                RADIOLIB_SX126X_IRQ_HEADER_VALID)) == 0u,
              "SX126x receive progress IRQs must not restart the radio");
#endif

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

    return s_lora->configureLoraRadio(freq_mhz,
                                      config.bw_khz,
                                      config.sf,
                                      config.cr,
                                      config.tx_power,
                                      config.preamble_len,
                                      config.sync_word,
                                      config.crc_len) == 0 &&
           s_lora->startRadioReceive() == 0;
}

bool transmit_packet(const uint8_t* data, std::size_t size)
{
    if (!s_lora || !data || size == 0)
    {
        return false;
    }

    const int state = s_lora->transmitRadio(data, size);
    (void)s_lora->startRadioReceive();
    return state == 0;
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
            // Preamble, sync-word, and header-valid IRQs are progress signals,
            // not a completed receive. Restarting here aborts the packet before
            // RX_DONE, especially for long low-rate LoRa frames.
            if ((irq & radio_terminal_irq_mask()) != 0u)
            {
                (void)s_lora->startRadioReceive();
            }
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
