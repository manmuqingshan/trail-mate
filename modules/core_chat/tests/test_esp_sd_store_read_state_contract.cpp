#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace
{

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    assert(stream.is_open());
    std::ostringstream out;
    out << stream.rdbuf();
    return out.str();
}

bool contains(const std::string& haystack, const char* needle)
{
    return haystack.find(needle) != std::string::npos;
}

std::size_t positionOf(const std::string& haystack, const char* needle)
{
    const auto pos = haystack.find(needle);
    assert(pos != std::string::npos);
    return pos;
}

std::size_t positionOfAfter(const std::string& haystack,
                            const char* needle,
                            std::size_t offset)
{
    const auto pos = haystack.find(needle, offset);
    assert(pos != std::string::npos);
    return pos;
}

std::string bodyBetween(const std::string& source,
                        const char* begin,
                        const char* end)
{
    const auto begin_pos = positionOf(source, begin);
    const auto end_pos = positionOfAfter(source, end, begin_pos);
    return source.substr(begin_pos, end_pos - begin_pos);
}

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path repo_root = argv[1];
    const std::string header = readFile(
        repo_root /
        "platform/esp/arduino_common/include/platform/esp/arduino_common/chat/infra/store/sd_store.h");
    const std::string source = readFile(
        repo_root /
        "platform/esp/arduino_common/src/chat/infra/store/sd_store.cpp");
    const std::string codec = readFile(
        repo_root /
        "platform/esp/arduino_common/src/chat/infra/store/protocol_chat_codec.cpp");
    const std::string journal = readFile(
        repo_root /
        "platform/esp/arduino_common/src/chat/infra/store/fixed_slot_journal.cpp");
    const std::string peer_header = readFile(
        repo_root /
        "platform/esp/arduino_common/include/platform/esp/arduino_common/chat/infra/store/sd_protocol_peer_repository.h");
    const std::string peer_source = readFile(
        repo_root /
        "platform/esp/arduino_common/src/chat/infra/store/sd_protocol_peer_repository.cpp");
    const std::string peer_codec = readFile(
        repo_root /
        "platform/esp/arduino_common/src/chat/infra/store/protocol_peer_codec.cpp");
    const std::string bindings = readFile(
        repo_root /
        "platform/esp/arduino_common/src/app_context_platform_bindings.cpp");
    const std::string startup = readFile(
        repo_root /
        "apps/esp32_lvgl/src/esp32_lvgl_arduino_startup_runtime.cpp");
    const std::string storage_runtime = readFile(
        repo_root /
        "platform/esp/arduino_common/src/storage/storage_runtime.cpp");
    const std::string board_runtime = readFile(
        repo_root / "platform/esp/boards/src/board_runtime.cpp");
    const std::string storage_owner_header = readFile(
        repo_root /
        "platform/esp/common/include/platform/esp/common/storage/storage_maintenance_owner.h");
    const std::string state_lock_header = readFile(
        repo_root /
        "platform/esp/arduino_common/include/platform/esp/arduino_common/storage/scoped_state_lock.h");
    const std::string idf_facade = readFile(
        repo_root /
        "apps/esp32_lvgl/src/esp32_lvgl_idf_app_facade_runtime.cpp");
    const std::string idf_startup = readFile(
        repo_root /
        "apps/esp32_lvgl/src/esp32_lvgl_startup_runtime.cpp");
    const std::string idf_storage_runtime = readFile(
        repo_root /
        "platform/esp/idf_common/src/storage_runtime.cpp");
    const std::string linux_config_runtime = readFile(
        repo_root /
        "platform/linux/common/src/app/linux_app_services.cpp");

    assert(contains(header, "kRoot = \"/data/v2\""));
    assert(contains(header, "kMeshtasticRoot = \"/data/v2/mt/chat\""));
    assert(contains(header, "kMeshCoreRoot = \"/data/v2/mc/chat\""));
    assert(contains(header, "kReticulumRoot = \"/data/v2/rt/chat\""));
    assert(contains(header, "PsramAllocator"));
    assert(contains(header, "mutable ScratchBuffer scratch_"));
    assert(!contains(header, "mutable uint8_t scratch_"));
    assert(!contains(header, "RecordV2"));
    assert(!contains(header, "RecordV3"));
    assert(!contains(header, "RecordV4"));
    assert(!contains(header, "kReadStateFile"));
    assert(!contains(source, "\"/chat/"));
    assert(!contains(source, "Legacy"));

    assert(contains(codec, "kMeshtasticTextMax"));
    assert(contains(codec, "kMeshCoreTextMax"));
    assert(contains(codec, "kReticulumTextMax"));
    assert(contains(codec, "struct MeshtasticMessageSlot"));
    assert(contains(codec, "struct MeshCoreMessageSlot"));
    assert(contains(codec, "struct ReticulumMessageSlot"));
    assert(contains(journal, "kStorageSchemaVersion"));
    assert(contains(journal, "PartialTail"));
    assert(contains(journal, "replaceFileAtomically"));
    assert(contains(journal, "recoverAtomicFile"));

    const std::string append_body = bodyBetween(
        source,
        "bool SdStore::appendInternal",
        "std::vector<ChatMessage> SdStore::loadRecent");
    assert(positionOf(append_body, "appendMessageRecord(message, sequence)") <
           positionOf(append_body, "rememberReticulumHash"));
    assert(positionOf(append_body, "rememberReticulumHash") <
           positionOf(append_body, "appendCatalogProjection"));
    assert(contains(append_body, "projection_dirty_"));
    assert(contains(append_body, "authoritative=1"));
    assert(contains(append_body, "return true;"));
    assert(contains(append_body, "ScopedPersistenceLease"));
    const std::size_t append_projection_write =
        positionOf(append_body,
                   "appendCatalogProjection(projection_snapshot)");
    assert(append_projection_write <
           positionOfAfter(append_body,
                           "ScopedRecursiveStateLock state_lock",
                           append_projection_write));

    const std::string unread_body =
        bodyBetween(source, "bool SdStore::setUnread", "int SdStore::getUnread");
    assert(positionOf(unread_body, "appendReadProjection(projection)") <
           positionOf(unread_body,
                      "appendCatalogProjection(catalog_snapshot)"));
    assert(contains(unread_body, "last_read_sequence"));
    const std::size_t unread_catalog_write =
        positionOf(unread_body,
                   "appendCatalogProjection(catalog_snapshot)");
    assert(unread_catalog_write <
           positionOfAfter(unread_body,
                           "ScopedRecursiveStateLock state_lock",
                           unread_catalog_write));

    const std::string message_page_body = bodyBetween(
        source,
        "SdStore::loadPageFromLatest",
        "SdStore::loadConversationPage");
    assert(contains(message_page_body, "ScopedPersistenceLease"));
    assert(!contains(message_page_body, "ScopedRecursiveStateLock"));

    const std::string clear_conversation_body = bodyBetween(
        source,
        "void SdStore::clearConversation",
        "void SdStore::clearAll");
    const std::size_t clear_read_tombstone =
        positionOf(clear_conversation_body,
                   "appendReadProjection(read_tombstone)");
    assert(clear_read_tombstone <
           positionOfAfter(clear_conversation_body,
                           "ScopedRecursiveStateLock state_lock",
                           clear_read_tombstone));

    const std::string clear_all_body = bodyBetween(
        source,
        "void SdStore::clearAll",
        "bool SdStore::updateMessageStatus");
    assert(positionOf(clear_all_body, "ensureLayout()") <
           positionOf(clear_all_body,
                      "ScopedRecursiveStateLock state_lock"));

    const std::string status_update_body = bodyBetween(
        source,
        "bool SdStore::updateMessageStatusForProtocol",
        "bool SdStore::getMessage");
    const std::size_t status_projection_write =
        positionOf(status_update_body,
                   "appendStatusProjection(protocol, projection)");
    assert(status_projection_write <
           positionOfAfter(status_update_body,
                           "ScopedRecursiveStateLock state_lock",
                           status_projection_write));

    const std::string ordinal_read_body = bodyBetween(
        source,
        "bool SdStore::readMessageByOrdinal",
        "bool SdStore::latestStoredMessage");
    assert(positionOf(ordinal_read_body, "journal_.read") <
           positionOf(ordinal_read_body,
                      "ScopedRecursiveStateLock state_lock"));

    const std::string conversation_query = bodyBetween(
        source,
        "SdStore::loadConversationPageForProtocol",
        "bool SdStore::setUnread");
    assert(contains(conversation_query, "sameProtocol"));

    const std::string clear_all = bodyBetween(
        source,
        "void SdStore::clearAll",
        "bool SdStore::updateMessageStatus");
    assert(contains(clear_all, "removeTree(protocolRoot(protocol))"));
    assert(!contains(clear_all, "RecordV"));

    assert(contains(peer_header, "IProtocolPeerRepository"));
    assert(contains(peer_header, "PsramAllocator<MeshPeerRecord>"));
    assert(contains(peer_source, "\"peers.snapshot\""));
    assert(contains(peer_source, "\"peers.delta\""));
    assert(contains(peer_source, "\"contacts.snapshot\""));
    assert(contains(peer_source, "\"contacts.delta\""));
    assert(contains(peer_source, "kEphemeralPeerCapacity = 2048U"));
    assert(contains(peer_source, "kProtectedContactCapacity = 4096U"));
    assert(contains(peer_source, "peerIsProtected"));
    assert(contains(peer_source, "peerReferencedByConversation"));
    assert(contains(peer_source, "replaceFileAtomically"));
    assert(contains(peer_header, "pending_peer_observations_"));
    assert(contains(peer_source, "queueDeferredObservation"));
    assert(contains(peer_source, "StorageUnavailable"));
    assert(contains(peer_source, "MeshPeerDirectoryStatusCode::Busy"));
    assert(contains(peer_source,
                    "MeshPeerDirectoryStatusCode::DeviceUnavailable"));
    assert(contains(peer_source,
                    "SdProtocolPeerRepository::flushPendingDeltas"));
    assert(contains(peer_source, "operationFailureKind"));
    assert(contains(peer_codec, "validPeerIdentityForProtocol"));
    assert(contains(peer_codec, "validContactIdentityForProtocol"));
    const std::string peer_prefix = bodyBetween(
        peer_codec,
        "struct PeerPrefix",
        "struct NodeFactsSlot");
    assert(!contains(peer_prefix, "user_alias"));
    assert(!contains(bindings, "EspSdMeshPeerDirectoryBlobStore"));
    assert(!contains(bindings, "chat::meshtastic::NodeStore"));
    assert(!contains(bindings, "chat::contacts::ContactStore"));
    assert(!contains(bindings, "\"/nodes.bin\""));
    assert(!contains(bindings, "\"/contacts.dat\""));
    assert(!contains(bindings, "\"/mesh/peers.bin\""));
    assert(!std::filesystem::exists(
        repo_root /
        "platform/esp/arduino_common/src/chat/infra/meshtastic/node_store.cpp"));
    assert(!std::filesystem::exists(
        repo_root /
        "platform/esp/arduino_common/src/chat/infra/contact_store.cpp"));

    assert(contains(source, "stepSeenRebuild"));
    assert(contains(source, "stepProtocolCatalogReconcile"));
    assert(contains(source, "stepConversationDirectoryReconcile"));
    assert(!contains(source, "loadSeenJournal"));
    assert(!contains(source, "rebuildSeenJournalFromMessages"));
    assert(!contains(source, "reconcileProtocolCatalog"));
    const std::string conversation_reconcile = bodyBetween(
        source,
        "SdStore::stepConversationDirectoryReconcile",
        "std::size_t SdStore::slotsPerMessageSegment");
    assert(contains(conversation_reconcile,
                    "maintenance_reconcile_segment_"));
    assert(contains(conversation_reconcile,
                    "ReconcileStepResult::InProgress"));
    assert(contains(conversation_reconcile,
                    "ConversationReconcilePhase::ScanUnread"));
    assert(!contains(conversation_reconcile,
                     "for (uint32_t segment"));
    assert(!contains(conversation_reconcile,
                     "countUnreadAfter"));
    const std::string flush_body = bodyBetween(
        source,
        "void SdStore::flush",
        "bool SdStore::ensureLayout");
    assert(!contains(flush_body, "journal_."));
    assert(!contains(flush_body, "compactProtocolProjections"));

    const std::string constructor_body = bodyBetween(
        source,
        "SdStore::SdStore()",
        "SdStore::~SdStore()");
    assert(!contains(constructor_body, "ensureLayout()"));
    assert(!contains(constructor_body, "loadRuntimeState()"));
    assert(!contains(constructor_body, "compactProtocolProjections"));
    assert(contains(constructor_body, "hydration=pending"));

    const std::string begin_body = bodyBetween(
        peer_source,
        "MeshPeerDirectoryStatus SdProtocolPeerRepository::begin()",
        "bool SdProtocolPeerRepository::ensureLayout()");
    assert(!contains(begin_body, "ensureLayout()"));
    assert(!contains(begin_body, "loadProtocol"));
    assert(!contains(begin_body, "compactProtocolAtBoot"));
    assert(contains(begin_body, "hydration=pending"));

    assert(positionOf(startup, "initializeShell()") <
           positionOf(startup, "startDeferredStorage()"));
    assert(contains(storage_runtime, "StorageMaintenanceOwner"));
    assert(contains(storage_runtime, "stepMaintenance"));
    assert(contains(storage_runtime, "StorageOperationBudget"));
    assert(contains(storage_owner_header, "step_budget"));
    assert(contains(journal, "FixedSlotJournalCursor"));
    assert(contains(storage_runtime, "storageCapabilities"));
    assert(!contains(storage_runtime, "ARDUINO_T_DECK"));
    assert(contains(board_runtime, "StorageBusTopology::SharedDisplaySpi"));
    assert(contains(board_runtime, "StorageBusTopology::Sdmmc"));
    assert(contains(state_lock_header, "StateLockResult"));
    assert(contains(state_lock_header, "Busy"));
    assert(contains(state_lock_header, "Unavailable"));
    assert(contains(storage_owner_header, "requestStop"));
    assert(contains(storage_owner_header, "latest_tick_generation_"));
    assert(contains(storage_runtime, "SdMaintenanceAdapter"));
    assert(contains(storage_runtime, "memory::admit"));
    assert(contains(storage_runtime, "generation"));
    assert(contains(storage_runtime, "tick_deferred_storage"));
    assert(contains(storage_runtime, "is_sleeping"));
    const std::string maintenance_step_body = bodyBetween(
        storage_runtime,
        "Result step(Operation operation,",
        "void cancelAtStepBoundary");
    const std::size_t peer_step =
        positionOf(maintenance_step_body,
                   "context_.peer_directory->stepMaintenance");
    const std::size_t peer_result_check =
        positionOfAfter(maintenance_step_body,
                        "if (!result.inProgress())",
                        peer_step);
    assert(peer_step < peer_result_check);
    assert(contains(storage_owner_header, "xTaskCreatePinnedToCore"));
    assert(contains(storage_owner_header, "xQueueCreate"));
    assert(contains(storage_owner_header, "xQueueSend"));
    assert(contains(storage_owner_header, "StorageRuntimeSnapshot"));
    assert(contains(storage_owner_header,
                    "std::atomic<bool> arm_event_pending_"));
    assert(contains(storage_owner_header, "compare_exchange_strong"));
    assert(contains(storage_owner_header, "terminal_without_completion"));
    assert(contains(storage_owner_header,
                    "config_.adapter->cancelAtStepBoundary(command.operation"));
    assert(!contains(storage_owner_header, "vTaskDelete(nullptr)"));
    assert(!contains(storage_runtime, "vTaskDelete(nullptr)"));
    assert(!contains(storage_runtime, "s_worker_task"));
    assert(!contains(storage_runtime, "storage_worker"));
    assert(contains(idf_facade, "createIdfChatStore"));
    assert(!contains(bodyBetween(idf_facade,
                                 "std::unique_ptr<chat::IChatStore> createIdfChatStore",
                                 "class IdfAppFacadeRuntime"),
                     "isReady()"));
    assert(contains(idf_facade, "startDeferredStorage"));
    assert(contains(idf_startup, "idf_app_runtime_access::startDeferredStorage()"));
    assert(contains(idf_storage_runtime, "StorageMaintenanceOwner"));
    assert(contains(idf_storage_runtime, "storageCapabilities"));
    assert(contains(idf_storage_runtime, "storageStartupGateSatisfied"));
    assert(contains(idf_storage_runtime, "compactionPending"));
    assert(contains(board_runtime, "storageStartupGateSatisfied"));
    assert(contains(board_runtime, "displayFrameCompletions"));
    const std::string idf_hydration_body = bodyBetween(
        idf_storage_runtime,
        "Result beginHydration",
        "Result stepHydration");
    assert(contains(idf_hydration_body, "streamPersistenceBlob"));
    assert(!contains(idf_hydration_body, "std::vector<uint8_t> blob"));
    const std::size_t idf_hydration_resume =
        positionOf(idf_hydration_body, "const bool resume");
    const std::size_t idf_hydration_resume_store =
        positionOfAfter(idf_hydration_body,
                        "store_->beginMaintenance",
                        idf_hydration_resume);
    const std::size_t idf_hydration_fresh_reset =
        positionOfAfter(idf_hydration_body,
                        "releasePeerHydrationPayload();",
                        idf_hydration_resume_store);
    assert(contains(idf_hydration_body,
                    "peer_hydration_generation_ == generation"));
    assert(contains(idf_hydration_body,
                    "peer_hydration_store_in_progress_ ||"));
    assert(idf_hydration_resume < idf_hydration_resume_store);
    assert(idf_hydration_resume_store < idf_hydration_fresh_reset);
    const std::string idf_hydration_step_body = bodyBetween(
        idf_storage_runtime,
        "Result stepHydration",
        "Result execute");
    const std::size_t idf_hydration_retry_guard =
        positionOf(idf_hydration_step_body,
                   "if (!store_result.retryable())");
    const std::size_t idf_hydration_terminal_release =
        positionOfAfter(idf_hydration_step_body,
                        "releasePeerHydrationPayload();",
                        idf_hydration_retry_guard);
    assert(idf_hydration_retry_guard < idf_hydration_terminal_release);
    const std::string idf_config_load_body = bodyBetween(
        idf_facade,
        "bool loadIdfAppConfig",
        "bool saveIdfAppConfig");
    assert(contains(idf_config_load_body, "get_blob_into"));
    assert(!contains(idf_config_load_body, "get_blob("));
    const std::string idf_config_submit_body = bodyBetween(
        idf_facade,
        "void saveConfig(app::AppConfigChangeSet changes) override",
        "void applyMeshConfig()");
    assert(contains(idf_config_submit_body,
                    "ConfigPersistenceUrgency::Debounced"));
    assert(!contains(idf_config_submit_body,
                     "ConfigPersistenceUrgency::Immediate"));
    const std::string linux_config_submit_body = bodyBetween(
        linux_config_runtime,
        "void LinuxAppServices::saveConfig(::app::AppConfigChangeSet changes)",
        "void LinuxAppServices::flushConfigPersistence");
    assert(contains(linux_config_submit_body,
                    "ConfigPersistenceUrgency::Debounced"));
    assert(!contains(linux_config_submit_body,
                     "ConfigPersistenceUrgency::Immediate"));
    assert(!contains(idf_storage_runtime, "PsramByteVector"));
    assert(contains(idf_storage_runtime, "class PsramPayload"));
    assert(contains(storage_owner_header, "StorageMaintenanceOwner"));
    assert(!contains(idf_storage_runtime, "vTaskDelete(nullptr)"));
    assert(!contains(idf_storage_runtime, "s_task"));
    assert(contains(storage_owner_header, "compare_exchange_strong"));
    assert(contains(storage_owner_header, "clearTickEventPending"));

    assert(!contains(source, "hydrateFromStorage"));
    assert(!contains(source, "compactDeferred"));
    assert(!contains(peer_source, "hydrateFromStorage"));
    assert(!contains(peer_source, "compactDeferred"));
    const std::string chat_begin_maintenance_body = bodyBetween(
        source,
        "SdStore::beginMaintenance",
        "SdStore::stepMaintenance");
    assert(contains(chat_begin_maintenance_body,
                    "maintenance_.operation == operation"));
    assert(contains(chat_begin_maintenance_body,
                    "maintenance_.generation == generation"));
    assert(contains(chat_begin_maintenance_body,
                    "maintenance_persistence_locked_ = true"));
    assert(contains(chat_begin_maintenance_body,
                    "A composite adapter may revisit this store"));
    const std::size_t chat_resume =
        positionOf(chat_begin_maintenance_body,
                   "if (maintenance_.operation == operation");
    const std::size_t chat_resume_ownership =
        positionOfAfter(chat_begin_maintenance_body,
                        "if (!maintenance_persistence_locked_ &&",
                        chat_resume);
    const std::size_t chat_resume_lease =
        positionOfAfter(chat_begin_maintenance_body,
                        "!acquirePersistenceLease",
                        chat_resume_ownership);
    const std::size_t chat_resume_lock =
        positionOfAfter(chat_begin_maintenance_body,
                        "maintenance_persistence_locked_ = true",
                        chat_resume_lease);
    const std::size_t chat_resume_return =
        positionOfAfter(chat_begin_maintenance_body,
                        "inProgressResult(",
                        chat_resume_lock);
    assert(chat_resume_ownership < chat_resume_lease);
    assert(chat_resume_lease < chat_resume_lock);
    assert(chat_resume_lock < chat_resume_return);
    assert(chat_resume_return <
           positionOf(chat_begin_maintenance_body,
                      "resetCatalogReconcileCursor()"));
    assert(chat_resume_return <
           positionOf(chat_begin_maintenance_body, "maintenance_ = {}"));
    const std::string chat_step_maintenance_body = bodyBetween(
        source,
        "SdStore::stepMaintenance",
        "void SdStore::cancelMaintenance");
    assert(contains(chat_step_maintenance_body, "result.completed()"));
    assert(contains(chat_step_maintenance_body,
                    "maintenance_.phase == MaintenancePhase::Failed"));
    const std::string chat_cancel_maintenance_body = bodyBetween(
        source,
        "void SdStore::cancelMaintenance",
        "SdStore::maintenanceFailure");
    assert(contains(chat_cancel_maintenance_body,
                    "maintenance_.phase = MaintenancePhase::Failed"));
    assert(contains(chat_cancel_maintenance_body,
                    "releaseMaintenanceLease()"));
    const std::string peer_begin_maintenance_body = bodyBetween(
        peer_source,
        "SdProtocolPeerRepository::beginMaintenance",
        "SdProtocolPeerRepository::stepMaintenance");
    assert(contains(peer_begin_maintenance_body,
                    "maintenance_.operation == operation"));
    assert(contains(peer_begin_maintenance_body,
                    "maintenance_.generation == generation"));
    assert(contains(peer_begin_maintenance_body,
                    "maintenance_persistence_locked_ = true"));
    assert(contains(peer_begin_maintenance_body,
                    "A composite adapter may revisit this repository"));
    const std::size_t peer_resume =
        positionOf(peer_begin_maintenance_body,
                   "if (maintenance_.operation == operation");
    const std::size_t peer_resume_ownership =
        positionOfAfter(peer_begin_maintenance_body,
                        "if (!maintenance_persistence_locked_ &&",
                        peer_resume);
    const std::size_t peer_resume_lease =
        positionOfAfter(peer_begin_maintenance_body,
                        "!acquirePersistenceLease",
                        peer_resume_ownership);
    const std::size_t peer_resume_lock =
        positionOfAfter(peer_begin_maintenance_body,
                        "maintenance_persistence_locked_ = true",
                        peer_resume_lease);
    const std::size_t peer_resume_return =
        positionOfAfter(peer_begin_maintenance_body,
                        "inProgressResult(",
                        peer_resume_lock);
    assert(peer_resume_ownership < peer_resume_lease);
    assert(peer_resume_lease < peer_resume_lock);
    assert(peer_resume_lock < peer_resume_return);
    assert(peer_resume_return <
           positionOf(peer_begin_maintenance_body, "maintenance_ = {}"));
    const std::string peer_step_maintenance_body = bodyBetween(
        peer_source,
        "SdProtocolPeerRepository::stepMaintenance",
        "void SdProtocolPeerRepository::cancelMaintenance");
    assert(contains(peer_step_maintenance_body, "result.completed()"));
    assert(contains(peer_step_maintenance_body,
                    "maintenance_.phase == MaintenancePhase::Failed"));
    const std::string peer_persistence_body = bodyBetween(
        peer_source,
        "SdProtocolPeerRepository::stepPersistence",
        "SdProtocolPeerRepository::stepCompaction");
    assert(!contains(peer_persistence_body,
                     "for (bool pending : protocol_reset_pending_)"));
    assert(positionOf(peer_persistence_body, "flushPendingDeltas") <
           positionOf(peer_persistence_body, "persistencePending()"));

    return 0;
}
