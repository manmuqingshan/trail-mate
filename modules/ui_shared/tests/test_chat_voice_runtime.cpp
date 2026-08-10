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

    ui::chat_voice::StartResult requestRecordAndSend(uint32_t target_id) override
    {
        last_target = target_id;
        ++request_count;
        return result;
    }

    std::size_t listReceivedMessages(ui::chat_voice::MessageSummary* out_messages,
                                     std::size_t capacity) const override
    {
        if (!out_messages || capacity == 0U)
        {
            return 0U;
        }
        out_messages[0] = summary;
        return 1U;
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
    mutable ui::chat_voice::MessageSummary summary{0xF00DU, 9U, 0U, 123U, false, true};
    uint64_t played_id = 0U;
    bool playback_result = true;
    ui::chat_voice::StartResult result = ui::chat_voice::StartResult::Queued;
};

void test_unbound_runtime_is_safe()
{
    ui::chat_voice::setRuntime(nullptr);
    assert(!ui::chat_voice::isAvailable());
    assert(!ui::chat_voice::canRecordAndSend());
    assert(ui::chat_voice::requestRecordAndSend(1U) ==
           ui::chat_voice::StartResult::Unsupported);
}

void test_runtime_forwards_without_transport_coupling()
{
    FakeVoiceRuntime runtime{};
    ui::chat_voice::setRuntime(&runtime);

    assert(ui::chat_voice::isAvailable());
    assert(ui::chat_voice::canRecordAndSend());
    assert(ui::chat_voice::requestRecordAndSend(0x11223344U) ==
           ui::chat_voice::StartResult::Queued);
    assert(runtime.request_count == 1U);
    assert(runtime.last_target == 0x11223344U);

    runtime.available = false;
    runtime.send_available = false;
    runtime.result = ui::chat_voice::StartResult::PrivateContactUnverified;
    assert(!ui::chat_voice::isAvailable());
    assert(!ui::chat_voice::canRecordAndSend());
    assert(ui::chat_voice::requestRecordAndSend(0x55667788U) ==
           ui::chat_voice::StartResult::PrivateContactUnverified);
    assert(runtime.request_count == 2U);
    assert(runtime.last_target == 0x55667788U);

    ui::chat_voice::MessageSummary summaries[1] = {};
    assert(ui::chat_voice::listReceivedMessages(summaries, 1U) == 1U);
    assert(summaries[0].local_id == runtime.summary.local_id);
    assert(summaries[0].source_unverified);
    assert(ui::chat_voice::requestPlayback(summaries[0].local_id));
    assert(runtime.played_id == summaries[0].local_id);

    ui::chat_voice::setRuntime(nullptr);
}

} // namespace

int main()
{
    test_unbound_runtime_is_safe();
    test_runtime_forwards_without_transport_coupling();
    return 0;
}
