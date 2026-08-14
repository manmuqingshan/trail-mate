#include "sys/event_bus.h"

#include <cassert>
#include <cstdint>

namespace
{
struct LegacyChatNewMessageEvent : public sys::Event
{
    uint8_t channel;
    uint32_t msg_id;
    char text[64];
    chat::RxMeta rx_meta;
};
} // namespace

int main()
{
    static_assert(sizeof(sys::ChatMessageContentKind) == sizeof(uint8_t));
    static_assert(sizeof(sys::ChatNewMessageEvent) == sizeof(LegacyChatNewMessageEvent));

    sys::ChatNewMessageEvent text_event(4U, 42U, "text");
    assert(text_event.type == sys::EventType::ChatNewMessage);
    assert(text_event.content_kind == sys::ChatMessageContentKind::Text);
    assert(text_event.msg_id == 42U);

    sys::ChatVoiceMessageEvent voice_event(7U, 0x1020304050607080ULL);
    assert(voice_event.type == sys::EventType::ChatNewMessage);
    assert(voice_event.content_kind == sys::ChatMessageContentKind::Voice);
    assert(voice_event.channel == 7U);
    assert(voice_event.msg_id == 0U);
    assert(voice_event.local_id == 0x1020304050607080ULL);

    return 0;
}
