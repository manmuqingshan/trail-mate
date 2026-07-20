#pragma once

#include "chat/infra/mesh_peer_directory_core.h"
#include "chat/ports/i_chat_store.h"
#include "chat/ports/i_protocol_peer_repository.h"
#include "platform/esp/arduino_common/chat/infra/store/fixed_slot_journal.h"
#include "platform/esp/arduino_common/chat/infra/store/protocol_peer_codec.h"
#include "platform/esp/arduino_common/memory/psram_allocator.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace chat
{

class SdProtocolPeerRepository final : public IProtocolPeerRepository
{
  public:
    explicit SdProtocolPeerRepository(IChatStore& chat_store);
    ~SdProtocolPeerRepository() override;

    SdProtocolPeerRepository(const SdProtocolPeerRepository&) = delete;
    SdProtocolPeerRepository& operator=(const SdProtocolPeerRepository&) = delete;

    MeshPeerDirectoryStatus begin() override;
    MeshPeerDirectoryStatus record(const MeshPeerRecord& record) override;
    MeshPeerDirectoryStatus find(const MeshPeerIdentity& identity,
                                 MeshPeerRecord& out_record) override;
    MeshPeerDirectoryStatus findByNodeId(MeshProtocol protocol,
                                         NodeId node_id,
                                         MeshPeerRecord& out_record) override;
    MeshPeerDirectoryStatus loadRecent(MeshProtocol protocol,
                                       MeshPeerRecord* out_records,
                                       std::size_t max_records,
                                       std::size_t* out_count) override;
    MeshPeerDirectoryStatus search(MeshProtocol protocol,
                                   const char* query,
                                   MeshPeerRecord* out_records,
                                   std::size_t max_records,
                                   std::size_t* out_count) override;
    MeshPeerDirectoryStatus setUserFlags(
        const MeshPeerIdentity& identity,
        const MeshPeerUserFlags& flags) override;
    MeshPeerDirectoryStatus remove(const MeshPeerIdentity& identity) override;
    MeshPeerDirectoryStatus clearProtocol(MeshProtocol protocol) override;
    MeshPeerDirectoryCapacity capacityFor(MeshProtocol protocol) const override;
    MeshPeerDirectoryStatus flush() override;

    contacts::INodeStore& nodeStoreView() override;
    contacts::IContactStore& contactStoreView() override;

  private:
    using PeerVector = std::vector<
        MeshPeerRecord,
        ::platform::esp::arduino_common::memory::PsramAllocator<MeshPeerRecord>>;
    using ContactVector = std::vector<
        storage::v2::ContactProjection,
        ::platform::esp::arduino_common::memory::PsramAllocator<
            storage::v2::ContactProjection>>;
    using PendingPeerVector = std::vector<
        storage::v2::PeerProjection,
        ::platform::esp::arduino_common::memory::PsramAllocator<
            storage::v2::PeerProjection>>;
    using PsramByteVector = std::vector<
        uint8_t,
        ::platform::esp::arduino_common::memory::PsramAllocator<uint8_t>>;

    struct PartitionState
    {
        uint32_t peer_delta_count = 0;
        uint32_t contact_delta_count = 0;
    };

    class NodeStoreView final : public contacts::INodeStore
    {
      public:
        explicit NodeStoreView(SdProtocolPeerRepository& owner);
        void setActiveProtocol(MeshProtocol protocol) override;
        void begin() override;
        void applyUpdate(uint32_t node_id,
                         const contacts::NodeUpdate& update) override;
        void upsert(uint32_t node_id,
                    const char* short_name,
                    const char* long_name,
                    uint32_t now_secs,
                    float snr,
                    float rssi,
                    uint8_t protocol,
                    uint8_t role,
                    uint8_t hops_away,
                    uint8_t hw_model,
                    uint8_t channel) override;
        void updateProtocol(uint32_t node_id,
                            uint8_t protocol,
                            uint32_t now_secs) override;
        void updatePosition(uint32_t node_id,
                            const contacts::NodePosition& position) override;
        bool remove(uint32_t node_id) override;
        const std::vector<contacts::NodeEntry>& getEntries() const override;
        void clear() override;
        bool flush() override;

      private:
        SdProtocolPeerRepository& owner_;
    };

    class ContactStoreView final : public contacts::IContactStore
    {
      public:
        explicit ContactStoreView(SdProtocolPeerRepository& owner);
        void setActiveProtocol(MeshProtocol protocol) override;
        void begin() override;
        std::string getNickname(uint32_t node_id) const override;
        bool setNickname(uint32_t node_id, const char* nickname) override;
        bool removeNickname(uint32_t node_id) override;
        bool hasNickname(const char* nickname) const override;
        std::vector<uint32_t> getAllContactIds() const override;
        size_t getCount() const override;

      private:
        SdProtocolPeerRepository& owner_;
    };

    bool ensureLayout();
    bool ensureProtocolLayout(MeshProtocol protocol);
    bool loadProtocol(MeshProtocol protocol);
    bool loadPeerJournal(MeshProtocol protocol, const char* name);
    bool loadContactJournal(MeshProtocol protocol, const char* name);
    bool compactProtocolAtBoot(MeshProtocol protocol);
    bool rewritePeerSnapshot(MeshProtocol protocol);
    bool rewriteContactSnapshot(MeshProtocol protocol);

    bool appendPeerDelta(const storage::v2::PeerProjection& projection);
    bool appendContactDelta(
        const storage::v2::ContactProjection& projection);
    bool queueOrAppendPeerDelta(
        const storage::v2::PeerProjection& projection);
    bool drainPendingPeerDeltas(std::size_t budget);
    bool applyPeerProjection(const storage::v2::PeerProjection& projection);
    bool applyContactProjection(
        const storage::v2::ContactProjection& projection);
    void overlayContactFacts();
    void overlayContactFactsForPeer(MeshPeerRecord& peer) const;
    void reconcileStableIdentities(MeshProtocol protocol);

    std::size_t findPeerIndex(const MeshPeerIdentity& identity) const;
    std::size_t findPeerIndexByNodeId(MeshProtocol protocol,
                                      NodeId node_id) const;
    std::size_t findContactIndex(const MeshPeerIdentity& identity) const;
    bool stableContactIdentity(const MeshPeerRecord& peer,
                               MeshPeerIdentity& out_identity) const;
    NodeId projectedNodeId(const MeshPeerRecord& peer) const;
    bool peerIsProtected(const MeshPeerRecord& peer) const;
    bool peerReferencedByConversation(const MeshPeerRecord& peer) const;
    bool peerReferencedByConversations(
        const MeshPeerRecord& peer,
        const std::vector<ConversationMeta>& conversations) const;
    std::size_t ephemeralCount(MeshProtocol protocol) const;
    bool evictOldestEphemeral(MeshProtocol protocol);

    void applyNodeUpdate(uint32_t node_id,
                         const contacts::NodeUpdate& update);
    bool removeNode(uint32_t node_id);
    void clearActiveNodes();
    const std::vector<contacts::NodeEntry>& activeNodeEntries() const;
    void rebuildActiveNodeProjection() const;
    bool toNodeEntry(const MeshPeerRecord& peer,
                     contacts::NodeEntry& out) const;

    std::string nicknameForNode(uint32_t node_id) const;
    bool setNicknameForNode(uint32_t node_id, const char* nickname);
    bool removeNicknameForNode(uint32_t node_id);
    bool hasNicknameForActiveProtocol(const char* nickname) const;
    std::vector<uint32_t> activeContactIds() const;
    size_t activeContactCount() const;
    bool persistContactFacts(const MeshPeerIdentity& identity,
                             const MeshPeerUserFlags& flags,
                             const char* alias,
                             bool deleted);

    static MeshProtocol normalizeProtocol(MeshProtocol protocol);
    static const char* protocolSlug(MeshProtocol protocol);
    static std::size_t protocolIndex(MeshProtocol protocol);
    static NodeId reticulumNodeId(const ReticulumPeerIdentity& identity);
    static bool ensureDirectory(const char* path);
    static void buildProtocolPath(MeshProtocol protocol,
                                  const char* name,
                                  char* out,
                                  std::size_t out_len);

    bool lock() const;
    void unlock() const;

    IChatStore& chat_store_;
    storage::v2::FixedSlotJournalEngine journal_{};
    PeerVector peers_{};
    ContactVector contacts_{};
    PendingPeerVector pending_peer_deltas_{};
    std::size_t pending_peer_head_ = 0U;
    PsramByteVector slot_scratch_{};
    PartitionState partitions_[3]{};
    mutable std::vector<contacts::NodeEntry> active_node_projection_{};
    mutable bool active_node_projection_dirty_ = true;
    MeshProtocol active_protocol_ = MeshProtocol::Meshtastic;
    bool begun_ = false;
    mutable SemaphoreHandle_t mutex_ = nullptr;
    NodeStoreView node_store_view_;
    ContactStoreView contact_store_view_;
};

} // namespace chat
