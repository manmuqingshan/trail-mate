#include "screen_app_internal.h"

#include "platform/ui/pack_repository_runtime.h"

#include <algorithm>
#include <string>
#include <vector>

namespace ui::mono::screens::screen_240x320::detail
{
namespace
{

struct ExtensionsPageState
{
    enum class Route : unsigned char
    {
        List,
        Detail,
        ConfirmInstall,
        ConfirmRemove,
    };

    std::vector<::ui::runtime::packs::PackageRecord> packages;
    std::vector<::ui::runtime::packs::InstalledPackageRecord> installed_packages;
    ::ui::runtime::packs::PackageInstallStatus install_status{};
    std::string error;
    size_t selected_index = 0;
    bool seeded = false;
    bool catalog_loaded = false;
    bool install_was_busy = false;
    Route route = Route::List;
};

ExtensionsPageState s_extensions_page_state;

} // namespace

std::string s_last_handled_extension_install_id;
::ui::runtime::packs::PackageInstallPhase s_last_handled_extension_install_phase =
    ::ui::runtime::packs::PackageInstallPhase::Idle;

void reset_extensions_page_state()
{
    s_extensions_page_state = ExtensionsPageState{};
    s_last_handled_extension_install_id.clear();
    s_last_handled_extension_install_phase = ::ui::runtime::packs::PackageInstallPhase::Idle;
}

bool is_extension_package(const ::ui::runtime::packs::PackageRecord& package)
{
    return package.package_type.empty() || package.package_type == "installed" ||
           package.package_type == "locale-bundle" ||
           package.package_type == "content-bundle" ||
           package.package_type == "input-bundle";
}

void seed_installed_extensions()
{
    if (s_extensions_page_state.seeded)
    {
        return;
    }

    s_extensions_page_state.seeded = true;
    s_extensions_page_state.error.clear();
    s_extensions_page_state.installed_packages.clear();
    if (!::ui::runtime::packs::load_installed_packages(s_extensions_page_state.installed_packages,
                                                       s_extensions_page_state.error))
    {
        return;
    }

    s_extensions_page_state.packages.clear();
    for (const auto& installed : s_extensions_page_state.installed_packages)
    {
        s_extensions_page_state.packages.emplace_back();
        auto& package = s_extensions_page_state.packages.back();
        package.id = installed.id;
        package.package_type = "installed";
        package.version = installed.version;
        package.display_name = installed.id;
        package.installed = true;
        package.compatible_firmware = true;
        package.compatible_memory_profile = true;
        package.installed_record = installed;
    }
}

void sync_extension_install_status()
{
    s_extensions_page_state.install_status = ::ui::runtime::packs::install_status();
    s_extensions_page_state.install_was_busy = s_extensions_page_state.install_status.busy;
    if (s_extensions_page_state.install_status.busy)
    {
        return;
    }

    const auto phase = s_extensions_page_state.install_status.phase;
    if (phase != ::ui::runtime::packs::PackageInstallPhase::Succeeded &&

        phase != ::ui::runtime::packs::PackageInstallPhase::Failed)
    {
        return;
    }
    if (s_last_handled_extension_install_phase == phase &&
        s_last_handled_extension_install_id == s_extensions_page_state.install_status.package_id)
    {
        return;
    }
    s_last_handled_extension_install_phase = phase;
    s_last_handled_extension_install_id = s_extensions_page_state.install_status.package_id;

    if (phase == ::ui::runtime::packs::PackageInstallPhase::Succeeded)
    {
        // Package installation runs on its own worker task.  This user-driven
        // state check is deliberately not a display timer: it is the point at
        // which the new locale/font fallback chain becomes live.
        ::ui::i18n::reload_language();
        s_extensions_page_state.catalog_loaded = false;
        s_extensions_page_state.seeded = false;
        set_notice("PACKAGE INSTALLED; FONT ACTIVE");
        return;
    }

    if (phase == ::ui::runtime::packs::PackageInstallPhase::Failed)
    {
        set_notice(s_extensions_page_state.install_status.message.empty()
                       ? "PACKAGE INSTALL FAILED"
                       : s_extensions_page_state.install_status.message.c_str());
    }
}

void load_extension_catalog()
{
    sync_extension_install_status();
    s_extensions_page_state.catalog_loaded = false;
    if (!::ui::runtime::packs::is_supported())
    {
        s_extensions_page_state.error = "PACKAGE REPOSITORY UNSUPPORTED";
        set_notice(s_extensions_page_state.error.c_str());
        return;
    }

    s_extensions_page_state.error.clear();
    s_extensions_page_state.packages.clear();
    if (!::ui::runtime::packs::fetch_catalog(s_extensions_page_state.packages, s_extensions_page_state.error))
    {
        s_extensions_page_state.seeded = false;
        seed_installed_extensions();
        set_notice(s_extensions_page_state.error.empty() ? "CATALOG LOAD FAILED"
                                                         : s_extensions_page_state.error.c_str());
        return;
    }

    s_extensions_page_state.packages.erase(
        std::remove_if(s_extensions_page_state.packages.begin(),
                       s_extensions_page_state.packages.end(),
                       [](const ::ui::runtime::packs::PackageRecord& package)
                       { return !is_extension_package(package); }),
        s_extensions_page_state.packages.end());
    s_extensions_page_state.catalog_loaded = true;
    if (s_extensions_page_state.packages.empty())
    {
        s_extensions_page_state.selected_index = 0;
        set_notice("NO EXTENSION PACKAGES");
        return;
    }
    if (s_extensions_page_state.selected_index >= s_extensions_page_state.packages.size())
    {
        s_extensions_page_state.selected_index = s_extensions_page_state.packages.size() - 1U;
    }
    set_notice("CATALOG UPDATED");
}

const ::ui::runtime::packs::PackageRecord* selected_extension()
{
    if (s_extensions_page_state.selected_index >= s_extensions_page_state.packages.size())
    {
        return nullptr;
    }
    return &s_extensions_page_state.packages[s_extensions_page_state.selected_index];
}

void render_extensions()
{
    seed_installed_extensions();
    sync_extension_install_status();
    // Completion invalidates the cached package records. Reload the durable
    // installed index now, as part of this explicit render, rather than
    // scheduling a background EPD refresh.
    seed_installed_extensions();

    if (!::ui::runtime::packs::is_supported())
    {

        set_line(0, "PACKAGE REPOSITORY UNSUPPORTED");
        set_line(1, "THIS TARGET HAS NO PACK STORAGE");
        clear_lines_from(2);
        return;
    }

    const ::ui::runtime::packs::PackageRecord* const package = selected_extension();
    if (s_extensions_page_state.route == ExtensionsPageState::Route::List)
    {
        set_text(s_state.title, "EXTENSIONS");
        set_text(s_state.subtitle, "LIST");
        set_linef(0,
                  "PACKS %u  CATALOG %s",
                  static_cast<unsigned>(s_extensions_page_state.packages.size()),
                  s_extensions_page_state.catalog_loaded ? "LOADED" : "OFFLINE");
        if (package == nullptr)
        {
            set_line(1, "NO EXTENSION PACKAGES");
            set_line(2, "RELOAD NEEDS WI-FI");
            set_line(3, s_state.notice[0] != '\0' ? s_state.notice : "RELOAD TO FETCH CATALOG");
            clear_lines_from(4);
            return;
        }
        set_linef(1,
                  "SELECT %u / %u",
                  static_cast<unsigned>(s_extensions_page_state.selected_index + 1U),
                  static_cast<unsigned>(s_extensions_page_state.packages.size()));
        size_t line = 2;
        for (size_t index = 0; index < s_extensions_page_state.packages.size() && line < 9;
             ++index)
        {
            const auto& item = s_extensions_page_state.packages[index];
            set_linef(line++,
                      "%c %s %s",
                      index == s_extensions_page_state.selected_index ? '>' : ' ',
                      item.display_name.empty() ? item.id.c_str() : item.display_name.c_str(),
                      item.installed ? "INSTALLED" : "AVAILABLE");
        }
        clear_lines_from(line);
        set_line(9, s_state.notice[0] != '\0' ? s_state.notice : "PREV/NEXT SELECT  DETAIL OPENS PACK");
        return;
    }

    if (s_extensions_page_state.route == ExtensionsPageState::Route::ConfirmInstall ||
        s_extensions_page_state.route == ExtensionsPageState::Route::ConfirmRemove)
    {
        set_text(s_state.title, "EXTENSIONS");
        set_text(s_state.subtitle, "CONFIRM");
        const bool remove = s_extensions_page_state.route == ExtensionsPageState::Route::ConfirmRemove;
        set_line(0, remove ? "REMOVE THIS PACKAGE?" : "INSTALL THIS PACKAGE?");
        set_linef(1, "NAME %s", package == nullptr ? "--" : (package->display_name.empty() ? package->id.c_str() : package->display_name.c_str()));
        set_linef(2, "VERSION %s", package == nullptr || package->version.empty() ? "--" : package->version.c_str());
        set_line(3, remove ? "REMOVAL RESTORES FONT FALLBACK" : "INSTALL RUNS IN BACKGROUND");
        set_line(4, s_state.notice[0] != '\0' ? s_state.notice : "CANCEL OR CONFIRM");
        clear_lines_from(5);
        return;
    }

    set_text(s_state.title, "EXTENSIONS");
    set_text(s_state.subtitle, "DETAIL");
    if (package == nullptr)
    {
        set_line(1, "NO PACKAGE SELECTED");
        set_line(2, "RELOAD NEEDS WI-FI");
        set_line(3,
                 s_extensions_page_state.error.empty() ? "INSTALLED PACKS SHOWN OFFLINE"
                                                       : s_extensions_page_state.error.c_str());
        set_line(4, s_state.notice[0] != '\0' ? s_state.notice : "RELOAD TO FETCH CATALOG");
        clear_lines_from(5);
        return;
    }

    set_linef(1,
              "SELECT %u / %u",
              static_cast<unsigned>(s_extensions_page_state.selected_index + 1U),
              static_cast<unsigned>(s_extensions_page_state.packages.size()));
    set_linef(2, "NAME %s", package->display_name.empty() ? package->id.c_str() : package->display_name.c_str());
    set_linef(3, "ID %s", package->id.c_str());
    set_linef(4, "VERSION %s", package->version.empty() ? "--" : package->version.c_str());
    if (!package->compatible_firmware || !package->compatible_memory_profile)
    {
        set_line(5, "STATE INCOMPATIBLE");
    }
    else if (package->update_available)
    {
        set_line(5, "STATE UPDATE AVAILABLE");
    }
    else
    {
        set_line(5, package->installed ? "STATE INSTALLED" : "STATE AVAILABLE");
    }
    set_linef(6,
              "LOCALE %u FONT %u IME %u",
              static_cast<unsigned>(package->provided_locale_ids.size()),
              static_cast<unsigned>(package->provided_font_ids.size()),
              static_cast<unsigned>(package->provided_ime_ids.size()));
    if (s_extensions_page_state.install_status.busy)
    {
        set_linef(7,
                  "INSTALL %d%% %s",
                  s_extensions_page_state.install_status.progress_percent,
                  s_extensions_page_state.install_status.message.c_str());
    }
    else if (!s_extensions_page_state.install_status.message.empty())
    {
        set_linef(7, "INSTALL %s", s_extensions_page_state.install_status.message.c_str());
    }
    else
    {
        set_line(7, "INSTALL IDLE");
    }
    set_line(8, s_extensions_page_state.error.empty() ? "FONT PACKS ACTIVATE AFTER INSTALL"
                                                      : s_extensions_page_state.error.c_str());
    set_line(9,
             s_state.notice[0] != '\0'
                 ? s_state.notice
                 : "PREV/NEXT SELECT  RELOAD / INSTALL");
}

void add_extensions_actions()
{
    add_action("PREV", Action::ExtensionsPrevious, kMargin, kActionTop, 48);
    add_action("NEXT", Action::ExtensionsNext, 62, kActionTop, 48);
    add_action("RELOAD", Action::ExtensionsRefresh, 116, kActionTop, 54);
    add_action("DETAIL", Action::ExtensionsDetails, 176, kActionTop, 56);
    add_action("BACK", Action::Back, kMargin, kActionTop + 22, 48);
}

bool handle_extensions_action(Action action)
{
    if (s_extensions_page_state.route == ExtensionsPageState::Route::ConfirmInstall ||
        s_extensions_page_state.route == ExtensionsPageState::Route::ConfirmRemove)
    {
        if (action == Action::Back || action == Action::ExtensionsCancel)
        {
            s_extensions_page_state.route = ExtensionsPageState::Route::Detail;
            set_notice("PACKAGE CHANGE CANCELLED");
            return true;
        }
        if (action != Action::ExtensionsConfirm)
        {
            return false;
        }
        const bool remove = s_extensions_page_state.route == ExtensionsPageState::Route::ConfirmRemove;
        s_extensions_page_state.route = ExtensionsPageState::Route::List;
        action = remove ? Action::ExtensionsRemove : Action::ExtensionsApply;
    }

    if (s_extensions_page_state.route == ExtensionsPageState::Route::Detail)
    {
        if (action == Action::Back)
        {
            s_extensions_page_state.route = ExtensionsPageState::Route::List;
            set_notice("PACKAGE LIST");
            return true;
        }
        if (action == Action::ExtensionsApply)
        {
            s_extensions_page_state.route = ExtensionsPageState::Route::ConfirmInstall;
            set_notice("CONFIRM INSTALL");
            return true;
        }
        if (action == Action::ExtensionsRemove)
        {
            s_extensions_page_state.route = ExtensionsPageState::Route::ConfirmRemove;
            set_notice("CONFIRM REMOVE");
            return true;
        }
    }

    switch (action)
    {
    case Action::ExtensionsPrevious:
    case Action::ExtensionsNext:
        if (s_extensions_page_state.packages.empty())
        {
            set_notice("NO PACKAGE SELECTED");
            return true;
        }
        if (action == Action::ExtensionsPrevious)
        {
            s_extensions_page_state.selected_index = s_extensions_page_state.selected_index == 0
                                                         ? s_extensions_page_state.packages.size() - 1U
                                                         : s_extensions_page_state.selected_index - 1U;
        }
        else
        {
            s_extensions_page_state.selected_index =
                (s_extensions_page_state.selected_index + 1U) % s_extensions_page_state.packages.size();
        }
        set_notice("");
        return true;
    case Action::ExtensionsRefresh:
        load_extension_catalog();
        return true;
    case Action::ExtensionsDetails:
        if (selected_extension() == nullptr)
        {
            set_notice("NO PACKAGE SELECTED");
            return true;
        }
        s_extensions_page_state.route = ExtensionsPageState::Route::Detail;
        set_notice("PACKAGE DETAIL");
        return true;
    case Action::ExtensionsApply:
    {
        const ::ui::runtime::packs::PackageRecord* const package = selected_extension();
        if (package == nullptr)
        {
            set_notice("NO PACKAGE SELECTED");
            return true;
        }
        if (!package->compatible_firmware || !package->compatible_memory_profile)
        {
            set_notice("PACKAGE INCOMPATIBLE");
            return true;
        }
        if (package->installed && !package->update_available)
        {
            set_notice("PACKAGE ALREADY INSTALLED");
            return true;
        }
        s_extensions_page_state.error.clear();
        if (!::ui::runtime::packs::start_install_package(*package, s_extensions_page_state.error))
        {
            set_notice(s_extensions_page_state.error.empty() ? "PACKAGE INSTALL REJECTED"
                                                             : s_extensions_page_state.error.c_str());
            return true;
        }
        s_extensions_page_state.install_was_busy = true;
        s_last_handled_extension_install_id.clear();
        s_last_handled_extension_install_phase = ::ui::runtime::packs::PackageInstallPhase::Idle;
        s_extensions_page_state.install_status = ::ui::runtime::packs::install_status();
        set_notice("PACKAGE INSTALL STARTED");
        return true;
    }
    case Action::ExtensionsRemove:
    {
        const ::ui::runtime::packs::PackageRecord* const package = selected_extension();
        if (package == nullptr || !package->installed)
        {
            set_notice("NO INSTALLED PACKAGE SELECTED");
            return true;
        }
        s_extensions_page_state.error.clear();
        if (!::ui::runtime::packs::uninstall_package(*package, s_extensions_page_state.error))
        {
            set_notice(s_extensions_page_state.error.empty() ? "PACKAGE REMOVE FAILED"
                                                             : s_extensions_page_state.error.c_str());
            return true;
        }
        s_extensions_page_state.catalog_loaded = false;
        s_extensions_page_state.seeded = false;
        s_extensions_page_state.route = ExtensionsPageState::Route::List;
        set_notice("PACKAGE REMOVED; FONT FALLBACK UPDATED");
        return true;
    }

    default:
        return false;
    }
}

void configure_extensions_actions()
{
    if (s_state.adapter == nullptr || s_state.adapter->page_kind() != PageKind::Extensions ||
        s_state.action_count < 5)
    {
        return;
    }

    const ::ui::runtime::packs::PackageRecord* const package = selected_extension();
    const bool can_apply = package != nullptr && package->compatible_firmware &&
                           package->compatible_memory_profile &&

                           (!package->installed || package->update_available) &&
                           !s_extensions_page_state.install_status.busy;
    if (s_extensions_page_state.route == ExtensionsPageState::Route::ConfirmInstall ||
        s_extensions_page_state.route == ExtensionsPageState::Route::ConfirmRemove)
    {
        set_action(0, "CANCEL", Action::ExtensionsCancel);
        set_action(1, "CONFIRM", Action::ExtensionsConfirm);
        for (size_t index = 0; index < 2; ++index)
        {
            set_action_visible(index, true);
        }
        for (size_t index = 2; index < s_state.action_count; ++index)
        {
            set_action_visible(index, false);
        }
        return;
    }

    if (s_extensions_page_state.route == ExtensionsPageState::Route::Detail)
    {
        set_action(0, package != nullptr && package->installed && !package->update_available ? "REMOVE" : "INSTALL", package != nullptr && package->installed && !package->update_available ? Action::ExtensionsRemove : Action::ExtensionsApply);
        set_action(1, "BACK", Action::Back);
        for (size_t index = 0; index < 2; ++index)
        {
            set_action_visible(index, true);
        }
        for (size_t index = 2; index < s_state.action_count; ++index)
        {
            set_action_visible(index, false);
        }
        return;
    }

    set_action(0, "PREV", Action::ExtensionsPrevious);
    set_action(1, "NEXT", Action::ExtensionsNext);
    set_action(2, "RELOAD", Action::ExtensionsRefresh);
    (void)can_apply;
    set_action(3, "DETAIL", Action::ExtensionsDetails);
    set_action(4, "BACK", Action::Back);
    for (size_t index = 0; index < 5; ++index)
    {
        set_action_visible(index, true);
    }
}
} // namespace ui::mono::screens::screen_240x320::detail
