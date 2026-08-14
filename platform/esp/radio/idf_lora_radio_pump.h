#pragma once

#include "board/LoraBoard.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace platform::esp::radio
{

enum class IdfLoraPollResult : uint8_t
{
    None = 0,
    Frame,
    RadioOffline,
    ReceiveStartFailed,
    ReceiveRestarted,
    ReceiveRestartFailed,
    InvalidPacketLength,
    ReadFailed,
};

struct IdfLoraRadioFrame
{
    const uint8_t* data = nullptr;
    std::size_t len = 0;
    int packet_length = 0;
    uint32_t irq = 0;
    float rssi = 0.0f;
    float snr = 0.0f;
};

class IdfLoraRadioPump final
{
  public:
    explicit IdfLoraRadioPump(LoraBoard& board);

    bool ensureReceiveStarted();
    bool restartReceive();
    void markReceiveStopped();
    int lastReceiveStartResult() const { return last_receive_start_result_; }
    IdfLoraPollResult poll(IdfLoraRadioFrame& out);

  private:
    static constexpr std::size_t kMaxPacketBytes = 255;

    LoraBoard& board_;
    std::array<uint8_t, kMaxPacketBytes> rx_scratch_{};
    bool receive_started_ = false;
    int last_receive_start_result_ = -1;
};

} // namespace platform::esp::radio
