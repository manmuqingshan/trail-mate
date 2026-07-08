#include "platform/esp/arduino_common/chat/infra/chat_event_bus_bridge.h"

#include "sys/event_bus.h"

#include <cstdio>

namespace chat::infra
{

ChatEventBusBridge::ChatEventBusBridge(chat::ChatService& service) : service_(service)
{
    service_.addIncomingMessageObserver(this);
}

ChatEventBusBridge::~ChatEventBusBridge()
{
    service_.removeIncomingMessageObserver(this);
}

void ChatEventBusBridge::onIncomingMessage(const chat::ChatMessage& msg, const chat::RxMeta* rx_meta)
{
    const bool published =
        sys::EventBus::publish(new sys::ChatNewMessageEvent(static_cast<uint8_t>(msg.channel),
                                                            msg.msg_id,
                                                            msg.text.c_str(),
                                                            rx_meta));
    std::printf("[APP][chat_event] publish ChatNewMessage msg=%lu ch=%u from=%08lX peer=%08lX len=%u origin=%u ok=%u pending=%u\n",
                static_cast<unsigned long>(msg.msg_id),
                static_cast<unsigned>(msg.channel),
                static_cast<unsigned long>(msg.from),
                static_cast<unsigned long>(msg.peer),
                static_cast<unsigned>(msg.text.size()),
                rx_meta ? static_cast<unsigned>(rx_meta->origin) : 0U,
                published ? 1U : 0U,
                static_cast<unsigned>(sys::EventBus::pendingCount()));
}

} // namespace chat::infra
