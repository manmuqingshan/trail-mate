#include "ui/screens/contacts/contacts_page_runtime.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "chat/domain/chat_types.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "platform/ui/reticulum_group_config_runtime.h"
#include "ui/app_runtime.h"
#include "ui/screens/chat/chat_compose_components.h"
#include "ui/screens/chat/chat_conversation_components.h"
#include "ui/screens/contacts/contacts_page_components.h"
#include "ui/screens/contacts/contacts_page_input.h"
#include "ui/screens/contacts/contacts_page_layout.h"
#include "ui/screens/contacts/contacts_state.h"
#include "ui/ui_common.h"
#include "ui/widgets/ime/ime_widget.h"
#include "ui/widgets/top_bar.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#define CONTACTS_DEBUG 0
#if CONTACTS_DEBUG
#define CONTACTS_LOG(...) std::printf(__VA_ARGS__)
#else
#define CONTACTS_LOG(...)
#endif

using namespace contacts::ui;

namespace
{

using contacts::ui::shell::Host;

const Host* s_host = nullptr;

void format_reticulum_hash_prefix(const chat::ReticulumPeerIdentity& identity,
                                  char* out,
                                  size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!chat::hasReticulumDestinationIdentity(identity) || out_len < 9)
    {
        std::snprintf(out, out_len, "-");
        return;
    }
    std::snprintf(out,
                  out_len,
                  "%02X%02X%02X%02X",
                  static_cast<unsigned>(identity.destination_hash[0]),
                  static_cast<unsigned>(identity.destination_hash[1]),
                  static_cast<unsigned>(identity.destination_hash[2]),
                  static_cast<unsigned>(identity.destination_hash[3]));
}

void request_exit()
{
    if (s_host)
    {
        ::ui::page::request_exit(s_host);
        return;
    }
    ui_request_exit_to_menu();
}

void contacts_top_bar_back(void*)
{
    if (g_contacts_state.exiting)
    {
        return;
    }
    g_contacts_state.exiting = true;
    if (g_contacts_state.refresh_timer != nullptr)
    {
        lv_timer_del(g_contacts_state.refresh_timer);
        g_contacts_state.refresh_timer = nullptr;
    }
    if (g_contacts_state.conversation_timer != nullptr)
    {
        lv_timer_del(g_contacts_state.conversation_timer);
        g_contacts_state.conversation_timer = nullptr;
    }
    if (g_contacts_state.discover_scan_timer != nullptr)
    {
        lv_timer_del(g_contacts_state.discover_scan_timer);
        g_contacts_state.discover_scan_timer = nullptr;
    }
    if (g_contacts_state.root)
    {
        lv_obj_add_flag(g_contacts_state.root, LV_OBJ_FLAG_HIDDEN);
    }
    request_exit();
}

void copy_text(char* out, size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%s", text ? text : "");
}

bool same_reticulum_groups(const chat::ReticulumGroupDestinationConfig* lhs,
                           const chat::ReticulumGroupDestinationConfig* rhs,
                           std::size_t count)
{
    if (!lhs || !rhs)
    {
        return lhs == rhs;
    }
    for (std::size_t index = 0; index < count; ++index)
    {
        if (lhs[index].enabled != rhs[index].enabled ||
            std::strncmp(lhs[index].name,
                         rhs[index].name,
                         sizeof(lhs[index].name)) != 0 ||
            !chat::sameReticulumPeerIdentity(lhs[index].identity, rhs[index].identity))
        {
            return false;
        }
    }
    return true;
}

void refresh_reticulum_group_storage_state(const platform::ui::reticulum_groups::Status& status)
{
    g_contacts_state.reticulum_group_storage_supported = status.supported;
    g_contacts_state.reticulum_group_storage_ready = status.sd_present;
    g_contacts_state.reticulum_group_storage_loaded = status.loaded;
    copy_text(g_contacts_state.reticulum_group_storage_message,
              sizeof(g_contacts_state.reticulum_group_storage_message),
              status.message);
    copy_text(g_contacts_state.reticulum_group_storage_detail,
              sizeof(g_contacts_state.reticulum_group_storage_detail),
              status.detail);
}

bool node_matches_active_protocol(const chat::contacts::NodeInfo& node)
{
    const chat::MeshProtocol active_protocol =
        chat::infra::normalizeMeshProtocol(
            app::configFacade().getConfig().mesh_protocol);

    if (chat::infra::isReticulumMeshProtocol(active_protocol))
    {
        return chat::infra::isReticulumNodeProtocol(node.protocol);
    }

    if (node.protocol == chat::contacts::NodeProtocolType::Unknown)
    {
        return true;
    }

    return chat::infra::meshProtocolFromNodeProtocol(
               node.protocol,
               active_protocol) == active_protocol;
}

