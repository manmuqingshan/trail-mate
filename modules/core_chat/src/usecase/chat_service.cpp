/**
 * @file chat_service.cpp
 * @brief Chat service implementation
 */

#include "chat/usecase/chat_service.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/ports/i_incoming_delivery_commit_port.h"
#include "chat/time_utils.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace chat
{
namespace
{
NodeId normalize_conversation_peer(NodeId peer)
{
    return (peer == 0 || peer == 0xFFFFFFFFUL) ? 0 : peer;
}

uint32_t persistable_now_timestamp()
{
    const uint32_t now = now_epoch_seconds();
    return is_valid_epoch(now) ? now : 0;
}

uint32_t persistable_incoming_timestamp(const MeshIncomingText& incoming)
{
    if (is_valid_epoch(incoming.timestamp))
    {
        return incoming.timestamp;
    }
    if (is_valid_epoch(incoming.rx_meta.rx_timestamp_s))
    {
        return incoming.rx_meta.rx_timestamp_s;
    }
    return persistable_now_timestamp();
}
} // namespace

#ifndef CHAT_SERVICE_LOG_ENABLE
#define CHAT_SERVICE_LOG_ENABLE 0
#endif

#if CHAT_SERVICE_LOG_ENABLE
#define CHAT_SERVICE_LOG(...) std::printf(__VA_ARGS__)
#else
#define CHAT_SERVICE_LOG(...)
#endif

#ifndef CHAT_SERVICE_DIAG_LOG_ENABLE
#define CHAT_SERVICE_DIAG_LOG_ENABLE 1
#endif

#if CHAT_SERVICE_DIAG_LOG_ENABLE
#define CHAT_SERVICE_DIAG_LOG(...) std::printf(__VA_ARGS__)
#else
#define CHAT_SERVICE_DIAG_LOG(...)
#endif

const char* failure_name(MeshOperationFailure failure)
{
    switch (failure)
    {
    case MeshOperationFailure::None:
        return "none";
    case MeshOperationFailure::InvalidInput:
        return "invalid_input";
    case MeshOperationFailure::Unsupported:
        return "unsupported";
    case MeshOperationFailure::NotReady:
        return "not_ready";
    case MeshOperationFailure::TxDisabled:
        return "tx_disabled";
    case MeshOperationFailure::RadioOffline:
        return "radio_offline";
    case MeshOperationFailure::DutyCycleLimited:
        return "duty_cycle_limited";
    case MeshOperationFailure::LocalIdentityMissing:
        return "local_identity_missing";
    case MeshOperationFailure::PeerKeyMissing:
        return "peer_key_missing";
    case MeshOperationFailure::ChannelKeyMissing:
        return "channel_key_missing";
    case MeshOperationFailure::EncodeFailed:
        return "encode_failed";
    case MeshOperationFailure::CryptoFailed:
        return "crypto_failed";
    case MeshOperationFailure::RadioTxFailed:
        return "radio_tx_failed";
    case MeshOperationFailure::Busy:
        return "busy";
    case MeshOperationFailure::Unknown:
        return "unknown";
    }
    return "unknown";
}

const char* protocol_name(MeshProtocol protocol)
{
    return infra::isValidMeshProtocol(protocol) ? infra::meshProtocolName(protocol)
                                                : "Unknown";
}

void format_reticulum_hash_prefix(const ReticulumPeerIdentity* identity,
                                  char* out,
                                  size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!identity || !hasReticulumDestinationIdentity(*identity) || out_len < 9)
    {
        std::snprintf(out, out_len, "-");
        return;
    }
    std::snprintf(out,
                  out_len,
                  "%02X%02X%02X%02X",
                  static_cast<unsigned>(identity->destination_hash[0]),
                  static_cast<unsigned>(identity->destination_hash[1]),
                  static_cast<unsigned>(identity->destination_hash[2]),
                  static_cast<unsigned>(identity->destination_hash[3]));
}

