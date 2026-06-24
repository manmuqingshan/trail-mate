#include "linux_uconsole_gtk_page_registry_renderer.h"

namespace trailmate::apps::linux_uconsole_gtk
{

bool LinuxUConsoleGtkPageRegistryRenderer::render(
    const LinuxUConsoleGtkPageRegistryAdoption& adoption)
{
    ready_ = false;
    primary_ = false;

    if (adoption.usingPrimaryScreenGraph())
    {
        ready_ = registry_.load(adoption.adoption());
        primary_ = ready_;
        return ready_;
    }

    return false;
}

bool LinuxUConsoleGtkPageRegistryRenderer::ready() const noexcept
{
    return ready_;
}

bool LinuxUConsoleGtkPageRegistryRenderer::usingPrimaryScreenGraph()
    const noexcept
{
    return primary_;
}

bool LinuxUConsoleGtkPageRegistryRenderer::usedPrimaryScreenGraph()
    const noexcept
{
    return primary_;
}

std::size_t LinuxUConsoleGtkPageRegistryRenderer::pageCount() const noexcept
{
    return ready_ ? registry_.pageCount() : 0;
}

const trailmate::uconsole::GtkDescriptorPage*
LinuxUConsoleGtkPageRegistryRenderer::pages() const noexcept
{
    return ready_ ? registry_.pages() : nullptr;
}

} // namespace trailmate::apps::linux_uconsole_gtk
