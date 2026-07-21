#include "platform/esp/arduino_common/chat/infra/store/sd_protocol_peer_repository.h"

#include "platform/esp/arduino_common/storage/scoped_state_lock.h"
#include "platform/esp/arduino_common/storage/sd_card_runtime.h"

#include <Arduino.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace chat
{
namespace
{
namespace storage_runtime = ::platform::esp::arduino_common::storage;
namespace storage_v2 = ::chat::storage::v2;

constexpr const char* kRoot = "/data/v2";
constexpr MeshProtocol kProtocols[] = {
    MeshProtocol::Meshtastic,
    MeshProtocol::MeshCore,
    MeshProtocol::Reticulum,
};
constexpr std::size_t kEphemeralPeerCapacity = 2048U;
constexpr std::size_t kProtectedContactCapacity = 4096U;
constexpr std::size_t kPeerHotCacheCapacity[] = {16U, 128U, 64U};
constexpr uint32_t kBootCompactionDeltaThreshold = 1024U;
constexpr std::size_t kPendingFlushBudget = 4U;
using ScopedRepositoryLock = storage_runtime::ScopedRecursiveStateLock;

bool hasText(const char* text)
{
    return text && text[0] != '\0';
}

bool textContains(const char* text, const char* query)
{
    return text && query && std::strstr(text, query) != nullptr;
}

void copyNodeText(char* out, std::size_t out_len, const char* text)
{
    copyMeshPeerText(out, out_len, text ? text : "");
}

MeshPeerNodeFacts& mutableNodeFacts(MeshPeerRecord& record)
{
    return record.identity.protocol == MeshProtocol::MeshCore
               ? record.meshcore.node
               : record.meshtastic.node;
}

const MeshPeerNodeFacts* nodeFacts(const MeshPeerRecord& record)
{
    if (record.identity.protocol == MeshProtocol::Meshtastic)
    {
        return &record.meshtastic.node;
    }
    if (record.identity.protocol == MeshProtocol::MeshCore)
    {
        return &record.meshcore.node;
    }
    return nullptr;
}

} // namespace

SdProtocolPeerRepository::SdProtocolPeerRepository(IChatStore& chat_store)
    : chat_store_(chat_store),
      mutex_(xSemaphoreCreateRecursiveMutex()),
      node_store_view_(*this),
      contact_store_view_(*this)
{
    peers_.reserve(256U);
    contacts_.reserve(64U);
    pending_peer_deltas_.reserve(16U);
    slot_scratch_.resize(512U, 0U);
}

SdProtocolPeerRepository::~SdProtocolPeerRepository()
{
    if (mutex_)
    {
        vSemaphoreDelete(mutex_);
        mutex_ = nullptr;
    }
}

MeshPeerDirectoryStatus SdProtocolPeerRepository::begin()
{
    ScopedRepositoryLock lock(mutex_);
    if (!lock.locked())
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::StorageUnavailable);
    }
    if (begun_)
    {
        return MeshPeerDirectoryStatus::success();
    }

    if (!ensureLayout())
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::StorageUnavailable);
    }

    peers_.clear();
    contacts_.clear();
    pending_peer_deltas_.clear();
    pending_peer_head_ = 0U;
    std::memset(partitions_, 0, sizeof(partitions_));
    bool ok = true;
    for (MeshProtocol protocol : kProtocols)
    {
        ok = loadProtocol(protocol) && ok;
    }
    if (!ok)
    {
        peers_.clear();
        contacts_.clear();
        return MeshPeerDirectoryStatus::fail(MeshPeerDirectoryStatusCode::IoError);
    }

    for (MeshProtocol protocol : kProtocols)
    {
        reconcileStableIdentities(protocol);
    }
    overlayContactFacts();
    for (MeshProtocol protocol : kProtocols)
    {
        if (!compactProtocolAtBoot(protocol))
        {
            Serial.printf("[PeerStoreV2] compaction deferred protocol=%s\n",
                          protocolSlug(protocol));
        }
    }
    begun_ = true;
    active_node_projection_dirty_ = true;
    Serial.printf("[PeerStoreV2] ready=1 peers=%u contacts=%u root=%s\n",
                  static_cast<unsigned>(peers_.size()),
                  static_cast<unsigned>(contacts_.size()),
                  kRoot);
    return MeshPeerDirectoryStatus::success();
}

bool SdProtocolPeerRepository::ensureLayout()
{
    if (!storage_runtime::sd_card_ready() || !ensureDirectory("/data") ||
        !ensureDirectory(kRoot))
    {
        return false;
    }
    bool ok = true;
    for (MeshProtocol protocol : kProtocols)
    {
        ok = ensureProtocolLayout(protocol) && ok;
    }
    return ok;
}

bool SdProtocolPeerRepository::ensureProtocolLayout(MeshProtocol protocol)
{
    char path[64] = {};
    buildProtocolPath(protocol, nullptr, path, sizeof(path));
    return ensureDirectory(path);
}

bool SdProtocolPeerRepository::loadProtocol(MeshProtocol protocol)
{
    for (const char* base : {"peers", "contacts"})
    {
        char final_path[96] = {};
        char temp_path[96] = {};
        char backup_path[96] = {};
        char name[32] = {};
        std::snprintf(name, sizeof(name), "%s.snapshot", base);
        buildProtocolPath(protocol,
                          name,
                          final_path,
                          sizeof(final_path));
        std::snprintf(name, sizeof(name), "%s.snapshot.tmp", base);
        buildProtocolPath(protocol,
                          name,
                          temp_path,
                          sizeof(temp_path));
        std::snprintf(name, sizeof(name), "%s.snapshot.bak", base);
        buildProtocolPath(protocol,
                          name,
                          backup_path,
                          sizeof(backup_path));
        if (!storage_v2::recoverAtomicFile(final_path,
                                           temp_path,
                                           backup_path))
        {
            return false;
        }
    }
    return loadPeerJournal(protocol, "peers.snapshot") &&
           loadPeerJournal(protocol, "peers.delta") &&
           loadContactJournal(protocol, "contacts.snapshot") &&
           loadContactJournal(protocol, "contacts.delta");
}

bool SdProtocolPeerRepository::loadPeerJournal(MeshProtocol protocol,
                                               const char* name)
{
    char path[96] = {};
    buildProtocolPath(protocol, name, path, sizeof(path));
    const std::size_t slot_size = storage_v2::peerSlotSize(protocol);
    const storage_v2::JournalKind kind =
        std::strstr(name, ".snapshot")
            ? storage_v2::JournalKind::PeerSnapshot
            : storage_v2::JournalKind::PeerDelta;
    const auto inspection = journal_.inspect(path, protocol, kind, slot_size);
    if (inspection.state == storage_v2::FixedSlotJournalEngine::State::Missing)
    {
        return true;
    }
    if (inspection.state != storage_v2::FixedSlotJournalEngine::State::Ready &&
        inspection.state !=
            storage_v2::FixedSlotJournalEngine::State::PartialTail)
    {
        Serial.printf("[PeerStoreV2] incompatible path=%s state=%u\n",
                      path,
                      static_cast<unsigned>(inspection.state));
        return false;
    }
    if (slot_size > slot_scratch_.size())
    {
        return false;
    }
    for (uint32_t index = 0; index < inspection.slot_count; ++index)
    {
        storage_v2::PeerProjection projection{};
        if (!journal_.read(path,
                           protocol,
                           kind,
                           slot_size,
                           index,
                           slot_scratch_.data()) ||
            !storage_v2::decodePeerSlot(protocol,
                                        slot_scratch_.data(),
                                        slot_size,
                                        projection))
        {
            Serial.printf("[PeerStoreV2] corrupt peer slot path=%s index=%lu\n",
                          path,
                          static_cast<unsigned long>(index));
            continue;
        }
        (void)applyPeerProjection(projection);
    }
    if (kind == storage_v2::JournalKind::PeerDelta)
    {
        partitions_[protocolIndex(protocol)].peer_delta_count =
            inspection.slot_count;
    }
    if (inspection.state ==
        storage_v2::FixedSlotJournalEngine::State::PartialTail)
    {
        Serial.printf("[PeerStoreV2] partial peer tail path=%s valid=%lu\n",
                      path,
                      static_cast<unsigned long>(inspection.slot_count));
    }
    return true;
}

