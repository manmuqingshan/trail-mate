#include "ui/screens/contacts/contacts_page_runtime.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "chat/domain/chat_types.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/infra/reticulum/reticulum_wire.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "platform/ui/reticulum_contact_projection_policy.h"
#include "platform/ui/reticulum_directory_runtime.h"
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
#include <limits>
#include <memory>
#include <new>
#include <string>

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
namespace rtcontacts = ::platform::ui::reticulum_contacts;
namespace rtdir = ::platform::ui::reticulum_directory;

constexpr uint32_t kContactsRefreshIntervalMs = 2000;
constexpr std::size_t kReticulumDirectoryProjectionLimit = 100;

const Host* s_host = nullptr;

static uint32_t hash_step(uint32_t hash, uint8_t byte)
{
    return (hash ^ byte) * 16777619U;
}

static uint32_t hash_bytes(uint32_t hash, const uint8_t* data, std::size_t len)
{
    if (!data)
    {
        return hash_step(hash, 0);
    }
    for (std::size_t i = 0; i < len; ++i)
    {
        hash = hash_step(hash, data[i]);
    }
    return hash;
}

static uint32_t hash_u32(uint32_t hash, uint32_t value)
{
    hash = hash_step(hash, static_cast<uint8_t>(value & 0xFFU));
    hash = hash_step(hash, static_cast<uint8_t>((value >> 8) & 0xFFU));
    hash = hash_step(hash, static_cast<uint8_t>((value >> 16) & 0xFFU));
    hash = hash_step(hash, static_cast<uint8_t>((value >> 24) & 0xFFU));
    return hash;
}

static uint32_t hash_text(uint32_t hash, const char* text)
{
    if (!text)
    {
        return hash_step(hash, 0);
    }
    while (*text)
    {
        hash = hash_step(hash, static_cast<uint8_t>(*text));
        ++text;
    }
    return hash_step(hash, 0);
}

static uint32_t hash_string(uint32_t hash, const std::string& text)
{
    return hash_text(hash, text.c_str());
}

static uint32_t hash_reticulum_identity(uint32_t hash,
                                        const chat::ReticulumPeerIdentity& identity)
{
    hash = hash_bytes(hash,
                      identity.destination_hash,
                      sizeof(identity.destination_hash));
    hash = hash_bytes(hash, identity.identity_hash, sizeof(identity.identity_hash));
    hash = hash_step(hash, identity.valid ? 1U : 0U);
    return hash;
}

static uint32_t hash_node(uint32_t hash, const chat::contacts::PeerDirectoryItem& node)
{
    hash = hash_u32(hash, node.node_id);
    hash = hash_text(hash, node.short_name);
    hash = hash_text(hash, node.long_name);
    hash = hash_string(hash, node.display_name);
    hash = hash_step(hash, static_cast<uint8_t>(node.protocol));
    hash = hash_step(hash, static_cast<uint8_t>(node.role));
    hash = hash_step(hash, node.channel);
    hash = hash_step(hash, node.hops_away);
    hash = hash_step(hash, node.is_contact ? 1U : 0U);
    hash = hash_step(hash, node.is_ignored ? 1U : 0U);
    hash = hash_step(hash, node.has_public_key ? 1U : 0U);
    hash = hash_step(hash, node.key_manually_verified ? 1U : 0U);
    return hash_reticulum_identity(hash, node.reticulum_identity);
}

static uint32_t hash_node_list(uint32_t hash,
                               const std::vector<chat::contacts::PeerDirectoryItem>& nodes)
{
    hash = hash_u32(hash, static_cast<uint32_t>(nodes.size()));
    for (const auto& node : nodes)
    {
        hash = hash_node(hash, node);
    }
    return hash;
}

static uint32_t compute_contacts_data_signature()
{
    uint32_t hash = 2166136261U;
    hash = hash_node_list(hash, g_contacts_state.contacts_list);
    hash = hash_node_list(hash, g_contacts_state.nearby_list);
    hash = hash_node_list(hash, g_contacts_state.reticulum_group_list);
    hash = hash_node_list(hash, g_contacts_state.ignored_list);
    hash = hash_step(hash, g_contacts_state.reticulum_group_storage_supported ? 1U : 0U);
    hash = hash_step(hash, g_contacts_state.reticulum_group_storage_ready ? 1U : 0U);
    hash = hash_step(hash, g_contacts_state.reticulum_group_storage_loaded ? 1U : 0U);
    hash = hash_text(hash, g_contacts_state.reticulum_group_storage_message);
    hash = hash_text(hash, g_contacts_state.reticulum_group_storage_detail);
    return hash;
}

