#pragma once

#include "linux_uconsole_gtk_app_shell.h"
#include "linux_uconsole_gtk_runtime_entry_adoption_probe.h"
#include "ui_gtk_runtime/gtk_runtime_entry_adoption.h"

#include <cstddef>

namespace trailmate::apps::linux_uconsole_gtk
{

enum class LinuxUConsoleGtkPageRegistrySource
{
    Unavailable,
    ScreenGraphAdoption,
};

class LinuxUConsoleGtkPageRegistryAdoption
{
  public:
    bool load(const LinuxUConsoleGtkAppShell& shell);

    bool ready() const;
    bool usingPrimaryScreenGraph() const noexcept;
    LinuxUConsoleGtkPageRegistrySource registrySource() const noexcept;
    std::size_t menuCount() const;
    std::size_t screenCount() const;

    const trailmate::uconsole::GtkMenuDescriptor* menuDescriptors() const;
    const trailmate::uconsole::GtkScreenDescriptor* screenDescriptors() const;
    const trailmate::uconsole::gtk::GtkRuntimeEntryAdoption& adoption() const;

  private:
    LinuxUConsoleGtkRuntimeEntryAdoptionProbe adoption_probe_{};
    LinuxUConsoleGtkPageRegistrySource registry_source_ =
        LinuxUConsoleGtkPageRegistrySource::Unavailable;
    bool ready_ = false;
};

} // namespace trailmate::apps::linux_uconsole_gtk