bool SdProtocolPeerRepository::loadContactJournal(MeshProtocol protocol,
                                                  const char* name)
{
    char path[96] = {};
    buildProtocolPath(protocol, name, path, sizeof(path));
    const std::size_t slot_size = storage_v2::contactSlotSize(protocol);
    const storage_v2::JournalKind kind =
        std::strstr(name, ".snapshot")
            ? storage_v2::JournalKind::ContactSnapshot
            : storage_v2::JournalKind::ContactDelta;
    const auto inspection = journal_.inspect(path, protocol, kind, slot_size);
    if (inspection.state == storage_v2::FixedSlotJournalEngine::State::Missing)
    {
        return true;
    }
    if (inspection.state != storage_v2::FixedSlotJournalEngine::State::Ready &&
        inspection.state !=
            storage_v2::FixedSlotJournalEngine::State::PartialTail)
    {
        Serial.printf("[PeerStoreV2] incompatible path=%s state=%u\n",
                      path,
                      static_cast<unsigned>(inspection.state));
        return false;
    }
    if (slot_size > slot_scratch_.size())
    {
        return false;
    }
    for (uint32_t index = 0; index < inspection.slot_count; ++index)
    {
        storage_v2::ContactProjection projection{};
        if (!journal_.read(path,
                           protocol,
                           kind,
                           slot_size,
                           index,
                           slot_scratch_.data()) ||
            !storage_v2::decodeContactSlot(protocol,
                                           slot_scratch_.data(),
                                           slot_size,
                                           projection))
        {
            Serial.printf("[PeerStoreV2] corrupt contact slot path=%s index=%lu\n",
                          path,
                          static_cast<unsigned long>(index));
            continue;
        }
        (void)applyContactProjection(projection);
    }
    if (kind == storage_v2::JournalKind::ContactDelta)
    {
        partitions_[protocolIndex(protocol)].contact_delta_count =
            inspection.slot_count;
    }
    if (inspection.state ==
        storage_v2::FixedSlotJournalEngine::State::PartialTail)
    {
        Serial.printf("[PeerStoreV2] partial contact tail path=%s valid=%lu\n",
                      path,
                      static_cast<unsigned long>(inspection.slot_count));
    }
    return true;
}

bool SdProtocolPeerRepository::compactProtocolAtBoot(MeshProtocol protocol)
{
    PartitionState& state = partitions_[protocolIndex(protocol)];
    bool ok = true;
    if (state.peer_delta_count >= kBootCompactionDeltaThreshold)
    {
        ok = rewritePeerSnapshot(protocol) && ok;
    }
    if (state.contact_delta_count >= kBootCompactionDeltaThreshold)
    {
        ok = rewriteContactSnapshot(protocol) && ok;
    }
    return ok;
}

bool SdProtocolPeerRepository::rewritePeerSnapshot(MeshProtocol protocol)
{
    char target[96] = {};
    char temp[96] = {};
    char backup[96] = {};
    char delta[96] = {};
    buildProtocolPath(protocol, "peers.snapshot", target, sizeof(target));
    buildProtocolPath(protocol, "peers.snapshot.tmp", temp, sizeof(temp));
    buildProtocolPath(protocol,
                      "peers.snapshot.bak",
                      backup,
                      sizeof(backup));
    buildProtocolPath(protocol, "peers.delta", delta, sizeof(delta));
    if (storage_runtime::sd_exists(temp))
    {
        storage_runtime::sd_remove(temp);
    }
    const std::size_t slot_size = storage_v2::peerSlotSize(protocol);
    if (!journal_.create(temp,
                         protocol,
                         storage_v2::JournalKind::PeerSnapshot,
                         slot_size))
    {
        return false;
    }
    for (const MeshPeerRecord& peer : peers_)
    {
        if (!meshPeerSameProtocol(peer.identity.protocol, protocol))
        {
            continue;
        }
        const storage_v2::PeerProjection projection{peer, false};
        if (!storage_v2::encodePeerSlot(protocol,
                                        projection,
                                        slot_scratch_.data(),
                                        slot_size) ||
            !journal_.append(temp,
                             protocol,
                             storage_v2::JournalKind::PeerSnapshot,
                             slot_size,
                             slot_scratch_.data()))
        {
            storage_runtime::sd_remove(temp);
            return false;
        }
    }
    if (!storage_v2::replaceFileAtomically(temp, target, backup) ||
        !journal_.create(delta,
                         protocol,
                         storage_v2::JournalKind::PeerDelta,
                         slot_size))
    {
        return false;
    }
    partitions_[protocolIndex(protocol)].peer_delta_count = 0U;
    return true;
}

bool SdProtocolPeerRepository::rewriteContactSnapshot(MeshProtocol protocol)
{
    char target[96] = {};
    char temp[96] = {};
    char backup[96] = {};
    char delta[96] = {};
    buildProtocolPath(protocol, "contacts.snapshot", target, sizeof(target));
    buildProtocolPath(protocol, "contacts.snapshot.tmp", temp, sizeof(temp));
    buildProtocolPath(protocol,
                      "contacts.snapshot.bak",
                      backup,
                      sizeof(backup));
    buildProtocolPath(protocol, "contacts.delta", delta, sizeof(delta));
    if (storage_runtime::sd_exists(temp))
    {
        storage_runtime::sd_remove(temp);
    }
    const std::size_t slot_size = storage_v2::contactSlotSize(protocol);
    if (!journal_.create(temp,
                         protocol,
                         storage_v2::JournalKind::ContactSnapshot,
                         slot_size))
    {
        return false;
    }
    for (const storage_v2::ContactProjection& contact : contacts_)
    {
        if (!meshPeerSameProtocol(contact.identity.protocol, protocol))
        {
            continue;
        }
        if (!storage_v2::encodeContactSlot(protocol,
                                           contact,
                                           slot_scratch_.data(),
                                           slot_size) ||
            !journal_.append(temp,
                             protocol,
                             storage_v2::JournalKind::ContactSnapshot,
                             slot_size,
                             slot_scratch_.data()))
        {
            storage_runtime::sd_remove(temp);
            return false;
        }
    }
    if (!storage_v2::replaceFileAtomically(temp, target, backup) ||
        !journal_.create(delta,
                         protocol,
                         storage_v2::JournalKind::ContactDelta,
                         slot_size))
    {
        return false;
    }
    partitions_[protocolIndex(protocol)].contact_delta_count = 0U;
    return true;
}

bool SdProtocolPeerRepository::appendPeerDelta(
    const storage_v2::PeerProjection& projection)
{
    const MeshProtocol protocol = normalizeProtocol(
        projection.record.identity.protocol);
    const std::size_t slot_size = storage_v2::peerSlotSize(protocol);
    if (slot_size == 0U || slot_size > slot_scratch_.size() ||
        !storage_v2::encodePeerSlot(protocol,
                                    projection,
                                    slot_scratch_.data(),
                                    slot_size))
    {
        return false;
    }
    char path[96] = {};
    buildProtocolPath(protocol, "peers.delta", path, sizeof(path));
    if (!journal_.append(path,
                         protocol,
                         storage_v2::JournalKind::PeerDelta,
                         slot_size,
                         slot_scratch_.data()))
    {
        return false;
    }
    ++partitions_[protocolIndex(protocol)].peer_delta_count;
    return true;
}

bool SdProtocolPeerRepository::appendContactDelta(
    const storage_v2::ContactProjection& projection)
{
    const MeshProtocol protocol = normalizeProtocol(projection.identity.protocol);
    const std::size_t slot_size = storage_v2::contactSlotSize(protocol);
    if (slot_size == 0U || slot_size > slot_scratch_.size() ||
        !storage_v2::encodeContactSlot(protocol,
                                       projection,
                                       slot_scratch_.data(),
                                       slot_size))
    {
        return false;
    }
    char path[96] = {};
    buildProtocolPath(protocol, "contacts.delta", path, sizeof(path));
    if (!journal_.append(path,
                         protocol,
                         storage_v2::JournalKind::ContactDelta,
                         slot_size,
                         slot_scratch_.data()))
    {
        return false;
    }
    ++partitions_[protocolIndex(protocol)].contact_delta_count;
    return true;
}

