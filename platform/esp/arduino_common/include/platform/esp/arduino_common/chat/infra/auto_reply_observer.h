#pragma once

#include "chat/usecase/chat_service.h"

#include <memory>

namespace chat::infra
{

std::unique_ptr<chat::ChatService::IncomingMessageObserver>
create_auto_reply_observer(chat::ChatService& service);

} // namespace chat::infra
