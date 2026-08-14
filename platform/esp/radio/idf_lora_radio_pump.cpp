#if defined(ESP_PLATFORM) && !defined(ARDUINO)

#include "idf_lora_radio_pump.h"

namespace platform::esp::radio
{
namespace
{

constexpr uint32_t kIrqRxDone = 0x0002;
constexpr uint32_t kIrqTxDone = 0x0001;
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
        last_receive_start_result_ = -1;
        return false;
    }

    last_receive_start_result_ = board_.startRadioReceive();
    receive_started_ = (last_receive_start_result_ == kRadioOk);
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
        return IdfLoraPollResult::ReceiveStartFailed;
    }

    out.irq = board_.getRadioIrqFlags();
    if (out.irq == 0)
    {
        return IdfLoraPollResult::None;
    }

    // Sending can run concurrently with this receive pump.  TX_DONE belongs
    // to transmitRadio(), which waits for and clears it.  Treating it as a
    // receive-side terminal IRQ loses that completion and makes a successful
    // over-the-air send look like a timeout to the caller.
    if ((out.irq & kIrqTxDone) != 0)
    {
        return IdfLoraPollResult::None;
    }

    board_.clearRadioIrqFlags(out.irq);
    if ((out.irq & kIrqRxDone) == 0)
    {
        return restartReceive() ? IdfLoraPollResult::ReceiveRestarted
                                : IdfLoraPollResult::ReceiveRestartFailed;
    }

    out.packet_length = board_.getRadioPacketLength(true);
    if (out.packet_length <= 0 ||
        out.packet_length > static_cast<int>(rx_scratch_.size()))
    {
        if (!restartReceive()) return IdfLoraPollResult::ReceiveRestartFailed;
        return IdfLoraPollResult::InvalidPacketLength;
    }

    if (board_.readRadioData(rx_scratch_.data(),
                             static_cast<std::size_t>(out.packet_length)) != kRadioOk)
    {
        if (!restartReceive()) return IdfLoraPollResult::ReceiveRestartFailed;
        return IdfLoraPollResult::ReadFailed;
    }

    out.data = rx_scratch_.data();
    out.len = static_cast<std::size_t>(out.packet_length);
    out.rssi = board_.getRadioRSSI();
    out.snr = board_.getRadioSNR();
    // Deliver a packet that has already been read even if arming the next
    // receive window fails.  The next poll will retry it and report that
    // failure instead of silently throwing away real traffic.
    (void)restartReceive();
    return IdfLoraPollResult::Frame;
}

} // namespace platform::esp::radio

#endif