bool SdProtocolPeerRepository::queueOrAppendPeerDelta(
    const storage_v2::PeerProjection& projection)
{
    if (pending_peer_head_ < pending_peer_deltas_.size())
    {
        storage_v2::PeerProjection& newest = pending_peer_deltas_.back();
        if (newest.deleted == projection.deleted &&
            sameMeshPeerIdentity(newest.record.identity,
                                 projection.record.identity))
        {
            newest = projection;
            return false;
        }
        pending_peer_deltas_.push_back(projection);
        return false;
    }
    if (appendPeerDelta(projection))
    {
        return true;
    }
    pending_peer_deltas_.push_back(projection);
    return false;
}

bool SdProtocolPeerRepository::drainPendingPeerDeltas(std::size_t budget)
{
    std::size_t drained = 0U;
    while (pending_peer_head_ < pending_peer_deltas_.size() &&
           drained < budget)
    {
        if (!appendPeerDelta(pending_peer_deltas_[pending_peer_head_]))
        {
            return false;
        }
        ++pending_peer_head_;
        ++drained;
    }
    if (pending_peer_head_ == pending_peer_deltas_.size())
    {
        pending_peer_deltas_.clear();
        pending_peer_head_ = 0U;
        return true;
    }
    return false;
}

bool SdProtocolPeerRepository::applyPeerProjection(
    const storage_v2::PeerProjection& projection)
{
    if (!meshPeerRecordIsValid(projection.record))
    {
        return false;
    }
    const std::size_t index = findPeerIndex(projection.record.identity);
    if (projection.deleted)
    {
        if (index < peers_.size())
        {
            peers_.erase(peers_.begin() + static_cast<std::ptrdiff_t>(index));
        }
        return true;
    }
    if (index < peers_.size())
    {
        peers_[index] = projection.record;
    }
    else
    {
        peers_.push_back(projection.record);
    }
    return true;
}

bool SdProtocolPeerRepository::applyContactProjection(
    const storage_v2::ContactProjection& projection)
{
    if (!meshPeerIdentityIsValid(projection.identity))
    {
        return false;
    }
    const std::size_t index = findContactIndex(projection.identity);
    if (projection.deleted)
    {
        if (index < contacts_.size())
        {
            contacts_.erase(contacts_.begin() +
                            static_cast<std::ptrdiff_t>(index));
        }
        return true;
    }
    if (index < contacts_.size())
    {
        contacts_[index] = projection;
    }
    else
    {
        contacts_.push_back(projection);
    }
    return true;
}

void SdProtocolPeerRepository::overlayContactFacts()
{
    for (MeshPeerRecord& peer : peers_)
    {
        peer.flags = MeshPeerUserFlags{};
        peer.user_alias[0] = '\0';
    }
    for (const storage_v2::ContactProjection& contact : contacts_)
    {
        std::size_t peer_index = findPeerIndex(contact.identity);
        if (peer_index >= peers_.size())
        {
            MeshPeerRecord placeholder{};
            placeholder.valid = true;
            placeholder.identity = contact.identity;
            placeholder.source = MeshPeerSource::Manual;
            if (placeholder.identity.protocol == MeshProtocol::MeshCore)
            {
                placeholder.meshcore.has_public_key = true;
                placeholder.meshcore.node_id_hint = contact.node_id_hint;
                std::memcpy(placeholder.meshcore.public_key,
                            placeholder.identity.public_key,
                            kMeshPeerMeshCorePublicKeyLen);
            }
            else if (placeholder.identity.protocol == MeshProtocol::Reticulum)
            {
                placeholder.reticulum.identity =
                    placeholder.identity.reticulum;
            }
            peers_.push_back(placeholder);
            peer_index = peers_.size() - 1U;
        }
        MeshPeerRecord& peer = peers_[peer_index];
        peer.flags = contact.flags;
        copyMeshPeerText(peer.user_alias,
                         sizeof(peer.user_alias),
                         contact.alias);
        if (peer.identity.protocol == MeshProtocol::MeshCore &&
            peer.meshcore.node_id_hint == 0U)
        {
            peer.meshcore.node_id_hint = contact.node_id_hint;
        }
    }
}

void SdProtocolPeerRepository::overlayContactFactsForPeer(
    MeshPeerRecord& peer) const
{
    peer.flags = MeshPeerUserFlags{};
    peer.user_alias[0] = '\0';
    MeshPeerIdentity stable{};
    if (!stableContactIdentity(peer, stable))
    {
        return;
    }
    const std::size_t contact_index = findContactIndex(stable);
    if (contact_index >= contacts_.size())
    {
        return;
    }
    const storage_v2::ContactProjection& contact = contacts_[contact_index];
    peer.flags = contact.flags;
    copyMeshPeerText(peer.user_alias,
                     sizeof(peer.user_alias),
                     contact.alias);
    if (peer.identity.protocol == MeshProtocol::MeshCore &&
        peer.meshcore.node_id_hint == 0U)
    {
        peer.meshcore.node_id_hint = contact.node_id_hint;
    }
}

void SdProtocolPeerRepository::reconcileStableIdentities(MeshProtocol protocol)
{
    protocol = normalizeProtocol(protocol);
    for (std::size_t stable_index = 0U; stable_index < peers_.size();
         ++stable_index)
    {
        MeshPeerRecord& stable = peers_[stable_index];
        if (!meshPeerSameProtocol(stable.identity.protocol, protocol) ||
            stable.identity.kind == MeshPeerIdentityKind::NodeId)
        {
            continue;
        }
        const NodeId node_id = projectedNodeId(stable);
        if (node_id == 0U)
        {
            continue;
        }
        for (std::size_t unresolved_index = 0U;
             unresolved_index < peers_.size(); ++unresolved_index)
        {
            if (unresolved_index == stable_index)
            {
                continue;
            }
            const MeshPeerRecord& unresolved = peers_[unresolved_index];
            if (!meshPeerSameProtocol(unresolved.identity.protocol, protocol) ||
                unresolved.identity.kind != MeshPeerIdentityKind::NodeId ||
                unresolved.identity.node_id != node_id)
            {
                continue;
            }
            MeshPeerRecord merged = mergeMeshPeerRecordFacts(unresolved, stable);
            merged.identity = stable.identity;
            peers_[stable_index] = merged;
            peers_.erase(peers_.begin() +
                         static_cast<std::ptrdiff_t>(unresolved_index));
            if (unresolved_index < stable_index)
            {
                --stable_index;
            }
            break;
        }
    }
}

