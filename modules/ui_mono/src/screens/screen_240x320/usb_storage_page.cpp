#include "screen_app_internal.h"

#include "ui/mono/screens/screen_240x320/runtime_feature_port.h"

namespace ui::mono::screens::screen_240x320::detail
{
namespace
{

::ui::mono::screens::screen_240x320::UsbStorageView s_usb_storage_view;

} // namespace

void render_usb_storage()
{
    ::ui::mono::screens::screen_240x320::RuntimeFeaturePort* const port =
        ::ui::mono::screens::screen_240x320::runtimeFeaturePort();
    s_usb_storage_view = {};
    if (port != nullptr)
    {
        port->readUsbStorage(s_usb_storage_view);
    }

    set_linef(0, "USB STORAGE %s", s_usb_storage_view.supported ? "READY" : "UNAVAILABLE");
    set_linef(1, "MASS STORAGE %s", s_usb_storage_view.active ? "ACTIVE" : "OFF");
    set_linef(2, "STOP REQUEST %s", s_usb_storage_view.stop_requested ? "YES" : "NO");
    set_linef(3, "STATUS %s",
              s_usb_storage_view.message[0] != '\0' ? s_usb_storage_view.message : "--");
    set_line(4, s_state.notice[0] != '\0' ? s_state.notice : "START / STOP USB DISK");
    clear_lines_from(5);
}

void add_usb_storage_actions()
{
    add_action("START/STOP", Action::ToggleUsb, kMargin, kActionTop, 112);
}

bool handle_usb_storage_action(Action action)
{
    if (action != Action::ToggleUsb)
    {
        return false;
    }

    ::ui::mono::screens::screen_240x320::RuntimeFeaturePort* const port =
        ::ui::mono::screens::screen_240x320::runtimeFeaturePort();
    if (port == nullptr)
    {
        set_notice("USB STORAGE UNAVAILABLE");
        return true;
    }
    port->readUsbStorage(s_usb_storage_view);
    if (!s_usb_storage_view.supported)
    {
        set_notice("USB STORAGE UNAVAILABLE");
    }
    else if (s_usb_storage_view.active)
    {
        port->stopUsbStorage();
        set_notice("USB STORAGE STOP REQUESTED");
    }
    else
    {
        set_notice(port->startUsbStorage() ? "USB STORAGE STARTED"
                                           : "USB STORAGE START FAILED");
    }
    return true;
}

} // namespace ui::mono::screens::screen_240x320::detail
