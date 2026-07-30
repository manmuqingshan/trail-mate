#include "chat/delivery/chat_delivery_event_port.h"
#include "chat/domain/chat_model.h"
#include "chat/infra/store/ram_store.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/usecase/chat_service.h"
#include "ui_chat_runtime/chat_delivery_event_projection_adapter.h"

#include <cassert>
#include <string>

namespace
{
::chat::delivery::ChatDeliveryRef refFor(::chat::MessageId id,
                                         ::chat::MeshProtocol protocol =
                                             ::chat::MeshProtocol::Meshtastic)
{
    ::chat::delivery::ChatDeliveryRef ref{};
    ref.protocol_id = id;
    ref.protocol = static_cast<uint8_t>(protocol);
    return ref;
}

class FakeMeshAdapter final : public ::chat::IMeshAdapter
{
  public:
    bool sendText(::chat::ChannelId,
                  const std::string&,
                  ::chat::MessageId* out_msg_id,
                  ::chat::NodeId = 0) override
    {
        if (out_msg_id != nullptr)
        {
            *out_msg_id = next_id++;
        }
        return send_ok;
    }

    bool pollIncomingText(::chat::MeshIncomingText*) override { return false; }
    bool sendAppData(::chat::ChannelId,
                     uint32_t,
                     const uint8_t*,
                     size_t,
                     ::chat::NodeId = 0,
                     bool = false,
                     ::chat::MessageId = 0,
                     bool = false) override
    {
        return false;
    }
    bool pollIncomingData(::chat::MeshIncomingData*) override { return false; }
    void applyConfig(const ::chat::MeshConfig&) override {}
    bool isReady() const override { return true; }
    bool pollIncomingRawPacket(uint8_t*, size_t&, size_t) override { return false; }

    bool send_ok = true;
    ::chat::MessageId next_id = 700;
};

} // namespace

