#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace
{

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    assert(input.good());
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

std::size_t positionOf(const std::string& source, const std::string& needle)
{
    const std::size_t position = source.find(needle);
    assert(position != std::string::npos);
    return position;
}

} // namespace

// This is deliberately a source-level boundary test. The ESP attachment
// adapter depends on SdFat/Arduino, while its most important regressions are
// architectural: bypassing text-storage hydration, exposing an object before
// a durable commit, or adding a bearer-side escape to local attachment data.
int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path root = argv[1];
    const std::string session = readFile(
        root / "platform/esp/arduino_common/src/voice/vmp_pager_session.cpp");
    const std::string app = readFile(
        root / "platform/esp/arduino_common/src/app_context.cpp");
    const std::string bindings = readFile(
        root / "platform/esp/arduino_common/src/app_context_platform_bindings.cpp");
    const std::string attachment = readFile(
        root / "platform/esp/arduino_common/src/chat/infra/store/"
               "message_attachment_store.cpp");
    const std::string attachment_header = readFile(
        root / "platform/esp/arduino_common/include/platform/esp/arduino_common/"
               "chat/infra/store/message_attachment_store.h");
    const std::string pager_header = readFile(
        root / "platform/esp/arduino_common/include/platform/esp/arduino_common/"
               "voice/vmp_pager_session.h");
    const std::string pager_audio = readFile(
        root / "platform/esp/arduino_common/src/voice/vmp_pager_audio.cpp");

    const std::size_t store_completed = positionOf(session, "bool storeCompletedVoice(");
    const std::size_t durable_commit = positionOf(session, "persistVoiceInbox(");
    const std::size_t rollback = positionOf(session, "media_->inbox.erase(local_id)");
    assert(store_completed < durable_commit);
    assert(durable_commit < rollback);
    assert(session.find("requires_durable_attachment_store_") !=
           std::string::npos);
    assert(session.find("inbox_ready_") != std::string::npos);
    assert(session.find("servicePersistentInbox") != std::string::npos);

    assert(app.find("getSelfNodeId(), deferred_storage_store_context_ != nullptr") !=
           std::string::npos);
    assert(app.find("vmp_session::servicePersistentInbox()") !=
           std::string::npos);
    assert(app.find("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT") !=
           std::string::npos);
    assert(app.find("UI metadata scratch unavailable in PSRAM") !=
           std::string::npos);
    assert(bindings.find("vmp_session::onPersistentStorageReady()") !=
           std::string::npos);

    assert(attachment.find("/data/v2/attachments/voice/inbox.v1") !=
           std::string::npos);
    assert(attachment.find("inbox.v1.tmp") != std::string::npos);
    assert(attachment.find("inbox.v1.bak") != std::string::npos);
    assert(attachment.find("payload_crc32") != std::string::npos);
    assert(attachment.find("restoreVoiceInboxSnapshot") != std::string::npos);
    assert(attachment.find("const VoiceInboxLoadResult backup_result") !=
           std::string::npos);
    assert(attachment.find("return backup_result;") != std::string::npos);
    // The prior snapshot must survive a successful temporary-to-primary
    // rename, otherwise a detected primary corruption has nothing to restore.
    assert(attachment.find("if (moved_current)\n        {\n            (void)storage::sd_remove(kVoiceSnapshotBackupPath);") ==
           std::string::npos);
    assert(attachment.find("AttachmentKind::Voice") != std::string::npos);
    assert(attachment_header.find("Image = 2U") != std::string::npos);
    assert(attachment_header.find("Location = 3U") != std::string::npos);
    assert(attachment.find("radio::") == std::string::npos);
    assert(attachment.find("mqtt_") == std::string::npos);
    assert(attachment.find("lxmf_") == std::string::npos);

    // The Pager's SX1262 variant can only create the isolated MQTT plan. It
    // must never become a hidden direct-RF/LXMF fallback merely because the
    // shared VMP session is compiled for both Pager radio variants.
    assert(session.find("#if defined(ARDUINO_T_LORA_PAGER)") !=
           std::string::npos);
    assert(session.find("direct_rf_voice_supported_") != std::string::npos);
    assert(session.find("sent = queueMqttPublication();") != std::string::npos);
    assert(session.find("return direct_rf_voice_supported_ && source_id != 0U") !=
           std::string::npos);
    assert(pager_header.find("SX1262 never has a") != std::string::npos);
    assert(pager_audio.find("#if defined(ARDUINO_T_LORA_PAGER)") !=
           std::string::npos);
    return 0;
}
