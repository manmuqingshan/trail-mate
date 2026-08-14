#include "screen_app_internal.h"

namespace ui::mono::screens::screen_240x320::detail
{
namespace
{

struct ContactsPageState
{
    enum class Route : unsigned char
    {
        List,
        Detail,
    };

    ::chat::contacts::PeerDirectoryItem selected_peer{};
    size_t selected_index = 0;
    bool nearby_view = false;
    Route route = Route::List;
};

ContactsPageState s_contacts_page_state;

} // namespace

const char* contact_display_name(const ::chat::contacts::PeerDirectoryItem& peer)
{
    if (!peer.display_name.empty())
    {
        return peer.display_name.c_str();
    }
    if (peer.long_name[0] != '\0')
    {
        return peer.long_name;
    }
    if (peer.short_name[0] != '\0')
    {
        return peer.short_name;
    }
    return "UNKNOWN";
}

bool select_contact();

void reset_contacts_page_state()
{
    s_contacts_page_state = ContactsPageState{};
}

void render_contacts()
{
    if (!::app::hasAppFacade())
    {
        set_line(0, "CONTACT SERVICE UNAVAILABLE");
        set_line(1, "WAIT FOR MESSAGING RUNTIME");
        clear_lines_from(2);
        return;
    }

    const ::chat::contacts::ContactService& service =
        ::app::messagingFacade().getContactService();
    const std::vector<::chat::contacts::PeerDirectoryItem>& peers =
        s_contacts_page_state.nearby_view ? service.getNearby() : service.getContacts();
    const char* const view = s_contacts_page_state.nearby_view ? "NEARBY" : "CONTACTS";

    if (s_contacts_page_state.route == ContactsPageState::Route::Detail)
    {
        if (!select_contact())
        {
            s_contacts_page_state.route = ContactsPageState::Route::List;
            set_notice("CONTACT UNAVAILABLE");
            render_contacts();
            return;
        }
        const ::chat::contacts::PeerDirectoryItem& peer = s_contacts_page_state.selected_peer;
        set_text(s_state.title, "CONTACTS");
        set_text(s_state.subtitle, "DETAIL");
        set_linef(0, "%s", contact_display_name(peer));
        set_linef(1, "ID %08lX", static_cast<unsigned long>(peer.node_id));
        set_linef(2, "TYPE %s", peer.is_contact ? "SAVED CONTACT" : "DISCOVERED NODE");
        set_linef(3, "STATE %s", peer.is_ignored ? "IGNORED" : "ACTIVE");
        set_linef(4,
                  "PROTOCOL %s",
                  peer.protocol == ::chat::contacts::NodeProtocolType::Reticulum ? "RETICULUM" : "MESH");
        set_line(5, "CHAT OPENS A DEDICATED THREAD");
        set_line(6, "BACK RETURNS TO CONTACT LIST");
        set_line(7, s_state.notice[0] != '\0' ? s_state.notice : "CHAT / BACK");
        clear_lines_from(8);
        return;
    }

    set_text(s_state.title, "CONTACTS");
    set_text(s_state.subtitle, s_contacts_page_state.nearby_view ? "NEARBY" : "LIST");
    set_linef(0, "%s %u", view, static_cast<unsigned>(peers.size()));

    if (peers.empty())
    {
        set_line(1,
                 s_contacts_page_state.nearby_view ? "NO NEARBY NODES" : "NO SAVED CONTACTS");
        set_line(2, "VIEW TO SWITCH LIST");
        set_line(3, s_state.notice[0] != '\0' ? s_state.notice : "REFRESH TO RETRY");
        clear_lines_from(4);
        return;
    }

    if (s_contacts_page_state.selected_index >= peers.size())
    {
        s_contacts_page_state.selected_index = peers.size() - 1U;
    }
    set_linef(1,
              "SELECT %u / %u",
              static_cast<unsigned>(s_contacts_page_state.selected_index + 1U),
              static_cast<unsigned>(peers.size()));

    size_t line = 2;
    for (size_t index = 0; index < peers.size() && line < 9; ++index)
    {
        const auto& peer = peers[index];
        set_linef(line++,
                  "%c %s %s %s",
                  index == s_contacts_page_state.selected_index ? '>' : ' ',
                  contact_display_name(peer),
                  peer.is_contact ? "SAVED" : "NODE",
                  peer.is_ignored ? "IGN" : "");
    }
    while (line < 9)
    {
        set_line(line++, "");
    }
    set_line(9,
             s_state.notice[0] != '\0'
                 ? s_state.notice
                 : "PREV/NEXT SELECT  DETAIL OPENS CONTACT");
}

bool select_contact()
{
    if (!::app::hasAppFacade())
    {
        return false;
    }

    const ::chat::contacts::ContactService& service =
        ::app::messagingFacade().getContactService();
    const std::vector<::chat::contacts::PeerDirectoryItem>& peers =
        s_contacts_page_state.nearby_view ? service.getNearby() : service.getContacts();
    if (peers.empty() || s_contacts_page_state.selected_index >= peers.size())
    {
        return false;
    }
    s_contacts_page_state.selected_peer = peers[s_contacts_page_state.selected_index];
    return true;
}

void add_contacts_actions()
{
    add_action("PREV", Action::ContactsPrevious, kMargin, kActionTop, 48);
    add_action("NEXT", Action::ContactsNext, 62, kActionTop, 48);
    add_action("DETAIL", Action::ContactsDetails, 116, kActionTop, 54);
    add_action("VIEW", Action::ContactsToggleView, 176, kActionTop, 56);
    add_action("BACK", Action::Back, kMargin, kActionTop + 22, 48);
}

bool handle_contacts_action(Action action)
{
    if (s_contacts_page_state.route == ContactsPageState::Route::Detail)
    {
        switch (action)
        {
        case Action::Back:
        case Action::ContactsBackToList:
            s_contacts_page_state.route = ContactsPageState::Route::List;
            set_notice("CONTACT LIST");
            return true;
        case Action::ContactsOpenChat:
            break;
        default:
            return false;
        }
    }

    switch (action)
    {
    case Action::ContactsPrevious:
    case Action::ContactsNext:
    {
        if (!::app::hasAppFacade())
        {
            set_notice("CONTACT SERVICE UNAVAILABLE");
            return true;
        }
        const ::chat::contacts::ContactService& service =
            ::app::messagingFacade().getContactService();
        const std::vector<::chat::contacts::PeerDirectoryItem>& peers =
            s_contacts_page_state.nearby_view ? service.getNearby() : service.getContacts();
        if (peers.empty())
        {
            set_notice("NO CONTACTS IN THIS VIEW");
            return true;
        }
        if (action == Action::ContactsPrevious)
        {
            s_contacts_page_state.selected_index = s_contacts_page_state.selected_index == 0
                                                       ? peers.size() - 1U
                                                       : s_contacts_page_state.selected_index - 1U;
        }
        else
        {
            s_contacts_page_state.selected_index =
                (s_contacts_page_state.selected_index + 1U) % peers.size();
        }
        set_notice("");
        return true;
    }
    case Action::ContactsToggleView:
        if (s_contacts_page_state.route != ContactsPageState::Route::List)
        {
            return false;
        }
        s_contacts_page_state.nearby_view = !s_contacts_page_state.nearby_view;
        s_contacts_page_state.selected_index = 0;
        set_notice(s_contacts_page_state.nearby_view ? "NEARBY NODES" : "SAVED CONTACTS");
        return true;
    case Action::ContactsOpenChat:
    {
        if (!select_contact())
        {
            set_notice("NO CONTACT SELECTED");
            return true;
        }
        const ::chat::contacts::PeerDirectoryItem& peer = s_contacts_page_state.selected_peer;
        const ::chat::MeshProtocol configured_protocol =
            ::chat::infra::normalizeMeshProtocol(
                ::app::configFacade().readConfig().mesh_protocol);
        const ::chat::MeshProtocol protocol = ::chat::infra::meshProtocolFromNodeProtocol(
            peer.protocol,
            configured_protocol);

        if (protocol != configured_protocol)
        {
            set_notice("SWITCH PROTOCOL BEFORE CHAT");
            return true;
        }

        ::chat::ConversationId core_id(::chat::ChannelId::PRIMARY, peer.node_id, protocol);
        if (protocol == ::chat::MeshProtocol::Reticulum &&
            ::chat::hasReticulumDestinationIdentity(peer.reticulum_identity))
        {
            core_id.peer = 0;
            core_id.reticulum_identity = peer.reticulum_identity;
        }
        const ::ui::chat::ConversationId conversation =
            ::chat_presentation_adapters::toUiConversationId(core_id);
        if (!queue_chat_conversation(conversation) ||
            !::ui::menu_layout::launchAppByStableId("chat"))
        {
            cancel_queued_chat_conversation();
            set_notice("CHAT ROUTE UNAVAILABLE");
            return true;
        }
        return true;
    }
    case Action::ContactsDetails:
        if (!select_contact())
        {
            set_notice("NO CONTACT SELECTED");
            return true;
        }
        s_contacts_page_state.route = ContactsPageState::Route::Detail;
        set_notice("CONTACT DETAIL");
        return true;

    default:
        return false;
    }
}

void configure_contacts_actions()
{
    if (s_state.adapter == nullptr || s_state.adapter->page_kind() != PageKind::Contacts ||
        s_state.action_count < 5)
    {
        return;
    }

    set_action(0, "PREV", Action::ContactsPrevious);
    set_action(1, "NEXT", Action::ContactsNext);
    if (s_contacts_page_state.route == ContactsPageState::Route::Detail)
    {
        set_action(0, "CHAT", Action::ContactsOpenChat);
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

    set_action(2, "DETAIL", Action::ContactsDetails);
    set_action(3, "VIEW", Action::ContactsToggleView);
    set_action(4, "BACK", Action::Back);
    for (size_t index = 0; index < 5; ++index)
    {
        set_action_visible(index, true);
    }
}
} // namespace ui::mono::screens::screen_240x320::detail