int main()
{
    ::chat::ChatModel model;
    FakeMeshAdapter adapter;
    ::chat::RamStore store;
    ::chat::ChatService service(model, adapter, store);

    ::chat::delivery::ChatDeliveryReadModel read_model;
    ::chat::delivery::ChatDeliveryEventProjector projector(read_model);
    ::chat::delivery::ProjectingChatDeliveryEventPort event_port(projector);
    ::ui_chat_runtime::ChatDeliveryEventProjectionAdapter projection_adapter(
        service,
        event_port);

    const auto sent_id =
        service.sendText(::chat::ChannelId::PRIMARY, "queued", 0);
    assert(sent_id == 700);

    projection_adapter.onChatSendResult(
        sent_id, ::chat::MessageStatus::Queued, 1200);

    ::chat::delivery::ChatDeliveryRecord record{};
    assert(read_model.find(refFor(sent_id), record));
    assert(record.state == ::chat::delivery::DeliveryState::Queued);
    assert(record.failure == ::chat::delivery::DeliveryFailureKind::None);
    assert(record.updated_at_ms == 1200);

    service.handleSendResult(sent_id, true);
    projection_adapter.onChatSendResult(
        sent_id, ::chat::MessageStatus::Sent, 1234);

    assert(read_model.find(refFor(sent_id), record));
    assert(record.state == ::chat::delivery::DeliveryState::Sent);
    assert(record.failure == ::chat::delivery::DeliveryFailureKind::None);
    assert(record.updated_at_ms == 1234);
    service.handleSendResult(sent_id, ::chat::MessageStatus::Delivered);
    projection_adapter.onChatSendResult(
        sent_id, ::chat::MessageStatus::Delivered, 1250);
    assert(read_model.find(refFor(sent_id), record));
    assert(record.state == ::chat::delivery::DeliveryState::Delivered);
    assert(record.failure == ::chat::delivery::DeliveryFailureKind::None);
    assert(record.updated_at_ms == 1250);
    projection_adapter.onChatSendResult(
        sent_id, ::chat::MessageStatus::Failed, 1300);
    assert(read_model.find(refFor(sent_id), record));
    assert(record.state == ::chat::delivery::DeliveryState::Delivered);
    assert(record.failure == ::chat::delivery::DeliveryFailureKind::None);
    assert(record.updated_at_ms == 1250);

    const auto failed_id =
        service.sendText(::chat::ChannelId::PRIMARY, "fail", 0);
    assert(failed_id == 701);

    service.handleSendResult(failed_id, false);
    projection_adapter.onChatSendResult(
        failed_id, ::chat::MessageStatus::Failed, 2345);

    assert(read_model.find(refFor(failed_id), record));
    assert(record.state == ::chat::delivery::DeliveryState::Failed);
    assert(record.failure == ::chat::delivery::DeliveryFailureKind::Unknown);
    assert(record.updated_at_ms == 2345);

    const auto ack_failed_id =
        service.sendText(::chat::ChannelId::PRIMARY, "ackfail", 0);
    assert(ack_failed_id == 702);
    service.handleSendResult(ack_failed_id,
                             ::chat::MessageStatus::Failed,
                             0,
                             ::chat::delivery::SendFailureKind::AckTimeout);
    projection_adapter.onChatSendResult(
        ack_failed_id,
        ::chat::MessageStatus::Failed,
        2400,
        ::chat::delivery::SendFailureKind::AckTimeout,
        true,
        ::chat::MeshProtocol::Meshtastic);
    assert(read_model.find(refFor(ack_failed_id), record));
    assert(record.state == ::chat::delivery::DeliveryState::Failed);
    assert(record.failure == ::chat::delivery::DeliveryFailureKind::AckTimeout);

    const auto timeout_id =
        service.sendText(::chat::ChannelId::PRIMARY, "timeout", 0);
    assert(timeout_id == 703);
    projection_adapter.onAckTimeout(timeout_id, 3456);
    assert(read_model.find(refFor(timeout_id), record));
    assert(record.state == ::chat::delivery::DeliveryState::Failed);
    assert(record.failure == ::chat::delivery::DeliveryFailureKind::AckTimeout);
    assert(record.updated_at_ms == 3456);

    projection_adapter.onAckTimeout(0, 4567);
    assert(read_model.size() == 4);

    adapter.next_id = 800;
    service.setActiveProtocol(::chat::MeshProtocol::Meshtastic);
    const auto mt_collision_id =
        service.sendText(::chat::ChannelId::PRIMARY, "mt collision", 0);
    assert(mt_collision_id == 800);

    adapter.next_id = 800;
    service.setActiveProtocol(::chat::MeshProtocol::MeshCore);
    const auto mc_collision_id =
        service.sendText(::chat::ChannelId::PRIMARY, "mc collision", 0);
    assert(mc_collision_id == 800);

    projection_adapter.onChatSendResult(
        mc_collision_id,
        ::chat::MessageStatus::Delivered,
        5100,
        ::chat::delivery::SendFailureKind::None,
        true,
        ::chat::MeshProtocol::MeshCore);
    projection_adapter.onChatSendResult(
        mt_collision_id,
        ::chat::MessageStatus::Failed,
        5200,
        ::chat::delivery::SendFailureKind::AckTimeout,
        true,
        ::chat::MeshProtocol::Meshtastic);

    assert(read_model.find(
        refFor(800, ::chat::MeshProtocol::MeshCore),
        record));
    assert(record.state == ::chat::delivery::DeliveryState::Delivered);
    assert(record.failure == ::chat::delivery::DeliveryFailureKind::None);
    assert(read_model.find(
        refFor(800, ::chat::MeshProtocol::Meshtastic),
        record));
    assert(record.state == ::chat::delivery::DeliveryState::Failed);
    assert(record.failure == ::chat::delivery::DeliveryFailureKind::AckTimeout);

    projection_adapter.onChatSendResult(
        9999, ::chat::MessageStatus::Failed, 0);
    assert(read_model.size() == 6);

    return 0;
}
