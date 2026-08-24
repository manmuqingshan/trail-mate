#include "app/tms_config_codec.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{

bool append(void* context, const char* data, std::size_t length)
{
    auto* text = static_cast<std::string*>(context);
    text->append(data, length);
    return true;
}

bool decodeDocument(std::string document, app::tms::Decoder& decoder)
{
    std::size_t cursor = 0U;
    while (cursor < document.size())
    {
        const std::size_t end = document.find('\n', cursor);
        const std::size_t length = (end == std::string::npos ? document.size() : end) - cursor;
        if (length >= app::tms::kMaxLineBytes)
        {
            return false;
        }
        char line[app::tms::kMaxLineBytes] = {};
        std::memcpy(line, document.data() + cursor, length);
        if (!decoder.consumeLine(line))
        {
            return false;
        }
        if (end == std::string::npos)
        {
            break;
        }
        cursor = end + 1U;
    }
    return decoder.finish();
}

void testRoundTripUsesBase64ForMeshtasticPsk()
{
    app::AppConfig source;
    source.mesh_protocol = chat::MeshProtocol::Meshtastic;
    source.meshtastic_config.primary_key_len = chat::kMeshtasticChannelKeyDefaultLen;
    for (uint8_t index = 0U; index < source.meshtastic_config.primary_key_len; ++index)
    {
        source.meshtastic_config.primary_key[index] = index;
    }
    auto& group = source.reticulumConfig().reticulum_groups[0];
    group.enabled = true;
    std::strncpy(group.name, "Pager Group", sizeof(group.name) - 1U);
    group.identity.valid = true;
    for (std::size_t index = 0U; index < chat::kReticulumPeerHashSize; ++index)
    {
        group.identity.destination_hash[index] = static_cast<uint8_t>(0x10U + index);
        group.identity.identity_hash[index] = static_cast<uint8_t>(0x20U + index);
    }

    std::string document;
    app::tms::LineScratch scratch{};
    app::tms::DocumentInfo emitted{};
    assert(app::tms::writeDocument(source,
                                   app::tms::DocumentKind::Working,
                                   {&document, append},
                                   scratch,
                                   &emitted));
    assert(document.find("mt.primary_psk=b64:AAECAwQFBgcICQoLDA0ODw==") != std::string::npos);
    assert(document.find("{") == std::string::npos);
    assert(emitted.records > 40U);

    app::tms::Decoder validation(nullptr, app::tms::DocumentKind::Working);
    const bool validation_ok = decodeDocument(document, validation);
    if (!validation_ok)
    {
        std::fprintf(stderr,
                     "validation error: %s\n",
                     app::tms::decodeErrorName(validation.info().error));
    }
    assert(validation_ok);
    assert(validation.info().unknown_records == 0U);

    app::AppConfig restored;
    app::tms::Decoder applying(&restored, app::tms::DocumentKind::Working);
    assert(decodeDocument(document, applying));
    assert(restored.mesh_protocol == chat::MeshProtocol::Meshtastic);
    assert(restored.meshtastic_config.primary_key_len ==
           chat::kMeshtasticChannelKeyDefaultLen);
    assert(std::memcmp(restored.meshtastic_config.primary_key,
                       source.meshtastic_config.primary_key,
                       chat::kMeshtasticChannelKeyDefaultLen) == 0);
    const auto& restored_group = restored.reticulumConfig().reticulum_groups[0];
    assert(restored_group.enabled);
    assert(std::strcmp(restored_group.name, "Pager Group") == 0);
    assert(restored_group.identity.valid);
    assert(std::memcmp(restored_group.identity.destination_hash,
                       group.identity.destination_hash,
                       chat::kReticulumPeerHashSize) == 0);
    assert(std::memcmp(restored_group.identity.identity_hash,
                       group.identity.identity_hash,
                       chat::kReticulumPeerHashSize) == 0);
}

