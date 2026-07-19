#include "chat/domain/chat_model.h"
#include "chat/ports/i_chat_store.h"
#include "chat/ports/i_mesh_adapter.h"
#include "chat/usecase/chat_service.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <iostream>
#include <string>
#include <vector>

namespace
{

class FakeMeshAdapter final : public ::chat::IMeshAdapter
{
  public:
    void pushIncoming(::chat::NodeId from,
                      ::chat::MessageId msg_id,
                      const std::string& text)
    {
        ::chat::MeshIncomingText incoming{};
        incoming.channel = ::chat::ChannelId::PRIMARY;
        incoming.from = from;
        incoming.to = 0xFFFFFFFFUL;
        incoming.msg_id = msg_id;
        incoming.text = text;
        incoming.timestamp = 1;
        incoming.hop_limit = 3;
        incoming.encrypted = true;
        incoming_.push_back(incoming);
    }

    void pushIncomingReticulum(::chat::NodeId from,
                               ::chat::NodeId to,
                               ::chat::MessageId msg_id,
                               const std::string& text,
                               const std::uint8_t lxmf_hash[::chat::kReticulumLxmfHashSize])
    {
        ::chat::MeshIncomingText incoming{};
        incoming.channel = ::chat::ChannelId::PRIMARY;
        incoming.from = from;
        incoming.to = to;
        incoming.msg_id = msg_id;
        incoming.text = text;
        incoming.timestamp = 1;
        incoming.hop_limit = 3;
        incoming.encrypted = true;
        incoming.has_reticulum_lxmf_hash = true;
        std::memcpy(incoming.reticulum_lxmf_hash,
                    lxmf_hash,
                    sizeof(incoming.reticulum_lxmf_hash));
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

    bool pollIncomingRawPacket(std::uint8_t*, std::size_t&, std::size_t) override
    {
        return false;
    }

  private:
    std::deque<::chat::MeshIncomingText> incoming_{};
};

class FakeChatStore final : public ::chat::IChatStore
{
  public:
    void append(const ::chat::ChatMessage& msg) override
    {
        messages_.push_back(msg);
        if (msg.status == ::chat::MessageStatus::Incoming)
        {
            ++unread_;
        }
    }

    std::vector<::chat::ChatMessage> loadRecent(const ::chat::ConversationId& conv,
                                                std::size_t n) override
    {
        std::vector<::chat::ChatMessage> matching;
        for (const auto& msg : messages_)
        {
            if (::chat::ConversationId(msg.channel, msg.peer, msg.protocol) == conv)
            {
                matching.push_back(msg);
            }
        }

        const std::size_t count = matching.size();
        const std::size_t start = count > n ? count - n : 0;
        return std::vector<::chat::ChatMessage>(
            matching.begin() + static_cast<std::ptrdiff_t>(start),
            matching.end());
    }

    std::vector<::chat::ConversationMeta> loadConversationPage(std::size_t,
                                                               std::size_t,
                                                               std::size_t* total) override
    {
        if (total != nullptr)
        {
            *total = 0;
        }
        return {};
    }

    bool setUnread(const ::chat::ConversationId&, int unread) override
    {
        unread_ = unread;
        return true;
    }

    int getUnread(const ::chat::ConversationId&) const override
    {
        return unread_;
    }

    void clearConversation(const ::chat::ConversationId& conv) override
    {
        std::vector<::chat::ChatMessage> kept;
        for (const auto& msg : messages_)
        {
            if (!(::chat::ConversationId(msg.channel, msg.peer, msg.protocol) == conv))
            {
                kept.push_back(msg);
            }
        }
        messages_ = kept;
        unread_ = 0;
    }

    void clearAll() override
    {
        messages_.clear();
        unread_ = 0;
    }

    bool updateMessageStatus(::chat::MessageId msg_id,
                             ::chat::MessageStatus status) override
    {
        for (auto& msg : messages_)
        {
            if (msg.msg_id == msg_id && msg.from == 0)
            {
                msg.status = status;
                return true;
            }
        }
        return false;
    }

    bool getMessage(::chat::MessageId msg_id, ::chat::ChatMessage* out) const override
    {
        for (const auto& msg : messages_)
        {
            if (msg.msg_id == msg_id)
            {
                if (out != nullptr)
                {
                    *out = msg;
                }
                return true;
            }
        }
        return false;
    }

    bool hasReticulumLxmfMessageHash(const std::uint8_t* lxmf_hash) const override
    {
        if (lxmf_hash == nullptr)
        {
            return false;
        }
        for (const auto& msg : messages_)
        {
            if (!::chat::hasReticulumLxmfMessageHash(msg))
            {
                continue;
            }
            if (std::memcmp(msg.reticulum_lxmf_hash,
                            lxmf_hash,
                            ::chat::kReticulumLxmfHashSize) == 0)
            {
                return true;
            }
        }
        return false;
    }

