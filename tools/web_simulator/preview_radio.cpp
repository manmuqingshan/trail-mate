#include "platform/ui/lora_runtime.h"
namespace platform::ui::lora
{
static bool acquired = false;
bool acquire()
{
    acquired = true;
    return true;
}
bool is_online() { return acquired; }
bool configure_receive(float, const ReceiveConfig&) { return acquired; }
bool transmit_packet(const uint8_t*, std::size_t) { return false; }
// A quiet simulated receiver must never invent OBSERVED/CONFIRMED evidence.
bool poll_received_packet(uint8_t*, std::size_t, ReceivedPacket*) { return false; }
float read_instant_rssi() { return -120.f; }
void release() { acquired = false; }
} // namespace platform::ui::lora