void testTms6GroupDestinationMigratesToIdentity()
{
    app::AppConfig source;
    auto& group = source.reticulumConfig().reticulum_groups[0];
    group.enabled = true;
    std::strncpy(group.name, "Legacy Group", sizeof(group.name) - 1U);
    group.identity.valid = true;
    for (std::size_t index = 0U; index < chat::kReticulumPeerHashSize; ++index)
    {
        group.identity.destination_hash[index] = static_cast<uint8_t>(0x30U + index);
        group.identity.identity_hash[index] = static_cast<uint8_t>(0x40U + index);
    }

    std::string document;
    app::tms::LineScratch scratch{};
    assert(app::tms::writeDocument(source,
                                   app::tms::DocumentKind::Working,
                                   {&document, append},
                                   scratch));
    document.replace(0U, std::strlen("TMSET7"), "TMSET6");
    const std::size_t schema = document.find("schema.version=u16:7");
    assert(schema != std::string::npos);
    document.replace(schema,
                     std::strlen("schema.version=u16:7"),
                     "schema.version=u16:6");

    for (std::size_t index = 0U;
         index < chat::kReticulumGroupDestinationMaxCount;
         ++index)
    {
        const std::string prefix = "rt.group." + std::to_string(index) + ".";
        const std::size_t valid = document.find(prefix + "identity_valid=");
        assert(valid != std::string::npos);
        const std::size_t valid_end = document.find('\n', valid);
        assert(valid_end != std::string::npos);
        document.erase(valid, valid_end - valid + 1U);

        const std::size_t destination = document.find(prefix + "destination_hash=");
        assert(destination != std::string::npos);
        document.replace(destination,
                         (prefix + "destination_hash").size(),
                         prefix + "destination");

        const std::size_t identity = document.find(prefix + "identity_hash=");
        assert(identity != std::string::npos);
        const std::size_t identity_end = document.find('\n', identity);
        assert(identity_end != std::string::npos);
        document.erase(identity, identity_end - identity + 1U);
    }

    app::AppConfig restored;
    app::tms::Decoder decoder(&restored, app::tms::DocumentKind::Working);
    assert(decodeDocument(document, decoder));
    assert(decoder.schemaVersion() == 6U);
    const auto& restored_group = restored.reticulumConfig().reticulum_groups[0];
    assert(restored_group.enabled);
    assert(std::strcmp(restored_group.name, "Legacy Group") == 0);
    assert(restored_group.identity.valid);
    assert(std::memcmp(restored_group.identity.destination_hash,
                       group.identity.destination_hash,
                       chat::kReticulumPeerHashSize) == 0);
    uint8_t zero_hash[chat::kReticulumPeerHashSize] = {};
    assert(std::memcmp(restored_group.identity.identity_hash,
                       zero_hash,
                       sizeof(zero_hash)) == 0);

    std::string empty_destination = document;
    const std::size_t encoded_destination = empty_destination.find("rt.group.0.destination=b64:");
    assert(encoded_destination != std::string::npos);
    const std::size_t encoded_destination_end = empty_destination.find('\n', encoded_destination);
    assert(encoded_destination_end != std::string::npos);
    empty_destination.erase(encoded_destination + std::strlen("rt.group.0.destination=b64:"),
                            encoded_destination_end -
                                (encoded_destination +
                                 std::strlen("rt.group.0.destination=b64:")));
    app::AppConfig empty_restored;
    app::tms::Decoder empty_decoder(&empty_restored, app::tms::DocumentKind::Working);
    assert(decodeDocument(empty_destination, empty_decoder));
    assert(!empty_restored.reticulumConfig().reticulum_groups[0].identity.valid);
}

void testInvalidPskCannotPartiallyApply()
{
    const std::string document =
        "TMSET2\n"
        "schema.version=u16:2\n"
        "document.kind=enum:working\n"
        "device.node_name=str:before\n"
        "mt.primary_psk=b64:AQID\n"
        "END\n";
    app::AppConfig target;
    std::strncpy(target.node_name, "original", sizeof(target.node_name) - 1U);
    app::tms::Decoder validation(nullptr, app::tms::DocumentKind::Working);
    assert(!decodeDocument(document, validation));
    assert(validation.info().error == app::tms::DecodeError::InvalidKnownValue);
    assert(std::strcmp(target.node_name, "original") == 0);
}

void testUnknownFutureKeyIsIgnored()
{
    const std::string document =
        "TMSET2\n"
        "schema.version=u16:2\n"
        "document.kind=enum:working\n"
        "future.setting=u32:42\n"
        "END\n";
    app::tms::Decoder decoder(nullptr, app::tms::DocumentKind::Working);
    assert(decodeDocument(document, decoder));
    assert(decoder.info().unknown_records == 1U);
}

void testCurrentSchemaRejectsDuplicateUnknownAndMissingCoreRecords()
{
    app::AppConfig source;
    std::string document;
    app::tms::LineScratch scratch{};
    assert(app::tms::writeDocument(source,
                                   app::tms::DocumentKind::Working,
                                   {&document, append},
                                   scratch));

    const std::size_t record_begin = document.find("map.source=");
    const std::size_t record_end = document.find('\n', record_begin) + 1U;
    assert(record_begin != std::string::npos);
    assert(record_end != std::string::npos);

    std::string duplicate = document;
    duplicate.insert(record_end, document.substr(record_begin, record_end - record_begin));
    app::tms::Decoder duplicate_decoder(nullptr, app::tms::DocumentKind::Working);
    assert(!decodeDocument(duplicate, duplicate_decoder));
    assert(duplicate_decoder.info().error == app::tms::DecodeError::DuplicateRecord);

    std::string unknown = document;
    unknown.insert(unknown.rfind("END"), "future.setting=u32:42\n");
    app::tms::Decoder unknown_decoder(nullptr, app::tms::DocumentKind::Working);
    assert(!decodeDocument(unknown, unknown_decoder));
    assert(unknown_decoder.info().error == app::tms::DecodeError::UnknownRecord);

    std::string missing = document;
    missing.erase(record_begin, record_end - record_begin);
    app::tms::Decoder missing_decoder(nullptr, app::tms::DocumentKind::Working);
    assert(!decodeDocument(missing, missing_decoder));
    assert(missing_decoder.info().error ==
           app::tms::DecodeError::MissingRequiredRecord);
}

