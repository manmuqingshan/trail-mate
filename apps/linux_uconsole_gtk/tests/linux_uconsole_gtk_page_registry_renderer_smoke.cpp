#include "linux_uconsole_gtk_app_shell.h"
#include "linux_uconsole_gtk_page_registry_adoption.h"
#include "linux_uconsole_gtk_page_registry_renderer.h"

#include <cassert>

int main()
{
    trailmate::apps::linux_uconsole_gtk::LinuxUConsoleGtkAppShell shell;
    trailmate::apps::linux_uconsole_gtk::LinuxUConsoleGtkPageRegistryAdoption
        adoption;
    assert(adoption.load(shell));
    assert(adoption.usingPrimaryScreenGraph());

    trailmate::apps::linux_uconsole_gtk::LinuxUConsoleGtkPageRegistryRenderer
        renderer;
    assert(renderer.render(adoption));
    assert(renderer.ready());
    assert(renderer.usingPrimaryScreenGraph());
    assert(renderer.usedPrimaryScreenGraph());
    assert(renderer.pageCount() > 0);
    assert(renderer.pages() != nullptr);
    assert(renderer.pages()[0].binding_id != nullptr);

    trailmate::apps::linux_uconsole_gtk::LinuxUConsoleGtkAppShellConfig
        invalid_config;
    invalid_config.ux_pack_id = "missing_phase11_pack";
    trailmate::apps::linux_uconsole_gtk::LinuxUConsoleGtkAppShell invalid_shell(
        invalid_config);
    trailmate::apps::linux_uconsole_gtk::LinuxUConsoleGtkPageRegistryAdoption
        invalid_adoption;
    assert(!invalid_adoption.load(invalid_shell));
    assert(invalid_adoption.registrySource() ==
           trailmate::apps::linux_uconsole_gtk::
               LinuxUConsoleGtkPageRegistrySource::Unavailable);
    assert(!renderer.render(invalid_adoption));
    assert(!renderer.ready());
    assert(!renderer.usingPrimaryScreenGraph());
    return 0;
}
