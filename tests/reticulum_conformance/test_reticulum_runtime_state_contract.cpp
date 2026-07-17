#include "chat/domain/reticulum_identity.h"
#include "chat/infra/mesh_incoming_queue.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_delivery_runtime.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_destination_registry.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_link_manager.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_link_runtime.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_lxst_telephony_client.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_network_page_client.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_packet_router.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_path_manager.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_ping_service.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_propagation_client.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_propagation_runtime.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_propagation_service_runtime.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_resource_runtime.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_runtime_state.h"
#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_transport_runtime.h"
#include "team/protocol/team_portnum.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{

template <std::size_t N>
std::array<uint8_t, N> filled_hash(uint8_t seed)
{
    std::array<uint8_t, N> out = {};
    for (std::size_t index = 0; index < out.size(); ++index)
    {
        out[index] = static_cast<uint8_t>(seed + index);
    }
    return out;
}

template <std::size_t N>
bool same_hash(const uint8_t (&lhs)[N], const std::array<uint8_t, N>& rhs)
{
    return std::memcmp(lhs, rhs.data(), N) == 0;
}

template <std::size_t N>
bool all_zero(const uint8_t (&value)[N])
{
    for (uint8_t byte : value)
    {
        if (byte != 0)
        {
            return false;
        }
    }
    return true;
}

template <std::size_t N>
void copy_hash(uint8_t (&out)[N], const std::array<uint8_t, N>& value)
{
    std::memcpy(out, value.data(), N);
}

std::array<uint8_t, chat::lxmf::runtime::kAnnounceRandomBlobSize>
announce_blob(uint8_t seed, uint64_t emitted)
{
    std::array<uint8_t, chat::lxmf::runtime::kAnnounceRandomBlobSize> out = {};
    for (std::size_t index = 0; index < 5; ++index)
    {
        out[index] = static_cast<uint8_t>(seed + index);
    }
    for (std::size_t index = out.size(); index > 5; --index)
    {
        out[index - 1U] = static_cast<uint8_t>(emitted & 0xFFU);
        emitted >>= 8U;
    }
    return out;
}

} // namespace

