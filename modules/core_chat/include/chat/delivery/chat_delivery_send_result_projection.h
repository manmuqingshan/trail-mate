#pragma once

#include "chat/delivery/chat_delivery_event_port.h"

#include <cstdint>

namespace chat::delivery
{

ChatDeliveryEvent makeChatSendResultDeliveryEvent(ChatDeliveryRef ref,
                                                  bool success,
                                                  SendFailureKind failure,
                                                  uint32_t timestamp_ms = 0);
ChatDeliveryEvent makeAckTimeoutDeliveryEvent(ChatDeliveryRef ref,
                                              uint32_t timestamp_ms = 0);

} // namespace chat::delivery
