#pragma once

#include "chat/domain/chat_types.h"

namespace chat::auto_reply
{

enum class Decision : uint8_t
{
    Reply,
    Disabled,
    EmptyText,
    ScreenAwake,
    NotPrivate,
    SelfMessage,
    CannotSend,
};

struct Context
{
    bool enabled = false;
    bool has_reply_text = false;
    bool screen_sleeping = false;
    bool source_is_local = false;
    bool can_send = false;
};

inline Decision decide(const ChatMessage& message, const Context& context)
{
    if (!context.enabled)
    {
        return Decision::Disabled;
    }
    if (!context.has_reply_text)
    {
        return Decision::EmptyText;
    }
    if (!context.screen_sleeping)
    {
        return Decision::ScreenAwake;
    }
    if (message.peer == 0)
    {
        return Decision::NotPrivate;
    }
    if (context.source_is_local)
    {
        return Decision::SelfMessage;
    }
    if (!context.can_send)
    {
        return Decision::CannotSend;
    }
    return Decision::Reply;
}

} // namespace chat::auto_reply
