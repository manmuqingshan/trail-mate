#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

namespace
{

std::string read_file(const char* root, const char* relative_path)
{
    std::string path(root);
    path += "/";
    path += relative_path;

    std::ifstream file(path, std::ios::binary);
    assert(file.is_open());
    return std::string((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());
}

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 2);

    const std::string auto_reply_settings =
        read_file(argv[1], "modules/core_sys/include/platform/ui/auto_reply_settings.h");
    const std::string settings_store = read_file(
        argv[1], "platform/esp/common/include/platform/esp/common/settings_store_impl.h");

    assert(auto_reply_settings.find(
               "inline constexpr char kEnabledKey[] = \"chat_auto_reply_enabled\";") !=
           std::string::npos);
    assert(auto_reply_settings.find(
               "inline constexpr char kTextKey[] = \"chat_auto_reply_text\";") !=
           std::string::npos);
    assert(settings_store.find(
               "{\"chat_auto_reply_enabled\", \"chat_auto_reply\"},") !=
           std::string::npos);
    assert(settings_store.find(
               "{\"chat_auto_reply_text\", \"chat_auto_txt\"},") !=
           std::string::npos);
    return 0;
}