void filter_to_active_protocol(std::vector<chat::contacts::NodeInfo>& nodes)
{
    nodes.erase(std::remove_if(nodes.begin(),
                               nodes.end(),
                               [](const chat::contacts::NodeInfo& node)
                               {
                                   return !node_matches_active_protocol(node);
                               }),
                nodes.end());
}

void refresh_reticulum_groups_data()
{
    g_contacts_state.reticulum_group_list.clear();
    const chat::MeshProtocol active_protocol =
        chat::infra::normalizeMeshProtocol(
            app::configFacade().getConfig().mesh_protocol);
    if (!chat::infra::isReticulumMeshProtocol(active_protocol))
    {
        g_contacts_state.reticulum_group_storage_supported = false;
        g_contacts_state.reticulum_group_storage_ready = false;
        g_contacts_state.reticulum_group_storage_loaded = false;
        g_contacts_state.reticulum_group_storage_message[0] = '\0';
        g_contacts_state.reticulum_group_storage_detail[0] = '\0';
        return;
    }

    app::AppConfig& config = app::configFacade().getConfig();
    chat::MeshConfig& reticulum_config = config.reticulumConfig();
    chat::ReticulumGroupDestinationConfig previous[chat::kReticulumGroupDestinationMaxCount] = {};
    std::memcpy(previous,
                reticulum_config.reticulum_groups,
                sizeof(previous));
    const auto status = platform::ui::reticulum_groups::load(
        reticulum_config.reticulum_groups,
        chat::kReticulumGroupDestinationMaxCount);
    refresh_reticulum_group_storage_state(status);
    if (!same_reticulum_groups(previous,
                               reticulum_config.reticulum_groups,
                               chat::kReticulumGroupDestinationMaxCount))
    {
        app::configFacade().applyMeshConfig();
    }

    const chat::MeshConfig& mesh_config = config.activeMeshConfig();
    for (std::size_t index = 0; index < chat::kReticulumGroupDestinationMaxCount; ++index)
    {
        const chat::ReticulumGroupDestinationConfig& group =
            mesh_config.reticulum_groups[index];
        if (!group.enabled ||
            !chat::hasReticulumDestinationIdentity(group.identity))
        {
            continue;
        }

        chat::contacts::NodeInfo item{};
        item.node_id = 0;
        item.protocol = chat::contacts::NodeProtocolType::Reticulum;
        item.role = chat::contacts::NodeRoleType::Client;
        item.channel = static_cast<uint8_t>(index);
        item.reticulum_identity = group.identity;
        const char* name = group.name[0] != '\0' ? group.name : nullptr;
        char fallback[chat::kReticulumGroupNameMaxLen] = {};
        if (!name)
        {
            std::snprintf(fallback,
                          sizeof(fallback),
                          "Group %u",
                          static_cast<unsigned>(index + 1));
            name = fallback;
        }
        std::strncpy(item.long_name, name, sizeof(item.long_name) - 1);
        item.long_name[sizeof(item.long_name) - 1] = '\0';
        item.display_name = item.long_name;
        std::snprintf(item.short_name,
                      sizeof(item.short_name),
                      "RT%u",
                      static_cast<unsigned>(index + 1));
        g_contacts_state.reticulum_group_list.push_back(item);

        char dest_hash[12] = {};
        format_reticulum_hash_prefix(group.identity, dest_hash, sizeof(dest_hash));
        std::printf("[Contacts][RTGroup] configured index=%u name=%s dest=%s\n",
                    static_cast<unsigned>(index),
                    name,
                    dest_hash);
    }
}

void refresh_contacts_data_impl_internal()
{
    chat::contacts::ContactService& contact_service = app::messagingFacade().getContactService();
    g_contacts_state.contacts_list = contact_service.getContacts();
    g_contacts_state.nearby_list = contact_service.getNearby();
    g_contacts_state.ignored_list = contact_service.getIgnoredNodes();
    refresh_reticulum_groups_data();
    filter_to_active_protocol(g_contacts_state.contacts_list);
    filter_to_active_protocol(g_contacts_state.nearby_list);
    filter_to_active_protocol(g_contacts_state.ignored_list);

    CONTACTS_LOG("[Contacts] Data refreshed: %zu contacts, %zu nearby, %zu groups, %zu ignored\n",
                 g_contacts_state.contacts_list.size(),
                 g_contacts_state.nearby_list.size(),
                 g_contacts_state.reticulum_group_list.size(),
                 g_contacts_state.ignored_list.size());
}

} // namespace