void format_log_text_preview(const std::string& text, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    const size_t max_copy = out_len - 1U;
    size_t used = 0;
    for (char value : text)
    {
        if (used >= max_copy)
        {
            break;
        }
        const unsigned char c = static_cast<unsigned char>(value);
        if (c == '\r' || c == '\n' || c == '\t')
        {
            out[used++] = ' ';
        }
        else if (c < 0x20U || c == 0x7FU)
        {
            out[used++] = '.';
        }
        else
        {
            out[used++] = value;
        }
    }
    out[used] = '\0';
}

ChatService::ChatService(ChatModel& model,
                         IMeshAdapter& adapter,
                         IChatStore& store,
                         MeshProtocol active_protocol)
    : model_(model), adapter_(adapter), store_(store),
      message_ledger_(model, store),
      current_channel_(ChannelId::PRIMARY),
      active_protocol_(active_protocol)
{
}

void ChatService::RecentIncomingWindow::clear()
{
    next = 0;
    count = 0;
}

bool ChatService::RecentIncomingWindow::contains(const IncomingIdentity& identity) const
{
    for (std::size_t i = 0; i < count; ++i)
    {
        if (entries[i] == identity)
        {
            return true;
        }
    }
    return false;
}

void ChatService::RecentIncomingWindow::remember(const IncomingIdentity& identity)
{
    entries[next] = identity;
    next = (next + 1) % kRecentIncomingLimit;
    if (count < kRecentIncomingLimit)
    {
        ++count;
    }
}

MessageId ChatService::sendText(ChannelId channel, const std::string& text, NodeId peer)
{
    return sendTextWithId(channel, text, 0, peer);
}

MessageId ChatService::sendTextWithId(ChannelId channel, const std::string& text,
                                      MessageId forced_msg_id, NodeId peer)
{
    const MeshSendResult result = sendTextWithIdDetailed(channel, text, forced_msg_id, peer);
    return result.ok ? result.msg_id : 0;
}

MeshSendResult ChatService::sendTextDetailed(ChannelId channel, const std::string& text,
                                             NodeId peer)
{
    return sendTextWithIdDetailed(channel, text, 0, peer);
}

bool ChatService::canSendToConversation(const ConversationId& conversation) const
{
    return conversation.protocol == active_protocol_;
}

MessageId ChatService::sendTextToConversation(const ConversationId& conversation,
                                              const std::string& text)
{
    const MeshSendResult result =
        sendTextToConversationDetailed(conversation, text);
    return result.ok ? result.msg_id : 0;
}

MeshSendResult ChatService::sendTextToConversationDetailed(
    const ConversationId& conversation,
    const std::string& text)
{
    if (!canSendToConversation(conversation))
    {
        return MeshSendResult::fail(MeshOperationFailure::Unsupported);
    }

    const ReticulumPeerIdentity* reticulum_destination = nullptr;
    NodeId routed_peer = conversation.peer;
    if (conversation.protocol == MeshProtocol::Reticulum &&
        hasReticulumDestinationIdentity(conversation.reticulum_identity))
    {
        reticulum_destination = &conversation.reticulum_identity;
        routed_peer = 0;
    }

    return sendTextResolvedDetailed(conversation.channel,
                                    text,
                                    0,
                                    routed_peer,
                                    reticulum_destination);
}

MeshSendResult ChatService::sendTextWithIdDetailed(ChannelId channel, const std::string& text,
                                                   MessageId forced_msg_id, NodeId peer)
{
    return sendTextResolvedDetailed(channel, text, forced_msg_id, peer, nullptr);
}

