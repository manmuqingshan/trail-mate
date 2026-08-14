#include "chat/infra/voice/vmp_control_ingress.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace
{

using namespace chat::voice::vmp;

class RecordingSink final : public IControlEnvelopeSink
{
  public:
    bool enqueueControl(const uint8_t* data,
                        std::size_t size,
                        const ControlRxMetadata& metadata) override
    {
        ++calls;
        last_size = size;
        last_metadata = metadata;
        std::memcpy(last_bytes.data(), data, size);
        return accepts;
    }

    bool accepts = true;
    std::size_t calls = 0;
    std::size_t last_size = 0;
    ControlRxMetadata last_metadata = {};
    std::array<uint8_t, kControlFrameSize> last_bytes = {};
};

void test_non_vmp_leaves_mesh_path_unchanged()
{
    ControlIngress ingress{};
    std::array<uint8_t, kControlFrameSize> packet{};
    packet[0] = static_cast<uint8_t>('M');
    packet[1] = static_cast<uint8_t>('T');
    packet[2] = kVersion;
    assert(!ingress.tryConsume(packet.data(), packet.size(), {}));
    assert(!ingress.tryConsume(packet.data(), packet.size() - 1U, {}));
}

void test_vmp_is_consumed_even_when_bounded_sink_is_full()
{
    ControlIngress ingress{};
    RecordingSink sink{};
    ingress.setSink(&sink);

    std::array<uint8_t, kControlFrameSize> packet{};
    packet[0] = static_cast<uint8_t>('V');
    packet[1] = static_cast<uint8_t>('M');
    packet[2] = kVersion;
    packet[3] = static_cast<uint8_t>(ControlType::Offer);
    const ControlRxMetadata metadata{-73.25f, 5.5f};
    assert(ingress.tryConsume(packet.data(), packet.size(), metadata));
    assert(sink.calls == 1U);
    assert(sink.last_size == packet.size());
    assert(std::memcmp(sink.last_bytes.data(), packet.data(), packet.size()) == 0);
    assert(sink.last_metadata.rssi == metadata.rssi);
    assert(sink.last_metadata.snr == metadata.snr);

    sink.accepts = false;
    assert(ingress.tryConsume(packet.data(), packet.size(), metadata));
    assert(sink.calls == 2U);
}

} // namespace

int main()
{
    test_non_vmp_leaves_mesh_path_unchanged();
    test_vmp_is_consumed_even_when_bounded_sink_is_full();
    return 0;
}
