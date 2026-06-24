#include "platform/ui/pack_repository_runtime.h"

namespace ui::runtime::packs
{

bool is_supported()
{
    return false;
}

bool load_installed_packages(std::vector<InstalledPackageRecord>& out_installed, std::string& out_error)
{
    out_installed.clear();
    out_error = "Pack installation is unsupported on this IDF target";
    return false;
}

bool fetch_catalog(std::vector<PackageRecord>& out_packages, std::string& out_error)
{
    out_packages.clear();
    out_error = "Pack installation is unsupported on this IDF target";
    return false;
}

bool install_package(const PackageRecord& package, std::string& out_error)
{
    (void)package;
    out_error = "Pack installation is unsupported on this IDF target";
    return false;
}

bool uninstall_package(const PackageRecord& package, std::string& out_error)
{
    (void)package;
    out_error = "Pack installation is unsupported on this IDF target";
    return false;
}

} // namespace ui::runtime::packs