void refresh_contacts_data_impl()
{
    refresh_contacts_data_impl_internal();
}

namespace contacts::ui::runtime
{

bool is_available()
{
    return app::hasAppFacade();
}

void enter(const shell::Host* host, lv_obj_t* parent)
{
    if (!is_available())
    {
        return;
    }

    s_host = host;

    CONTACTS_LOG("[Contacts] Entering Contacts page\n");

    if (g_contacts_state.root != nullptr)
    {
        lv_obj_del(g_contacts_state.root);
        g_contacts_state.root = nullptr;
    }

    g_contacts_state.exiting = false;
    g_contacts_state.contact_service = &app::messagingFacade().getContactService();
    g_contacts_state.chat_service = &app::messagingFacade().getChatService();

    lv_group_t* prev_group = lv_group_get_default();
    set_default_group(nullptr);

    g_contacts_state.root = contacts::ui::layout::create_root(parent);
    contacts::ui::layout::create_header(g_contacts_state.root, contacts_top_bar_back, nullptr);

    lv_obj_t* content = contacts::ui::layout::create_content(g_contacts_state.root);
    g_contacts_state.page = content;
    contacts::ui::layout::create_footer(g_contacts_state.root);
    ui_update_top_bar_battery(g_contacts_state.top_bar);

    create_filter_panel(content);
    contacts::ui::layout::create_list_panel(content);

    set_default_group(prev_group);

    g_contacts_state.current_mode = ContactsMode::Contacts;
    g_contacts_state.last_action_mode = ContactsMode::Contacts;
    g_contacts_state.current_page = 0;
    g_contacts_state.selected_index = -1;

    init_contacts_input();
    refresh_contacts_data();
    std::printf("[Contacts] open protocol=%s contacts=%u nearby=%u groups=%u ignored=%u\n",
                chat::infra::meshProtocolName(
                    chat::infra::normalizeMeshProtocol(
                        app::configFacade().getConfig().mesh_protocol)),
                static_cast<unsigned>(g_contacts_state.contacts_list.size()),
                static_cast<unsigned>(g_contacts_state.nearby_list.size()),
                static_cast<unsigned>(g_contacts_state.reticulum_group_list.size()),
                static_cast<unsigned>(g_contacts_state.ignored_list.size()));
    refresh_ui();

    g_contacts_state.initialized = true;
    CONTACTS_LOG("[Contacts] Contacts page initialized\n");
}

void exit(lv_obj_t* parent)
{
    (void)parent;

    if (!is_available())
    {
        s_host = nullptr;
        return;
    }

    CONTACTS_LOG("[Contacts] Exiting Contacts page\n");

    if (g_contacts_state.compose_screen)
    {
        if (g_contacts_state.compose_ime)
        {
            g_contacts_state.compose_ime->detach();
            delete g_contacts_state.compose_ime;
            g_contacts_state.compose_ime = nullptr;
        }
        delete g_contacts_state.compose_screen;
        g_contacts_state.compose_screen = nullptr;
    }
    if (g_contacts_state.conversation_screen)
    {
        delete g_contacts_state.conversation_screen;
        g_contacts_state.conversation_screen = nullptr;
    }
    if (g_contacts_state.conversation_timer != nullptr)
    {
        lv_timer_del(g_contacts_state.conversation_timer);
        g_contacts_state.conversation_timer = nullptr;
    }
    if (g_contacts_state.discover_scan_timer != nullptr)
    {
        lv_timer_del(g_contacts_state.discover_scan_timer);
        g_contacts_state.discover_scan_timer = nullptr;
    }
    if (g_contacts_state.refresh_timer != nullptr)
    {
        lv_timer_del(g_contacts_state.refresh_timer);
        g_contacts_state.refresh_timer = nullptr;
    }

    cleanup_contacts_input();
    cleanup_modals();

    if (g_contacts_state.root != nullptr)
    {
        lv_obj_del(g_contacts_state.root);
        g_contacts_state.root = nullptr;
    }

    g_contacts_state = ContactsPageState{};
    CONTACTS_LOG("[Contacts] Contacts page cleaned up\n");
    s_host = nullptr;
}

} // namespace contacts::ui::runtime
