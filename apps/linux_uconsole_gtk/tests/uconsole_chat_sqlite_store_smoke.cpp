#include "chat/linux_sqlite_chat_store.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/usecase/chat_service.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <string>

namespace
{

void set_env_var(const char* name, const std::string& value)
{
    setenv(name, value.c_str(), 1);
}

::chat::ReticulumPeerIdentity makeReticulumIdentity(
    std::uint8_t destination_seed,
    std::uint8_t identity_seed)
{
    std::uint8_t destination_hash[::chat::kReticulumPeerHashSize] = {};
    std::uint8_t identity_hash[::chat::kReticulumPeerHashSize] = {};
    for (std::size_t index = 0; index < ::chat::kReticulumPeerHashSize; ++index)
    {
        destination_hash[index] =
            static_cast<std::uint8_t>(destination_seed + index);
        identity_hash[index] = static_cast<std::uint8_t>(identity_seed + index);
    }
    return ::chat::makeReticulumPeerIdentity(destination_hash, identity_hash);
}

void assertSameReticulumIdentity(
    const ::chat::ReticulumPeerIdentity& actual,
    const ::chat::ReticulumPeerIdentity& expected)
{
    assert(actual.valid == expected.valid);
    assert(std::memcmp(actual.destination_hash,
                       expected.destination_hash,
                       ::chat::kReticulumPeerHashSize) == 0);
    assert(std::memcmp(actual.identity_hash,
                       expected.identity_hash,
                       ::chat::kReticulumPeerHashSize) == 0);
}

class FakeMeshAdapter final : public ::chat::IMeshAdapter
{
  public:
    void pushIncoming(::chat::NodeId from,
                      ::chat::MessageId msg_id,
                      const std::string& text,
                      ::chat::NodeId to = 0xFFFFFFFFUL,
                      ::chat::ReticulumPeerIdentity reticulum_identity = {})
    {
        ::chat::MeshIncomingText incoming{};
        incoming.channel = ::chat::ChannelId::PRIMARY;
        incoming.from = from;
        incoming.to = to;
        incoming.msg_id = msg_id;
        incoming.timestamp = 1;
        incoming.text = text;
        incoming.hop_limit = 3;
        incoming.encrypted = false;
        incoming.reticulum_identity = reticulum_identity;
        incoming_.push_back(incoming);
    }

    bool sendText(::chat::ChannelId,
                  const std::string&,
                  ::chat::MessageId* out_msg_id,
                  ::chat::NodeId = 0) override
    {
        if (out_msg_id != nullptr)
        {
            *out_msg_id = 0;
        }
        return false;
    }

    bool pollIncomingText(::chat::MeshIncomingText* out) override
    {
        if (incoming_.empty())
        {
            return false;
        }
        if (out != nullptr)
        {
            *out = incoming_.front();
        }
        incoming_.pop_front();
        return true;
    }

    bool sendAppData(::chat::ChannelId,
                     std::uint32_t,
                     const std::uint8_t*,
                     std::size_t,
                     ::chat::NodeId = 0,
                     bool = false,
                     ::chat::MessageId = 0,
                     bool = false) override
    {
        return false;
    }

    bool pollIncomingData(::chat::MeshIncomingData*) override
    {
        return false;
    }

    void applyConfig(const ::chat::MeshConfig&) override {}

    bool isReady() const override
    {
        return true;
    }

    bool pollIncomingRawPacket(std::uint8_t*, std::size_t&, std::size_t)
        override
    {
        return false;
    }

  private:
    std::deque<::chat::MeshIncomingText> incoming_{};
};

} // namespace