MeshPeerDirectoryStatus SdProtocolPeerRepository::record(
    const MeshPeerRecord& input)
{
    ScopedRepositoryLock lock(mutex_);
    if (!lock.locked() || !begun_ || !meshPeerRecordIsValid(input))
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::InvalidArgument);
    }

    MeshPeerRecord incoming = input;
    incoming.identity.protocol = normalizeProtocol(incoming.identity.protocol);
    if (incoming.identity.protocol == MeshProtocol::Reticulum &&
        incoming.identity.kind == MeshPeerIdentityKind::ReticulumDestination)
    {
        incoming.reticulum.identity = incoming.identity.reticulum;
    }
    const std::size_t exact_index = findPeerIndex(incoming.identity);
    std::size_t merge_index = exact_index;
    if (merge_index >= peers_.size() &&
        incoming.identity.kind != MeshPeerIdentityKind::NodeId)
    {
        const NodeId node_id = projectedNodeId(incoming);
        if (node_id != 0U)
        {
            merge_index = findPeerIndexByNodeId(incoming.identity.protocol,
                                                node_id);
        }
    }

    if (merge_index >= peers_.size() &&
        ephemeralCount(incoming.identity.protocol) >= kEphemeralPeerCapacity &&
        !evictOldestEphemeral(incoming.identity.protocol))
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::CapacityExceeded);
    }

    MeshPeerRecord next = incoming;
    MeshPeerIdentity replaced_identity{};
    bool identity_upgrade = false;
    if (merge_index < peers_.size())
    {
        replaced_identity = peers_[merge_index].identity;
        next = mergeMeshPeerRecordFacts(peers_[merge_index], incoming);
        if (!sameMeshPeerIdentity(replaced_identity, incoming.identity) &&
            incoming.identity.kind != MeshPeerIdentityKind::NodeId)
        {
            next.identity = incoming.identity;
            identity_upgrade = true;
        }
    }
    next.valid = true;
    next.identity.protocol = normalizeProtocol(next.identity.protocol);
    if (next.last_seen_s < next.first_seen_s)
    {
        next.last_seen_s = next.first_seen_s;
    }

    const storage_v2::PeerProjection next_projection{next, false};
    const bool immediately_durable = queueOrAppendPeerDelta(next_projection);
    if (identity_upgrade)
    {
        storage_v2::PeerProjection tombstone{};
        tombstone.record = peers_[merge_index];
        tombstone.deleted = true;
        (void)queueOrAppendPeerDelta(tombstone);
    }

    if (merge_index < peers_.size())
    {
        peers_[merge_index] = next;
    }
    else
    {
        peers_.push_back(next);
    }
    if (merge_index < peers_.size())
    {
        overlayContactFactsForPeer(peers_[merge_index]);
    }
    active_node_projection_dirty_ = true;
    if (!immediately_durable)
    {
        Serial.printf("[PeerStoreV2] peer queued protocol=%s pending=%u\n",
                      protocolSlug(next.identity.protocol),
                      static_cast<unsigned>(pending_peer_deltas_.size() -
                                            pending_peer_head_));
    }
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus SdProtocolPeerRepository::find(
    const MeshPeerIdentity& identity,
    MeshPeerRecord& out_record)
{
    ScopedRepositoryLock lock(mutex_);
    if (!lock.locked())
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::StorageUnavailable);
    }
    const std::size_t index = findPeerIndex(identity);
    if (index >= peers_.size())
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::NotFound);
    }
    out_record = peers_[index];
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus SdProtocolPeerRepository::findByNodeId(
    MeshProtocol protocol,
    NodeId node_id,
    MeshPeerRecord& out_record)
{
    ScopedRepositoryLock lock(mutex_);
    if (!lock.locked() || node_id == 0U)
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::InvalidArgument);
    }
    const std::size_t index = findPeerIndexByNodeId(protocol, node_id);
    if (index >= peers_.size())
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::NotFound);
    }
    out_record = peers_[index];
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus SdProtocolPeerRepository::loadRecent(
    MeshProtocol protocol,
    MeshPeerRecord* out_records,
    std::size_t max_records,
    std::size_t* out_count)
{
    ScopedRepositoryLock lock(mutex_);
    if (!lock.locked() || !out_count || (!out_records && max_records > 0U))
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::InvalidArgument);
    }
    protocol = normalizeProtocol(protocol);
    using PeerPtrVector = std::vector<
        const MeshPeerRecord*,
        ::platform::esp::arduino_common::memory::PsramAllocator<
            const MeshPeerRecord*>>;
    PeerPtrVector matches;
    matches.reserve(peers_.size());
    for (const MeshPeerRecord& peer : peers_)
    {
        if (meshPeerSameProtocol(peer.identity.protocol, protocol))
        {
            matches.push_back(&peer);
        }
    }
    std::sort(matches.begin(),
              matches.end(),
              [](const MeshPeerRecord* lhs, const MeshPeerRecord* rhs)
              {
                  return lhs->last_seen_s > rhs->last_seen_s;
              });
    *out_count = std::min(max_records, matches.size());
    for (std::size_t index = 0U; index < *out_count; ++index)
    {
        out_records[index] = *matches[index];
    }
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus SdProtocolPeerRepository::search(
    MeshProtocol protocol,
    const char* query,
    MeshPeerRecord* out_records,
    std::size_t max_records,
    std::size_t* out_count)
{
    ScopedRepositoryLock lock(mutex_);
    if (!lock.locked() || !query || !out_count ||
        (!out_records && max_records > 0U))
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::InvalidArgument);
    }
    protocol = normalizeProtocol(protocol);
    using PeerPtrVector = std::vector<
        const MeshPeerRecord*,
        ::platform::esp::arduino_common::memory::PsramAllocator<
            const MeshPeerRecord*>>;
    PeerPtrVector matches;
    matches.reserve(peers_.size());
    for (const MeshPeerRecord& peer : peers_)
    {
        const MeshPeerNodeFacts* facts = nodeFacts(peer);
        if (!meshPeerSameProtocol(peer.identity.protocol, protocol) ||
            (!textContains(peer.user_alias, query) &&
             !textContains(peer.display_name, query) &&
             (!facts ||
              (!textContains(facts->short_name, query) &&
               !textContains(facts->long_name, query)))))
        {
            continue;
        }
        matches.push_back(&peer);
    }
    std::sort(matches.begin(),
              matches.end(),
              [](const MeshPeerRecord* lhs, const MeshPeerRecord* rhs)
              {
                  return lhs->last_seen_s > rhs->last_seen_s;
              });
    *out_count = std::min(max_records, matches.size());
    for (std::size_t index = 0U; index < *out_count; ++index)
    {
        out_records[index] = *matches[index];
    }
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus SdProtocolPeerRepository::setUserFlags(
    const MeshPeerIdentity& identity,
    const MeshPeerUserFlags& flags)
{
    ScopedRepositoryLock lock(mutex_);
    if (!lock.locked())
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::StorageUnavailable);
    }
    const std::size_t peer_index = findPeerIndex(identity);
    if (peer_index >= peers_.size())
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::NotFound);
    }
    MeshPeerIdentity stable{};
    if (!stableContactIdentity(peers_[peer_index], stable))
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::Unsupported);
    }
    const bool remove_projection = !flags.favorite && !flags.ignored &&
                                   !flags.trusted &&
                                   peers_[peer_index].user_alias[0] == '\0';
    if (!persistContactFacts(stable,
                             flags,
                             peers_[peer_index].user_alias,
                             remove_projection))
    {
        return MeshPeerDirectoryStatus::fail(MeshPeerDirectoryStatusCode::IoError);
    }
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus SdProtocolPeerRepository::remove(
    const MeshPeerIdentity& identity)
{
    ScopedRepositoryLock lock(mutex_);
    if (!lock.locked())
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::StorageUnavailable);
    }
    const std::size_t index = findPeerIndex(identity);
    if (index >= peers_.size())
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::NotFound);
    }
    if (peerIsProtected(peers_[index]))
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::Unsupported);
    }
    storage_v2::PeerProjection tombstone{};
    tombstone.record = peers_[index];
    tombstone.deleted = true;
    (void)queueOrAppendPeerDelta(tombstone);
    peers_.erase(peers_.begin() + static_cast<std::ptrdiff_t>(index));
    active_node_projection_dirty_ = true;
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryStatus SdProtocolPeerRepository::clearProtocol(
    MeshProtocol protocol)
{
    ScopedRepositoryLock lock(mutex_);
    if (!lock.locked() || !begun_)
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::StorageUnavailable);
    }
    protocol = normalizeProtocol(protocol);
    peers_.erase(std::remove_if(peers_.begin(),
                                peers_.end(),
                                [protocol](const MeshPeerRecord& peer)
                                {
                                    return meshPeerSameProtocol(
                                        peer.identity.protocol,
                                        protocol);
                                }),
                 peers_.end());
    if (pending_peer_head_ > 0U)
    {
        pending_peer_deltas_.erase(
            pending_peer_deltas_.begin(),
            pending_peer_deltas_.begin() +
                static_cast<std::ptrdiff_t>(pending_peer_head_));
        pending_peer_head_ = 0U;
    }
    pending_peer_deltas_.erase(
        std::remove_if(pending_peer_deltas_.begin(),
                       pending_peer_deltas_.end(),
                       [protocol](const storage_v2::PeerProjection& projection)
                       {
                           return meshPeerSameProtocol(
                               projection.record.identity.protocol,
                               protocol);
                       }),
        pending_peer_deltas_.end());
    overlayContactFacts();

    if (!rewritePeerSnapshot(protocol))
    {
        return MeshPeerDirectoryStatus::fail(MeshPeerDirectoryStatusCode::IoError);
    }
    active_node_projection_dirty_ = true;
    return MeshPeerDirectoryStatus::success();
}

