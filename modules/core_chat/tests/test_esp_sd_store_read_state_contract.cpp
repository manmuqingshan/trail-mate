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
    const std::string idf_facade = readFile(
        repo_root /
        "apps/esp32_lvgl/src/esp32_lvgl_idf_app_facade_runtime.cpp");
    const std::string idf_startup = readFile(
        repo_root /
        "apps/esp32_lvgl/src/esp32_lvgl_startup_runtime.cpp");
    const std::string idf_storage_runtime = readFile(
        repo_root /
        "platform/esp/idf_common/src/storage_runtime.cpp");

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

    const std::string unread_body =
        bodyBetween(source, "bool SdStore::setUnread", "int SdStore::getUnread");
    assert(positionOf(unread_body, "appendReadProjection(projection)") <
           positionOf(unread_body, "appendCatalogProjection(*catalog)"));
    assert(contains(unread_body, "last_read_sequence"));

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

    const std::string seen_load = bodyBetween(
        source,
        "bool SdStore::loadSeenJournal",
        "bool SdStore::reconcileProtocolCatalog");
    assert(contains(seen_load, "rebuildSeenJournalFromMessages"));
    assert(contains(seen_load, "authoritative=messages"));
    const std::string flush_body = bodyBetween(
        source,
        "void SdStore::flush",
        "bool SdStore::ensureLayout");
    assert(contains(flush_body, "projection_dirty_"));
    assert(contains(flush_body, "kProjectionRetryIntervalMs"));

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
        "MeshPeerDirectoryStatus SdProtocolPeerRepository::hydrateFromStorage()");
    assert(!contains(begin_body, "ensureLayout()"));
    assert(!contains(begin_body, "loadProtocol"));
    assert(!contains(begin_body, "compactProtocolAtBoot"));
    assert(contains(begin_body, "hydration=pending"));

    assert(positionOf(startup, "initializeShell()") <
           positionOf(startup, "startDeferredStorage()"));
    assert(contains(storage_runtime, "xTaskCreatePinnedToCore"));
    assert(contains(storage_runtime, "vTaskDelete(nullptr)"));
    assert(contains(storage_runtime, "memory::admit"));
    assert(contains(storage_runtime, "storage_worker"));
    assert(contains(storage_runtime, "retry scheduled"));
    assert(contains(storage_runtime, "tick_deferred_storage"));
    assert(contains(storage_runtime, "is_sleeping"));
    assert(contains(idf_facade, "createIdfChatStore"));
    assert(!contains(bodyBetween(idf_facade,
                                 "std::unique_ptr<chat::IChatStore> createIdfChatStore",
                                 "class IdfAppFacadeRuntime"),
                     "isReady()"));
    assert(contains(idf_facade, "startDeferredStorage"));
    assert(contains(idf_startup, "idf_app_runtime_access::startDeferredStorage()"));
    assert(contains(idf_storage_runtime, "retry scheduled"));
    assert(contains(idf_storage_runtime, "xTaskCreatePinnedToCore"));

    return 0;
}
