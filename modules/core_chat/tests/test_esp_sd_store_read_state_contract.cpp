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

    assert(contains(header, "kReadStateFile = \"/chat/read_state.bin\""));
    assert(contains(header, "struct ReadStateEntry"));
    assert(contains(header, "kReadStateMagic"));
    assert(contains(header, "readStateUnreadOrLegacy"));
    assert(contains(header, "writeReadStateUnread"));
    assert(contains(header, "removeReadStateEntry"));

    const std::string ctor_body =
        bodyBetween(source, "SdStore::SdStore()", "void SdStore::append");
    assert(contains(ctor_body, "reconcileIndexUnread(entries)"));
    assert(positionOf(ctor_body, "reconcileIndexUnread(entries)") <
           positionOf(ctor_body, "writeIndex(entries)"));

    const std::string append_body = bodyBetween(
        source, "bool SdStore::appendInternal", "std::vector<ChatMessage> SdStore::loadRecent");
    assert(positionOf(append_body, "already_committed") <
           positionOf(append_body, "readStateUnreadOrLegacy(conv, &committed_unread)"));
    assert(positionOf(append_body, "readStateUnreadOrLegacy(conv, &committed_unread)") <
           positionOf(append_body, "updateIndexForMessage(msg, committed_unread)"));
    assert(positionOf(append_body, "writeReadStateUnread(conv, unread)") <
           positionOf(append_body, "updateIndexForMessage(msg, unread)"));
    assert(positionOf(append_body, "CHAT_STORE_LOG(\"[AppContext] chat unread persist failed stage=read_state") <
           positionOf(append_body, "removeReadStateEntry(conv)"));

    const std::string set_unread_body =
        bodyBetween(source, "bool SdStore::setUnread", "int SdStore::getUnread");
    assert(positionOf(set_unread_body, "writeReadStateUnread(conv, unread_count)") <
           positionOf(set_unread_body, "writeConversationUnread(conv, unread_count)"));
    assert(positionOf(set_unread_body, "writeConversationUnread(conv, unread_count)") <
           positionOf(set_unread_body, "entries[index].unread = unread_count"));
    assert(positionOf(set_unread_body, "entries[index].unread = unread_count") <
           positionOf(set_unread_body, "writeIndex(entries)"));

    const std::string get_unread_body =
        bodyBetween(source, "int SdStore::getUnread", "void SdStore::clearConversation");
    assert(contains(get_unread_body, "readStateUnreadOrLegacy(conv, &unread)"));

    const std::string clear_conversation_body =
        bodyBetween(source, "void SdStore::clearConversation", "void SdStore::clearAll");
    assert(contains(clear_conversation_body, "removeReadStateEntry(conv)"));

    const std::string clear_all_body =
        bodyBetween(source, "void SdStore::clearAll", "bool SdStore::updateMessageStatus");
    assert(contains(clear_all_body, "sd_remove(kReadStateFile)"));
    assert(contains(clear_all_body, "sd_remove(kTempReadStateFile)"));
    assert(contains(clear_all_body, "sd_remove(kBackupReadStateFile)"));

    const std::string reconcile_body =
        bodyBetween(source, "bool SdStore::reconcileIndexUnread", "bool SdStore::readIndex");
    assert(positionOf(reconcile_body, "readStateUnreadOrLegacy(conv, &durable_unread)") <
           positionOf(reconcile_body, "durable_unread = entry.unread"));
    assert(positionOf(reconcile_body, "durable_unread = entry.unread") <
           positionOf(reconcile_body, "writeReadStateUnread(conv, durable_unread)"));

    const std::string rebuild_body =
        bodyBetween(source, "void SdStore::rebuildIndex", "bool SdStore::loadFileHeader");
    assert(contains(rebuild_body, "readStateUnreadOrLegacy(conv, &ledger_unread)"));
    assert(contains(rebuild_body, "writeReadStateUnread(conv, unread)"));

    return 0;
}