MeshSendResult ChatService::sendTextResolvedDetailed(
    ChannelId channel,
    const std::string& text,
    MessageId forced_msg_id,
    NodeId peer,
    const ReticulumPeerIdentity* reticulum_destination)
{
    if (text.empty())
    {
        return MeshSendResult::fail(MeshOperationFailure::InvalidInput);
    }

    const bool has_reticulum_destination =
        reticulum_destination &&
        hasReticulumDestinationIdentity(*reticulum_destination);
    char dest_hash[12] = {};
    char text_preview[64] = {};
    format_reticulum_hash_prefix(reticulum_destination, dest_hash, sizeof(dest_hash));
    format_log_text_preview(text, text_preview, sizeof(text_preview));
    CHAT_SERVICE_DIAG_LOG("[ChatService][TX] begin protocol=%s mode=%s ch=%u peer=%08lX forced=%lu dest=%s len=%u text=\"%s\"\n",
                          protocol_name(active_protocol_),
                          has_reticulum_destination ? "reticulum_destination" : "peer",
                          static_cast<unsigned>(channel),
                          static_cast<unsigned long>(normalize_conversation_peer(peer)),
                          static_cast<unsigned long>(forced_msg_id),
                          dest_hash,
                          static_cast<unsigned>(text.size()),
                          text_preview);
    MeshSendResult result =
        has_reticulum_destination
            ? adapter_.sendTextToReticulumDestination(channel,
                                                      text,
                                                      forced_msg_id,
                                                      *reticulum_destination)
            : adapter_.sendTextDetailed(channel, text, forced_msg_id, peer);
    CHAT_SERVICE_DIAG_LOG("[ChatService][TX] adapter_result ok=%u msg=%lu failure=%s dest=%s\n",
                          result.ok ? 1U : 0U,
                          static_cast<unsigned long>(result.msg_id),
                          failure_name(result.failure),
                          dest_hash);
    if (!result.ok && result.msg_id == 0)
    {
        return result;
    }
    if (!hasReticulumDestinationIdentity(result.reticulum_identity) &&
        has_reticulum_destination)
    {
        result.reticulum_identity = *reticulum_destination;
    }

    ChatMessage msg;
    msg.protocol = active_protocol_;
    msg.channel = channel;
    msg.from = 0;
    msg.peer = normalize_conversation_peer(peer);
    msg.msg_id = result.msg_id;
    msg.timestamp = persistable_now_timestamp();
    msg.text = text;
    msg.reticulum_identity = result.reticulum_identity;
    msg.status = result.ok ? MessageStatus::Queued : MessageStatus::Failed;

    message_ledger_.recordOutbound(msg, model_enabled_);
    CHAT_SERVICE_DIAG_LOG("[ChatService][TX] stored msg=%lu status=%u peer=%08lX dest=%s text=\"%s\"\n",
                          static_cast<unsigned long>(msg.msg_id),
                          static_cast<unsigned>(msg.status),
                          static_cast<unsigned long>(msg.peer),
                          dest_hash,
                          text_preview);

    if (result.ok && result.msg_id != 0)
    {
        MeshIncomingText outgoing{};
        outgoing.channel = channel;
        outgoing.from = adapter_.getNodeId();
        outgoing.to = has_reticulum_destination ? 0 : ((peer != 0) ? peer : 0xFFFFFFFFUL);
        outgoing.msg_id = result.msg_id;
        outgoing.timestamp = msg.timestamp;
        outgoing.text = text;
        outgoing.hop_limit = 0;
        outgoing.encrypted = false;
        outgoing.reticulum_identity = result.reticulum_identity;

        for (auto* observer : outgoing_text_observers_)
        {
            if (observer)
            {
                observer->onOutgoingText(outgoing);
            }
        }
    }

    return result;
}

bool ChatService::triggerDiscoveryAction(MeshDiscoveryAction action)
{
    return triggerDiscoveryActionDetailed(action).ok;
}

MeshActionResult ChatService::triggerDiscoveryActionDetailed(MeshDiscoveryAction action)
{
    return adapter_.triggerDiscoveryActionDetailed(action);
}

MeshActionResult ChatService::startReticulumAudioCall(
    const ReticulumPeerIdentity& destination)
{
    if (active_protocol_ != MeshProtocol::Reticulum)
    {
        return MeshActionResult::fail(MeshOperationFailure::Unsupported);
    }
    return adapter_.startReticulumAudioCall(destination);
}

