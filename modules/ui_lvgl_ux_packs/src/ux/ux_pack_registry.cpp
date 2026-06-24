#include "ui_lvgl_ux_packs/ux/ux_pack_registry.h"

#include "ui_lvgl_ux_packs/packs/cardputer_compact_ux_pack.h"
#include "ui_lvgl_ux_packs/packs/compatibility_ux_pack.h"
#include "ui_lvgl_ux_packs/packs/simulator_full_ux_pack.h"
#include "ui_lvgl_ux_packs/packs/t_display_p4_touch_ux_pack.h"
#include "ui_lvgl_ux_packs/packs/tiny_node_status_ux_pack.h"
#include "ui_lvgl_ux_packs/packs/uconsole_desktop_ux_pack.h"

#include <cstring>

namespace ui_lvgl_ux
{

const IUxPack* findUxPackById(const char* id)
{
    if (id == nullptr)
    {
        return nullptr;
    }

    static const CompatibilityUxPack compatibility_pack;
    static const CardputerCompactUxPack cardputer_compact_pack;
    static const UConsoleDesktopUxPack uconsole_desktop_pack;
    static const TinyNodeStatusUxPack tiny_node_status_pack;
    static const SimulatorFullUxPack simulator_full_pack;
    static const TDisplayP4TouchUxPack t_display_p4_touch_pack;

    const IUxPack* const packs[] = {
        &compatibility_pack,
        &cardputer_compact_pack,
        &uconsole_desktop_pack,
        &tiny_node_status_pack,
        &simulator_full_pack,
        &t_display_p4_touch_pack,
    };

    for (const IUxPack* pack : packs)
    {
        if (std::strcmp(pack->id(), id) == 0)
        {
            return pack;
        }
    }

    return nullptr;
}

} // namespace ui_lvgl_ux
