#if defined(ESP_PLATFORM) && !defined(ARDUINO)

#include "idf_lora_radio_pump.h"

namespace platform::esp::radio
{
namespace
{

constexpr uint32_t kIrqRxDone = 0x0002;
constexpr int kRadioOk = 0;

} // namespace

IdfLoraRadioPump::IdfLoraRadioPump(LoraBoard& board)
    : board_(board)
{
}

bool IdfLoraRadioPump::ensureReceiveStarted()
{
    if (receive_started_)
    {
        return true;
    }
    if (!board_.isRadioOnline())
    {
        return false;
    }

    receive_started_ = (board_.startRadioReceive() == kRadioOk);
    return receive_started_;
}

bool IdfLoraRadioPump::restartReceive()
{
    receive_started_ = false;
    return ensureReceiveStarted();
}

void IdfLoraRadioPump::markReceiveStopped()
{
    receive_started_ = false;
}

IdfLoraPollResult IdfLoraRadioPump::poll(IdfLoraRadioFrame& out)
{
    out = IdfLoraRadioFrame{};
    if (!board_.isRadioOnline())
    {
        receive_started_ = false;
        return IdfLoraPollResult::RadioOffline;
    }
    if (!ensureReceiveStarted())
    {
        return IdfLoraPollResult::RadioOffline;
    }

    out.irq = board_.getRadioIrqFlags();
    if (out.irq == 0)
    {
        return IdfLoraPollResult::None;
    }

    board_.clearRadioIrqFlags(out.irq);
    if ((out.irq & kIrqRxDone) == 0)
    {
        (void)restartReceive();
        return IdfLoraPollResult::ReceiveRestarted;
    }

    out.packet_length = board_.getRadioPacketLength(true);
    if (out.packet_length <= 0 ||
        out.packet_length > static_cast<int>(rx_scratch_.size()))
    {
        (void)restartReceive();
        return IdfLoraPollResult::InvalidPacketLength;
    }

    if (board_.readRadioData(rx_scratch_.data(),
                             static_cast<std::size_t>(out.packet_length)) != kRadioOk)
    {
        (void)restartReceive();
        return IdfLoraPollResult::ReadFailed;
    }

    out.data = rx_scratch_.data();
    out.len = static_cast<std::size_t>(out.packet_length);
    out.rssi = board_.getRadioRSSI();
    out.snr = board_.getRadioSNR();
    (void)restartReceive();
    return IdfLoraPollResult::Frame;
}

} // namespace platform::esp::radio

#endif