MeshActionResult ChatService::pingReticulumDestination(
    const ReticulumPeerIdentity& destination)
{
    if (active_protocol_ != MeshProtocol::Reticulum)
    {
        return MeshActionResult::fail(MeshOperationFailure::Unsupported);
    }
    return adapter_.pingReticulumDestination(destination);
}

MeshActionResult ChatService::persistReticulumPeer(
    const ReticulumPeerIdentity& destination,
    bool favorite)
{
    if (active_protocol_ != MeshProtocol::Reticulum)
    {
        return MeshActionResult::fail(MeshOperationFailure::Unsupported);
    }
    return adapter_.persistReticulumPeer(destination, favorite);
}

void ChatService::switchChannel(ChannelId channel)
{
    current_channel_ = channel;
}

bool ChatService::resendFailed(MessageId msg_id)
{
    ChatMessage msg;
    if (const ChatMessage* model_msg = model_.getMessage(msg_id))
    {
        msg = *model_msg;
    }
    else if (!store_.getMessage(msg_id, &msg))
    {
        return false;
    }

    if (msg.status != MessageStatus::Failed)
    {
        return false;
    }
    if (msg.protocol != active_protocol_)
    {
        return false;
    }

    const bool resend_reticulum_destination =
        msg.protocol == MeshProtocol::Reticulum &&
        hasReticulumDestinationIdentity(msg.reticulum_identity);
    const ReticulumPeerIdentity* resend_destination =
        resend_reticulum_destination ? &msg.reticulum_identity : nullptr;
    char dest_hash[12] = {};
    format_reticulum_hash_prefix(resend_destination, dest_hash, sizeof(dest_hash));
    CHAT_SERVICE_DIAG_LOG("[ChatService][TX] resend begin msg=%lu mode=%s peer=%08lX dest=%s len=%u\n",
                          static_cast<unsigned long>(msg.msg_id),
                          resend_reticulum_destination ? "reticulum_destination" : "peer",
                          static_cast<unsigned long>(msg.peer),
                          dest_hash,
                          static_cast<unsigned>(msg.text.size()));
    const MeshSendResult result =
        resend_reticulum_destination
            ? adapter_.sendTextToReticulumDestination(msg.channel,
                                                      msg.text,
                                                      msg.msg_id,
                                                      msg.reticulum_identity)
            : adapter_.sendTextDetailed(msg.channel, msg.text, msg.msg_id, msg.peer);
    CHAT_SERVICE_DIAG_LOG("[ChatService][TX] resend result ok=%u msg=%lu failure=%s dest=%s\n",
                          result.ok ? 1U : 0U,
                          static_cast<unsigned long>(result.msg_id),
                          failure_name(result.failure),
                          dest_hash);
    if (!result.ok || result.msg_id != msg.msg_id)
    {
        return false;
    }

    return message_ledger_.markRetryQueued(msg.msg_id, model_enabled_);
}

std::vector<ChatMessage> ChatService::getRecentMessages(const ConversationId& conv, size_t limit) const
{
    return store_.loadRecent(conv, limit);
}

std::vector<ChatMessage> ChatService::getMessagePageFromLatest(
    const ConversationId& conv,
    size_t offset_from_latest,
    size_t limit,
    size_t* total) const
{
    return store_.loadPageFromLatest(conv, offset_from_latest, limit, total);
}

std::vector<ConversationMeta> ChatService::getConversations(size_t offset,
                                                            size_t limit,
                                                            size_t* total) const
{
    return store_.loadConversationPage(offset, limit, total);
}

int ChatService::getTotalUnread() const
{
    size_t total = 0;
    auto convs = store_.loadConversationPage(0, 0, &total);
    int sum = 0;
    for (const auto& conv : convs)
    {
        sum += conv.unread;
    }
    return sum;
}

void ChatService::clearAllMessages()
{
    model_.clearAll();
    store_.clearAll();
    recent_incoming_.clear();
}

