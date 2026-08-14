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

std::size_t positionOf(const std::string& text, const char* needle)
{
    const std::size_t position = text.find(needle);
    assert(position != std::string::npos);
    return position;
}

std::size_t positionOfAfter(const std::string& text,
                            const char* needle,
                            std::size_t offset)
{
    const std::size_t position = text.find(needle, offset);
    assert(position != std::string::npos);
    return position;
}

} // namespace

// LXMF is permitted to carry VMP, but a received VMP envelope must end at the
// local voice inbox.  It may not leak into the generic AppData queue, where a
// future service could bridge it into a different protocol or radio bearer.
int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path root = argv[1];
    const std::string session_header = readFile(
        root / "platform/esp/arduino_common/include/platform/esp/arduino_common/voice/"
               "vmp_pager_session.h");
    const std::string session_source = readFile(
        root / "platform/esp/arduino_common/src/voice/vmp_pager_session.cpp");
    const std::string lxmf_source = readFile(
        root / "platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_adapter.cpp");
    const std::string app_source = readFile(
        root / "platform/esp/arduino_common/src/app_context.cpp");

    assert(session_header.find("kLxmfAppDataPort = 0x564D5001UL") != std::string::npos);
    assert(session_header.find("bool acceptLxmfEnvelope(") != std::string::npos);

    const std::size_t lxmf_port_branch = positionOf(
        lxmf_source,
        "delivery.app_data.incoming.portnum ==\n"
        "            ::platform::esp::arduino_common::voice::vmp_session::kLxmfAppDataPort");
    const std::size_t generic_queue_push = positionOfAfter(
        lxmf_source, "data_receive_queue_.push(", lxmf_port_branch);
    const std::string port_branch =
        lxmf_source.substr(lxmf_port_branch, generic_queue_push - lxmf_port_branch);
    assert(port_branch.find("acceptLxmfEnvelope(") != std::string::npos);
    assert(port_branch.find("local_only=%u") != std::string::npos);
    assert(port_branch.find("return true;") != std::string::npos);
    assert(port_branch.find("data_receive_queue_") == std::string::npos);

    const std::size_t send_begin = positionOf(session_source, "bool sendLxmfVoice()");
    const std::size_t control_begin = positionOfAfter(
        session_source, "bool prepareOutboundControl()", send_begin);
    const std::string send_body = session_source.substr(send_begin, control_begin - send_begin);
    assert(send_body.find("preparePrivate(") != std::string::npos);
    assert(send_body.find("radio::") == std::string::npos);
    assert(send_body.find("acknowledgement, a relay, or a VMP resend") != std::string::npos);

    assert(app_source.find("sendVmpLxmfEnvelope") != std::string::npos);
    assert(app_source.find("kLxmfAppDataPort") != std::string::npos);
    assert(app_source.find("isReticulumMeshProtocol") != std::string::npos);
    return 0;
}
