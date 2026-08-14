#include "screen_app_internal.h"

namespace ui::mono::screens::screen_240x320::detail
{

void reset_page_state(PageKind page_kind)
{
    switch (page_kind)
    {
    case PageKind::Map:
        reset_map_page_state();
        break;
    case PageKind::SkyPlot:
        reset_sky_plot_page_state();
        break;
    case PageKind::Chat:
        reset_direct_chat_flow();
        break;
    case PageKind::Team:
        reset_team_page_state();
        break;
    case PageKind::Settings:
        reset_settings_page_state();
        break;
    case PageKind::Contacts:
        reset_contacts_page_state();
        break;
    case PageKind::Extensions:
        reset_extensions_page_state();
        break;
    case PageKind::ProtocolProbe:
        reset_protocol_probe_page_state();
        break;
    default:
        break;
    }
}

void create_page_actions(PageKind page_kind)
{
    switch (page_kind)
    {
    case PageKind::Map:
        add_map_actions();
        break;
    case PageKind::SkyPlot:
        add_sky_plot_actions();
        break;
    case PageKind::Network:
        add_network_actions();
        break;
    case PageKind::Settings:
        add_settings_actions();
        break;
    case PageKind::Tracker:
        add_tracker_actions();
        break;
    case PageKind::Walkie:
        add_walkie_actions();
        break;
    case PageKind::Sstv:
        add_sstv_actions();
        break;
    case PageKind::UsbStorage:
        add_usb_storage_actions();
        break;
    case PageKind::Chat:
        add_chat_actions();
        break;
    case PageKind::Team:
        add_team_actions();
        break;
    case PageKind::Contacts:
        add_contacts_actions();
        break;
    case PageKind::Extensions:
        add_extensions_actions();
        break;
    case PageKind::ProtocolProbe:
        add_protocol_probe_actions();
        break;
    }
}

void refresh_page()
{
    if (s_state.adapter == nullptr || !valid(s_state.root))
    {
        return;
    }

    switch (s_state.adapter->page_kind())
    {
    case PageKind::Map:
        render_map();
        break;
    case PageKind::SkyPlot:
        render_sky_plot();
        break;
    case PageKind::Network:
        render_network();
        break;
    case PageKind::Settings:
        render_settings();
        configure_settings_actions();
        break;
    case PageKind::Tracker:
        render_tracker();
        break;
    case PageKind::Walkie:
        render_walkie();
        break;
    case PageKind::Sstv:
        render_sstv();
        break;
    case PageKind::UsbStorage:
        render_usb_storage();
        break;
    case PageKind::Chat:
        render_chat();
        configure_chat_actions();
        break;
    case PageKind::Team:
        render_team();
        configure_team_actions();
        break;
    case PageKind::Contacts:
        render_contacts();
        configure_contacts_actions();
        break;
    case PageKind::Extensions:
        render_extensions();
        configure_extensions_actions();
        break;
    case PageKind::ProtocolProbe:
        render_protocol_probe();
        configure_protocol_probe_actions();
        break;
    }
}

bool run_page_action(Action action)
{
    if (s_state.adapter == nullptr)
    {
        return false;
    }

    switch (s_state.adapter->page_kind())
    {
    case PageKind::Map:
        return handle_map_action(action);
    case PageKind::SkyPlot:
        return handle_sky_plot_action(action);
    case PageKind::Network:
        return false;
    case PageKind::Settings:
        return handle_settings_action(action);
    case PageKind::Tracker:
        return handle_tracker_action(action);
    case PageKind::Walkie:
        return handle_walkie_action(action);
    case PageKind::Sstv:
        return handle_sstv_action(action);
    case PageKind::UsbStorage:
        return handle_usb_storage_action(action);
    case PageKind::Chat:
        return handle_chat_action(action);
    case PageKind::Team:
        return handle_team_action(action);
    case PageKind::Contacts:
        return handle_contacts_action(action);
    case PageKind::Extensions:
        return handle_extensions_action(action);
    case PageKind::ProtocolProbe:
        return handle_protocol_probe_action(action);
    }
    return false;
}

} // namespace ui::mono::screens::screen_240x320::detail
