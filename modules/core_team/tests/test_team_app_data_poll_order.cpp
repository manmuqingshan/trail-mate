#include "chat/domain/chat_model.h"
#include "chat/ports/i_chat_store.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/usecase/chat_service.h"
#include "team/ports/i_team_crypto.h"
#include "team/ports/i_team_event_sink.h"
#include "team/ports/i_team_runtime.h"
#include "team/protocol/team_chat.h"
#include "team/protocol/team_portnum.h"
#include "team/protocol/team_wire.h"
#include "team/usecase/team_service.h"

#include <cassert>
#include <deque>
#include <vector>

namespace
{

class FakeMeshAdapter final : public chat::IMeshAdapter
{
  public:
    chat::MeshCapabilities getCapabilities() const override
    {
        chat::MeshCapabilities caps{};
        caps.supports_broadcast_appdata = true;
        caps.provides_appdata_sender = true;
        return caps;
    }

    bool sendText(chat::ChannelId,
                  const std::string&,
                  chat::MessageId* out_msg_id,
                  chat::NodeId = 0) override
    {
        if (out_msg_id)
        {
            *out_msg_id = 1;
        }
        return true;
    }

    bool pollIncomingText(chat::MeshIncomingText*) override { return false; }

    bool sendAppData(chat::ChannelId,
                     uint32_t,
                     const uint8_t*,
                     size_t,
                     chat::NodeId = 0,
                     bool = false,
                     chat::MessageId = 0,
                     bool = false) override
    {
        return true;
    }

    bool pollIncomingData(chat::MeshIncomingData* out) override
    {
        if (incoming.empty())
        {
            return false;
        }
        if (out)
        {
            *out = incoming.front();
        }
        incoming.pop_front();
        return true;
    }

    void applyConfig(const chat::MeshConfig&) override {}
    bool isReady() const override { return true; }
    bool pollIncomingRawPacket(uint8_t*, size_t& out_len, size_t) override
    {
        out_len = 0;
        return false;
    }
    chat::NodeId getNodeId() const override { return 0x0C16AAEC; }

    std::deque<chat::MeshIncomingData> incoming;
};

class FakeChatStore final : public chat::IChatStore
{
  public:
    void append(const chat::ChatMessage&) override {}
    std::vector<chat::ChatMessage> loadRecent(const chat::ConversationId&, size_t) override
    {
        return {};
    }
    std::vector<chat::ConversationMeta> loadConversationPage(size_t,
                                                             size_t,
                                                             size_t* total) override
    {
        if (total)
        {
            *total = 0;
        }
        return {};
    }
    bool setUnread(const chat::ConversationId&, int) override { return true; }
    int getUnread(const chat::ConversationId&) const override { return 0; }
    void clearConversation(const chat::ConversationId&) override {}
    void clearAll() override {}
    bool updateMessageStatus(chat::MessageId, chat::MessageStatus) override
    {
        return false;
    }
    bool getMessage(chat::MessageId, chat::ChatMessage*) const override
    {
        return false;
    }
};

class FakeCrypto final : public team::ITeamCrypto
{
  public:
    bool deriveKey(const uint8_t* key,
                   size_t key_len,
                   const char*,
                   uint8_t* out,
                   size_t out_len) override
    {
        if (!key || key_len == 0 || !out)
        {
            return false;
        }
        for (size_t i = 0; i < out_len; ++i)
        {
            out[i] = static_cast<uint8_t>(key[i % key_len] ^ 0x5A);
        }
        return true;
    }

    bool aeadEncrypt(const uint8_t*,
                     size_t,
                     const uint8_t*,
                     size_t,
                     const uint8_t*,
                     size_t,
                     const uint8_t* plain,
                     size_t plain_len,
                     std::vector<uint8_t>& out_cipher) override
    {
        out_cipher.assign(plain, plain + plain_len);
        return true;
    }

