#include "ui/chat_voice_runtime.h"

#include <cassert>
#include <cstddef>
#include <cstdint>

namespace
{

class FakeVoiceRuntime final : public ui::chat_voice::IVoiceMessageRuntime
{
  public:
    bool isAvailable() const override
    {
        return available;
    }

    bool canRecordAndSend() const override
    {
        return send_available;
    }

    ui::chat_voice::StartResult requestRecordAndSend(
        const ui::chat_voice::SendRequest& request) override
    {
        last_target = request.target_id;
        last_protocol = request.presentation_protocol;
        last_channel = request.presentation_channel;
        ++request_count;
        return result;
    }

    bool requestStopRecording() override
    {
        ++stop_request_count;
        return stop_result;
    }

    bool isOutboundActive() const override
    {
        return outbound_active;
    }

    std::size_t listMessages(ui::chat_voice::MessageSummary* out_messages,
                             std::size_t capacity) const override
    {
        if (!out_messages || capacity == 0U)
        {
            return 0U;
        }
        out_messages[0] = summary;
        return 1U;
    }

    bool markConversationRead(uint8_t protocol,
                              uint8_t channel,
                              uint32_t peer_id,
                              bool broadcast) override
    {
        read_protocol = protocol;
        read_channel = channel;
        read_peer = peer_id;
        read_broadcast = broadcast;
        ++read_request_count;
        return read_result;
    }

    bool requestPlayback(uint64_t local_id) override
    {
        played_id = local_id;
        return playback_result;
    }

    bool available = true;
    bool send_available = true;
    uint32_t last_target = 0U;
    uint32_t request_count = 0U;
    uint8_t last_protocol = 0U;
    uint8_t last_channel = 0U;
    uint8_t read_protocol = 0U;
    uint8_t read_channel = 0U;
    uint32_t read_peer = 0U;
    bool read_broadcast = false;
    uint32_t read_request_count = 0U;
    bool read_result = true;
    mutable ui::chat_voice::MessageSummary summary{
        0xF00DU,
        9U,
        0U,
        123U,
        2000U,
        false,
        true,
        true,
        ui::chat_voice::DeliveryState::Sending,
        1U,
        0U,
        false};
    uint64_t played_id = 0U;
    bool playback_result = true;
    uint32_t stop_request_count = 0U;
    bool stop_result = true;
    bool outbound_active = false;
    ui::chat_voice::StartResult result = ui::chat_voice::StartResult::Queued;
};

void test_unbound_runtime_is_safe()
{
    ui::chat_voice::setRuntime(nullptr);
    assert(!ui::chat_voice::isRuntimeBound());
    assert(!ui::chat_voice::isAvailable());
    assert(!ui::chat_voice::canRecordAndSend());
    assert(ui::chat_voice::requestRecordAndSend({1U, 1U, 0U}) ==
           ui::chat_voice::StartResult::Unsupported);
}

void test_runtime_forwards_without_transport_coupling()
{
    FakeVoiceRuntime runtime{};
    ui::chat_voice::setRuntime(&runtime);

    assert(ui::chat_voice::isRuntimeBound());
    assert(ui::chat_voice::isAvailable());
    assert(ui::chat_voice::canRecordAndSend());
    assert(ui::chat_voice::requestRecordAndSend({0x11223344U, 1U, 1U}) ==
           ui::chat_voice::StartResult::Queued);
    assert(runtime.request_count == 1U);
    assert(runtime.last_target == 0x11223344U);
    assert(runtime.last_protocol == 1U);
    assert(runtime.last_channel == 1U);
    assert(ui::chat_voice::requestStopRecording());
    assert(runtime.stop_request_count == 1U);
    runtime.outbound_active = true;
    assert(ui::chat_voice::isOutboundActive());

    runtime.available = false;
    runtime.send_available = false;
    runtime.result = ui::chat_voice::StartResult::PrivateContactUnverified;
    // Runtime binding is stable across transient storage/carrier readiness.
    // Compose uses this state to keep the compact Voice control visible.
    assert(ui::chat_voice::isRuntimeBound());
    assert(!ui::chat_voice::isAvailable());
    assert(!ui::chat_voice::canRecordAndSend());
    assert(ui::chat_voice::requestRecordAndSend({0x55667788U, 4U, 0U}) ==
           ui::chat_voice::StartResult::PrivateContactUnverified);
    assert(runtime.request_count == 2U);
    assert(runtime.last_target == 0x55667788U);
    assert(runtime.last_protocol == 4U);

    ui::chat_voice::MessageSummary summaries[1] = {};
    assert(ui::chat_voice::listMessages(summaries, 1U) == 1U);
    assert(summaries[0].local_id == runtime.summary.local_id);
    assert(summaries[0].source_unverified);
    assert(summaries[0].outgoing);
    assert(summaries[0].delivery == ui::chat_voice::DeliveryState::Sending);
    assert(!summaries[0].read);
    assert(ui::chat_voice::markConversationRead(1U, 0U, 9U, false));
    assert(runtime.read_request_count == 1U);
    assert(runtime.read_protocol == 1U);
    assert(runtime.read_channel == 0U);
    assert(runtime.read_peer == 9U);
    assert(!runtime.read_broadcast);
    // The legacy name is intentionally only a source-compatible alias.  It
    // must expose the exact same mixed-direction timeline rather than
    // restoring the old receive-only meaning.
    assert(ui::chat_voice::listReceivedMessages(summaries, 1U) == 1U);
    assert(summaries[0].outgoing);
    assert(ui::chat_voice::requestPlayback(summaries[0].local_id));
    assert(runtime.played_id == summaries[0].local_id);

    ui::chat_voice::setRuntime(nullptr);
    assert(!ui::chat_voice::isRuntimeBound());
}

} // namespace

int main()
{
    test_unbound_runtime_is_safe();
    test_runtime_forwards_without_transport_coupling();
    return 0;
}