void ChatService::clearConversation(const ConversationId& conv)
{
    model_.clearConversation(conv);
    store_.clearConversation(conv);
    recent_incoming_.clear();
}

void ChatService::markConversationRead(const ConversationId& conv)
{
    model_.markRead(conv);
    store_.setUnread(conv, 0);
}

void ChatService::processIncoming()
{
    MeshIncomingText incoming_text;
    while (adapter_.pollIncomingText(&incoming_text))
    {
        ChatMessage msg;
        msg.protocol = active_protocol_;
        msg.channel = incoming_text.channel;
        msg.from = incoming_text.from;
        msg.peer = normalize_conversation_peer(incoming_text.to) == 0 ? 0 : incoming_text.from;
        msg.msg_id = incoming_text.msg_id;
        msg.timestamp = persistable_incoming_timestamp(incoming_text);
        msg.text = incoming_text.text;
        msg.reticulum_identity = incoming_text.reticulum_identity;
        msg.has_reticulum_lxmf_hash = incoming_text.has_reticulum_lxmf_hash;
        std::memcpy(msg.reticulum_lxmf_hash,
                    incoming_text.reticulum_lxmf_hash,
                    sizeof(msg.reticulum_lxmf_hash));
        msg.source_unverified = incoming_text.source_unverified;
        msg.rx_origin = incoming_text.rx_meta.origin;
        msg.status = MessageStatus::Incoming;

        const ReticulumPeerIdentity* incoming_identity =
            hasReticulumDestinationIdentity(incoming_text.reticulum_identity)
                ? &incoming_text.reticulum_identity
                : nullptr;
        char incoming_dest_hash[12] = {};
        format_reticulum_hash_prefix(incoming_identity,
                                     incoming_dest_hash,
                                     sizeof(incoming_dest_hash));
        CHAT_SERVICE_DIAG_LOG("[ChatService][RX] text protocol=%s msg=%lu from=%08lX to=%08lX peer=%08lX dest=%s len=%u encrypted=%u unverified=%u lxmf_hash=%u\n",
                              protocol_name(active_protocol_),
                              static_cast<unsigned long>(msg.msg_id),
                              static_cast<unsigned long>(msg.from),
                              static_cast<unsigned long>(incoming_text.to),
                              static_cast<unsigned long>(msg.peer),
                              incoming_dest_hash,
                              static_cast<unsigned>(msg.text.size()),
                              incoming_text.encrypted ? 1U : 0U,
                              incoming_text.source_unverified ? 1U : 0U,
                              chat::hasReticulumLxmfMessageHash(msg) ? 1U : 0U);

        CHAT_SERVICE_LOG("[ChatService] incoming text ch=%u from=%08lX to=%08lX peer=%08lX ts=%lu len=%u\n",
                         static_cast<unsigned>(msg.channel),
                         static_cast<unsigned long>(msg.from),
                         static_cast<unsigned long>(incoming_text.to),
                         static_cast<unsigned long>(msg.peer),
                         static_cast<unsigned long>(msg.timestamp),
                         static_cast<unsigned>(msg.text.size()));

        if (isDuplicateIncoming(msg))
        {
            CHAT_SERVICE_DIAG_LOG("[ChatService][RX] duplicate incoming text ignored ch=%u from=%08lX peer=%08lX id=%08lX lxmf_hash=%u\n",
                                  static_cast<unsigned>(msg.channel),
                                  static_cast<unsigned long>(msg.from),
                                  static_cast<unsigned long>(msg.peer),
                                  static_cast<unsigned long>(msg.msg_id),
                                  chat::hasReticulumLxmfMessageHash(msg) ? 1U : 0U);
            if (IIncomingDeliveryCommitPort* commit_port =
                    adapter_.incomingDeliveryCommitPort())
            {
                commit_port->commitIncomingText(incoming_text, true);
            }
            continue;
        }

        if (!store_.appendIncomingDurably(msg))
        {
            CHAT_SERVICE_DIAG_LOG("[ChatService][RX] durable append failed protocol=%s msg=%lu lxmf_hash=%u\n",
                                  protocol_name(active_protocol_),
                                  static_cast<unsigned long>(msg.msg_id),
                                  chat::hasReticulumLxmfMessageHash(msg) ? 1U : 0U);
            if (IIncomingDeliveryCommitPort* commit_port =
                    adapter_.incomingDeliveryCommitPort())
            {
                commit_port->commitIncomingText(incoming_text, false);
            }
            continue;
        }

        rememberIncoming(msg);

        if (model_enabled_)
        {
            model_.onIncoming(msg);
        }

        if (IIncomingDeliveryCommitPort* commit_port =
                adapter_.incomingDeliveryCommitPort())
        {
            commit_port->commitIncomingText(incoming_text, true);
        }

        for (auto* observer : incoming_message_observers_)
        {
            if (observer)
            {
                observer->onIncomingMessage(msg, &incoming_text.rx_meta);
            }
        }

        for (auto* observer : incoming_text_observers_)
        {
            if (observer)
            {
                observer->onIncomingText(incoming_text);
            }
        }
    }

    if (incoming_data_observers_.empty())
    {
        return;
    }

    MeshIncomingData incoming_data;
    while (adapter_.pollIncomingData(&incoming_data))
    {
        CHAT_SERVICE_LOG("[ChatService] incoming data ch=%u from=%08lX to=%08lX pkt=%08lX port=%u len=%u\n",
                         static_cast<unsigned>(incoming_data.channel),
                         static_cast<unsigned long>(incoming_data.from),
                         static_cast<unsigned long>(incoming_data.to),
                         static_cast<unsigned long>(incoming_data.packet_id),
                         static_cast<unsigned>(incoming_data.portnum),
                         static_cast<unsigned>(incoming_data.payload.size()));

        for (auto* observer : incoming_data_observers_)
        {
            if (observer)
            {
                observer->onIncomingData(incoming_data);
            }
        }
    }
}

