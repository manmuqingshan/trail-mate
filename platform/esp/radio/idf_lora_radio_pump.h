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
    ReceiveRestarted,
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
    IdfLoraPollResult poll(IdfLoraRadioFrame& out);

  private:
    static constexpr std::size_t kMaxPacketBytes = 255;

    LoraBoard& board_;
    std::array<uint8_t, kMaxPacketBytes> rx_scratch_{};
    bool receive_started_ = false;
};

} // namespace platform::esp::radio