void testPriorStrictSchemaIsAcceptedForOneTimeMigration()
{
    app::AppConfig source;
    std::string document;
    app::tms::LineScratch scratch{};
    assert(app::tms::writeDocument(source,
                                   app::tms::DocumentKind::Working,
                                   {&document, append},
                                   scratch));

    const std::size_t magic = document.find("TMSET7");
    const std::size_t version = document.find("schema.version=u16:7");
    assert(magic == 0U);
    assert(version != std::string::npos);
    document.replace(magic, std::strlen("TMSET7"), "TMSET5");
    document.replace(version, std::strlen("schema.version=u16:7"),
                     "schema.version=u16:5");

    app::tms::Decoder decoder(nullptr, app::tms::DocumentKind::Working);
    assert(decodeDocument(document, decoder));
    assert(decoder.schemaVersion() == 5U);
}

void testLongPhysicalLineIsRejected()
{
    char line[app::tms::kMaxLineBytes + 1U] = {};
    std::memset(line, 'x', app::tms::kMaxLineBytes);
    app::tms::Decoder decoder(nullptr, app::tms::DocumentKind::Working);
    assert(!decoder.consumeLine(line));
    assert(decoder.info().error == app::tms::DecodeError::MalformedRecord);
}

struct ExtensionState
{
    bool seen = false;
    bool finalized = false;
    int32_t value = 0;
};

app::tms::RecordConsumeResult consumeExtension(void* context,
                                               const app::tms::RecordReader& reader)
{
    auto* state = static_cast<ExtensionState*>(context);
    if (!state || std::strcmp(reader.key(), "extension.example") != 0)
    {
        return app::tms::RecordConsumeResult::Unhandled;
    }
    if (state->seen || !reader.i32(&state->value))
    {
        return app::tms::RecordConsumeResult::Invalid;
    }
    state->seen = true;
    return app::tms::RecordConsumeResult::Accepted;
}

bool finishExtension(void* context, bool applying, uint16_t schema_version)
{
    auto* state = static_cast<ExtensionState*>(context);
    if (!state || !state->seen || schema_version != app::tms::kSchemaVersion)
    {
        return false;
    }
    state->finalized = applying;
    return true;
}

bool writeExtension(void* context, app::tms::RecordWriter& writer)
{
    const auto* value = static_cast<const int32_t*>(context);
    return value && writer.i32("extension.example", *value);
}

void testCurrentSchemaConsumesPlatformExtension()
{
    int32_t extension_value = -42;
    std::string document;
    app::tms::LineScratch scratch{};
    app::AppConfig source;
    assert(app::tms::writeDocument(source,
                                   app::tms::DocumentKind::Working,
                                   {&document, append},
                                   scratch,
                                   nullptr,
                                   writeExtension,
                                   &extension_value));
    ExtensionState validation_state{};
    app::tms::Decoder validation(nullptr,
                                 app::tms::DocumentKind::Working,
                                 consumeExtension,
                                 &validation_state,
                                 finishExtension);
    assert(decodeDocument(document, validation));
    assert(validation_state.value == -42);
    assert(!validation_state.finalized);
    assert(validation.info().unknown_records == 0U);

    ExtensionState applying_state{};
    app::AppConfig target;
    app::tms::Decoder applying(&target,
                               app::tms::DocumentKind::Working,
                               consumeExtension,
                               &applying_state,
                               finishExtension);
    assert(decodeDocument(document, applying));
    assert(applying_state.value == -42);
    assert(applying_state.finalized);
}

} // namespace

int main()
{
    testRoundTripUsesBase64ForMeshtasticPsk();
    testTms6GroupDestinationMigratesToIdentity();
    testInvalidPskCannotPartiallyApply();
    testUnknownFutureKeyIsIgnored();
    testCurrentSchemaRejectsDuplicateUnknownAndMissingCoreRecords();
    testPriorStrictSchemaIsAcceptedForOneTimeMigration();
    testLongPhysicalLineIsRejected();
    testCurrentSchemaConsumesPlatformExtension();
    return 0;
}
