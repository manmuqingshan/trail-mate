#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace
{

std::string read_file(const std::filesystem::path& path)
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

bool not_contains(const std::string& haystack, const char* needle)
{
    return !contains(haystack, needle);
}

std::size_t position_of(const std::string& haystack, const char* needle)
{
    const auto pos = haystack.find(needle);
    assert(pos != std::string::npos);
    return pos;
}

std::size_t position_of_after(const std::string& haystack,
                              const char* needle,
                              std::size_t offset)
{
    const auto pos = haystack.find(needle, offset);
    assert(pos != std::string::npos);
    return pos;
}

} // namespace

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path repo_root = argv[1];

    const std::string registry = read_file(
        repo_root / "modules/ui_shared/src/ui/i18n/resource_pack_registry.cpp");

    assert(contains(registry, "bool s_defer_font_load_overlay_present = false;"));
    assert(contains(registry, "font_load_overlay_policy()"));
    assert(contains(registry, "Policy::Overlay"));
    assert(contains(registry, "Policy::OverlayImmediate"));
    assert(contains(registry, "font_load_overlay_policy(),"));
    assert(contains(registry, "FontPackRecord* s_pending_content_supplement_load = nullptr;"));
    assert(contains(registry, "lv_timer_t* s_content_supplement_retry_timer = nullptr;"));

    const std::size_t forced_overlay = position_of(registry, "class ScopedForcedFontLoadOverlay");
    const std::size_t defer_restore = position_of(registry, "previous_defer_present_");
    const std::size_t defer_enable = position_of(registry, "s_defer_font_load_overlay_present = true;");
    assert(forced_overlay < defer_restore);
    assert(defer_restore < defer_enable);

    const std::size_t hot_path_guard = position_of(registry, "bool can_activate_content_supplement_for_text");
    const std::size_t hot_path_check = position_of(registry, "can_load_font_from_content_hot_path(pack)");
    const std::size_t budget_check = position_of(registry, "can_add_content_supplement(pack)");
    assert(hot_path_guard < hot_path_check);
    assert(hot_path_check < budget_check);
    const std::size_t deferred_guard =
        position_of(registry, "bool can_schedule_deferred_content_supplement_load");
    assert(hot_path_guard < deferred_guard);
    assert(contains(registry, "can_load_font_from_activation_path(pack)"));
    assert(contains(registry, "bool prepare_content_font_for_text"));
    assert(contains(registry, "ScopedExternalFontActivation activation(force_overlay);"));

    const std::size_t content_ensure = position_of(registry, "bool ensure_content_font_for_text");
    const std::size_t supplement_check = position_of(
        registry,
        "!can_activate_content_supplement_for_text(*candidate)");
    const std::size_t hot_path_reason = position_of_after(registry, "\"ui_hot_path\"", supplement_check);
    const std::size_t content_budget_reason = position_of_after(registry, "\"content_budget\"", supplement_check);
    const std::size_t deferred_candidate_guard = position_of_after(
        registry,
        "can_schedule_deferred_content_supplement_load(*candidate)",
        supplement_check);
    const std::size_t deferred_queue = position_of_after(
        registry,
        "queue_deferred_content_supplement_load(*candidate, reason);",
        deferred_candidate_guard);
    const std::size_t deferred_skip = position_of_after(
        registry,
        "ui_hot_path_no_deferred_load",
        deferred_candidate_guard);
    assert(content_ensure < supplement_check);
    assert(supplement_check < hot_path_reason);
    assert(supplement_check < content_budget_reason);
    assert(supplement_check < deferred_candidate_guard);
    assert(deferred_candidate_guard < deferred_queue);
    assert(deferred_candidate_guard < deferred_skip);

    const std::size_t async_schedule =
        position_of(registry, "bool schedule_deferred_content_supplement_async");
    const std::size_t async_call = position_of_after(
        registry,
        "lv_async_call(deferred_content_supplement_load_cb, nullptr)",
        async_schedule);
    const std::size_t retry_timer = position_of(
        registry,
        "lv_timer_create(deferred_content_supplement_retry_timer_cb");
    assert(async_schedule < async_call);
    assert(async_schedule < retry_timer);

    const std::size_t async_callback =
        position_of(registry, "void deferred_content_supplement_load_cb");
    const std::size_t loader_call =
        position_of_after(registry, "ensure_font_pack_loaded(pack)", async_callback);
    const std::size_t append_call = position_of_after(
        registry,
        "append_unique_pack(s_content_supplement_packs, pack);",
        async_callback);
    const std::size_t rebuild_call =
        position_of_after(registry, "rebuild_runtime_font_chains();", append_call);
    const std::size_t invalidate_call = position_of_after(
        registry,
        "invalidate_active_screen_after_font_chain_change();",
        rebuild_call);
    assert(async_callback < loader_call);
    assert(loader_call < append_call);
    assert(append_call < rebuild_call);
    assert(rebuild_call < invalidate_call);

    assert(not_contains(
        registry,
        "foreground::make_snapshot(foreground::Slot::I18nFontLoad,\n"
        "                                      foreground::Policy::OverlayImmediate"));

    const std::string network_page = read_file(
        repo_root / "modules/ui_shared/src/ui/screens/network/network_page_shell.cpp");
    assert(contains(network_page, "prepare_content_font_for_text(g_state.page_body.data(), true)"));
    assert(contains(network_page, "::ui::fonts::apply_content_font(label,"));
    assert(contains(network_page, "::ui::fonts::apply_content_font(text,"));

    return 0;
}