MeshPeerDirectoryCapacity SdProtocolPeerRepository::capacityFor(
    MeshProtocol protocol) const
{
    const std::size_t index = protocolIndex(protocol);
    return MeshPeerDirectoryCapacity{kEphemeralPeerCapacity,
                                     kPeerHotCacheCapacity[index]};
}

MeshPeerDirectoryStatus SdProtocolPeerRepository::flush()
{
    ScopedRepositoryLock lock(mutex_);
    if (!lock.locked() || !begun_)
    {
        return MeshPeerDirectoryStatus::fail(
            MeshPeerDirectoryStatusCode::StorageUnavailable);
    }
    return drainPendingPeerDeltas(kPendingFlushBudget)
               ? MeshPeerDirectoryStatus::success()
               : MeshPeerDirectoryStatus::fail(
                     MeshPeerDirectoryStatusCode::StorageUnavailable);
}

std::size_t SdProtocolPeerRepository::findPeerIndex(
    const MeshPeerIdentity& identity) const
{
    for (std::size_t index = 0U; index < peers_.size(); ++index)
    {
        if (sameMeshPeerIdentity(peers_[index].identity, identity))
        {
            return index;
        }
    }
    return peers_.size();
}

std::size_t SdProtocolPeerRepository::findPeerIndexByNodeId(
    MeshProtocol protocol,
    NodeId node_id) const
{
    protocol = normalizeProtocol(protocol);
    for (std::size_t index = 0U; index < peers_.size(); ++index)
    {
        const MeshPeerRecord& peer = peers_[index];
        if (meshPeerSameProtocol(peer.identity.protocol, protocol) &&
            projectedNodeId(peer) == node_id)
        {
            return index;
        }
    }
    return peers_.size();
}

std::size_t SdProtocolPeerRepository::findContactIndex(
    const MeshPeerIdentity& identity) const
{
    for (std::size_t index = 0U; index < contacts_.size(); ++index)
    {
        if (sameMeshPeerIdentity(contacts_[index].identity, identity))
        {
            return index;
        }
    }
    return contacts_.size();
}

bool SdProtocolPeerRepository::stableContactIdentity(
    const MeshPeerRecord& peer,
    MeshPeerIdentity& out_identity) const
{
    const MeshProtocol protocol = normalizeProtocol(peer.identity.protocol);
    if ((protocol == MeshProtocol::Meshtastic &&
         peer.identity.kind == MeshPeerIdentityKind::NodeId) ||
        (protocol == MeshProtocol::MeshCore &&
         peer.identity.kind == MeshPeerIdentityKind::PublicKey) ||
        (protocol == MeshProtocol::Reticulum &&
         peer.identity.kind == MeshPeerIdentityKind::ReticulumDestination))
    {
        out_identity = peer.identity;
        out_identity.protocol = protocol;
        return true;
    }
    out_identity = MeshPeerIdentity{};
    return false;
}

NodeId SdProtocolPeerRepository::projectedNodeId(
    const MeshPeerRecord& peer) const
{
    const MeshProtocol protocol = normalizeProtocol(peer.identity.protocol);
    if (peer.identity.kind == MeshPeerIdentityKind::NodeId)
    {
        return peer.identity.node_id;
    }
    if (protocol == MeshProtocol::MeshCore)
    {
        return peer.meshcore.node_id_hint;
    }
    if (protocol == MeshProtocol::Reticulum &&
        peer.identity.kind == MeshPeerIdentityKind::ReticulumDestination)
    {
        return reticulumNodeId(peer.identity.reticulum);
    }
    return 0U;
}

bool SdProtocolPeerRepository::peerIsProtected(
    const MeshPeerRecord& peer) const
{
    MeshPeerIdentity stable{};
    if (stableContactIdentity(peer, stable) &&
        findContactIndex(stable) < contacts_.size())
    {
        return true;
    }
    return peerReferencedByConversation(peer);
}

bool SdProtocolPeerRepository::peerReferencedByConversation(
    const MeshPeerRecord& peer) const
{
    const MeshProtocol protocol = normalizeProtocol(peer.identity.protocol);
    const std::vector<ConversationMeta> conversations =
        chat_store_.loadConversationPageForProtocol(protocol, 0U, 0U, nullptr);
    return peerReferencedByConversations(peer, conversations);
}

bool SdProtocolPeerRepository::peerReferencedByConversations(
    const MeshPeerRecord& peer,
    const std::vector<ConversationMeta>& conversations) const
{
    const MeshProtocol protocol = normalizeProtocol(peer.identity.protocol);
    const NodeId node_id = projectedNodeId(peer);
    for (const ConversationMeta& conversation : conversations)
    {
        if (normalizeProtocol(conversation.id.protocol) != protocol)
        {
            continue;
        }
        if (protocol == MeshProtocol::Reticulum &&
            peer.identity.kind == MeshPeerIdentityKind::ReticulumDestination &&
            hasReticulumDestinationIdentity(
                conversation.id.reticulum_identity) &&
            sameReticulumDestinationHash(
                conversation.id.reticulum_identity,
                peer.identity.reticulum))
        {
            return true;
        }
        if (node_id != 0U && conversation.id.peer == node_id)
        {
            return true;
        }
    }
    return false;
}

std::size_t SdProtocolPeerRepository::ephemeralCount(
    MeshProtocol protocol) const
{
    protocol = normalizeProtocol(protocol);
    const std::vector<ConversationMeta> conversations =
        chat_store_.loadConversationPageForProtocol(protocol, 0U, 0U, nullptr);
    std::size_t count = 0U;
    for (const MeshPeerRecord& peer : peers_)
    {
        MeshPeerIdentity stable{};
        const bool is_contact =
            stableContactIdentity(peer, stable) &&
            findContactIndex(stable) < contacts_.size();
        if (meshPeerSameProtocol(peer.identity.protocol, protocol) &&
            !is_contact &&
            !peerReferencedByConversations(peer, conversations))
        {
            ++count;
        }
    }
    return count;
}

bool SdProtocolPeerRepository::evictOldestEphemeral(MeshProtocol protocol)
{
    protocol = normalizeProtocol(protocol);
    const std::vector<ConversationMeta> conversations =
        chat_store_.loadConversationPageForProtocol(protocol, 0U, 0U, nullptr);
    std::size_t candidate = peers_.size();
    uint32_t oldest_seen = UINT32_MAX;
    for (std::size_t index = 0U; index < peers_.size(); ++index)
    {
        const MeshPeerRecord& peer = peers_[index];
        MeshPeerIdentity stable{};
        const bool is_contact =
            stableContactIdentity(peer, stable) &&
            findContactIndex(stable) < contacts_.size();
        if (!meshPeerSameProtocol(peer.identity.protocol, protocol) ||
            is_contact ||
            peerReferencedByConversations(peer, conversations))
        {
            continue;
        }
        if (candidate >= peers_.size() || peer.last_seen_s < oldest_seen)
        {
            candidate = index;
            oldest_seen = peer.last_seen_s;
        }
    }
    if (candidate >= peers_.size())
    {
        return false;
    }
    storage_v2::PeerProjection tombstone{};
    tombstone.record = peers_[candidate];
    tombstone.deleted = true;
    (void)queueOrAppendPeerDelta(tombstone);
    peers_.erase(peers_.begin() + static_cast<std::ptrdiff_t>(candidate));
    active_node_projection_dirty_ = true;
    return true;
}

contacts::INodeStore& SdProtocolPeerRepository::nodeStoreView()
{
    return node_store_view_;
}

contacts::IContactStore& SdProtocolPeerRepository::contactStoreView()
{
    return contact_store_view_;
}

SdProtocolPeerRepository::NodeStoreView::NodeStoreView(
    SdProtocolPeerRepository& owner)
    : owner_(owner)
{
}