bool ChatService::isDuplicateIncoming(const ChatMessage& msg) const
{
    if (msg.status != MessageStatus::Incoming)
    {
        return false;
    }

    if (chat::hasReticulumLxmfMessageHash(msg) &&
        store_.hasReticulumLxmfMessageHash(msg.reticulum_lxmf_hash))
    {
        return true;
    }

    if (msg.msg_id == 0)
    {
        return false;
    }

    IncomingIdentity identity{};
    identity.protocol = msg.protocol;
    identity.channel = msg.channel;
    identity.from = msg.from;
    identity.peer = msg.peer;
    identity.msg_id = msg.msg_id;
    if (chat::hasReticulumLxmfMessageHash(msg))
    {
        identity.has_reticulum_lxmf_hash = true;
        std::memcpy(identity.reticulum_lxmf_hash,
                    msg.reticulum_lxmf_hash,
                    sizeof(identity.reticulum_lxmf_hash));
    }
    if (msg.protocol == MeshProtocol::Reticulum &&
        hasReticulumDestinationIdentity(msg.reticulum_identity))
    {
        identity.has_reticulum_destination = copyReticulumDestinationHash(
            identity.reticulum_destination_hash,
            msg.reticulum_identity);
    }
    return recent_incoming_.contains(identity);
}

void ChatService::rememberIncoming(const ChatMessage& msg)
{
    if (msg.status != MessageStatus::Incoming ||
        (msg.msg_id == 0 && !chat::hasReticulumLxmfMessageHash(msg)))
    {
        return;
    }

    IncomingIdentity identity{};
    identity.protocol = msg.protocol;
    identity.channel = msg.channel;
    identity.from = msg.from;
    identity.peer = msg.peer;
    identity.msg_id = msg.msg_id;
    if (chat::hasReticulumLxmfMessageHash(msg))
    {
        identity.has_reticulum_lxmf_hash = true;
        std::memcpy(identity.reticulum_lxmf_hash,
                    msg.reticulum_lxmf_hash,
                    sizeof(identity.reticulum_lxmf_hash));
    }
    if (msg.protocol == MeshProtocol::Reticulum &&
        hasReticulumDestinationIdentity(msg.reticulum_identity))
    {
        identity.has_reticulum_destination = copyReticulumDestinationHash(
            identity.reticulum_destination_hash,
            msg.reticulum_identity);
    }
    recent_incoming_.remember(identity);
}

