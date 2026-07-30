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

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path repo_root = argv[1];
    const std::string header = readFile(
        repo_root /
        "modules/ui_shared/include/ui/screens/node_info/node_info_page_components.h");
    const std::string node_info = readFile(
        repo_root /
        "modules/ui_shared/src/ui/screens/node_info/node_info_page_components.cpp");
    const std::string contacts = readFile(
        repo_root /
        "modules/ui_shared/src/ui/screens/contacts/contacts_page_components.cpp");

    assert(contains(header, "struct InputCallbacks"));
    assert(contains(header, "void (*back_requested)(void* user_data)"));
    assert(notContains(header, "help_btn"));
    assert(notContains(header, "help_label"));

    assert(contains(node_info, "void on_node_info_input_key"));
    assert(contains(node_info, "key == 'h' || key == 'H'"));
    assert(contains(node_info, "key == LV_KEY_ESC || key == LV_KEY_BACKSPACE"));
    assert(contains(node_info, "key == '-' || key == '_'"));
    assert(contains(node_info, "key == '+' || key == '='"));
    assert(contains(node_info, "key == 'l' || key == 'L'"));
    assert(contains(node_info, "{\"H\", nullptr, \"Close help\"}"));
    assert(contains(node_info, "lv_group_set_editing(group, false)"));
    assert(contains(node_info, "top_bar_set_back_callback"));
    assert(notContains(node_info, "on_help_button_clicked"));

    assert(contains(
        contacts,
        "node_info::ui::bind_input_group(g_contacts_state.node_info_group,"));
    assert(notContains(contacts, "lv_obj_t* node_info_controls[]"));

    return 0;
}