void SdProtocolPeerRepository::NodeStoreView::setActiveProtocol(
    MeshProtocol protocol)
{
    ScopedRepositoryLock lock(owner_.mutex_);
    if (!lock.locked())
    {
        return;
    }
    owner_.active_protocol_ = normalizeProtocol(protocol);
    owner_.active_node_projection_dirty_ = true;
}

void SdProtocolPeerRepository::NodeStoreView::begin()
{
    (void)owner_.begin();
}

void SdProtocolPeerRepository::NodeStoreView::applyUpdate(
    uint32_t node_id,
    const contacts::NodeUpdate& update)
{
    owner_.applyNodeUpdate(node_id, update);
}

void SdProtocolPeerRepository::NodeStoreView::upsert(
    uint32_t node_id,
    const char* short_name,
    const char* long_name,
    uint32_t now_secs,
    float snr,
    float rssi,
    uint8_t protocol,
    uint8_t role,
    uint8_t hops_away,
    uint8_t hw_model,
    uint8_t channel)
{
    contacts::NodeUpdate update{};
    update.short_name = short_name;
    update.long_name = long_name;
    update.has_last_seen = true;
    update.last_seen = now_secs;
    update.has_snr = !std::isnan(snr);
    update.snr = snr;
    update.has_rssi = !std::isnan(rssi);
    update.rssi = rssi;
    update.has_protocol = protocol != 0U;
    update.protocol = protocol;
    update.has_role = role != contacts::kNodeRoleUnknown;
    update.role = role;
    update.has_hops_away = hops_away != 0xFFU;
    update.hops_away = hops_away;
    update.has_hw_model = hw_model != 0U;
    update.hw_model = hw_model;
    update.has_channel = channel != 0xFFU;
    update.channel = channel;
    owner_.applyNodeUpdate(node_id, update);
}

void SdProtocolPeerRepository::NodeStoreView::updateProtocol(
    uint32_t node_id,
    uint8_t protocol,
    uint32_t now_secs)
{
    contacts::NodeUpdate update{};
    update.has_protocol = protocol != 0U;
    update.protocol = protocol;
    update.has_last_seen = true;
    update.last_seen = now_secs;
    owner_.applyNodeUpdate(node_id, update);
}

void SdProtocolPeerRepository::NodeStoreView::updatePosition(
    uint32_t node_id,
    const contacts::NodePosition& position)
{
    contacts::NodeUpdate update{};
    update.has_position = true;
    update.position = position;
    owner_.applyNodeUpdate(node_id, update);
}

bool SdProtocolPeerRepository::NodeStoreView::remove(uint32_t node_id)
{
    return owner_.removeNode(node_id);
}

const std::vector<contacts::NodeEntry>&
SdProtocolPeerRepository::NodeStoreView::getEntries() const
{
    return owner_.activeNodeEntries();
}

void SdProtocolPeerRepository::NodeStoreView::clear()
{
    owner_.clearActiveNodes();
}

bool SdProtocolPeerRepository::NodeStoreView::flush()
{
    return owner_.flush().succeeded();
}

void SdProtocolPeerRepository::applyNodeUpdate(
    uint32_t node_id,
    const contacts::NodeUpdate& update)
{
    if (node_id == 0U)
    {
        return;
    }
    ScopedRepositoryLock lock(mutex_);
    if (!lock.locked() || !begun_)
    {
        return;
    }
    MeshProtocol protocol = active_protocol_;
    if (update.has_protocol)
    {
        const MeshProtocol requested =
            static_cast<MeshProtocol>(update.protocol);
        if (requested == MeshProtocol::Meshtastic ||
            requested == MeshProtocol::MeshCore ||
            requested == MeshProtocol::Reticulum ||
            requested == MeshProtocol::RNode)
        {
            protocol = normalizeProtocol(requested);
        }
    }

    std::size_t existing_index = findPeerIndexByNodeId(protocol, node_id);
    MeshPeerRecord incoming{};
    incoming.valid = true;
    incoming.source = MeshPeerSource::RuntimeRx;
    if (protocol == MeshProtocol::Reticulum &&
        hasReticulumDestinationIdentity(update.reticulum_identity))
    {
        incoming.identity = makeMeshPeerReticulumIdentity(
            update.reticulum_identity);
    }
    else if (existing_index < peers_.size())
    {
        incoming.identity = peers_[existing_index].identity;
    }
    else
    {
        incoming.identity = makeMeshPeerNodeIdentity(protocol, node_id);
    }
    incoming.identity.protocol = protocol;
    incoming.first_seen_s = update.has_last_seen ? update.last_seen : 0U;
    incoming.last_seen_s = update.has_last_seen ? update.last_seen : 0U;
    if (hasText(update.long_name))
    {
        copyMeshPeerText(incoming.display_name,
                         sizeof(incoming.display_name),
                         update.long_name);
    }
    else if (hasText(update.short_name))
    {
        copyMeshPeerText(incoming.display_name,
                         sizeof(incoming.display_name),
                         update.short_name);
    }
    if (update.has_snr)
    {
        incoming.observations.has_snr = true;
        incoming.observations.snr = update.snr;
    }
    if (update.has_rssi)
    {
        incoming.observations.has_rssi = true;
        incoming.observations.rssi = update.rssi;
    }
    if (update.has_device_metrics)
    {
        incoming.observations.has_device_metrics = true;
        incoming.observations.device_metrics = update.device_metrics;
    }
    if (update.has_position)
    {
        incoming.observations.has_position = true;
        incoming.observations.position = update.position;
    }

    if (protocol == MeshProtocol::Reticulum)
    {
        incoming.reticulum.identity = incoming.identity.reticulum;
    }
    else
    {
        MeshPeerNodeFacts& facts = mutableNodeFacts(incoming);
        copyNodeText(facts.short_name,
                     sizeof(facts.short_name),
                     update.short_name);
        copyNodeText(facts.long_name,
                     sizeof(facts.long_name),
                     update.long_name);
        if (update.has_role)
        {
            facts.role = update.role;
        }
        if (update.has_hw_model)
        {
            facts.hw_model = update.hw_model;
        }
        if (update.has_channel)
        {
            facts.channel = update.channel;
        }
        if (update.has_hops_away)
        {
            facts.hops_away = update.hops_away;
        }
        if (update.has_macaddr)
        {
            facts.has_macaddr = true;
            std::memcpy(facts.macaddr,
                        update.macaddr,
                        sizeof(facts.macaddr));
        }
        if (update.has_via_mqtt)
        {
            facts.via_mqtt = update.via_mqtt;
        }
        if (update.has_next_hop)
        {
            if (protocol == MeshProtocol::Meshtastic)
            {
                incoming.meshtastic.has_next_hop = true;
                incoming.meshtastic.next_hop = update.next_hop;
            }
            else
            {
                incoming.meshcore.has_next_hop = true;
                incoming.meshcore.next_hop = update.next_hop;
            }
        }
        if (protocol == MeshProtocol::MeshCore)
        {
            incoming.meshcore.node_id_hint = node_id;
        }
    }

    MeshPeerRecord next = incoming;
    bool identity_upgrade = false;
    if (existing_index < peers_.size())
    {
        next = mergeMeshPeerRecordFacts(peers_[existing_index], incoming);
        if (!sameMeshPeerIdentity(peers_[existing_index].identity,
                                  incoming.identity) &&
            incoming.identity.kind != MeshPeerIdentityKind::NodeId)
        {
            next.identity = incoming.identity;
            identity_upgrade = true;
        }
        if (update.has_key_manually_verified)
        {
            if (protocol == MeshProtocol::Meshtastic &&
                next.meshtastic.has_public_key)
            {
                next.meshtastic.key_manually_verified =
                    update.key_manually_verified;
            }
            else if (protocol == MeshProtocol::MeshCore &&
                     next.meshcore.has_public_key)
            {
                next.meshcore.public_key_verified =
                    update.key_manually_verified;
            }
        }
    }
    const storage_v2::PeerProjection projection{next, false};
    (void)queueOrAppendPeerDelta(projection);
    if (identity_upgrade)
    {
        storage_v2::PeerProjection tombstone{};
        tombstone.record = peers_[existing_index];
        tombstone.deleted = true;
        (void)queueOrAppendPeerDelta(tombstone);
    }
    if (existing_index < peers_.size())
    {
        peers_[existing_index] = next;
    }
    else
    {
        peers_.push_back(next);
        existing_index = peers_.size() - 1U;
    }

    if (update.has_is_ignored &&
        (update.is_ignored ||
         findContactIndex(peers_[existing_index].identity) < contacts_.size()))
    {
        MeshPeerIdentity stable{};
        if (stableContactIdentity(peers_[existing_index], stable))
        {
            MeshPeerUserFlags flags = peers_[existing_index].flags;
            flags.ignored = update.is_ignored;
            const bool remove_projection = !flags.favorite && !flags.ignored &&
                                           !flags.trusted &&
                                           peers_[existing_index]
                                                   .user_alias[0] == '\0';
            (void)persistContactFacts(
                stable,
                flags,
                peers_[existing_index].user_alias,
                remove_projection);
        }
    }
    overlayContactFactsForPeer(peers_[existing_index]);
    active_node_projection_dirty_ = true;
}

