#include "esp32_lvgl_runtime_config.h"
#include "product_composition/target_profile.h"
#include "product_composition/target_ux_binding.h"
#include "ui_lvgl_ux_packs/ux/ux_pack_registry.h"
#include "ui_presentation/page/page_manifest.h"

#include <cassert>
#include <cstring>

int main()
{
    using namespace trailmate::apps::esp32_lvgl;
    const auto& config = esp32LvglRuntimeConfig();
    assert(std::strcmp(config.target_id, "wio_tracker_l2") == 0);
    assert(config.loop_stack_size == 12288);
    assert(hasEsp32LvglRuntimeTargetProfile());
    const auto* profile = esp32LvglRuntimeTargetProfile();
    assert(profile && profile->has_touch && !profile->has_keyboard && !profile->has_trackball);
    assert(profile->platform == product_composition::TargetPlatform::PlatformIo);
    const auto* deck = product_composition::findTargetProfile("tdeck");
    assert(deck && std::strcmp(profile->page_manifest_id, deck->page_manifest_id) == 0);
    assert(std::strcmp(profile->app_shell, deck->app_shell) == 0);
    const auto* binding = esp32LvglRuntimeUxBinding();
    assert(binding && binding->final_ux_pack_available && !binding->fallback_ux_pack_id);
    const auto* pack = ui_lvgl_ux::findUxPackById(binding->active_ux_pack_id);
    assert(pack && pack->profile().input_model == ui_lvgl_ux::InputModel::Touch);
    ui_lvgl_ux::ScreenRegistry screens;
    pack->buildScreens(screens);
    const auto* manifest = ui::presentation::findPageManifest(profile->page_manifest_id);
    assert(manifest && screens.size() == manifest->item_count);
    ui_lvgl_ux::InputBindingSet inputs;
    pack->buildInputBindings(inputs);
    assert(inputs.size() > 0);
    return 0;
}