static void mark_contacts_data_refreshed()
{
    const uint32_t signature = compute_contacts_data_signature();
    if (!g_contacts_state.contacts_data_signature_valid ||
        signature != g_contacts_state.contacts_data_signature)
    {
        g_contacts_state.contacts_data_signature = signature;
        ++g_contacts_state.contacts_data_revision;
        if (g_contacts_state.contacts_data_revision == 0)
        {
            g_contacts_state.contacts_data_revision = 1;
        }
        g_contacts_state.contacts_data_signature_valid = true;
    }
}

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
    request_exit();
}

void contacts_refresh_timer_cb(lv_timer_t* timer)
{
    (void)timer;
    if (g_contacts_state.exiting || g_contacts_state.root == nullptr)
    {
        return;
    }

    const size_t before_contacts = g_contacts_state.contacts_list.size();
    const size_t before_nearby = g_contacts_state.nearby_list.size();
    const size_t before_ignored = g_contacts_state.ignored_list.size();

    const uint32_t before_revision = g_contacts_state.contacts_data_revision;
    refresh_contacts_data();
    const bool data_changed =
        before_revision != g_contacts_state.contacts_data_revision;
    if (before_contacts != g_contacts_state.contacts_list.size() ||
        before_nearby != g_contacts_state.nearby_list.size() ||
        before_ignored != g_contacts_state.ignored_list.size())
    {
        std::printf("[Contacts] refresh source=timer contacts=%u nearby=%u ignored=%u\n",
                    static_cast<unsigned>(g_contacts_state.contacts_list.size()),
                    static_cast<unsigned>(g_contacts_state.nearby_list.size()),
                    static_cast<unsigned>(g_contacts_state.ignored_list.size()));
    }
    if (data_changed ||
        g_contacts_state.rendered_data_revision != g_contacts_state.contacts_data_revision)
    {
        refresh_ui();
    }
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

bool node_matches_active_protocol(const chat::contacts::PeerDirectoryItem& node)
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

void filter_to_active_protocol(std::vector<chat::contacts::PeerDirectoryItem>& nodes)
{
    nodes.erase(std::remove_if(nodes.begin(),
                               nodes.end(),
                               [](const chat::contacts::PeerDirectoryItem& node)
                               {
                                   return !node_matches_active_protocol(node);
                               }),
                nodes.end());
}

uint32_t reticulum_node_id_from_destination_hash(
    const uint8_t destination_hash[chat::kReticulumPeerHashSize])
{
    return chat::reticulum::nodeIdFromDestinationHash(destination_hash);
}

void copy_node_text(char* out, std::size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%s", text ? text : "");
}

std::string reticulum_record_fallback_name(
    const chat::ReticulumPeerIdentity& identity)
{
    (void)identity;
    return "Anonymous Peer";
}

chat::contacts::PeerDirectoryItem node_from_lxmf_address(
    const rtdir::LxmfAddressRecord& record,
    bool as_contact)
{
    chat::contacts::PeerDirectoryItem item{};
    item.node_id = reticulum_node_id_from_destination_hash(record.destination_hash);
    item.last_seen = record.last_seen_s;
    item.snr = std::numeric_limits<float>::quiet_NaN();
    item.rssi = std::numeric_limits<float>::quiet_NaN();
    item.hops_away = 0xFF;
    item.channel = 0xFF;
    item.is_contact = as_contact || record.favorite;
    item.protocol = chat::contacts::NodeProtocolType::Reticulum;
    item.role = chat::contacts::NodeRoleType::Unknown;
    item.is_ignored = record.ignored;
    item.has_public_key = true;
    item.key_manually_verified = record.trusted;
    item.reticulum_identity =
        chat::makeReticulumPeerIdentity(record.destination_hash,
                                        record.identity_hash);

    std::snprintf(item.short_name,
                  sizeof(item.short_name),
                  "%04lX",
                  static_cast<unsigned long>(item.node_id & 0xFFFFUL));
    const std::string display =
        record.display_name[0] != '\0'
            ? std::string(record.display_name)
            : reticulum_record_fallback_name(item.reticulum_identity);
    copy_node_text(item.long_name, sizeof(item.long_name), display.c_str());
    item.display_name = display;
    return item;
}

bool same_reticulum_node(const chat::contacts::PeerDirectoryItem& lhs,
                         const chat::contacts::PeerDirectoryItem& rhs)
{
    if (chat::hasReticulumDestinationIdentity(lhs.reticulum_identity) &&
        chat::hasReticulumDestinationIdentity(rhs.reticulum_identity))
    {
        return chat::sameReticulumDestinationHash(lhs.reticulum_identity,
                                                  rhs.reticulum_identity);
    }
    return lhs.node_id != 0 && lhs.node_id == rhs.node_id;
}

void upsert_reticulum_projection(std::vector<chat::contacts::PeerDirectoryItem>& nodes,
                                 const chat::contacts::PeerDirectoryItem& projection,
                                 bool force_contact)
{
    for (auto& existing : nodes)
    {
        if (!same_reticulum_node(existing, projection))
        {
            continue;
        }
        existing.protocol = chat::contacts::NodeProtocolType::Reticulum;
        existing.reticulum_identity = projection.reticulum_identity;
        existing.last_seen =
            projection.last_seen != 0 ? projection.last_seen : existing.last_seen;
        existing.is_ignored = projection.is_ignored;
        existing.has_public_key = existing.has_public_key || projection.has_public_key;
        existing.key_manually_verified =
            existing.key_manually_verified || projection.key_manually_verified;
        existing.is_contact = existing.is_contact || force_contact || projection.is_contact;
        if (projection.long_name[0] != '\0')
        {
            copy_node_text(existing.long_name,
                           sizeof(existing.long_name),
                           projection.long_name);
            existing.display_name = projection.display_name;
        }
        return;
    }

    chat::contacts::PeerDirectoryItem inserted = projection;
    inserted.is_contact = inserted.is_contact || force_contact;
    nodes.push_back(inserted);
}

void merge_reticulum_directory_projection()
{
    const chat::MeshProtocol active_protocol =
        chat::infra::normalizeMeshProtocol(
            app::configFacade().getConfig().mesh_protocol);
    if (!chat::infra::isReticulumMeshProtocol(active_protocol))
    {
        return;
    }

    auto records = std::unique_ptr<rtdir::LxmfAddressRecord[]>(
        new (std::nothrow) rtdir::LxmfAddressRecord[kReticulumDirectoryProjectionLimit]);
    if (!records)
    {
        std::printf("[Contacts][RTDirectory] load skipped reason=oom records=%u\n",
                    static_cast<unsigned>(kReticulumDirectoryProjectionLimit));
        return;
    }

    std::size_t count = 0;
    const bool search_active = g_contacts_state.search_query[0] != '\0';
    const auto status =
        search_active
            ? rtdir::load_lxmf_addresses_matching(g_contacts_state.search_query,
                                                  records.get(),
                                                  kReticulumDirectoryProjectionLimit,
                                                  &count)
            : rtdir::load_lxmf_addresses(records.get(),
                                         kReticulumDirectoryProjectionLimit,
                                         &count);
    if (!status.loaded)
    {
        if (status.sd_present)
        {
            std::printf("[Contacts][RTDirectory] load failed message=%s detail=%s\n",
                        status.message,
                        status.detail);
        }
        return;
    }

    for (std::size_t index = 0; index < count; ++index)
    {
        const auto& record = records[index];
        const rtcontacts::ProjectionBucket bucket =
            rtcontacts::classify(record);
        if (bucket == rtcontacts::ProjectionBucket::Hidden)
        {
            continue;
        }

        const chat::contacts::PeerDirectoryItem item =
            node_from_lxmf_address(
                record,
                bucket == rtcontacts::ProjectionBucket::Contact);
        if (bucket == rtcontacts::ProjectionBucket::Contact)
        {
            upsert_reticulum_projection(g_contacts_state.contacts_list, item, true);
        }
        else if (bucket == rtcontacts::ProjectionBucket::Ignored)
        {
            upsert_reticulum_projection(g_contacts_state.ignored_list, item, false);
        }
        else
        {
            upsert_reticulum_projection(g_contacts_state.nearby_list, item, false);
        }
    }
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

        chat::contacts::PeerDirectoryItem item{};
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
    merge_reticulum_directory_projection();
    const chat::MeshProtocol active_protocol =
        chat::infra::normalizeMeshProtocol(
            app::configFacade().getConfig().mesh_protocol);
    if (chat::infra::isReticulumMeshProtocol(active_protocol))
    {
        std::stable_sort(
            g_contacts_state.nearby_list.begin(),
            g_contacts_state.nearby_list.end(),
            [](const chat::contacts::PeerDirectoryItem& lhs,
               const chat::contacts::PeerDirectoryItem& rhs)
            {
                return lhs.last_seen > rhs.last_seen;
            });
    }
    mark_contacts_data_refreshed();

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
    g_contacts_state.rendered_mode_valid = false;
    g_contacts_state.rendered_search_query[0] = '\0';
    g_contacts_state.contacts_data_signature = 0;
    g_contacts_state.contacts_data_revision = 0;
    g_contacts_state.rendered_data_revision = 0;
    g_contacts_state.contacts_data_signature_valid = false;
    g_contacts_state.focused_filter_mode = ContactsMode::Contacts;
    g_contacts_state.focused_filter_mode_valid = false;

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
    g_contacts_state.refresh_timer =
        lv_timer_create(contacts_refresh_timer_cb, kContactsRefreshIntervalMs, nullptr);
    if (g_contacts_state.refresh_timer)
    {
        std::printf("[Contacts] refresh timer started interval_ms=%u\n",
                    static_cast<unsigned>(kContactsRefreshIntervalMs));
    }

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

    cleanup_modals();
    cleanup_contacts_input();

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