void ChatService::flushStore()
{
    store_.flush();
}

void ChatService::addIncomingMessageObserver(IncomingMessageObserver* observer)
{
    if (!observer)
    {
        return;
    }
    for (auto* existing : incoming_message_observers_)
    {
        if (existing == observer)
        {
            return;
        }
    }
    incoming_message_observers_.push_back(observer);
}

void ChatService::removeIncomingMessageObserver(IncomingMessageObserver* observer)
{
    if (!observer)
    {
        return;
    }
    for (auto it = incoming_message_observers_.begin(); it != incoming_message_observers_.end(); ++it)
    {
        if (*it == observer)
        {
            incoming_message_observers_.erase(it);
            return;
        }
    }
}

void ChatService::addOutgoingTextObserver(OutgoingTextObserver* observer)
{
    if (!observer)
    {
        return;
    }
    for (auto* existing : outgoing_text_observers_)
    {
        if (existing == observer)
        {
            return;
        }
    }
    outgoing_text_observers_.push_back(observer);
}

void ChatService::removeOutgoingTextObserver(OutgoingTextObserver* observer)
{
    if (!observer)
    {
        return;
    }
    for (auto it = outgoing_text_observers_.begin(); it != outgoing_text_observers_.end(); ++it)
    {
        if (*it == observer)
        {
            outgoing_text_observers_.erase(it);
            return;
        }
    }
}

void ChatService::addIncomingDataObserver(IncomingDataObserver* observer)
{
    if (!observer)
    {
        return;
    }
    for (auto* existing : incoming_data_observers_)
    {
        if (existing == observer)
        {
            return;
        }
    }
    incoming_data_observers_.push_back(observer);
}

void ChatService::removeIncomingDataObserver(IncomingDataObserver* observer)
{
    if (!observer)
    {
        return;
    }
    for (auto it = incoming_data_observers_.begin(); it != incoming_data_observers_.end(); ++it)
    {
        if (*it == observer)
        {
            incoming_data_observers_.erase(it);
            return;
        }
    }
}

void ChatService::handleSendResult(MessageId msg_id, bool ok)
{
    handleSendResult(msg_id,
                     ok ? MessageStatus::Sent : MessageStatus::Failed);
}

void ChatService::handleSendResult(MessageId msg_id, MessageStatus status)
{
    (void)message_ledger_.applyOutboundStatus(msg_id, status, model_enabled_);
}

const ChatMessage* ChatService::getMessage(MessageId msg_id) const
{
    if (const ChatMessage* msg = model_.getMessage(msg_id))
    {
        return msg;
    }
    if (store_.getMessage(msg_id, &store_lookup_cache_))
    {
        return &store_lookup_cache_;
    }
    return nullptr;
}

void ChatService::setModelEnabled(bool enabled)
{
    if (model_enabled_ == enabled)
    {
        return;
    }
    model_enabled_ = enabled;
    if (!model_enabled_)
    {
        model_.clearAll();
    }
}

void ChatService::addIncomingTextObserver(IncomingTextObserver* observer)
{
    if (!observer)
    {
        return;
    }
    for (auto* existing : incoming_text_observers_)
    {
        if (existing == observer)
        {
            return;
        }
    }
    incoming_text_observers_.push_back(observer);
}

void ChatService::removeIncomingTextObserver(IncomingTextObserver* observer)
{
    if (!observer)
    {
        return;
    }
    for (auto it = incoming_text_observers_.begin(); it != incoming_text_observers_.end(); ++it)
    {
        if (*it == observer)
        {
            incoming_text_observers_.erase(it);
            return;
        }
    }
}

} // namespace chat
