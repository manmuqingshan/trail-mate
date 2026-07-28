#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace
{

[[noreturn]] void fail(const char* requirement)
{
    std::fprintf(stderr, "UI lifecycle contract failed: %s\n", requirement);
    std::exit(EXIT_FAILURE);
}

std::string read_file(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
    {
        fail("required source file must be readable");
    }

    std::ostringstream out;
    out << stream.rdbuf();
    return out.str();
}

std::size_t position_of(const std::string& source, const char* token)
{
    const std::size_t position = source.find(token);
    if (position == std::string::npos)
    {
        fail(token);
    }
    return position;
}

std::string function_body(const std::string& source,
                          const char* begin,
                          const char* end)
{
    const std::size_t start = position_of(source, begin);
    const std::size_t finish = position_of(source.substr(start), end) + start;
    if (finish <= start)
    {
        fail("function body must have a positive range");
    }
    return source.substr(start, finish - start);
}

void require_before(const std::string& source,
                    const char* first,
                    const char* second)
{
    if (position_of(source, first) >= position_of(source, second))
    {
        fail("required lifecycle ordering");
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        fail("repository root argument");
    }

    const std::filesystem::path repo_root = argv[1];
    const std::string app_runtime = read_file(
        repo_root / "modules/ui_shared/src/ui/app_runtime.cpp");

    const std::string show_menu_internal = function_body(
        app_runtime,
        "void show_menu_internal()",
        "uint32_t child_count");
    if (show_menu_internal.find("ui_clear_active_app()") != std::string::npos)
    {
        fail("menu presentation must not clear the active app");
    }

    const std::string menu_show = function_body(
        app_runtime,
        "void menu_show()",
        "AppScreen* ui_get_active_app()");
    require_before(menu_show,
                   "if (s_active_app != nullptr)",
                   "ui_request_exit_to_menu();");
    require_before(menu_show,
                   "ui_request_exit_to_menu();",
                   "show_menu_internal();");

    const std::string exit_to_menu = function_body(
        app_runtime,
        "void exit_to_menu_timer_cb",
        "void rebuild_active_app_timer_cb");
    const std::string no_main_screen_exit = function_body(
        exit_to_menu,
        "if (main_screen == nullptr)",
        "lv_obj_t* parent =");
    require_before(no_main_screen_exit,
                   "app->exit(nullptr);",
                   "s_active_app = nullptr;");
    const std::size_t parent_exit = position_of(exit_to_menu, "app->exit(parent);");
    const std::string normal_exit = exit_to_menu.substr(parent_exit);
    require_before(normal_exit, "app->exit(parent);", "s_active_app = nullptr;");
    require_before(normal_exit, "s_active_app = nullptr;", "show_menu_internal();");

    const std::string gps_runtime = read_file(
        repo_root / "modules/ui_shared/src/ui/screens/gps/gps_page_runtime.cpp");
    const std::string gps_exit = function_body(
        gps_runtime,
        "void exit(lv_obj_t* parent)",
        "} // namespace gps::ui::runtime");
    require_before(gps_exit,
                   "::ui::widgets::map::destroy(s_map_runtime);",
                   "lv_obj_del(s_root);");
    require_before(gps_exit, "lv_obj_del(s_root);", "s_top_bar = {};");

    return EXIT_SUCCESS;
}
