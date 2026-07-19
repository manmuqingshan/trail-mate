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

bool notContains(const std::string& haystack, const char* needle)
{
    return !contains(haystack, needle);
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

std::string sliceFunction(const std::string& source,
                          const char* begin,
                          const char* next)
{
    const std::size_t start = positionOf(source, begin);
    const std::size_t end = positionOfAfter(source, next, start);
    return source.substr(start, end - start);
}

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path repo_root = argv[1];
    const std::string adapter = readFile(
        repo_root /
        "platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_adapter.cpp");
    const std::string call_profile = readFile(
        repo_root /
        "platform/esp/arduino_common/include/platform/esp/arduino_common/chat/infra/lxmf/lxmf_call_profile.h");
    const std::string telephony_client = readFile(
        repo_root /
        "platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_lxst_telephony_client.cpp");
    const std::string settings = readFile(
        repo_root /
        "modules/ui_shared/src/ui/screens/settings/settings_page_components.cpp");
    const std::string notification = readFile(
        repo_root /
        "platform/esp/arduino_common/src/notification_runtime.cpp");
    const std::string event_runtime = readFile(
        repo_root /
        "platform/esp/arduino_common/src/app_event_runtime_support.cpp");

    assert(contains(call_profile, "#define TRAIL_MATE_ENABLE_MESHCHAT_CALL_AUDIO_COMPAT 0"));
    assert(contains(call_profile, "return ::platform::ui::reticulum_call::WireProfile::SidebandLxst;"));
    assert(contains(call_profile, "#if TRAIL_MATE_ENABLE_MESHCHAT_CALL_AUDIO_COMPAT"));
    assert(contains(telephony_client, "session.call_wire_profile = ReticulumCallWireProfile::SidebandLxst;"));

    const std::string start_call = sliceFunction(
        adapter,
        "MeshActionResult LxmfAdapter::startReticulumAudioCall",
        "MeshActionResult LxmfAdapter::pingReticulumDestination");
    assert(contains(start_call, "ReticulumCallWireProfile::SidebandLxst"));
    assert(notContains(start_call, "ReticulumCallWireProfile::MeshChatCallAudio"));

    const std::string call_destination = sliceFunction(
        adapter,
        "void callDestinationHashForIdentity",
        "void fillRandomBytes");
    assert(contains(call_destination, "\"lxst\", \"telephony\""));
    const std::size_t compat_guard =
        positionOf(call_destination, "#if TRAIL_MATE_ENABLE_MESHCHAT_CALL_AUDIO_COMPAT");
    const std::size_t meshchat_branch =
        positionOfAfter(call_destination,
                        "ReticulumCallWireProfile::MeshChatCallAudio",
                        compat_guard);
    assert(compat_guard < meshchat_branch);
    assert(positionOfAfter(call_destination, "\"call\", \"audio\"", compat_guard) >
           meshchat_branch);

    assert(notContains(settings, "Call Protocol"));
    assert(notContains(settings, "MeshChat call"));
    assert(notContains(settings, "call.audio compatibility"));

    assert(contains(notification, "board.playMessageTone();"));
    assert(contains(notification, "board.vibrator();"));
    assert(contains(event_runtime, "notification::play_alert(app_context, notification::AlertKind::Message)"));
    assert(notContains(event_runtime, "board->playMessageTone();"));
    assert(notContains(event_runtime, "board->vibrator();"));

    return 0;
}
