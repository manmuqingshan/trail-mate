#pragma once

#include "lvgl.h"

#include "app/app_facades.h"
#include "ui/app_catalog.h"
#include "ui_presentation/menu/menu_model.h"

namespace ui
{
namespace menu_layout
{

struct InitOptions
{
    app::IAppMessagingFacade* messaging = nullptr;
    AppCatalog apps{};
    const ui::menu::MenuModel* ux_menu = nullptr;
};

void init(const InitOptions& options);
lv_obj_t* menuPanel();
// Performs a full app lifecycle transition using a catalog stable id. This is
// used when one app hands the user to another app-owned screen.
bool launchAppByStableId(const char* stable_id);
void bringContentToFront();
void refresh_localized_text();
void set_bottom_bar_node_text(const char* text);
void set_bottom_bar_help_text(const char* text);
void set_bottom_bar_ram_text(const char* text);
void set_bottom_bar_psram_text(const char* text);
void set_bottom_bar_psram_visible(bool visible);
void setMenuVisible(bool visible);

} // namespace menu_layout
} // namespace ui