  private:
    std::vector<::chat::ChatMessage> messages_{};
    int unread_ = 0;
};

} // namespace

int expect(bool condition, const char* message)
{
    if (condition)
    {
        return 0;
    }
    std::cerr << message << '\n';
    return 1;
}

int main()
{
    ::chat::ChatModel model;
    FakeMeshAdapter adapter;
    FakeChatStore store;
    ::chat::ChatService service(model,
                                adapter,
                                store,
                                ::chat::MeshProtocol::Meshtastic);
    const ::chat::ConversationId broadcast(::chat::ChannelId::PRIMARY,
                                           0,
                                           ::chat::MeshProtocol::Meshtastic);

    adapter.pushIncoming(0x1234ABCDU, 0x42U, "test");
    adapter.pushIncoming(0x1234ABCDU, 0x42U, "test");
    adapter.pushIncoming(0x1234ABCDU, 0x42U, "test");
    service.processIncoming();

    auto messages = store.loadRecent(broadcast, 10);
    if (int rc = expect(messages.size() == 1U,
                        "duplicate incoming text was stored more than once"))
    {
        return rc;
    }
    if (int rc = expect(messages.front().from == 0x1234ABCDU,
                        "stored message sender changed"))
    {
        return rc;
    }
    if (int rc = expect(messages.front().msg_id == 0x42U,
                        "stored message id changed"))
    {
        return rc;
    }
    if (int rc = expect(messages.front().text == "test",
                        "stored message text changed"))
    {
        return rc;
    }
    if (int rc = expect(store.getUnread(broadcast) == 1,
                        "duplicate incoming text inflated unread count"))
    {
        return rc;
    }

    adapter.pushIncoming(0x1234ABCDU, 0x43U, "next");
    adapter.pushIncoming(0x0000BEEFU, 0x42U, "same id from another node");
    service.processIncoming();

    messages = store.loadRecent(broadcast, 10);
    if (int rc = expect(messages.size() == 3U,
                        "distinct incoming identities were incorrectly suppressed"))
    {
        return rc;
    }
    if (int rc = expect(messages[1].msg_id == 0x43U,
                        "new message id from same node was suppressed"))
    {
        return rc;
    }
    if (int rc = expect(messages[2].from == 0x0000BEEFU,
                        "same message id from another node was suppressed"))
    {
        return rc;
    }
    if (int rc = expect(messages[2].msg_id == 0x42U,
                        "message id from another node changed"))
    {
        return rc;
    }
    if (int rc = expect(store.getUnread(broadcast) == 3,
                        "unread count does not match unique incoming messages"))
    {
        return rc;
    }

    for (std::uint32_t i = 0; i < 256U; ++i)
    {
        adapter.pushIncoming(0x1234ABCDU, 0x1000U + i, "window fill");
    }
    service.processIncoming();

    messages = store.loadRecent(broadcast, 300);
    if (int rc = expect(messages.size() == 259U,
                        "recent incoming fixed window dropped unique messages early"))
    {
        return rc;
    }

    adapter.pushIncoming(0x1234ABCDU, 0x10FFU, "recent duplicate");
    adapter.pushIncoming(0x1234ABCDU, 0x42U, "evicted original id");
    service.processIncoming();

    messages = store.loadRecent(broadcast, 300);
    if (int rc = expect(messages.size() == 260U,
                        "recent incoming fixed window did not preserve eviction semantics"))
    {
        return rc;
    }
    if (int rc = expect(messages.back().msg_id == 0x42U,
                        "evicted original incoming id was not accepted again"))
    {
        return rc;
    }

    std::uint8_t lxmf_hash[::chat::kReticulumLxmfHashSize] = {};
    for (std::size_t index = 0; index < sizeof(lxmf_hash); ++index)
    {
        lxmf_hash[index] = static_cast<std::uint8_t>(index + 1U);
    }
    service.setActiveProtocol(::chat::MeshProtocol::Reticulum);
    adapter.pushIncomingReticulum(0x0BADCAFEU,
                                  0x11121314U,
                                  0xA0A0U,
                                  "propagated original",
                                  lxmf_hash);
    adapter.pushIncomingReticulum(0x0BADCAFEU,
                                  0x11121314U,
                                  0xB0B0U,
                                  "propagated duplicate",
                                  lxmf_hash);
    service.processIncoming();

    const ::chat::ConversationId reticulum_conv(::chat::ChannelId::PRIMARY,
                                                0x0BADCAFEU,
                                                ::chat::MeshProtocol::Reticulum);
    messages = store.loadRecent(reticulum_conv, 10);
    if (int rc = expect(messages.size() == 1U,
                        "duplicate Reticulum LXMF hash was stored more than once"))
    {
        return rc;
    }
    if (int rc = expect(messages.front().msg_id == 0xA0A0U,
                        "Reticulum LXMF duplicate replaced the original message"))
    {
        return rc;
    }
    if (int rc = expect(messages.front().text == "propagated original",
                        "Reticulum LXMF duplicate changed stored text"))
    {
        return rc;
    }

    return 0;
}
