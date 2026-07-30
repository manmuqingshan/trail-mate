#include "chat/usecase/auto_reply_policy.h"

#include <cassert>

int main()
{
    chat::ChatMessage private_message{};
    private_message.peer = 0x11223344UL;

    chat::auto_reply::Context ready{};
    ready.enabled = true;
    ready.has_reply_text = true;
    ready.screen_sleeping = true;
    ready.can_send = true;

    assert(chat::auto_reply::decide(private_message, ready) ==
           chat::auto_reply::Decision::Reply);

    auto disabled = ready;
    disabled.enabled = false;
    assert(chat::auto_reply::decide(private_message, disabled) ==
           chat::auto_reply::Decision::Disabled);

    auto empty_text = ready;
    empty_text.has_reply_text = false;
    assert(chat::auto_reply::decide(private_message, empty_text) ==
           chat::auto_reply::Decision::EmptyText);

    auto awake = ready;
    awake.screen_sleeping = false;
    assert(chat::auto_reply::decide(private_message, awake) ==
           chat::auto_reply::Decision::ScreenAwake);

    chat::ChatMessage broadcast{};
    assert(chat::auto_reply::decide(broadcast, ready) ==
           chat::auto_reply::Decision::NotPrivate);

    auto unverified_message = private_message;
    unverified_message.source_unverified = true;
    assert(chat::auto_reply::decide(unverified_message, ready) ==
           chat::auto_reply::Decision::Reply);

    auto self_message = ready;
    self_message.source_is_local = true;
    assert(chat::auto_reply::decide(private_message, self_message) ==
           chat::auto_reply::Decision::SelfMessage);

    auto cannot_send = ready;
    cannot_send.can_send = false;
    assert(chat::auto_reply::decide(private_message, cannot_send) ==
           chat::auto_reply::Decision::CannotSend);

    return 0;
}