    bool aeadDecrypt(const uint8_t*,
                     size_t,
                     const uint8_t*,
                     size_t,
                     const uint8_t*,
                     size_t,
                     const uint8_t* cipher,
                     size_t cipher_len,
                     std::vector<uint8_t>& out_plain) override
    {
        out_plain.assign(cipher, cipher + cipher_len);
        return true;
    }
};

class FakeRuntime final : public team::ITeamRuntime
{
  public:
    uint32_t nowMillis() override { return 1234; }
    uint32_t nowUnixSeconds() override { return 5678; }
    void fillRandomBytes(uint8_t* out, size_t len) override
    {
        for (size_t i = 0; i < len; ++i)
        {
            out[i] = static_cast<uint8_t>(0xA0 + i);
        }
    }
};

class FakeTeamSink final : public team::ITeamEventSink
{
  public:
    void onTeamKick(const team::TeamKickEvent&) override {}
    void onTeamTransferLeader(const team::TeamTransferLeaderEvent&) override {}
    void onTeamKeyDist(const team::TeamKeyDistEvent&) override {}
    void onTeamKeyRequest(const team::TeamKeyRequestEvent&) override {}
    void onTeamStatus(const team::TeamStatusEvent&) override {}
    void onTeamPosition(const team::TeamPositionEvent&) override {}
    void onTeamWaypoint(const team::TeamWaypointEvent&) override {}
    void onTeamTrack(const team::TeamTrackEvent&) override {}
    void onTeamChat(const team::TeamChatEvent& event) override
    {
        chat_count += 1;
        last_from = event.ctx.from;
        last_payload = event.msg.payload;
    }
    void onTeamError(const team::TeamErrorEvent&) override
    {
        error_count += 1;
    }

    int chat_count = 0;
    int error_count = 0;
    chat::NodeId last_from = 0;
    std::vector<uint8_t> last_payload;
};

team::TeamId testTeamId()
{
    team::TeamId id{};
    id[0] = 0x42;
    id[1] = 0x24;
    return id;
}

team::TeamKeys testKeys()
{
    team::TeamKeys keys{};
    keys.team_id = testTeamId();
    keys.key_id = 7;
    keys.valid = true;
    for (size_t i = 0; i < team::kTeamKeySize; ++i)
    {
        keys.chat_key[i] = static_cast<uint8_t>(0x30 + i);
        keys.mgmt_key[i] = static_cast<uint8_t>(0x40 + i);
        keys.pos_key[i] = static_cast<uint8_t>(0x50 + i);
        keys.wp_key[i] = static_cast<uint8_t>(0x60 + i);
    }
    return keys;
}

std::vector<uint8_t> makeTeamChatWire()
{
    team::proto::TeamChatMessage msg{};
    msg.header.version = team::proto::kTeamChatVersion;
    msg.header.type = team::proto::TeamChatType::Text;
    msg.header.from = 0x4ECF80D8;
    msg.payload = {'p', 'i', 'n', 'g'};

    std::vector<uint8_t> plain;
    assert(team::proto::encodeTeamChatMessage(msg, plain));

    team::proto::TeamEncrypted envelope{};
    envelope.team_id = testTeamId();
    envelope.key_id = 7;
    envelope.sender_id = 0x4ECF80D8;
    envelope.ciphertext = plain;

    std::vector<uint8_t> wire;
    assert(team::proto::encodeTeamEncrypted(envelope, wire));
    return wire;
}

chat::MeshIncomingData makeIncomingTeamChat()
{
    chat::MeshIncomingData data{};
    data.portnum = team::proto::TEAM_CHAT_APP;
    data.from = 0x4ECF80D8;
    data.to = 0xFFFFFFFF;
    data.packet_id = 0x09EDDEC3;
    data.channel = static_cast<chat::ChannelId>(2);
    data.channel_hash = 8;
    data.want_response = false;
    data.payload = makeTeamChatWire();
    return data;
}

void testChatServiceDoesNotDrainAppDataWithoutObservers()
{
    FakeMeshAdapter mesh;
    mesh.incoming.push_back(makeIncomingTeamChat());

    chat::ChatModel model;
    FakeChatStore store;
    chat::ChatService chat_service(
        model,
        mesh,
        store,
        chat::MeshProtocol::Meshtastic);

    chat_service.processIncoming();
    assert(mesh.incoming.size() == 1);
}

void testTeamServiceReceivesTeamChatAfterChatServiceTick()
{
    FakeMeshAdapter mesh;
    mesh.incoming.push_back(makeIncomingTeamChat());

    chat::ChatModel model;
    FakeChatStore store;
    chat::ChatService chat_service(
        model,
        mesh,
        store,
        chat::MeshProtocol::Meshtastic);

    FakeCrypto crypto;
    FakeRuntime runtime;
    FakeTeamSink sink;
    team::TeamService team_service(crypto, mesh, sink, runtime);
    team_service.setKeys(testKeys());

    chat_service.processIncoming();
    team_service.processIncoming();

    assert(sink.error_count == 0);
    assert(sink.chat_count == 1);
    assert(sink.last_from == 0x4ECF80D8);
    assert(std::string(sink.last_payload.begin(), sink.last_payload.end()) == "ping");
}

} // namespace

int main()
{
    testChatServiceDoesNotDrainAppDataWithoutObservers();
    testTeamServiceReceivesTeamChatAfterChatServiceTick();
    return 0;
}
