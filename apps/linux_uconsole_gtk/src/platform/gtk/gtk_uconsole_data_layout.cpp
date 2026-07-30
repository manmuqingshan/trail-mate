#include "platform/gtk/gtk_uconsole_pages.h"
#include "platform/gtk/gtk_uconsole_widgets.h"

namespace trailmate::uconsole::gtk
{

GtkWidget* launchDataLayout(GtkUConsoleAppState& state)
{
    return buildDetailsWorkspace(
        "Data & offline maps",
        "Local SQLite state, automatic map downloads, cache health, and offline storage.",
        &state.data_page_box);
}

} // namespace trailmate::uconsole::gtk