bool SdProtocolPeerRepository::removeNode(uint32_t node_id)
{
    ScopedRepositoryLock lock(mutex_);
    if (!lock.locked() || node_id == 0U)
    {
        return false;
    }
    const std::size_t index = findPeerIndexByNodeId(active_protocol_, node_id);
    if (index >= peers_.size() || peerIsProtected(peers_[index]))
    {
        return false;
    }
    storage_v2::PeerProjection tombstone{};
    tombstone.record = peers_[index];
    tombstone.deleted = true;
    (void)queueOrAppendPeerDelta(tombstone);
    peers_.erase(peers_.begin() + static_cast<std::ptrdiff_t>(index));
    active_node_projection_dirty_ = true;
    return true;
}

void SdProtocolPeerRepository::clearActiveNodes()
{
    (void)clearProtocol(active_protocol_);
}

const std::vector<contacts::NodeEntry>&
SdProtocolPeerRepository::activeNodeEntries() const
{
    ScopedRepositoryLock lock(mutex_);
    if (lock.locked() && active_node_projection_dirty_)
    {
        rebuildActiveNodeProjection();
    }
    return active_node_projection_;
}

void SdProtocolPeerRepository::rebuildActiveNodeProjection() const
{
    active_node_projection_.clear();
    active_node_projection_.reserve(
        std::count_if(peers_.begin(),
                      peers_.end(),
                      [this](const MeshPeerRecord& peer)
                      {
                          return meshPeerSameProtocol(
                              peer.identity.protocol,
                              active_protocol_);
                      }));
    for (const MeshPeerRecord& peer : peers_)
    {
        contacts::NodeEntry entry{};
        if (meshPeerSameProtocol(peer.identity.protocol, active_protocol_) &&
            toNodeEntry(peer, entry))
        {
            active_node_projection_.push_back(entry);
        }
    }
    active_node_projection_dirty_ = false;
}

bool SdProtocolPeerRepository::toNodeEntry(
    const MeshPeerRecord& peer,
    contacts::NodeEntry& out) const
{
    const NodeId node_id = projectedNodeId(peer);
    if (node_id == 0U)
    {
        return false;
    }
    out = contacts::NodeEntry{};
    out.node_id = node_id;
    out.last_seen = peer.last_seen_s;
    out.protocol = static_cast<uint8_t>(normalizeProtocol(
        peer.identity.protocol));
    const MeshPeerNodeFacts* facts = nodeFacts(peer);
    if (facts)
    {
        copyNodeText(out.short_name,
                     sizeof(out.short_name),
                     facts->short_name);
        copyNodeText(out.long_name,
                     sizeof(out.long_name),
                     facts->long_name);
        out.role = facts->role;
        out.hw_model = facts->hw_model;
        out.channel = facts->channel;
        out.hops_away = facts->hops_away;
        out.has_macaddr = facts->has_macaddr;
        std::memcpy(out.macaddr, facts->macaddr, sizeof(out.macaddr));
        out.via_mqtt = facts->via_mqtt;
    }
    else
    {
        copyNodeText(out.long_name,
                     sizeof(out.long_name),
                     peer.display_name);
        out.role = static_cast<uint8_t>(contacts::NodeRoleType::Client);
        out.hops_away = 0U;
    }
    if (out.long_name[0] == '\0')
    {
        copyNodeText(out.long_name,
                     sizeof(out.long_name),
                     peer.display_name);
    }
    out.snr = peer.observations.has_snr ? peer.observations.snr : 0.0F;
    out.rssi = peer.observations.has_rssi ? peer.observations.rssi : 0.0F;
    out.is_ignored = peer.flags.ignored;
    if (peer.identity.protocol == MeshProtocol::Meshtastic)
    {
        out.next_hop = peer.meshtastic.has_next_hop
                           ? peer.meshtastic.next_hop
                           : 0U;
        out.has_public_key = peer.meshtastic.has_public_key;
        out.key_manually_verified =
            peer.meshtastic.key_manually_verified;
    }
    else if (peer.identity.protocol == MeshProtocol::MeshCore)
    {
        out.next_hop = peer.meshcore.has_next_hop
                           ? peer.meshcore.next_hop
                           : 0U;
        out.has_public_key = peer.meshcore.has_public_key ||
                             peer.identity.kind ==
                                 MeshPeerIdentityKind::PublicKey;
        out.key_manually_verified = peer.meshcore.public_key_verified;
    }
    else if (peer.identity.protocol == MeshProtocol::Reticulum)
    {
        out.has_public_key = peer.reticulum.has_public_keys;
        out.reticulum_identity = peer.identity.reticulum;
    }
    if (peer.observations.has_device_metrics)
    {
        out.has_device_metrics = true;
        out.device_metrics = peer.observations.device_metrics;
    }
    if (peer.observations.has_position)
    {
        const contacts::NodePosition& position =
            peer.observations.position;
        out.position_valid = position.valid;
        out.position_latitude_i = position.latitude_i;
        out.position_longitude_i = position.longitude_i;
        out.position_has_altitude = position.has_altitude;
        out.position_altitude = position.altitude;
        out.position_timestamp = position.timestamp;
        out.position_precision_bits = position.precision_bits;
        out.position_pdop = position.pdop;
        out.position_hdop = position.hdop;
        out.position_vdop = position.vdop;
        out.position_gps_accuracy_mm = position.gps_accuracy_mm;
    }
    return true;
}

SdProtocolPeerRepository::ContactStoreView::ContactStoreView(
    SdProtocolPeerRepository& owner)
    : owner_(owner)
{
}

void SdProtocolPeerRepository::ContactStoreView::setActiveProtocol(
    MeshProtocol protocol)
{
    ScopedRepositoryLock lock(owner_.mutex_);
    if (!lock.locked())
    {
        return;
    }
    owner_.active_protocol_ = normalizeProtocol(protocol);
    owner_.active_node_projection_dirty_ = true;
}

void SdProtocolPeerRepository::ContactStoreView::begin()
{
    (void)owner_.begin();
}

std::string SdProtocolPeerRepository::ContactStoreView::getNickname(
    uint32_t node_id) const
{
    return owner_.nicknameForNode(node_id);
}

bool SdProtocolPeerRepository::ContactStoreView::setNickname(
    uint32_t node_id,
    const char* nickname)
{
    return owner_.setNicknameForNode(node_id, nickname);
}

bool SdProtocolPeerRepository::ContactStoreView::removeNickname(
    uint32_t node_id)
{
    return owner_.removeNicknameForNode(node_id);
}

bool SdProtocolPeerRepository::ContactStoreView::hasNickname(
    const char* nickname) const
{
    return owner_.hasNicknameForActiveProtocol(nickname);
}

std::vector<uint32_t>
SdProtocolPeerRepository::ContactStoreView::getAllContactIds() const
{
    return owner_.activeContactIds();
}

size_t SdProtocolPeerRepository::ContactStoreView::getCount() const
{
    return owner_.activeContactCount();
}

