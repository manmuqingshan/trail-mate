#pragma once

#include "chat/domain/chat_types.h"
#include "chat/runtime/self_identity_provider.h"
#include "chat/usecase/contact_service.h"

#include <memory>

namespace chat
{
class IMeshAdapter;
}

namespace platform::nrf52::protocol
{

std::unique_ptr<chat::IMeshAdapter> createProtocolAdapter(chat::MeshProtocol protocol,
                                                          const chat::runtime::SelfIdentityProvider* identity_provider,
                                                          chat::contacts::ContactService* contact_service = nullptr);

} // namespace platform::nrf52::protocol
