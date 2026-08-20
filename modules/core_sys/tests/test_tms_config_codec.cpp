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

void testLongPhysicalLineIsRejected()
{
    char line[app::tms::kMaxLineBytes + 1U] = {};
    std::memset(line, 'x', app::tms::kMaxLineBytes);
    app::tms::Decoder decoder(nullptr, app::tms::DocumentKind::Working);
    assert(!decoder.consumeLine(line));
    assert(decoder.info().error == app::tms::DecodeError::MalformedRecord);
}

} // namespace

int main()
{
    testRoundTripUsesBase64ForMeshtasticPsk();
    testInvalidPskCannotPartiallyApply();
    testUnknownFutureKeyIsIgnored();
    testLongPhysicalLineIsRejected();
    return 0;
}
