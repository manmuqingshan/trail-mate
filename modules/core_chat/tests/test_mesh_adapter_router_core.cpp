#include "chat/infra/mesh_adapter_router_core.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace
{

class FakeMeshAdapter final : public chat::IMeshAdapter
{
  public:
    explicit FakeMeshAdapter(chat::NodeId node_id) : node_id_(node_id) {}

    bool sendText(chat::ChannelId channel, const std::string& text,
                  chat::MessageId* out_msg_id, chat::NodeId peer = 0) override
    {
        ++send_count;
        last_channel = channel;
        last_peer = peer;
        last_text = text;
        if (out_msg_id)
        {
            *out_msg_id = next_message_id;
        }
        return send_ok;
    }

    bool pollIncomingText(chat::MeshIncomingText*) override
    {
        return false;
    }

    chat::MeshSendResult sendTextToReticulumDestination(
        chat::ChannelId channel,
        const std::string& text,
        chat::MessageId forced_msg_id,
        const chat::ReticulumPeerIdentity& destination) override
    {
        ++destination_send_count;
        last_channel = channel;
        last_text = text;
        last_forced_id = forced_msg_id;
        last_destination = destination;
        chat::MeshSendResult result =
            send_ok ? chat::MeshSendResult::success(forced_msg_id != 0 ? forced_msg_id
                                                                       : next_message_id)
                    : chat::MeshSendResult::fail(chat::MeshOperationFailure::RadioTxFailed,
                                                 forced_msg_id != 0 ? forced_msg_id
                                                                    : next_message_id);
        result.reticulum_identity = destination;
        return result;
    }

    bool sendAppData(chat::ChannelId, uint32_t, const uint8_t*, size_t,
                     chat::NodeId = 0, bool = false, chat::MessageId = 0,
                     bool = false) override
    {
        return false;
    }

    bool pollIncomingData(chat::MeshIncomingData*) override
    {
        return false;
    }

    bool requestNodeInfo(chat::NodeId dest, bool want_response) override
    {
        ++node_info_request_count;
        last_node_info_dest = dest;
        last_node_info_want_response = want_response;
        return node_info_request_ok;
    }

    bool broadcastSelfIdentity() override
    {
        ++self_identity_broadcast_count;
        return self_identity_broadcast_ok;
    }

    chat::MeshActionResult triggerDiscoveryActionDetailed(
        chat::MeshDiscoveryAction action) override
    {
        ++discovery_count;
        last_discovery = action;
        return discovery_result;
    }

    void applyConfig(const chat::MeshConfig&) override {}

    bool isReady() const override
    {
        return ready;
    }

    bool pollIncomingRawPacket(uint8_t*, size_t& out_len, size_t) override
    {
        out_len = 0;
        return false;
    }

    chat::NodeId getNodeId() const override
    {
        return node_id_;
    }

    chat::NodeId node_id_ = 0;
    bool ready = true;
    bool send_ok = true;
    int send_count = 0;
    int destination_send_count = 0;
    int discovery_count = 0;
    int node_info_request_count = 0;
    int self_identity_broadcast_count = 0;
    bool node_info_request_ok = true;
    bool self_identity_broadcast_ok = true;
    chat::MessageId next_message_id = 42;
    chat::ChannelId last_channel = chat::ChannelId::PRIMARY;
    chat::NodeId last_peer = 0;
    chat::NodeId last_node_info_dest = 0;
    chat::MessageId last_forced_id = 0;
    bool last_node_info_want_response = false;
    std::string last_text;
    chat::ReticulumPeerIdentity last_destination{};
    chat::MeshDiscoveryAction last_discovery = chat::MeshDiscoveryAction::ScanLocal;
    chat::MeshActionResult discovery_result = chat::MeshActionResult::success();
};

chat::ReticulumPeerIdentity makeReticulumDestination(std::uint8_t base)
{
    std::uint8_t destination_hash[chat::kReticulumPeerHashSize] = {};
    for (std::size_t index = 0; index < chat::kReticulumPeerHashSize; ++index)
    {
        destination_hash[index] = static_cast<std::uint8_t>(base + index);
    }
    return chat::makeReticulumDestinationIdentity(destination_hash);
}

} // namespace

int main()
{
    chat::MeshAdapterRouterCore router;

    auto meshcore_backend = std::unique_ptr<FakeMeshAdapter>(
        new FakeMeshAdapter(0x4D430001UL));
    FakeMeshAdapter* meshcore = meshcore_backend.get();
    meshcore->discovery_result =
        chat::MeshActionResult::fail(chat::MeshOperationFailure::RadioOffline);

    assert(router.installBackend(chat::MeshProtocol::MeshCore,
                                 std::move(meshcore_backend)));
    assert(router.backendProtocol() == chat::MeshProtocol::MeshCore);
    assert(router.hasBackend());
    assert(router.getNodeId() == 0x4D430001UL);

    chat::MeshActionResult discovery =
        router.triggerDiscoveryActionDetailed(chat::MeshDiscoveryAction::ScanLocal);
    assert(!discovery.ok);
    assert(discovery.failure == chat::MeshOperationFailure::RadioOffline);
    assert(meshcore->discovery_count == 1);
    assert(meshcore->last_discovery == chat::MeshDiscoveryAction::ScanLocal);
    assert(router.broadcastSelfIdentity());
    assert(meshcore->self_identity_broadcast_count == 1);
    assert(meshcore->node_info_request_count == 0);

    auto meshtastic_backend = std::unique_ptr<FakeMeshAdapter>(
        new FakeMeshAdapter(0x11112222UL));
    FakeMeshAdapter* meshtastic = meshtastic_backend.get();

    assert(router.installBackend(chat::MeshProtocol::Meshtastic,
                                 std::move(meshtastic_backend)));
    assert(router.backendProtocol() == chat::MeshProtocol::Meshtastic);
    assert(router.getNodeId() == 0x11112222UL);

    chat::MeshSendResult sent =
        router.sendTextDetailed(chat::ChannelId::PRIMARY, "hello", 0, 0x44);
    assert(sent.ok);
    assert(sent.msg_id == 42);
    assert(meshtastic->send_count == 1);
    assert(meshtastic->last_text == "hello");
    assert(meshtastic->last_peer == 0x44);

    const chat::ReticulumPeerIdentity group_destination =
        makeReticulumDestination(0x80);
    sent = router.sendTextToReticulumDestination(chat::ChannelId::PRIMARY,
                                                 "group",
                                                 0x1234,
                                                 group_destination);
    assert(sent.ok);
    assert(sent.msg_id == 0x1234);
    assert(meshtastic->destination_send_count == 1);
    assert(meshtastic->last_forced_id == 0x1234);
    assert(chat::sameReticulumDestinationHash(meshtastic->last_destination,
                                              group_destination));

    router.setActiveProtocol(chat::MeshProtocol::MeshCore);
    assert(router.backendProtocol() == chat::MeshProtocol::MeshCore);
    discovery =
        router.triggerDiscoveryActionDetailed(chat::MeshDiscoveryAction::SendIdBroadcast);
    assert(!discovery.ok);
    assert(discovery.failure == chat::MeshOperationFailure::RadioOffline);
    assert(meshcore->discovery_count == 2);
    assert(meshcore->last_discovery == chat::MeshDiscoveryAction::SendIdBroadcast);

    return 0;
}
