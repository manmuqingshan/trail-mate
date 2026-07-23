#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
std::string read_file(const std::filesystem::path& path)
{
    std::ifstream stream(path);
    assert(stream.good());
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

void require_contains(const std::string& text, const std::string& needle)
{
    assert(text.find(needle) != std::string::npos);
}
} // namespace

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path root(argv[1]);
    const std::string node_info = read_file(
        root / "modules/ui_shared/src/ui/screens/node_info/node_info_page_components.cpp");
    const std::string gps_page = read_file(
        root / "modules/ui_shared/src/ui/screens/gps/gps_page_runtime.cpp");
    const std::string viewport = read_file(
        root / "modules/ui_shared/src/ui/widgets/map/map_viewport.cpp");
    const std::string esp_tiles = read_file(
        root / "platform/esp/arduino_common/src/ui/widgets/map/map_tiles.cpp");

    require_contains(node_info, "namespace map_viewport = ::ui::widgets::map;");
    require_contains(node_info, "map_viewport::create(s_state.viewport, s_widgets.content)");
    require_contains(gps_page, "::ui::widgets::map::create(s_map_runtime, viewport, 180)");
    require_contains(viewport, "init_tile_context(impl->tile_ctx");
    require_contains(esp_tiles, "xTaskCreateStatic(taskThunk");
    require_contains(esp_tiles, "s_map_tile_worker_task_stack");
    assert(esp_tiles.find("xTaskCreate(taskThunk") == std::string::npos);

    return 0;
}