int main()
{
    const auto root =
        std::filesystem::temp_directory_path() /
        "trailmate_uconsole_chat_sqlite_store_smoke";

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root / "settings", ec);
    std::filesystem::create_directories(root / "sd", ec);
    std::filesystem::create_directories(root / "cache", ec);

    set_env_var("TRAIL_MATE_SETTINGS_ROOT", (root / "settings").string());
    set_env_var("TRAIL_MATE_SD_ROOT", (root / "sd").string());
    set_env_var("TRAIL_MATE_CACHE_ROOT", (root / "cache").string());

    const ::chat::ConversationId broadcast(::chat::ChannelId::PRIMARY,
                                           0,
                                           ::chat::MeshProtocol::Meshtastic);

    {
        ::chat::ChatModel model;
        FakeMeshAdapter adapter;
        trailmate::linux_app::LinuxSqliteChatStore store;
        ::chat::ChatService service(model,
                                    adapter,
                                    store,
                                    ::chat::MeshProtocol::Meshtastic);

        adapter.pushIncoming(0x1234ABCDU, 0x42U, "persisted test");
        service.processIncoming();
        assert(store.getUnread(broadcast) == 1);
    }

    {
        trailmate::linux_app::LinuxSqliteChatStore store;
        auto messages = store.loadRecent(broadcast, 8);
        assert(messages.size() == 1U);
        assert(messages.front().from == 0x1234ABCDU);
        assert(messages.front().msg_id == 0x42U);
        assert(messages.front().text == "persisted test");
        assert(store.getUnread(broadcast) == 1);

        auto conversations = store.loadConversationPage(0, 8, nullptr);
        assert(conversations.size() == 1U);
        assert(conversations.front().id == broadcast);
        assert(conversations.front().unread == 1);
        assert(conversations.front().preview == "persisted test");

        store.setUnread(broadcast, 0);
    }

    {
        trailmate::linux_app::LinuxSqliteChatStore store;
        assert(store.getUnread(broadcast) == 0);
        auto messages = store.loadRecent(broadcast, 8);
        assert(messages.size() == 1U);
        store.clearAll();
        assert(store.loadRecent(broadcast, 8).empty());
    }

    const ::chat::ReticulumPeerIdentity reticulum_identity =
        makeReticulumIdentity(0x30U, 0x80U);
    ::chat::ConversationId reticulum_conversation(
        ::chat::ChannelId::PRIMARY,
        0x1234ABCDU,
        ::chat::MeshProtocol::Reticulum);
    reticulum_conversation.reticulum_identity = reticulum_identity;

    {
        ::chat::ChatModel model;
        FakeMeshAdapter adapter;
        trailmate::linux_app::LinuxSqliteChatStore store;
        ::chat::ChatService service(model,
                                    adapter,
                                    store,
                                    ::chat::MeshProtocol::Reticulum);

        adapter.pushIncoming(0x1234ABCDU,
                             0x500U,
                             "reticulum first",
                             0x00000001U,
                             reticulum_identity);
        adapter.pushIncoming(0x87654321U,
                             0x501U,
                             "reticulum second",
                             0x00000001U,
                             reticulum_identity);
        service.processIncoming();

        ::chat::ChatMessage duplicate{};
        duplicate.protocol = ::chat::MeshProtocol::Reticulum;
        duplicate.channel = ::chat::ChannelId::PRIMARY;
        duplicate.from = 0x55556666U;
        duplicate.peer = 0x55556666U;
        duplicate.msg_id = 0x501U;
        duplicate.timestamp = 2;
        duplicate.text = "reticulum duplicate";
        duplicate.reticulum_identity = reticulum_identity;
        duplicate.status = ::chat::MessageStatus::Incoming;
        store.append(duplicate);

        assert(store.getUnread(reticulum_conversation) == 2);
    }

    {
        trailmate::linux_app::LinuxSqliteChatStore store;
        auto messages = store.loadRecent(reticulum_conversation, 8);
        assert(messages.size() == 2U);
        assert(messages.front().from == 0x1234ABCDU);
        assert(messages.front().msg_id == 0x500U);
        assert(messages.front().text == "reticulum first");
        assertSameReticulumIdentity(messages.front().reticulum_identity,
                                    reticulum_identity);
        assert(messages.back().from == 0x87654321U);
        assert(messages.back().msg_id == 0x501U);
        assert(messages.back().text == "reticulum second");
        assertSameReticulumIdentity(messages.back().reticulum_identity,
                                    reticulum_identity);
        assert(store.getUnread(reticulum_conversation) == 2);

        std::size_t total = 0;
        auto conversations = store.loadConversationPage(0, 8, &total);
        assert(total == 1U);
        assert(conversations.size() == 1U);
        assert(conversations.front().id == reticulum_conversation);
        assert(conversations.front().unread == 2);
        assert(conversations.front().preview == "reticulum second");
        assertSameReticulumIdentity(conversations.front().reticulum_identity,
                                    reticulum_identity);
        assertSameReticulumIdentity(
            conversations.front().id.reticulum_identity,
            reticulum_identity);

        store.setUnread(reticulum_conversation, 0);
    }

    {
        trailmate::linux_app::LinuxSqliteChatStore store;
        assert(store.getUnread(reticulum_conversation) == 0);
        store.clearConversation(reticulum_conversation);
        assert(store.loadRecent(reticulum_conversation, 8).empty());
        std::size_t total = 0;
        auto conversations = store.loadConversationPage(0, 8, &total);
        assert(total == 0U);
        assert(conversations.empty());
    }

    std::filesystem::remove_all(root, ec);
    return 0;
}
