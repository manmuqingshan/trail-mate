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

std::size_t positionOfAfter(const std::string& source,
                            const std::string& needle,
                            std::size_t offset)
{
    const std::size_t position = source.find(needle, offset);
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
    const std::string chat_controller = readFile(
        root / "modules/ui_shared/src/ui/screens/chat/chat_ui_controller.cpp");
    const std::string chat_compose = readFile(
        root / "modules/ui_shared/src/ui/screens/chat/chat_compose_components.cpp");

    const std::size_t store_completed = positionOf(session, "bool storeCompletedVoice(");
    const std::size_t outbound_store = positionOf(session, "bool storeOutboundVoice()");
    const std::string inbound_store_body =
        session.substr(store_completed, outbound_store - store_completed);
    const std::size_t durable_commit = positionOf(inbound_store_body, "persistVoiceInbox(");
    const std::size_t rollback = positionOf(inbound_store_body, "media_->inbox.erase(local_id)");
    assert(durable_commit < rollback);

    // VMP is a local attachment-message extension, not a receive-only
    // scratch inbox. The sender must durably create `Sending` before choosing
    // a carrier, then persist a terminal local state after that one attempt.
    const std::size_t outbound_run = positionOf(session, "void runOutbound()");
    const std::size_t outbound_store_call = positionOfAfter(
        session, "storeOutboundVoice()", outbound_run);
    const std::size_t carrier_select = positionOfAfter(
        session, "if (!direct_rf_voice_supported_)", outbound_store_call);
    const std::size_t terminal_commit = positionOfAfter(
        session, "commitOutboundDelivery(", carrier_select);
    assert(outbound_store_call < carrier_select);
    assert(carrier_select < terminal_commit);
    const std::size_t outbound_store_commit = positionOfAfter(
        session, "persistVoiceInbox(", outbound_store);
    const std::size_t outbound_local_id = positionOfAfter(
        session, "outbound_local_id_ = local_id", outbound_store_commit);
    assert(outbound_store_commit < outbound_local_id);
    assert(session.find("VoiceDeliveryState::Sending") != std::string::npos);
    assert(session.find("VoiceDeliveryState::Sent") != std::string::npos);
    assert(session.find("VoiceDeliveryState::Failed") != std::string::npos);
    assert(session.find("requires_durable_attachment_store_") !=
           std::string::npos);
    assert(session.find("inbox_ready_") != std::string::npos);
    assert(session.find("servicePersistentInbox") != std::string::npos);

    assert(app.find("getSelfNodeId(), deferred_storage_store_context_ != nullptr") !=
           std::string::npos);
    assert(app.find("vmp_session::servicePersistentInbox()") !=
           std::string::npos);
    // The VMP runtime projects a chat-message timeline in both directions.
    // Do not regress to the original receive-only inbox API: the sender's
    // durable Sending/Sent/Failed object must be visible to the controller.
    assert(app.find("std::size_t listMessages(") != std::string::npos);
    assert(chat_controller.find("::ui::chat_voice::listMessages(") !=
           std::string::npos);
    assert(chat_controller.find("::ui::chat_voice::listReceivedMessages(") ==
           std::string::npos);
    // Typed attachments have the same conversation boundary as text: a peer
    // number alone is insufficient because channels and mesh backends can
    // legitimately reuse it. The logical chat channel is part of VMP control
    // and the local protocol/channel binding is persisted/projected.
    assert(chat_controller.find("summary.presentation_protocol !=") !=
           std::string::npos);
    assert(chat_controller.find("appendVoiceConversationsToControllerList") !=
           std::string::npos);
    assert(chat_controller.find("::ui::chat_voice::markConversationRead(") !=
           std::string::npos);
    assert(chat_controller.find("current_conv_.peer == 0U ? chat::voice::vmp::kBroadcastTargetId") !=
           std::string::npos);
    assert(session.find("outgoing_control_.conversation_channel =") !=
           std::string::npos);
    assert(session.find("control.conversation_channel") != std::string::npos);
    assert(app.find("metadata.presentation_protocol") != std::string::npos);
    assert(app.find("metadata.presentation_channel") != std::string::npos);
    // Press-to-talk begins on PRESSED, but release must leave the compose
    // event stack before it can destroy that view. The conversation then
    // polls the local attachment commit quickly enough to expose Sending.
    assert(chat_compose.find("LV_EVENT_PRESSED") != std::string::npos);
    assert(chat_compose.find("schedule_action_async(ActionIntent::VoiceStop)") !=
           std::string::npos);
    assert(chat_controller.find("kVoiceProjectionBusyPollMs = 150U") !=
           std::string::npos);
    assert(chat_controller.find("voice release stop_requested=%u; return to timeline") !=
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
    assert(attachment.find("kSnapshotSchemaVersion = 2U") != std::string::npos);
    assert(attachment.find("record.reserved[1] = static_cast<uint8_t>(metadata.presentation_protocol)") !=
           std::string::npos);
    assert(attachment.find("record.reserved[2] = metadata.presentation_channel") !=
           std::string::npos);
    assert(attachment.find("kVoiceReadFlag") != std::string::npos);
    assert(attachment.find("restoreVoiceInboxSnapshot") != std::string::npos);
    assert(attachment.find("header.record_count - index") != std::string::npos);
    // Snapshot records and restore sequencing are a matched on-disk contract:
    // listMetadata() is newest-first, serialization must retain that order,
    // and hydrate assigns the first record the highest sequence.  Reversing
    // only one side silently inverts the chat timeline after reboot.
    const std::size_t persist = positionOf(attachment, "bool persistVoiceInbox(");
    const std::string persist_body = attachment.substr(persist);
    assert(persist_body.find("for (std::size_t index = 0U; wrote && index < count; ++index)") !=
           std::string::npos);
    assert(persist_body.find("metadata_scratch[index]") != std::string::npos);
    assert(persist_body.find("metadata_scratch[remaining - 1U]") == std::string::npos);
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

    // An RF capture must be diagnosable from *both* Pagers. Keep the ingress,
    // readiness, shard/FEC, and timeout spine explicit so an on-device report
    // can distinguish a missing offer, 2.4 GHz setup failure, ready-probe
    // failure, no-first-media condition, or durable-store failure.
    assert(session.find("[VMP][RX] private offer accepted") != std::string::npos);
    assert(session.find("[VMP][RX] private accept sent; switch_2g_rx") !=
           std::string::npos);
    assert(session.find("[VMP][RX] private 2g rx ready window_ms=") !=
           std::string::npos);
    assert(session.find("[VMP][RX] private ready sent; wait_first_voice") !=
           std::string::npos);
    assert(session.find("[VMP][RX] private media timeout ready=") !=
           std::string::npos);
    assert(session.find("[VMP][RX] broadcast announce accepted") !=
           std::string::npos);
    assert(session.find("[VMP][RX] broadcast 2g rx ready window_ms=") !=
           std::string::npos);
    assert(session.find("[VMP][RX] broadcast media timeout unique_shards=") !=
           std::string::npos);
    assert(session.find("[VMP][RX] shard quorum reached unique_shards=") !=
           std::string::npos);
    assert(session.find("[VMP][RX] inbox durable_commit") != std::string::npos);
    return 0;
}