std::string SdProtocolPeerRepository::nicknameForNode(uint32_t node_id) const
{
    ScopedRepositoryLock lock(mutex_);
    if (!lock.locked())
    {
        return {};
    }
    const std::size_t peer_index = findPeerIndexByNodeId(active_protocol_,
                                                         node_id);
    if (peer_index >= peers_.size())
    {
        return {};
    }
    MeshPeerIdentity stable{};
    if (!stableContactIdentity(peers_[peer_index], stable))
    {
        return {};
    }
    const std::size_t contact_index = findContactIndex(stable);
    return contact_index < contacts_.size()
               ? std::string(contacts_[contact_index].alias)
               : std::string();
}

bool SdProtocolPeerRepository::setNicknameForNode(uint32_t node_id,
                                                  const char* nickname)
{
    if (!nickname || nickname[0] == '\0' ||
        std::strlen(nickname) > kMeshPeerUserAliasMaxLen)
    {
        return false;
    }
    ScopedRepositoryLock lock(mutex_);
    if (!lock.locked())
    {
        return false;
    }
    const std::size_t peer_index = findPeerIndexByNodeId(active_protocol_,
                                                         node_id);
    if (peer_index >= peers_.size())
    {
        return false;
    }
    MeshPeerIdentity stable{};
    if (!stableContactIdentity(peers_[peer_index], stable))
    {
        return false;
    }
    MeshPeerUserFlags flags = peers_[peer_index].flags;
    flags.favorite = true;
    return persistContactFacts(stable, flags, nickname, false);
}

bool SdProtocolPeerRepository::removeNicknameForNode(uint32_t node_id)
{
    ScopedRepositoryLock lock(mutex_);
    if (!lock.locked())
    {
        return false;
    }
    const std::size_t peer_index = findPeerIndexByNodeId(active_protocol_,
                                                         node_id);
    if (peer_index >= peers_.size())
    {
        return false;
    }
    MeshPeerIdentity stable{};
    if (!stableContactIdentity(peers_[peer_index], stable))
    {
        return false;
    }
    const std::size_t contact_index = findContactIndex(stable);
    if (contact_index >= contacts_.size())
    {
        return false;
    }
    MeshPeerUserFlags flags = contacts_[contact_index].flags;
    flags.favorite = false;
    const bool deleted = !flags.ignored && !flags.trusted;
    return persistContactFacts(stable, flags, "", deleted);
}

bool SdProtocolPeerRepository::hasNicknameForActiveProtocol(
    const char* nickname) const
{
    if (!nickname || nickname[0] == '\0')
    {
        return false;
    }
    ScopedRepositoryLock lock(mutex_);
    if (!lock.locked())
    {
        return false;
    }
    for (const storage_v2::ContactProjection& contact : contacts_)
    {
        if (meshPeerSameProtocol(contact.identity.protocol,
                                 active_protocol_) &&
            std::strcmp(contact.alias, nickname) == 0)
        {
            return true;
        }
    }
    return false;
}

std::vector<uint32_t> SdProtocolPeerRepository::activeContactIds() const
{
    ScopedRepositoryLock lock(mutex_);
    std::vector<uint32_t> ids;
    if (!lock.locked())
    {
        return ids;
    }
    ids.reserve(contacts_.size());
    for (const storage_v2::ContactProjection& contact : contacts_)
    {
        if (!meshPeerSameProtocol(contact.identity.protocol,
                                  active_protocol_) ||
            (!contact.flags.favorite && contact.alias[0] == '\0'))
        {
            continue;
        }
        const std::size_t peer_index = findPeerIndex(contact.identity);
        if (peer_index < peers_.size())
        {
            const NodeId node_id = projectedNodeId(peers_[peer_index]);
            if (node_id != 0U)
            {
                ids.push_back(node_id);
            }
        }
    }
    return ids;
}

size_t SdProtocolPeerRepository::activeContactCount() const
{
    ScopedRepositoryLock lock(mutex_);
    if (!lock.locked())
    {
        return 0U;
    }
    return static_cast<size_t>(std::count_if(
        contacts_.begin(),
        contacts_.end(),
        [this](const storage_v2::ContactProjection& contact)
        {
            return meshPeerSameProtocol(contact.identity.protocol,
                                        active_protocol_) &&
                   (contact.flags.favorite || contact.alias[0] != '\0');
        }));
}

bool SdProtocolPeerRepository::persistContactFacts(
    const MeshPeerIdentity& identity,
    const MeshPeerUserFlags& flags,
    const char* alias,
    bool deleted)
{
    const std::size_t existing_contact_index = findContactIndex(identity);
    if (!deleted && existing_contact_index >= contacts_.size() &&
        contacts_.size() >= kProtectedContactCapacity)
    {
        Serial.printf("[PeerStoreV2] contact capacity reached count=%u\n",
                      static_cast<unsigned>(contacts_.size()));
        return false;
    }
    storage_v2::ContactProjection projection{};
    projection.identity = identity;
    projection.flags = flags;
    projection.deleted = deleted;
    copyMeshPeerText(projection.alias,
                     sizeof(projection.alias),
                     alias ? alias : "");
    const std::size_t peer_index = findPeerIndex(identity);
    if (peer_index < peers_.size())
    {
        projection.node_id_hint = projectedNodeId(peers_[peer_index]);
    }
    if (!appendContactDelta(projection))
    {
        return false;
    }
    (void)applyContactProjection(projection);
    if (peer_index < peers_.size())
    {
        overlayContactFactsForPeer(peers_[peer_index]);
    }
    active_node_projection_dirty_ = true;
    return true;
}

MeshProtocol SdProtocolPeerRepository::normalizeProtocol(
    MeshProtocol protocol)
{
    return protocol == MeshProtocol::RNode ? MeshProtocol::Reticulum
                                           : protocol;
}

const char* SdProtocolPeerRepository::protocolSlug(MeshProtocol protocol)
{
    switch (normalizeProtocol(protocol))
    {
    case MeshProtocol::Meshtastic:
        return "mt";
    case MeshProtocol::MeshCore:
        return "mc";
    case MeshProtocol::Reticulum:
        return "rt";
    default:
        return "unknown";
    }
}

std::size_t SdProtocolPeerRepository::protocolIndex(MeshProtocol protocol)
{
    switch (normalizeProtocol(protocol))
    {
    case MeshProtocol::MeshCore:
        return 1U;
    case MeshProtocol::Reticulum:
        return 2U;
    case MeshProtocol::Meshtastic:
    default:
        return 0U;
    }
}

NodeId SdProtocolPeerRepository::reticulumNodeId(
    const ReticulumPeerIdentity& identity)
{
    if (!hasReticulumDestinationIdentity(identity))
    {
        return 0U;
    }
    const uint8_t* hash = identity.destination_hash;
    return (static_cast<NodeId>(hash[12]) << 24U) |
           (static_cast<NodeId>(hash[13]) << 16U) |
           (static_cast<NodeId>(hash[14]) << 8U) |
           static_cast<NodeId>(hash[15]);
}

bool SdProtocolPeerRepository::ensureDirectory(const char* path)
{
    return path && path[0] != '\0' &&
           (storage_runtime::sd_exists(path) ||
            storage_runtime::sd_mkdir(path));
}

void SdProtocolPeerRepository::buildProtocolPath(MeshProtocol protocol,
                                                 const char* name,
                                                 char* out,
                                                 std::size_t out_len)
{
    if (!out || out_len == 0U)
    {
        return;
    }
    if (name && name[0] != '\0')
    {
        std::snprintf(out,
                      out_len,
                      "%s/%s/%s",
                      kRoot,
                      protocolSlug(protocol),
                      name);
    }
    else
    {
        std::snprintf(out,
                      out_len,
                      "%s/%s",
                      kRoot,
                      protocolSlug(protocol));
    }
}

bool SdProtocolPeerRepository::lock() const
{
    return mutex_ && xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE;
}

void SdProtocolPeerRepository::unlock() const
{
    if (mutex_)
    {
        xSemaphoreGive(mutex_);
    }
}

} // namespace chat