int main()
{
    using namespace chat::lxmf::runtime;
    namespace reticulum = chat::reticulum;

    static_assert(!std::is_copy_constructible<DestinationRegistry>::value,
                  "DestinationRegistry owns peer state and must not be copied");
    static_assert(!std::is_move_constructible<DestinationRegistry>::value,
                  "DestinationRegistry ownership must stay in place");
    static_assert(!std::is_copy_constructible<PathManager>::value,
                  "PathManager owns transport state and must not be copied");
    static_assert(!std::is_move_constructible<PathManager>::value,
                  "PathManager ownership must stay in place");
    static_assert(!std::is_copy_constructible<LinkManager>::value,
                  "LinkManager owns link sessions and must not be copied");
    static_assert(!std::is_move_constructible<LinkManager>::value,
                  "LinkManager ownership must stay in place");
    static_assert(!std::is_copy_constructible<PingService>::value,
                  "PingService owns pending ping state and must not be copied");
    static_assert(!std::is_copy_constructible<NetworkPageClient>::value,
                  "NetworkPageClient owns pending page state and must not be copied");
    static_assert(!std::is_copy_constructible<PropagationClient>::value,
                  "PropagationClient owns propagation state and must not be copied");
    static_assert(!std::is_copy_constructible<LxstTelephonyClient>::value,
                  "LxstTelephonyClient owns call scratch state and must not be copied");
    static_assert(
        std::is_same<ResourcePayloadList::allocator_type,
                     PsramAllocator<ResourcePayloadBuffer>>::value,
        "Resource payload list ownership must stay on PSRAM allocator");
    static_assert(
        std::is_same<ResourceMetadataBuffer::allocator_type,
                     PsramAllocator<uint8_t>>::value,
        "Resource metadata buffers must stay on PSRAM allocator");
    static_assert(
        std::is_same<ResourceBitmapBuffer::allocator_type,
                     PsramAllocator<uint8_t>>::value,
        "Resource bitmap buffers must stay on PSRAM allocator");
    static_assert(
        std::is_same<ResourceMapHashList::allocator_type,
                     PsramAllocator<RuntimeMapHash>>::value,
        "Resource map hash lists must stay on PSRAM allocator");
    static_assert(
        std::is_same<PropagationIdList::allocator_type,
                     PsramAllocator<RuntimeByteBuffer>>::value,
        "Propagation id lists must stay on PSRAM allocator");
    static_assert(
        std::is_same<PropagationMessageList::allocator_type,
                     PsramAllocator<RuntimeByteBuffer>>::value,
        "Propagation message lists must stay on PSRAM allocator");

    const auto registry_destination =
        filled_hash<reticulum::kTruncatedHashSize>(0x04);
    const auto registry_identity =
        filled_hash<reticulum::kTruncatedHashSize>(0x24);
    DestinationRegistry registry{};
    assert(registry.size() == 0);
    PeerInfo& registry_peer = registry.upsertDestination(registry_destination.data());
    assert(registry.size() == 1);
    assert(same_hash(registry_peer.destination_hash, registry_destination));
    copy_hash(registry_peer.identity_hash, registry_identity);
    assert(registry.findByDestinationHash(registry_destination.data()) == &registry_peer);
    assert(registry.findByIdentityHash(registry_identity.data()) == &registry_peer);
    assert(registry.findByNodeId(registry_peer.node_id) == &registry_peer);
    assert(&registry.upsertDestination(registry_destination.data()) == &registry_peer);
    registry.clear();
    assert(registry.size() == 0);

    ReticulumPacketRouter router{};
    reticulum::ParsedPacket route_packet{};
    route_packet.packet_type = reticulum::PacketType::Announce;
    assert(router.route(route_packet) == PacketRoute::Announce);
    route_packet.packet_type = reticulum::PacketType::Proof;
    assert(router.route(route_packet) == PacketRoute::Proof);
    route_packet.packet_type = reticulum::PacketType::LinkRequest;
    assert(router.route(route_packet) == PacketRoute::LinkRequest);
    route_packet.packet_type = reticulum::PacketType::Data;
    assert(router.route(route_packet) == PacketRoute::Data);
    route_packet.packet_type = static_cast<reticulum::PacketType>(0x7F);
    assert(router.route(route_packet) == PacketRoute::LinkOrTransport);

    const auto manager_destination =
        filled_hash<reticulum::kTruncatedHashSize>(0x14);
    const auto manager_packet_hash = filled_hash<reticulum::kFullHashSize>(0x34);
    PathManager path_manager{};
    assert(!path_manager.isDuplicatePacket(manager_packet_hash.data()));
    path_manager.rememberPacket(manager_packet_hash.data(), 100, 4);
    assert(path_manager.isDuplicatePacket(manager_packet_hash.data()));
    path_manager.forgetPacket(manager_packet_hash.data());
    assert(!path_manager.isDuplicatePacket(manager_packet_hash.data()));
    PathEntry& managed_path = path_manager.upsertPath(manager_destination.data(), 4);
    managed_path.hops = 2;
    managed_path.updated_ms = 500;
    assert(path_manager.findAnyPath(manager_destination.data()) == &managed_path);
    assert(path_manager.findPath(manager_destination.data(), 600, 1000) == &managed_path);
    assert(path_manager.findPath(manager_destination.data(), 1601, 1000) == nullptr);
    path_manager.notePendingPathRequest(manager_destination.data(), 700, 4);
    assert(path_manager.findPendingPathRequest(manager_destination.data()) != nullptr);
    path_manager.resolvePendingPathRequest(manager_destination.data());
    assert(path_manager.findPendingPathRequest(manager_destination.data()) == nullptr);

    LinkManager link_manager{};
    LinkSession* managed_session = link_manager.appendSession(2);
    assert(managed_session != nullptr);
    copy_hash(managed_session->link_id, manager_destination);
    copy_hash(managed_session->remote_destination_hash, manager_destination);
    managed_session->destination = LocalDestinationKind::Delivery;
    managed_session->state = LinkState::Active;
    assert(link_manager.size() == 1);
    assert(link_manager.findSession(manager_destination.data()) == managed_session);
    assert(link_manager.findOpenSessionByDestination(manager_destination.data(),
                                                     LocalDestinationKind::Delivery) ==
           managed_session);
    assert(link_manager.closeSession(*managed_session, LinkCloseReason::LocalClose, 900));
    assert(managed_session->state == LinkState::Closed);
    link_manager.clear();
    assert(link_manager.size() == 0);
    managed_session = link_manager.appendSession(2);
    assert(managed_session != nullptr);
    const auto resource_hash = filled_hash<reticulum::kFullHashSize>(0x44);
    const auto resource_original_hash =
        filled_hash<reticulum::kFullHashSize>(0x64);
    const uint8_t resource_random[kResourceMapHashLen] = {0x01, 0x02, 0x03, 0x04};
    const uint8_t resource_request_id[] = {0x09, 0x0A};
    ResourceMetadataBuffer resource_hashmap;
    resource_hashmap.insert(resource_hashmap.end(),
                            resource_random,
                            resource_random + sizeof(resource_random));
    LinkResourceTransfer* managed_incoming_resource =
        link_manager.startIncomingResource(*managed_session,
                                           resource_hash.data(),
                                           resource_random,
                                           resource_original_hash.data(),
                                           resource_request_id,
                                           sizeof(resource_request_id),
                                           resource_hashmap.data(),
                                           resource_hashmap.size(),
                                           8,
                                           8,
                                           1,
                                           1,
                                           1,
                                           0,
                                           false,
                                           false,
                                           false,
                                           false,
                                           1000,
                                           4);
    assert(managed_incoming_resource != nullptr);
    assert(link_manager.findIncomingResource(*managed_session,
                                             resource_hash.data()) ==
           managed_incoming_resource);
    const ResourceWindowRequest window_request =
        link_manager.buildNextResourceWindowRequest(*managed_incoming_resource);
    assert(window_request.valid);
    link_manager.noteResourceWindowRequested(*managed_incoming_resource, false, 1005);
    assert(managed_incoming_resource->last_activity_ms == 1005);
    assert(link_manager.eraseIncomingResource(*managed_session,
                                              resource_hash.data()));
    LinkResourceTransfer managed_outgoing_resource{};
    assert(link_manager.initialiseOutgoingResource(managed_outgoing_resource,
                                                   resource_request_id,
                                                   sizeof(resource_request_id),
                                                   8,
                                                   8,
                                                   1,
                                                   0,
                                                   1010,
                                                   4));
    copy_hash(managed_outgoing_resource.resource_hash, resource_hash);
    managed_outgoing_resource.message_id = 77;
    assert(link_manager.appendOutgoingResource(*managed_session,
                                               std::move(managed_outgoing_resource)) !=
           nullptr);
    LinkResourceTransfer* queued_outgoing_resource =
        link_manager.findOutgoingResource(*managed_session, resource_hash.data());
    assert(queued_outgoing_resource != nullptr);
    bool saw_resource_message_id = false;
    link_manager.takeTrackedOutgoingResourceMessageIds(
        *managed_session,
        [&saw_resource_message_id](uint32_t message_id)
        {
            saw_resource_message_id = message_id == 77;
        });
    assert(saw_resource_message_id);
    assert(queued_outgoing_resource->message_id == 0);
    assert(link_manager.eraseOutgoingResource(*managed_session,
                                              resource_hash.data()));
    link_manager.clear();

    PingService ping_service{};
    const uint8_t zero_destination[reticulum::kTruncatedHashSize] = {};
    assert(ping_service.queue(zero_destination, 100, 2) ==
           PendingPingQueueResult::Invalid);
    assert(ping_service.queue(manager_destination.data(), 100, 2) ==
           PendingPingQueueResult::Queued);
    assert(ping_service.queue(manager_destination.data(), 100, 2) ==
           PendingPingQueueResult::Duplicate);
    bool ping_dispatched = false;
    ping_service.pump(
        250,
        false,
        1000,
        100,
        100,
        [](const uint8_t*)
        { return true; },
        [&ping_dispatched](const uint8_t*, uint32_t, uint32_t)
        {
            ping_dispatched = true;
            return true;
        },
        [](const uint8_t*, uint32_t) {},
        [](const PendingPingRequest&, uint32_t)
        { assert(false); });
    assert(ping_dispatched);
    assert(ping_service.size() == 0);
    assert(ping_service.queue(manager_destination.data(), 100, 2) ==
           PendingPingQueueResult::Queued);
    bool ping_timed_out = false;
    ping_service.pump(
        500,
        false,
        100,
        100,
        100,
        [](const uint8_t*)
        { return false; },
        [](const uint8_t*, uint32_t, uint32_t)
        { return false; },
        [](const uint8_t*, uint32_t) {},
        [&ping_timed_out](const PendingPingRequest&, uint32_t)
        { ping_timed_out = true; });
    assert(ping_timed_out);
    assert(ping_service.size() == 0);

    NetworkPageClient page_client{};
    PendingNomadPageRequest* page_request = nullptr;
    assert(page_client.empty());
    assert(page_client.queue(manager_destination.data(),
                             "/",
                             100,
                             1,
                             sizeof(PendingNomadPageRequest::path),
                             &page_request) == NetworkPageQueueResult::Queued);
    assert(page_request != nullptr);
    assert(page_client.size() == 1);
    copy_hash(page_request->request_id, manager_destination);
    assert(page_client.findByRequestId(manager_destination.data(),
                                       manager_destination.data(),
                                       reticulum::kTruncatedHashSize) == page_request);
    assert(page_client.queue(manager_destination.data(),
                             "/",
                             200,
                             1,
                             sizeof(PendingNomadPageRequest::path),
                             &page_request) == NetworkPageQueueResult::Duplicate);
    page_client.eraseAt(0);
    assert(page_client.empty());

    PropagationClient propagation_client{};
    assert(!propagation_client.state().has_active_node);
    PropagationPeerState active_propagation_peer{};
    copy_hash(active_propagation_peer.propagation_hash, manager_destination);
    active_propagation_peer.node_active = true;
    active_propagation_peer.last_seen_s = 100;
    propagation_client.state().peers.push_back(active_propagation_peer);
    PropagationActivePeerSelection selected_peer =
        propagation_client.selectActivePeer(false,
                                            manager_destination.data(),
                                            100,
                                            30,
                                            true);
    assert(selected_peer.peer != nullptr);
    assert(selected_peer.changed);
    propagation_client.peerScratch().node_id = 0x12345678;
    assert(propagation_client.state().has_active_node);
    assert(same_hash(propagation_client.state().active_node_hash, manager_destination));
    PropagationActivePeerSelection selected_again =
        propagation_client.selectActivePeer(false,
                                            manager_destination.data(),
                                            101,
                                            30,
                                            true);
    assert(selected_again.peer != nullptr);
    assert(!selected_again.changed);
    propagation_client.clearActivePeer();
    assert(!propagation_client.state().has_active_node);
    assert(all_zero(propagation_client.state().active_node_hash));
    assert(propagation_client.peerScratch().node_id == 0x12345678);
    assert(propagation_client.startSyncIfDue(200, 1000, 60));
    assert(propagation_client.state().sync_stage == PropagationSyncStage::NeedList);
    uint8_t sync_request_id[reticulum::kTruncatedHashSize] = {};
    copy_hash(sync_request_id, manager_destination);
    LinkPendingRequest sync_request{};
    sync_request.request_id.assign(sync_request_id,
                                   sync_request_id + sizeof(sync_request_id));
    assert(!propagation_client.syncRequestMatches(sync_request));
    propagation_client.markSyncRequestSent(sync_request_id,
                                           PropagationSyncStage::Listing);
    assert(propagation_client.syncRequestMatches(sync_request));
    PropagationIdList remote_ids;
    RuntimeByteBuffer known_id(reticulum::kFullHashSize, 0x11);
    RuntimeByteBuffer wanted_id(reticulum::kFullHashSize, 0x22);
    propagation_client.rememberDeliveredTransient(known_id.data(), 200, 4);
    remote_ids.push_back(known_id);
    remote_ids.push_back(wanted_id);
    propagation_client.noteListingResult(remote_ids, 1);
    assert(propagation_client.state().sync_stage ==
           PropagationSyncStage::NeedMessages);
    assert(propagation_client.state().sync_haves.size() == 1);
    assert(propagation_client.state().sync_wants.size() == 1);
    propagation_client.noteDownloadResult(false, 1200);
    assert(propagation_client.state().sync_stage ==
           PropagationSyncStage::NeedAcknowledge);
    propagation_client.markAcknowledged();
    assert(propagation_client.state().sync_stage == PropagationSyncStage::Complete);
    assert(propagation_client.syncHaveCount() == 1);
    propagation_client.finishSyncComplete(220);
    assert(propagation_client.state().sync_stage == PropagationSyncStage::Idle);
    assert(!propagation_client.state().initial_sync_pending);
    PendingPropagationUpload upload_a{};
    upload_a.message_id = 101;
    upload_a.created_ms = 1000;
    upload_a.state = PropagationUploadState::WaitingNode;
    PendingPropagationUpload* queued_upload =
        propagation_client.queueUpload(std::move(upload_a), 2);
    assert(queued_upload != nullptr);
    assert(propagation_client.hasPendingUploads());
    assert(propagation_client.firstPendingUpload() == queued_upload);
    assert(propagation_client.firstPendingUpload()->message_id == 101);

    PendingPropagationUpload upload_b{};
    upload_b.message_id = 102;
    upload_b.created_ms = 1005;
    upload_b.state = PropagationUploadState::WaitingNode;
    assert(propagation_client.queueUpload(std::move(upload_b), 2) != nullptr);
    PendingPropagationUpload upload_c{};
    upload_c.message_id = 103;
    upload_c.created_ms = 1010;
    upload_c.state = PropagationUploadState::WaitingNode;
    assert(propagation_client.queueUpload(std::move(upload_c), 2) == nullptr);

    propagation_client.markExpiredUploads(1101, 100);
    std::vector<PendingPropagationUpload> failed_uploads =
        propagation_client.takeFailedUploads();
    assert(failed_uploads.size() == 1);
    assert(failed_uploads[0].message_id == 101);
    assert(propagation_client.hasPendingUploads());
    assert(propagation_client.firstPendingUpload()->message_id == 102);
    assert(propagation_client.removeFirstPendingUpload());
    assert(!propagation_client.removeFirstPendingUpload());
    assert(!propagation_client.hasPendingUploads());

    PendingPropagationUpload upload_d{};
    upload_d.message_id = 104;
    upload_d.state = PropagationUploadState::NeedsStamp;
    assert(propagation_client.queueUpload(std::move(upload_d), 2) != nullptr);
    std::vector<PendingPropagationUpload> all_uploads =
        propagation_client.takeAllPendingUploads();
    assert(all_uploads.size() == 1);
    assert(all_uploads[0].message_id == 104);
    assert(!propagation_client.hasPendingUploads());
#if !defined(TRAIL_MATE_RETICULUM_PARSE_ONLY)
    propagation_client.stamp().reset();
#endif

    LxstTelephonyClient telephony_client{};
    assert(telephony_client.scratch() != nullptr);
    assert(telephony_client.scratchCapacity() == reticulum::kReticulumMtu);
    telephony_client.scratch()[0] = 0xA5;
    assert(telephony_client.scratch()[0] == 0xA5);

    TransportRuntime transport{};
    assert(transport.paths.empty());
    assert(transport.packet_filter.empty());
    assert(transport.reverse_table.empty());
    assert(transport.pending_path_requests.empty());
    assert(transport.pending_ping_receipts.empty());
    assert(transport.pending_delivery_receipts.empty());
    assert(transport.link_relays.empty());

    const TransportRuntimeLimits limits{
        4,
        4,
        4,
        4,
        4,
        30000,
        45000,
        60000,
        300000,
        4,
        1000,
        500,
        4,
        750};

    const auto destination = filled_hash<reticulum::kTruncatedHashSize>(0x10);
    PathEntry& path = upsertPath(transport, destination.data(), limits.max_paths);
    path.cached_announce_len = 42;
    const auto first_announce = announce_blob(0xA0, 100);
    assert(evaluatePathAnnounce(nullptr,
                                2,
                                first_announce.data(),
                                100,
                                limits.path_ttl_ms) ==
           PathAnnounceDecision::AcceptNew);
    applyPathAnnounce(path, 2, first_announce.data(), 100, 1234);
    path.direct = false;
    assert(transport.paths.size() == 1);
    assert(same_hash(transport.paths.front().destination_hash, destination));
    assert(transport.paths.front().cached_announce_len == 42);
    assert(findPath(transport, destination.data()) == &transport.paths.front());
    assert(!pathExpired(path, 1100, limits.path_ttl_ms));
    assert(pathExpired(path, 1101, limits.path_ttl_ms));
    assert(evaluatePathAnnounce(&path,
                                2,
                                first_announce.data(),
                                200,
                                limits.path_ttl_ms) ==
           PathAnnounceDecision::RejectReplay);
    const auto stale_better_announce = announce_blob(0xB0, 99);
    assert(evaluatePathAnnounce(&path,
                                1,
                                stale_better_announce.data(),
                                200,
                                limits.path_ttl_ms) ==
           PathAnnounceDecision::RejectStale);
    const auto newer_worse_announce = announce_blob(0xC0, 101);
    assert(evaluatePathAnnounce(&path,
                                3,
                                newer_worse_announce.data(),
                                200,
                                limits.path_ttl_ms) ==
           PathAnnounceDecision::AcceptNewer);
    applyPathAnnounce(path, 3, newer_worse_announce.data(), 200, 1235);
    assert(path.hops == 3);
    assert(path.announce_timebase == 101);
    assert(evaluatePathAnnounce(&path,
                                1,
                                stale_better_announce.data(),
                                1201,
                                limits.path_ttl_ms) ==
           PathAnnounceDecision::RejectStale);
    assert(evaluatePathAnnounce(&path,
                                4,
                                stale_better_announce.data(),
                                1201,
                                limits.path_ttl_ms) ==
           PathAnnounceDecision::AcceptExpired);

    const auto packet_hash = filled_hash<reticulum::kFullHashSize>(0x20);
    rememberPacket(transport, packet_hash.data(), 100, limits.max_packet_filter);
    assert(same_hash(transport.packet_filter.front().packet_hash, packet_hash));
    assert(isDuplicatePacket(transport, packet_hash.data()));
    forgetPacket(transport, packet_hash.data());
    assert(!isDuplicatePacket(transport, packet_hash.data()));
    rememberPacket(transport, packet_hash.data(), 100, limits.max_packet_filter);

    rememberReversePath(transport,
                        destination.data(),
                        7,
                        3,
                        200,
                        limits.max_reverse_entries);
    ReverseEntry* reverse = findReversePath(transport, destination.data());
    assert(reverse != nullptr);
    assert(reverse->interface_id == 7);
    assert(reverse->expected_hops == 3);

    notePendingPathRequest(transport, destination.data(), 300, limits.max_pending_path_requests);
    notePendingPathRequest(transport, destination.data(), 350, limits.max_pending_path_requests);
    const PendingPathRequest* pending = findPendingPathRequest(transport, destination.data());
    assert(pending != nullptr);
    assert(pending->created_ms == 300);
    assert(pending->last_attempt_ms == 350);
    assert(pending->attempts == 2);
    assert(!pending->resolved);
    resolvePendingPathRequest(transport, destination.data());
    assert(findPendingPathRequest(transport, destination.data()) == nullptr);

    const auto peer_sig_pub =
        filled_hash<chat::lxmf::LxmfIdentity::kSigPubKeySize>(0x55);
    notePendingPingReceipt(transport,
                           packet_hash.data(),
                           destination.data(),
                           peer_sig_pub.data(),
                           350,
                           limits.max_pending_ping_receipts);
    PendingPingReceipt* ping_receipt =
        findPendingPingReceipt(transport, packet_hash.data());
    assert(ping_receipt != nullptr);
    assert(same_hash(ping_receipt->packet_hash, packet_hash));
    assert(same_hash(ping_receipt->destination_hash, destination));
    assert(same_hash(ping_receipt->peer_sig_pub, peer_sig_pub));

    notePendingDeliveryReceipt(transport,
                               packet_hash.data(),
                               destination.data(),
                               peer_sig_pub.data(),
                               1234,
                               350,
                               limits.max_pending_delivery_receipts);
    PendingDeliveryReceipt* delivery_receipt =
        findPendingDeliveryReceipt(transport, packet_hash.data());
    assert(delivery_receipt != nullptr);
    assert(delivery_receipt->message_id == 1234);
    assert(same_hash(delivery_receipt->packet_hash, packet_hash));
    assert(same_hash(delivery_receipt->destination_hash, destination));
    assert(same_hash(delivery_receipt->peer_sig_pub, peer_sig_pub));

    LinkRelayEntry& relay = upsertLinkRelay(transport, destination.data(), limits.max_link_relays);
    relay.initiator_hops = 1;
    relay.responder_hops = 2;
    relay.last_seen_ms = 400;
    assert(transport.link_relays.front().responder_hops == 2);
    assert(findLinkRelay(transport, destination.data()) == &transport.link_relays.front());

    cullTransportRuntime(transport, 400 + limits.link_relay_ttl_ms + 1, limits);
    assert(transport.packet_filter.empty());
    assert(transport.reverse_table.empty());
    assert(transport.pending_path_requests.empty());
    assert(transport.pending_ping_receipts.empty());
    assert(transport.pending_delivery_receipts.empty());
    assert(transport.link_relays.empty());
    assert(transport.paths.empty());

    LinkRuntime links{};
    assert(links.sessions.empty());

    LinkSession session{};
    copy_hash(session.link_id, destination);
    copy_hash(session.remote_destination_hash, destination);
    assert(session.destination == LocalDestinationKind::Delivery);
    assert(session.state == LinkState::Pending);
    assert(session.close_reason == LinkCloseReason::None);
    assert(session.mtu == reticulum::kReticulumMtu);
    assert(session.mdu == reticulum::kReticulumMdu);
    assert(session.pending_requests.empty());
    assert(session.deferred_payloads.empty());
    assert(session.incoming_resources.empty());
    assert(session.incoming_resource_assemblies.empty());
    assert(session.outgoing_resources.empty());

    LinkPendingRequest request{};
    request.request_id = {0x01, 0x02, 0x03};
    request.awaiting_resource = true;
    session.pending_requests.push_back(request);

    DeferredLinkPayload deferred{};
    deferred.payload = {0xAA, 0xBB};
    session.deferred_payloads.push_back(deferred);

    LinkResourceTransfer resource{};
    copy_hash(resource.resource_hash, packet_hash);
    assert(resource.window_size == 4);
    assert(resource.segment_index == 1);
    assert(resource.total_segments == 1);
    assert(resource.incoming);
    assert(!resource.complete);
    assert(!resource.waiting_for_proof);
    session.incoming_resources.push_back(resource);

    LinkResourceAssembly assembly{};
    copy_hash(assembly.original_hash, packet_hash);
    assembly.payload = {0x10, 0x11, 0x12};
    session.incoming_resource_assemblies.push_back(assembly);

    links.sessions.push_back(session);
    assert(links.sessions.size() == 1);
    assert(links.sessions.front().pending_requests.size() == 1);
    assert(links.sessions.front().deferred_payloads.size() == 1);
    assert(links.sessions.front().incoming_resources.size() == 1);
    assert(links.sessions.front().incoming_resource_assemblies.size() == 1);
    assert(findLinkSession(links, destination.data()) == &links.sessions.front());
    assert(findActiveLinkSessionByDestination(links,
                                              destination.data(),
                                              LocalDestinationKind::Delivery) == nullptr);

    const LinkRuntimeLimits link_limits{
        2,
        50,
        100,
        200,
        10000,
        100,
        4,
        5000};
    const ResourceRuntimeLimits resource_limits{100};

    links.sessions.front().state = LinkState::Active;
    links.sessions.front().initiator = true;
    links.sessions.front().last_inbound_ms = 100;
    links.sessions.front().last_outbound_ms = 120;
    links.sessions.front().keepalive_interval_ms = 50;
    links.sessions.front().stale_timeout_ms = 200;
    assert(findOpenLinkSessionByDestination(links,
                                            destination.data(),
                                            LocalDestinationKind::Delivery) ==
           &links.sessions.front());
    assert(findActiveLinkSessionByDestination(links,
                                              destination.data(),
                                              LocalDestinationKind::Delivery) ==
           &links.sessions.front());

    LinkRuntimeMaintenance maintenance =
        advanceLinkSessionLifecycle(links.sessions.front(), 160, link_limits);
    assert(maintenance.flush_deferred_payloads);
    assert(maintenance.send_keepalive);
    assert(!maintenance.close_timeout);
    assert(!maintenance.marked_stale);
    assert(links.sessions.front().state == LinkState::Active);

    maintenance = advanceLinkSessionLifecycle(links.sessions.front(), 300, link_limits);
    assert(maintenance.flush_deferred_payloads);
    assert(maintenance.send_keepalive);
    assert(!maintenance.close_timeout);
    assert(maintenance.marked_stale);
    assert(links.sessions.front().state == LinkState::Active);
    markLinkSessionStale(links.sessions.front());
    assert(links.sessions.front().state == LinkState::Stale);

    maintenance = advanceLinkSessionLifecycle(links.sessions.front(), 401, link_limits);
    assert(maintenance.close_timeout);

    LinkSession& second = appendLinkSession(links, link_limits.max_link_sessions);
    second.state = LinkState::Closed;
    second.last_inbound_ms = 1;
    second.last_outbound_ms = 1;
    LinkSession& replacement = appendLinkSession(links, link_limits.max_link_sessions);
    const auto replacement_link = filled_hash<reticulum::kTruncatedHashSize>(0x44);
    copy_hash(replacement.link_id, replacement_link);
    assert(links.sessions.size() == 2);
    assert(findLinkSession(links, replacement_link.data()) == &links.sessions.back());

    LinkSession closing{};
    copy_hash(closing.link_id, destination);
    copy_hash(closing.remote_destination_hash, destination);
    closing.state = LinkState::Active;
    closing.close_reason = LinkCloseReason::None;
    closing.remote_identity_known = true;
    closing.validated = true;
    closing.propagation_offer_validated = true;
    closing.rtt_s = 1.0f;
    closing.last_keepalive_ms = 700;
    std::memset(closing.local_enc_priv, 0xA1, sizeof(closing.local_enc_priv));
    std::memset(closing.local_sig_priv, 0xA2, sizeof(closing.local_sig_priv));
    std::memset(closing.derived_key, 0xA3, sizeof(closing.derived_key));
    std::memset(closing.peer_enc_pub, 0xA4, sizeof(closing.peer_enc_pub));
    std::memset(closing.peer_link_sig_pub, 0xA5, sizeof(closing.peer_link_sig_pub));
    std::memset(closing.peer_identity_sig_pub, 0xA6, sizeof(closing.peer_identity_sig_pub));
    closing.pending_requests.push_back(request);
    closing.deferred_payloads.push_back(deferred);
    closing.incoming_resources.push_back(resource);
    closing.incoming_resource_assemblies.push_back(assembly);
    closing.outgoing_resources.push_back(resource);
    assert(closeLinkSession(closing, LinkCloseReason::Timeout, 900));
    assert(closing.state == LinkState::Closed);
    assert(closing.close_reason == LinkCloseReason::Timeout);
    assert(closing.last_inbound_ms == 900);
    assert(closing.last_outbound_ms == 900);
    assert(closing.pending_requests.empty());
    assert(closing.deferred_payloads.empty());
    assert(closing.incoming_resources.empty());
    assert(closing.incoming_resource_assemblies.empty());
    assert(closing.outgoing_resources.empty());
    assert(!closing.remote_identity_known);
    assert(!closing.validated);
    assert(!closing.propagation_offer_validated);
    assert(closing.rtt_s == 0.0f);
    assert(closing.last_keepalive_ms == 0);
    assert(all_zero(closing.local_enc_priv));
    assert(all_zero(closing.local_sig_priv));
    assert(all_zero(closing.derived_key));
    assert(all_zero(closing.peer_enc_pub));
    assert(all_zero(closing.peer_link_sig_pub));
    assert(all_zero(closing.peer_identity_sig_pub));
    assert(!closeLinkSession(closing, LinkCloseReason::Error, 1000));
    assert(closing.close_reason == LinkCloseReason::Timeout);

    LinkSession culling{};
    culling.pending_requests.push_back(request);
    culling.pending_requests.back().created_ms = 1;
    culling.incoming_resources.push_back(resource);
    culling.incoming_resources.back().last_activity_ms = 1;
    culling.outgoing_resources.push_back(resource);
    culling.outgoing_resources.back().complete = true;
    culling.incoming_resource_assemblies.push_back(assembly);
    culling.incoming_resource_assemblies.back().last_activity_ms = 1;
    cullLinkSessionTables(culling, 200, link_limits);
    assert(culling.pending_requests.empty());
    assert(culling.incoming_resources.size() == 1);
    assert(culling.outgoing_resources.size() == 1);
    assert(culling.incoming_resource_assemblies.size() == 1);
    cullLinkResources(culling, 200, resource_limits);
    assert(culling.incoming_resources.empty());
    assert(culling.outgoing_resources.empty());
    assert(culling.incoming_resource_assemblies.empty());

    LinkRuntime removal{};
    removal.sessions.push_back(closing);
    removal.sessions.front().last_inbound_ms = 1;
    removal.sessions.front().last_outbound_ms = 1;
    removeExpiredLinkSessions(removal, 1 + link_limits.closed_retention_ms + 1, link_limits);
    assert(removal.sessions.empty());

    const auto original_hash = filled_hash<reticulum::kFullHashSize>(0x60);
    const auto random_hash = filled_hash<kResourceMapHashLen>(0x70);
    const std::array<uint8_t, kResourceMapHashLen> map_hash_0{0xA0, 0xA1, 0xA2, 0xA3};
    const std::array<uint8_t, kResourceMapHashLen> map_hash_1{0xB0, 0xB1, 0xB2, 0xB3};
    const std::array<uint8_t, kResourceMapHashLen> map_hash_2{0xC0, 0xC1, 0xC2, 0xC3};
    ResourceMetadataBuffer first_hashmap;
    first_hashmap.insert(first_hashmap.end(), map_hash_0.begin(), map_hash_0.end());
    first_hashmap.insert(first_hashmap.end(), map_hash_1.begin(), map_hash_1.end());
    const uint8_t incoming_request_id[] = {0x31, 0x32};

    LinkResourceTransfer incoming_resource{};
    assert(initialiseIncomingResourceTransfer(incoming_resource,
                                              packet_hash.data(),
                                              random_hash.data(),
                                              original_hash.data(),
                                              incoming_request_id,
                                              sizeof(incoming_request_id),
                                              first_hashmap.data(),
                                              first_hashmap.size(),
                                              6,
                                              9,
                                              3,
                                              1,
                                              1,
                                              0x40,
                                              true,
                                              false,
                                              false,
                                              false,
                                              500,
                                              2));
    assert(same_hash(incoming_resource.resource_hash, packet_hash));
    assert(same_hash(incoming_resource.original_hash, original_hash));
    assert(incoming_resource.hashmap_height == 2);
    assert(incoming_resource.parts.size() == 3);
    assert(incoming_resource.received_bitmap.size() == 3);
    assert(incoming_resource.map_hash_known[2] == 0);
    assert(incoming_resource.waiting_for_hashmap);

    ResourceWindowRequest window = buildNextResourceWindowRequest(incoming_resource);
    assert(window.valid);
    assert(!window.needs_more_hashmap);
    assert(window.requested_hashes.size() == 2);
    assert(window.requested_hashes[0] == map_hash_0);
    assert(window.requested_hashes[1] == map_hash_1);
    noteResourceWindowRequest(incoming_resource, window.needs_more_hashmap, 510);
    assert(!incoming_resource.waiting_for_hashmap);
    assert(incoming_resource.last_activity_ms == 510);

    auto part_0_hash = filled_hash<reticulum::kFullHashSize>(0xA0);
    for (std::size_t i = 0; i < map_hash_0.size(); ++i)
    {
        part_0_hash[i] = map_hash_0[i];
    }
    const uint8_t part_0[] = {0x01, 0x02};
    std::size_t matched_index = 9;
    bool resource_complete = true;
    assert(recordResourcePart(incoming_resource,
                              part_0,
                              sizeof(part_0),
                              part_0_hash.data(),
                              520,
                              &matched_index,
                              &resource_complete));
    assert(matched_index == 0);
    assert(!resource_complete);
    assert(incoming_resource.consecutive_complete_index == 0);
    assert(incoming_resource.parts[0].size() == sizeof(part_0));

    window = buildNextResourceWindowRequest(incoming_resource);
    assert(window.valid);
    assert(window.needs_more_hashmap);
    assert(window.last_known_hash == map_hash_1);
    assert(window.requested_hashes.size() == 1);
    assert(window.requested_hashes[0] == map_hash_1);
    noteResourceWindowRequest(incoming_resource, window.needs_more_hashmap, 530);
    assert(incoming_resource.waiting_for_hashmap);

    ResourceMetadataBuffer second_hashmap(map_hash_2.begin(), map_hash_2.end());
    assert(applyResourceHashmapUpdate(incoming_resource,
                                      1,
                                      second_hashmap.data(),
                                      second_hashmap.size(),
                                      2,
                                      540));
    assert(!incoming_resource.waiting_for_hashmap);
    assert(incoming_resource.hashmap_height == 3);
    assert(incoming_resource.map_hash_known[2] == 1);

    window = buildNextResourceWindowRequest(incoming_resource);
    assert(window.valid);
    assert(!window.needs_more_hashmap);
    assert(window.requested_hashes.size() == 2);
    assert(window.requested_hashes[0] == map_hash_1);
    assert(window.requested_hashes[1] == map_hash_2);

    auto part_1_hash = filled_hash<reticulum::kFullHashSize>(0xB0);
    auto part_2_hash = filled_hash<reticulum::kFullHashSize>(0xC0);
    for (std::size_t i = 0; i < map_hash_1.size(); ++i)
    {
        part_1_hash[i] = map_hash_1[i];
        part_2_hash[i] = map_hash_2[i];
    }
    const uint8_t part_1[] = {0x03, 0x04, 0x05};
    const uint8_t part_2[] = {0x06};
    resource_complete = true;
    assert(recordResourcePart(incoming_resource,
                              part_1,
                              sizeof(part_1),
                              part_1_hash.data(),
                              550,
                              &matched_index,
                              &resource_complete));
    assert(matched_index == 1);
    assert(!resource_complete);
    assert(recordResourcePart(incoming_resource,
                              part_2,
                              sizeof(part_2),
                              part_2_hash.data(),
                              560,
                              &matched_index,
                              &resource_complete));
    assert(matched_index == 2);
    assert(resource_complete);
    markResourceComplete(incoming_resource, 570);
    assert(incoming_resource.complete);
    assert(incoming_resource.last_activity_ms == 570);

    LinkSession resource_session{};
    resource_session.incoming_resources.push_back(incoming_resource);
    assert(findLinkResource(resource_session.incoming_resources, packet_hash.data()) ==
           &resource_session.incoming_resources.front());
    assert(eraseLinkResourceByHash(resource_session.incoming_resources, packet_hash.data()));
    assert(!eraseLinkResourceByHash(resource_session.incoming_resources, packet_hash.data()));

    LinkResourceTransfer split_segment_1{};
    const uint8_t split_request_id[] = {0x44};
    ResourceMetadataBuffer split_hashmap(map_hash_0.begin(), map_hash_0.end());
    assert(initialiseIncomingResourceTransfer(split_segment_1,
                                              packet_hash.data(),
                                              random_hash.data(),
                                              original_hash.data(),
                                              split_request_id,
                                              sizeof(split_request_id),
                                              split_hashmap.data(),
                                              split_hashmap.size(),
                                              3,
                                              3,
                                              1,
                                              1,
                                              2,
                                              0x80,
                                              false,
                                              false,
                                              false,
                                              true,
                                              600,
                                              1));
    ResourcePayloadBuffer split_payload_1{0x11, 0x12};
    assert(appendResourceAssemblySegment(resource_session,
                                         split_segment_1,
                                         split_payload_1,
                                         610) ==
           ResourceAssemblyResult::WaitingForNextSegment);
    assert(resource_session.incoming_resource_assemblies.size() == 1);
    assert(split_segment_1.last_activity_ms == 0);

    LinkResourceTransfer split_segment_2 = split_segment_1;
    split_segment_2.segment_index = 2;
    split_segment_2.last_activity_ms = 620;
    ResourcePayloadBuffer split_payload_2{0x13};
    assert(appendResourceAssemblySegment(resource_session,
                                         split_segment_2,
                                         split_payload_2,
                                         630) ==
           ResourceAssemblyResult::Complete);
    assert(split_payload_2.size() == 3);
    assert(split_payload_2[0] == 0x11);
    assert(split_payload_2[2] == 0x13);
    assert(resource_session.incoming_resource_assemblies.empty());

    LinkResourceTransfer outgoing_resource{};
    const uint8_t outgoing_request_id[] = {0x51, 0x52, 0x53};
    assert(initialiseOutgoingResourceTransfer(outgoing_resource,
                                              outgoing_request_id,
                                              sizeof(outgoing_request_id),
                                              12,
                                              24,
                                              2,
                                              0x40,
                                              700,
                                              3));
    assert(!outgoing_resource.incoming);
    assert(outgoing_resource.encrypted);
    assert(outgoing_resource.parts.size() == 2);
    assert(outgoing_resource.map_hash_known.size() == 2);
    assert(outgoing_resource.request_id.size() == sizeof(outgoing_request_id));
    assert(!markResourceProofReceived(outgoing_resource, packet_hash.data(), 710));
    copy_hash(outgoing_resource.expected_proof, packet_hash);
    assert(markResourceProofReceived(outgoing_resource, packet_hash.data(), 720));
    assert(outgoing_resource.complete);

    PropagationRuntime propagation{};
    assert(propagation.entries.empty());
    assert(propagation.transients.empty());
    assert(propagation.peers.empty());

    const PropagationRuntimeLimits propagation_limits{
        2,
        2,
        2,
        10,
        20,
        10};
    const auto propagation_hash = filled_hash<reticulum::kTruncatedHashSize>(0x80);
    const auto delivery_hash = filled_hash<reticulum::kTruncatedHashSize>(0x90);
    const auto identity_hash = filled_hash<reticulum::kTruncatedHashSize>(0xA0);
    PropagationPeerState& propagation_peer =
        upsertPropagationPeer(propagation,
                              propagation_hash.data(),
                              delivery_hash.data(),
                              identity_hash.data(),
                              propagation_limits.max_peers);
    assert(same_hash(propagation_peer.propagation_hash, propagation_hash));
    assert(same_hash(propagation_peer.delivery_hash, delivery_hash));
    assert(same_hash(propagation_peer.identity_hash, identity_hash));
    markPropagationPeerSeen(propagation_peer, 800);
    notePropagationPeerIncomingMessage(propagation_peer);
    notePropagationPeerServedMessages(propagation_peer, 3);
    assert(propagation_peer.last_seen_s == 800);
    assert(propagation_peer.incoming_messages == 1);
    assert(propagation_peer.served_messages == 3);
    assert(findPropagationPeer(propagation, propagation_hash.data()) == &propagation_peer);

    const auto propagation_hash_2 = filled_hash<reticulum::kTruncatedHashSize>(0xB0);
    const auto propagation_hash_3 = filled_hash<reticulum::kTruncatedHashSize>(0xC0);
    (void)upsertPropagationPeer(propagation,
                                propagation_hash_2.data(),
                                nullptr,
                                nullptr,
                                propagation_limits.max_peers);
    (void)upsertPropagationPeer(propagation,
                                propagation_hash_3.data(),
                                nullptr,
                                nullptr,
                                propagation_limits.max_peers);
    assert(propagation.peers.size() == 2);
    assert(findPropagationPeer(propagation, propagation_hash.data()) == nullptr);
    assert(findPropagationPeer(propagation, propagation_hash_3.data()) != nullptr);

    const auto transient_a = filled_hash<reticulum::kFullHashSize>(0xD0);
    const auto transient_b = filled_hash<reticulum::kFullHashSize>(0xE0);
    const auto transient_c = filled_hash<reticulum::kFullHashSize>(0xF0);
    const uint8_t propagated_a[] = {0x90, 0x91, 0xA0, 0xA1};
    const uint8_t propagated_b[] = {0x90, 0x92, 0xB0};
    assert(rememberPropagationEntry(propagation,
                                    transient_a.data(),
                                    delivery_hash.data(),
                                    propagated_a,
                                    sizeof(propagated_a),
                                    900,
                                    propagation_limits.max_entries));
    assert(findPropagationEntry(propagation, transient_a.data()) != nullptr);
    assert(findPropagationEntry(propagation, transient_b.data()) == nullptr);

    auto append_runtime_id = [](PropagationIdList& out, const auto& id)
    {
        ResourcePayloadBuffer item;
        item.assign(id.begin(), id.end());
        out.push_back(std::move(item));
    };

    PropagationIdList offer_ids;
    append_runtime_id(offer_ids, transient_a);
    append_runtime_id(offer_ids, transient_b);
    offer_ids.push_back(ResourcePayloadBuffer{0x01, 0x02});
    PropagationIdList missing_ids =
        collectMissingPropagationTransientIds(propagation, offer_ids);
    assert(missing_ids.size() == 1);
    assert(missing_ids.front().size() == transient_b.size());
    assert(std::memcmp(missing_ids.front().data(), transient_b.data(), transient_b.size()) == 0);

    rememberPropagationTransient(propagation,
                                 transient_b.data(),
                                 false,
                                 910,
                                 propagation_limits.max_transients);
    bool delivered = true;
    assert(hasSeenPropagationTransient(propagation, transient_b.data(), &delivered));
    assert(!delivered);
    rememberPropagationTransient(propagation,
                                 transient_b.data(),
                                 true,
                                 920,
                                 propagation_limits.max_transients);
    assert(hasSeenPropagationTransient(propagation, transient_b.data(), &delivered));
    assert(delivered);
    assert(propagation.transients.front().seen_s == 920);
    missing_ids = collectMissingPropagationTransientIds(propagation, offer_ids);
    assert(missing_ids.empty());

    assert(rememberPropagationEntry(propagation,
                                    transient_b.data(),
                                    delivery_hash.data(),
                                    propagated_b,
                                    sizeof(propagated_b),
                                    930,
                                    propagation_limits.max_entries));
    PropagationIdList destination_ids =
        collectPropagationEntryIdsForDestination(propagation, delivery_hash.data());
    assert(destination_ids.size() == 2);

    PropagationIdList want_ids;
    append_runtime_id(want_ids, transient_a);
    append_runtime_id(want_ids, transient_b);
    PropagationMessageSelection selection =
        collectPropagationMessagesForWants(propagation,
                                           want_ids,
                                           delivery_hash.data(),
                                           0,
                                           24,
                                           16);
    assert(selection.messages.size() == 2);
    assert(selection.served_count == 2);
    assert(findPropagationEntry(propagation, transient_a.data())->served_count == 1);
    assert(findPropagationEntry(propagation, transient_b.data())->served_count == 1);

    selection = collectPropagationMessagesForWants(propagation,
                                                   want_ids,
                                                   delivery_hash.data(),
                                                   24 + sizeof(propagated_a) + 16,
                                                   24,
                                                   16);
    assert(selection.messages.size() == 1);
    assert(selection.served_count == 1);

    assert(removePropagationEntriesForDestination(propagation,
                                                  transient_a.data(),
                                                  delivery_hash.data()) == 1);
    assert(findPropagationEntry(propagation, transient_a.data()) == nullptr);

    assert(rememberPropagationEntry(propagation,
                                    transient_c.data(),
                                    delivery_hash.data(),
                                    propagated_a,
                                    sizeof(propagated_a),
                                    940,
                                    propagation_limits.max_entries));
    assert(propagation.entries.size() == 2);

    PropagationRuntime stale_propagation{};
    assert(rememberPropagationEntry(stale_propagation,
                                    transient_a.data(),
                                    delivery_hash.data(),
                                    propagated_a,
                                    sizeof(propagated_a),
                                    100,
                                    propagation_limits.max_entries));
    rememberPropagationTransient(stale_propagation,
                                 transient_b.data(),
                                 false,
                                 100,
                                 propagation_limits.max_transients);
    PropagationPeerState& stale_peer =
        upsertPropagationPeer(stale_propagation,
                              propagation_hash.data(),
                              delivery_hash.data(),
                              identity_hash.data(),
                              propagation_limits.max_peers);
    markPropagationPeerSeen(stale_peer, 100);
    cullPropagationRuntime(stale_propagation, 119, propagation_limits);
    assert(stale_propagation.entries.empty());
    assert(stale_propagation.transients.size() == 1);
    assert(stale_propagation.peers.empty());
    cullPropagationRuntime(stale_propagation, 121, propagation_limits);
    assert(stale_propagation.transients.empty());

    PropagationRuntime message_propagation{};
    PropagationPeerState& message_peer =
        upsertPropagationPeer(message_propagation,
                              propagation_hash.data(),
                              delivery_hash.data(),
                              identity_hash.data(),
                              propagation_limits.max_peers);
    markPropagationPeerSeen(message_peer, 1000);

    PropagationMessageContext message_context{};
    message_context.local_delivery_hash_known = true;
    copy_hash(message_context.local_delivery_hash, delivery_hash);
    message_context.remote_propagation_hash_known = true;
    copy_hash(message_context.remote_propagation_hash, propagation_hash);
    message_context.now_s = 1200;
    message_context.max_entries = propagation_limits.max_entries;
    message_context.max_transients = propagation_limits.max_transients;

    const uint8_t short_propagated[] = {0x01, 0x02, 0x03};
    PropagationMessageAcceptance message_acceptance{};
    assert(!planPropagationMessageAcceptance(message_propagation,
                                             short_propagated,
                                             sizeof(short_propagated),
                                             message_context,
                                             &message_acceptance));
    assert(message_acceptance.action == PropagationMessageAction::Rejected);

    std::vector<uint8_t> local_propagated;
    local_propagated.insert(local_propagated.end(),
                            delivery_hash.begin(),
                            delivery_hash.end());
    local_propagated.push_back(0xA1);
    local_propagated.push_back(0xA2);
    local_propagated.push_back(0xA3);
    assert(planPropagationMessageAcceptance(message_propagation,
                                            local_propagated.data(),
                                            local_propagated.size(),
                                            message_context,
                                            &message_acceptance));
    assert(message_acceptance.action == PropagationMessageAction::DeliverLocal);
    assert(same_hash(message_acceptance.destination_hash, delivery_hash));
    assert(message_acceptance.local_delivery_payload.size() == 3);
    assert(message_acceptance.local_delivery_payload[0] == 0xA1);
    assert(!hasSeenPropagationTransient(message_propagation,
                                        message_acceptance.transient_id,
                                        nullptr));
    notePropagationLocalDeliveryResult(message_propagation,
                                       message_acceptance.transient_id,
                                       true,
                                       1201,
                                       propagation_limits.max_transients);
    assert(hasSeenPropagationTransient(message_propagation,
                                       message_acceptance.transient_id,
                                       &delivered));
    assert(delivered);
    assert(planPropagationMessageAcceptance(message_propagation,
                                            local_propagated.data(),
                                            local_propagated.size(),
                                            message_context,
                                            &message_acceptance));
    assert(message_acceptance.action == PropagationMessageAction::Duplicate);

    PropagationRuntime forwarded_propagation{};
    PropagationPeerState& forwarded_peer =
        upsertPropagationPeer(forwarded_propagation,
                              propagation_hash.data(),
                              delivery_hash.data(),
                              identity_hash.data(),
                              propagation_limits.max_peers);
    markPropagationPeerSeen(forwarded_peer, 1000);
    PropagationMessageContext forwarded_context = message_context;
    forwarded_context.now_s = 1210;

    const auto nonlocal_delivery_hash =
        filled_hash<reticulum::kTruncatedHashSize>(0x55);
    std::vector<uint8_t> nonlocal_propagated;
    nonlocal_propagated.insert(nonlocal_propagated.end(),
                               nonlocal_delivery_hash.begin(),
                               nonlocal_delivery_hash.end());
    nonlocal_propagated.push_back(0xB1);
    nonlocal_propagated.push_back(0xB2);
    assert(planPropagationMessageAcceptance(forwarded_propagation,
                                            nonlocal_propagated.data(),
                                            nonlocal_propagated.size(),
                                            forwarded_context,
                                            &message_acceptance));
    assert(message_acceptance.action == PropagationMessageAction::Stored);
    const PropagationEntry* stored_message =
        findPropagationEntry(forwarded_propagation, message_acceptance.transient_id);
    assert(stored_message != nullptr);
    assert(same_hash(stored_message->destination_hash, nonlocal_delivery_hash));
    assert(stored_message->lxmf_data.size() == nonlocal_propagated.size());
    assert(hasSeenPropagationTransient(forwarded_propagation,
                                       message_acceptance.transient_id,
                                       &delivered));
    assert(!delivered);
    PropagationPeerState* stored_peer =
        findPropagationPeer(forwarded_propagation, propagation_hash.data());
    assert(stored_peer != nullptr);
    assert(stored_peer->last_seen_s == 1210);
    assert(planPropagationMessageAcceptance(forwarded_propagation,
                                            nonlocal_propagated.data(),
                                            nonlocal_propagated.size(),
                                            forwarded_context,
                                            &message_acceptance));
    assert(message_acceptance.action == PropagationMessageAction::Duplicate);

    PropagationRuntime batch_propagation{};
    PropagationBatchContext batch_context{};
    batch_context.local_delivery_hash_known = true;
    copy_hash(batch_context.local_delivery_hash, delivery_hash);
    batch_context.peer_context.remote_identity_known = true;
    copy_hash(batch_context.peer_context.remote_propagation_hash, propagation_hash);
    copy_hash(batch_context.peer_context.remote_delivery_hash, delivery_hash);
    copy_hash(batch_context.peer_context.remote_identity_hash, identity_hash);
    batch_context.now_s = 1300;
    const PropagationBatchLimits batch_limits{
        propagation_limits.max_entries,
        propagation_limits.max_transients,
        propagation_limits.max_peers,
        1};

    std::vector<std::vector<uint8_t>> batch_messages;
    batch_messages.push_back(local_propagated);
    batch_messages.push_back(nonlocal_propagated);
    std::vector<uint8_t> encoded_batch(128, 0);
    std::size_t encoded_batch_len = encoded_batch.size();
    assert(::chat::lxmf::encodePropagationBatch(1300.5,
                                                batch_messages,
                                                encoded_batch.data(),
                                                &encoded_batch_len));
    encoded_batch.resize(encoded_batch_len);

    PropagationBatchAcceptance batch_acceptance{};
    assert(!planPropagationBatchAcceptance(batch_propagation,
                                           encoded_batch.data(),
                                           encoded_batch.size(),
                                           batch_context,
                                           batch_limits,
                                           &batch_acceptance));
    assert(batch_acceptance.messages.empty());

    batch_context.offer_validated = true;
    assert(planPropagationBatchAcceptance(batch_propagation,
                                          encoded_batch.data(),
                                          encoded_batch.size(),
                                          batch_context,
                                          batch_limits,
                                          &batch_acceptance));
    assert(batch_acceptance.remote_propagation_hash_known);
    assert(same_hash(batch_acceptance.remote_propagation_hash, propagation_hash));
    assert(batch_acceptance.messages.size() == 2);
    assert(batch_acceptance.messages[0].action ==
           PropagationMessageAction::DeliverLocal);
    assert(batch_acceptance.messages[1].action == PropagationMessageAction::Stored);
    PropagationPeerState* batch_peer =
        findPropagationPeer(batch_propagation, propagation_hash.data());
    assert(batch_peer != nullptr);
    assert(batch_peer->last_seen_s == 1300);
    assert(findPropagationEntry(batch_propagation,
                                batch_acceptance.messages[1].transient_id) != nullptr);
    notePropagationLocalDeliveryResult(batch_propagation,
                                       batch_acceptance.messages[0].transient_id,
                                       true,
                                       1301,
                                       propagation_limits.max_transients);
    notePropagationBatchMessageHandled(batch_propagation, batch_acceptance);
    notePropagationBatchMessageHandled(batch_propagation, batch_acceptance);
    assert(batch_peer->incoming_messages == 2);

    PropagationRuntime service_propagation{};
    PropagationServicePeerContext service_peer{};
    service_peer.remote_identity_known = true;
    copy_hash(service_peer.remote_propagation_hash, propagation_hash);
    copy_hash(service_peer.remote_delivery_hash, delivery_hash);
    copy_hash(service_peer.remote_identity_hash, identity_hash);
    const PropagationServiceLimits service_limits{
        propagation_limits.max_transients,
        propagation_limits.max_peers,
        24,
        16};

    std::vector<std::vector<uint8_t>> service_offer_ids;
    service_offer_ids.emplace_back(transient_a.begin(), transient_a.end());
    std::vector<uint8_t> encoded_offer_ids(64, 0);
    std::size_t encoded_offer_ids_len = encoded_offer_ids.size();
    assert(::chat::lxmf::encodePropagationIdListPayload(service_offer_ids,
                                                        encoded_offer_ids.data(),
                                                        &encoded_offer_ids_len));
    encoded_offer_ids.resize(encoded_offer_ids_len);

    std::vector<uint8_t> offer_payload;
    offer_payload.push_back(0x92);
    offer_payload.push_back(0xC4);
    offer_payload.push_back(0x01);
    offer_payload.push_back(0xA5);
    offer_payload.insert(offer_payload.end(),
                         encoded_offer_ids.begin(),
                         encoded_offer_ids.end());

    ::chat::lxmf::DecodedLinkRequest service_request{};
    propagationServicePathHash(PropagationServicePath::Offer,
                               service_request.path_hash);
    service_request.packed_data = offer_payload;
    PropagationServiceResponse service_response{};
    assert(planPropagationServiceResponse(service_propagation,
                                          service_request,
                                          service_peer,
                                          1100,
                                          service_limits,
                                          &service_response));
    assert(service_response.send_response);
    assert(service_response.offer_validated);
    assert(!service_response.response_data_is_nil);
    assert(service_response.packed_response.size() == 1);
    assert(service_response.packed_response[0] == 0xC3);
    PropagationPeerState* service_peer_state =
        findPropagationPeer(service_propagation, propagation_hash.data());
    assert(service_peer_state != nullptr);
    assert(service_peer_state->last_seen_s == 1100);

    assert(rememberPropagationEntry(service_propagation,
                                    transient_a.data(),
                                    delivery_hash.data(),
                                    propagated_a,
                                    sizeof(propagated_a),
                                    1110,
                                    propagation_limits.max_entries));

    ::chat::lxmf::DecodedLinkRequest get_list_request{};
    propagationServicePathHash(PropagationServicePath::Get,
                               get_list_request.path_hash);
    get_list_request.packed_data = {0x92, 0xC0, 0xC0};
    service_response = {};
    assert(planPropagationServiceResponse(service_propagation,
                                          get_list_request,
                                          service_peer,
                                          1120,
                                          service_limits,
                                          &service_response));
    assert(service_response.send_response);
    assert(service_response.packed_response.size() ==
           3 + reticulum::kFullHashSize);
    assert(service_response.packed_response[0] == 0x91);
    assert(service_response.packed_response[1] == 0xC4);
    assert(service_response.packed_response[2] == reticulum::kFullHashSize);
    assert(std::memcmp(service_response.packed_response.data() + 3,
                       transient_a.data(),
                       reticulum::kFullHashSize) == 0);

    std::vector<uint8_t> encoded_wants(64, 0);
    std::size_t encoded_wants_len = encoded_wants.size();
    assert(::chat::lxmf::encodePropagationIdListPayload(service_offer_ids,
                                                        encoded_wants.data(),
                                                        &encoded_wants_len));
    encoded_wants.resize(encoded_wants_len);
    ::chat::lxmf::DecodedLinkRequest get_messages_request{};
    propagationServicePathHash(PropagationServicePath::Get,
                               get_messages_request.path_hash);
    get_messages_request.packed_data.push_back(0x93);
    get_messages_request.packed_data.insert(get_messages_request.packed_data.end(),
                                            encoded_wants.begin(),
                                            encoded_wants.end());
    get_messages_request.packed_data.push_back(0xC0);
    get_messages_request.packed_data.push_back(0x01);
    service_response = {};
    assert(planPropagationServiceResponse(service_propagation,
                                          get_messages_request,
                                          service_peer,
                                          1130,
                                          service_limits,
                                          &service_response));
    assert(service_response.send_response);
    assert(service_response.packed_response.size() == 3 + sizeof(propagated_a));
    assert(service_response.packed_response[0] == 0x91);
    assert(service_response.packed_response[1] == 0xC4);
    assert(service_response.packed_response[2] == sizeof(propagated_a));
    assert(std::memcmp(service_response.packed_response.data() + 3,
                       propagated_a,
                       sizeof(propagated_a)) == 0);
    service_peer_state = findPropagationPeer(service_propagation,
                                             propagation_hash.data());
    assert(service_peer_state != nullptr);
    assert(service_peer_state->served_messages == 1);

    LxmfDeliveryContext delivery_context{};
    delivery_context.peer_node_id = 0x01020304;
    delivery_context.local_node_id = 0x11121314;
    delivery_context.message_id = 0xAABBCCDD;
    delivery_context.has_message_hash = true;
    for (std::size_t index = 0; index < sizeof(delivery_context.message_hash); ++index)
    {
        delivery_context.message_hash[index] = static_cast<uint8_t>(0x80U + index);
    }
    delivery_context.timestamp_s = 1234567890;
    delivery_context.peer_identity =
        ::chat::makeReticulumPeerIdentity(delivery_hash.data(), identity_hash.data());
    delivery_context.rx_meta.origin = ::chat::RxOrigin::Mesh;
    delivery_context.rx_meta.direct = true;
    delivery_context.rx_meta.rssi_dbm_x10 = -710;

    ::chat::lxmf::DecodedTextPayload text_payload{};
    text_payload.content = "verified reticulum delivery";
    uint8_t encoded_text[128] = {};
    std::size_t encoded_text_len = sizeof(encoded_text);
    assert(::chat::lxmf::encodeTextPayload(1.0,
                                           "",
                                           text_payload.content.c_str(),
                                           encoded_text,
                                           &encoded_text_len));
    LxmfVerifiedDelivery verified_text{};
    assert(materialiseVerifiedLxmfDelivery(encoded_text,
                                           encoded_text_len,
                                           delivery_context,
                                           &verified_text));
    assert(verified_text.kind == LxmfDeliveryKind::Text);
    const LxmfMaterialisedText& text_delivery = verified_text.text;
    assert(text_delivery.text == text_payload.content);
    assert(text_delivery.incoming.channel == ::chat::ChannelId::PRIMARY);
    assert(text_delivery.incoming.from == delivery_context.peer_node_id);
    assert(text_delivery.incoming.to == delivery_context.local_node_id);
    assert(text_delivery.incoming.msg_id == delivery_context.message_id);
    assert(text_delivery.incoming.has_reticulum_lxmf_hash);
    assert(std::memcmp(text_delivery.incoming.reticulum_lxmf_hash,
                       delivery_context.message_hash,
                       sizeof(delivery_context.message_hash)) == 0);
    assert(text_delivery.incoming.timestamp == delivery_context.timestamp_s);
    assert(text_delivery.incoming.hop_limit == 0xFF);
    assert(text_delivery.incoming.encrypted);
    assert(!text_delivery.incoming.source_unverified);
    assert(text_delivery.incoming.text == text_payload.content);
    assert(::chat::sameReticulumPeerIdentity(text_delivery.incoming.reticulum_identity,
                                             delivery_context.peer_identity));
    assert(text_delivery.incoming.rx_meta.rssi_dbm_x10 == -710);

    ::chat::infra::IncomingTextQueue<1, 64> text_queue{};
    ::chat::infra::IncomingQueuePushReport text_report{};
    assert(text_queue.push(text_delivery.incoming,
                           text_delivery.text.data(),
                           text_delivery.text.size(),
                           ::chat::infra::IncomingQueuePriority::P1User,
                           &text_report));
    ::chat::MeshIncomingText popped_text{};
    assert(text_queue.pop(&popped_text));
    assert(popped_text.text == text_payload.content);
    assert(popped_text.has_reticulum_lxmf_hash);
    assert(std::memcmp(popped_text.reticulum_lxmf_hash,
                       delivery_context.message_hash,
                       sizeof(delivery_context.message_hash)) == 0);
    assert(!popped_text.source_unverified);
    assert(::chat::sameReticulumPeerIdentity(popped_text.reticulum_identity,
                                             delivery_context.peer_identity));
    assert(popped_text.rx_meta.rssi_dbm_x10 == -710);

    LxmfDeliveryContext unverified_context = delivery_context;
    unverified_context.source_unverified = true;
    unverified_context.peer_identity =
        ::chat::makeReticulumDestinationIdentity(delivery_hash.data());
    LxmfMaterialisedText unverified_text{};
    assert(materialiseLxmfTextDelivery(text_payload,
                                       unverified_context,
                                       &unverified_text));
    assert(unverified_text.incoming.source_unverified);
    ::chat::infra::IncomingTextQueue<1, 64> unverified_text_queue{};
    assert(unverified_text_queue.push(unverified_text.incoming,
                                      unverified_text.text.data(),
                                      unverified_text.text.size(),
                                      ::chat::infra::IncomingQueuePriority::P0Critical));
    assert(unverified_text_queue.pop(&popped_text));
    assert(popped_text.source_unverified);
    assert(::chat::sameReticulumPeerIdentity(
        popped_text.reticulum_identity,
        unverified_context.peer_identity));

    ::chat::infra::IncomingTextQueue<1, 64> accepted_text_queue{};
    assert(accepted_text_queue.push(text_delivery.incoming,
                                    text_delivery.text.data(),
                                    text_delivery.text.size(),
                                    ::chat::infra::IncomingQueuePriority::P0Critical,
                                    &text_report));
    assert(!accepted_text_queue.push(text_delivery.incoming,
                                     text_delivery.text.data(),
                                     text_delivery.text.size(),
                                     ::chat::infra::IncomingQueuePriority::P0Critical,
                                     &text_report));
    assert(text_report.dropped_new);
    assert(!text_report.dropped_existing);
    assert(accepted_text_queue.pop(&popped_text));
    assert(popped_text.text == text_payload.content);

    ::chat::lxmf::DecodedAppData app_payload{};
    app_payload.portnum = 77;
    app_payload.packet_id = 0x0102;
    app_payload.request_id = 0x03040506;
    app_payload.want_response = true;
    app_payload.payload = {0x21, 0x22, 0x23};
    uint8_t encoded_app_data[64] = {};
    std::size_t encoded_app_data_len = sizeof(encoded_app_data);
    assert(::chat::lxmf::encodeAppDataPayload(app_payload.portnum,
                                              app_payload.packet_id,
                                              app_payload.request_id,
                                              app_payload.want_response,
                                              app_payload.payload.data(),
                                              app_payload.payload.size(),
                                              encoded_app_data,
                                              &encoded_app_data_len));
    LxmfVerifiedDelivery verified_app_data{};
    assert(materialiseVerifiedLxmfDelivery(encoded_app_data,
                                           encoded_app_data_len,
                                           delivery_context,
                                           &verified_app_data));
    assert(verified_app_data.kind == LxmfDeliveryKind::AppData);
    const LxmfMaterialisedAppData& app_delivery = verified_app_data.app_data;
    assert(app_delivery.incoming.portnum == app_payload.portnum);
    assert(app_delivery.incoming.from == delivery_context.peer_node_id);
    assert(app_delivery.incoming.to == delivery_context.local_node_id);
    assert(app_delivery.incoming.packet_id == app_payload.packet_id);
    assert(app_delivery.incoming.request_id == app_payload.request_id);
    assert(app_delivery.incoming.channel == ::chat::ChannelId::PRIMARY);
    assert(app_delivery.incoming.want_response);
    assert(app_delivery.incoming.rx_meta.rssi_dbm_x10 == -710);
    assert(app_delivery.payload == app_payload.payload);

    const uint32_t team_location_ports[] = {
        team::proto::TEAM_POSITION_APP,
        team::proto::TEAM_TRACK_APP,
    };
    for (uint32_t portnum : team_location_ports)
    {
        ::chat::lxmf::DecodedAppData team_payload{};
        team_payload.portnum = portnum;
        team_payload.packet_id = 0x11223344;
        team_payload.payload = {0x31, 0x32, 0x33, 0x34};
        uint8_t encoded_team_app_data[64] = {};
        std::size_t encoded_team_app_data_len = sizeof(encoded_team_app_data);
        assert(::chat::lxmf::encodeAppDataPayload(team_payload.portnum,
                                                  team_payload.packet_id,
                                                  team_payload.request_id,
                                                  team_payload.want_response,
                                                  team_payload.payload.data(),
                                                  team_payload.payload.size(),
                                                  encoded_team_app_data,
                                                  &encoded_team_app_data_len));
        LxmfVerifiedDelivery verified_team_app_data{};
        assert(materialiseVerifiedLxmfDelivery(encoded_team_app_data,
                                               encoded_team_app_data_len,
                                               delivery_context,
                                               &verified_team_app_data));
        assert(verified_team_app_data.kind == LxmfDeliveryKind::AppData);
        const LxmfMaterialisedAppData& team_delivery =
            verified_team_app_data.app_data;
        assert(team_delivery.incoming.portnum == team_payload.portnum);
        assert(team_delivery.incoming.from == delivery_context.peer_node_id);
        assert(team_delivery.incoming.to == delivery_context.local_node_id);
        assert(team_delivery.incoming.packet_id == team_payload.packet_id);
        assert(team_delivery.incoming.channel == ::chat::ChannelId::PRIMARY);
        assert(!team_delivery.incoming.want_response);
        assert(team_delivery.payload == team_payload.payload);
    }

    const uint8_t invalid_delivery_payload[] = {0x01, 0x02, 0x03};
    LxmfVerifiedDelivery invalid_delivery{};
    assert(!materialiseVerifiedLxmfDelivery(invalid_delivery_payload,
                                            sizeof(invalid_delivery_payload),
                                            delivery_context,
                                            &invalid_delivery));
    assert(invalid_delivery.kind == LxmfDeliveryKind::None);

    return 0;
}
