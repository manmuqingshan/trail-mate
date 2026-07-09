#include "ui/screens/network/network_page_shell.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "platform/ui/reticulum_directory_runtime.h"
#include "ui/app_runtime.h"
#include "ui/assets/fonts/font_utils.h"
#include "ui/components/floating_search_box.h"
#include "ui/components/shortcut_help_modal.h"
#include "ui/components/two_pane_layout.h"
#include "ui/components/two_pane_styles.h"
#include "ui/localization.h"
#include "ui/page/page_profile.h"
#include "ui/runtime/ui_feedback.h"
#include "ui/ui_common.h"
#include "ui/widgets/top_bar.h"

#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#if !defined(LV_FONT_MONTSERRAT_10) || !LV_FONT_MONTSERRAT_10
#define lv_font_montserrat_10 lv_font_montserrat_12
#endif
#if !defined(LV_FONT_MONTSERRAT_12) || !LV_FONT_MONTSERRAT_12
#define lv_font_montserrat_12 lv_font_montserrat_14
#endif
#if !defined(LV_FONT_MONTSERRAT_16) || !LV_FONT_MONTSERRAT_16
#define lv_font_montserrat_16 lv_font_montserrat_14
#endif
#ifndef LV_SYMBOL_SEARCH
#define LV_SYMBOL_SEARCH "S"
#endif
#ifndef LV_SYMBOL_REFRESH
#define LV_SYMBOL_REFRESH "R"
#endif
#ifndef LV_SYMBOL_HOME
#define LV_SYMBOL_HOME "H"
#endif
#ifndef LV_SYMBOL_RIGHT
#define LV_SYMBOL_RIGHT ">"
#endif
#ifndef LV_SYMBOL_LEFT
#define LV_SYMBOL_LEFT "<"
#endif
#ifndef LV_SYMBOL_STAR
#define LV_SYMBOL_STAR "*"
#endif

namespace
{

namespace rtdir = ::platform::ui::reticulum_directory;

constexpr std::size_t kMaxVisibleAnnounces = 100;
constexpr std::size_t kMaxVisibleAddresses = 100;
constexpr std::size_t kMaxDirectoryRows = 100;
constexpr std::size_t kMaxPageLinks = 16;
constexpr std::size_t kMaxHistoryEntries = 8;
constexpr std::size_t kAddressTextLen = 160;
constexpr std::size_t kSearchTextLen = 32;
constexpr std::size_t kHashTextLen = (rtdir::kReticulumHashSize * 2U) + 1U;

enum class DirectoryMode : uint8_t
{
    Favourites,
    Announces,
};

enum class DirectoryRowKind : uint8_t
{
    Favourite,
    Announce,
};

struct DirectoryRowContext
{
    DirectoryRowKind kind = DirectoryRowKind::Announce;
    std::size_t index = 0;
};

struct LinkContext
{
    char target[kAddressTextLen] = {};
};

struct HistoryEntry
{
    char address[kAddressTextLen] = {};
};

struct NetworkPageState
{
    lv_obj_t* root = nullptr;
    lv_obj_t* header = nullptr;
    lv_obj_t* content = nullptr;
    lv_obj_t* directory_panel = nullptr;
    lv_obj_t* directory_tabs = nullptr;
    lv_obj_t* favourites_tab = nullptr;
    lv_obj_t* announces_tab = nullptr;
    lv_obj_t* directory_search_btn = nullptr;
    lv_obj_t* directory_list = nullptr;
    lv_obj_t* browser_panel = nullptr;
    lv_obj_t* browser_toolbar = nullptr;
    lv_obj_t* browser_back_btn = nullptr;
    lv_obj_t* home_btn = nullptr;
    lv_obj_t* refresh_btn = nullptr;
    lv_obj_t* address_area = nullptr;
    lv_obj_t* go_btn = nullptr;
    lv_obj_t* viewport = nullptr;
    ::ui::widgets::TopBar top_bar;
    ::ui::components::floating_search_box::State search_box;
    ::ui::components::shortcut_help_modal::State help_modal;
    std::vector<rtdir::AnnounceRecord> announces;
    std::vector<rtdir::LxmfAddressRecord> addresses;
    std::array<DirectoryRowContext, kMaxDirectoryRows> row_contexts{};
    std::array<LinkContext, kMaxPageLinks> link_contexts{};
    std::array<HistoryEntry, kMaxHistoryEntries> history{};
    std::size_t announce_count = 0;
    std::size_t address_count = 0;
    std::size_t row_context_count = 0;
    std::size_t link_context_count = 0;
    std::size_t history_count = 0;
    std::size_t history_pos = 0;
    DirectoryMode directory_mode = DirectoryMode::Announces;
    bool immersive = false;
    bool directory_collapsed = false;
    bool browser_collapsed = false;
    bool suppress_history = false;
    rtdir::Status announce_status{};
    rtdir::Status address_status{};
    char current_address[kAddressTextLen] = "home:/";
    char search_query[kSearchTextLen] = {};
};

NetworkPageState g_state;

constexpr uint32_t kAmber = ::ui::components::two_pane_styles::kAccent;
constexpr uint32_t kAmberDark = 0xC98118;
constexpr uint32_t kWarmBg = ::ui::components::two_pane_styles::kSidePanelBg;
constexpr uint32_t kPanelBg = ::ui::components::two_pane_styles::kMainPanelBg;
constexpr uint32_t kLine = ::ui::components::two_pane_styles::kBorder;
constexpr uint32_t kText = ::ui::components::two_pane_styles::kTextPrimary;
constexpr uint32_t kTextDim = ::ui::components::two_pane_styles::kTextMuted;
constexpr uint32_t kTerminalBg = 0x050505;
constexpr uint32_t kTerminalText = 0xE8E0CC;
constexpr uint32_t kTerminalDim = 0x9A917F;
constexpr uint32_t kTerminalGreen = 0x4EA646;
constexpr uint32_t kTerminalAmber = 0xF4B443;

const char* safe_tr(const char* text)
{
    return ::ui::i18n::tr(text);
}

void copy_text(char* out, std::size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%s", text ? text : "");
}

const lv_font_t* body_font()
{
    return ::ui::page_profile::resolve_body_font();
}

const lv_font_t* caption_font()
{
    return ::ui::page_profile::resolve_caption_font();
}

const lv_font_t* tiny_font()
{
    return ::ui::page_profile::resolve_tiny_font();
}

void add_to_group(lv_obj_t* obj)
{
    if (app_g && obj)
    {
        lv_group_add_obj(app_g, obj);
    }
}

bool activation_event(lv_event_t* event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_CLICKED)
    {
        return true;
    }
    if (code != LV_EVENT_KEY)
    {
        return false;
    }
    const uint32_t key = lv_event_get_key(event);
    return key == LV_KEY_ENTER || key == LV_KEY_RIGHT;
}

void set_label(lv_obj_t* label,
               const char* text,
               uint32_t color,
               const lv_font_t* font = nullptr)
{
    if (!label)
    {
        return;
    }
    const char* value = text ? text : "";
    lv_label_set_text(label, value);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    ::ui::fonts::apply_localized_font(label, value, font ? font : body_font());
}

void style_plain_container(lv_obj_t* obj, uint32_t bg, lv_opa_t opa = LV_OPA_COVER)
{
    if (!obj)
    {
        return;
    }
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, opa, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
}

void style_panel(lv_obj_t* obj, uint32_t bg = kPanelBg)
{
    if (!obj)
    {
        return;
    }
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_hex(kLine), LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 8, LV_PART_MAIN);
}

void style_focusable(lv_obj_t* obj)
{
    if (!obj)
    {
        return;
    }
    lv_obj_set_style_border_color(obj, lv_color_hex(kAmberDark), LV_STATE_FOCUSED);
    lv_obj_set_style_outline_width(obj, 2, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_color(obj, lv_color_hex(kAmberDark), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_pad(obj, 1, LV_STATE_FOCUS_KEY);
}

void style_chrome_button(lv_obj_t* btn, bool compact = true)
{
    if (!btn)
    {
        return;
    }
    ::ui::components::two_pane_styles::apply_btn_basic(btn);
    lv_obj_set_style_radius(btn, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(kPanelBg), LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(kAmber), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(btn, lv_color_hex(kAmber), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_bg_color(btn, lv_color_hex(kAmberDark), LV_STATE_PRESSED);
    style_focusable(btn);
    if (compact)
    {
        lv_obj_set_style_pad_all(btn, 0, LV_PART_MAIN);
    }
}

uint8_t hex_nibble(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return static_cast<uint8_t>(ch - '0');
    }
    if (ch >= 'A' && ch <= 'F')
    {
        return static_cast<uint8_t>(ch - 'A' + 10);
    }
    if (ch >= 'a' && ch <= 'f')
    {
        return static_cast<uint8_t>(ch - 'a' + 10);
    }
    return 0xFF;
}

bool is_hex_text(const char* text, std::size_t len)
{
    if (!text)
    {
        return false;
    }
    for (std::size_t i = 0; i < len; ++i)
    {
        if (hex_nibble(text[i]) == 0xFF)
        {
            return false;
        }
    }
    return true;
}

void format_hash_hex(const uint8_t* hash, char* out, std::size_t out_len)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!hash || out_len < kHashTextLen)
    {
        copy_text(out, out_len, "--");
        return;
    }
    for (std::size_t i = 0; i < rtdir::kReticulumHashSize; ++i)
    {
        out[i * 2U] = kHex[(hash[i] >> 4U) & 0x0FU];
        out[(i * 2U) + 1U] = kHex[hash[i] & 0x0FU];
    }
    out[rtdir::kReticulumHashSize * 2U] = '\0';
}

void format_hash_prefix(const uint8_t* hash, char* out, std::size_t out_len)
{
    char full[kHashTextLen] = {};
    format_hash_hex(hash, full, sizeof(full));
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%.8s", full);
}

bool hash_matches_text(const uint8_t* hash, const char* hash_text)
{
    char full[kHashTextLen] = {};
    format_hash_hex(hash, full, sizeof(full));
    return hash_text && std::strncmp(full, hash_text, rtdir::kReticulumHashSize * 2U) == 0;
}

const char* aspect_label(rtdir::AnnounceAspect aspect)
{
    switch (aspect)
    {
    case rtdir::AnnounceAspect::LxmfDelivery:
        return "LXMF";
    case rtdir::AnnounceAspect::LxmfPropagation:
        return "Prop";
    case rtdir::AnnounceAspect::CallAudio:
        return "Call";
    case rtdir::AnnounceAspect::Unknown:
    default:
        return "Node";
    }
}

bool contains_ci(const char* text, const char* query)
{
    if (!query || query[0] == '\0')
    {
        return true;
    }
    if (!text)
    {
        return false;
    }
    const std::size_t query_len = std::strlen(query);
    const std::size_t text_len = std::strlen(text);
    if (query_len == 0)
    {
        return true;
    }
    if (query_len > text_len)
    {
        return false;
    }
    for (std::size_t start = 0; start + query_len <= text_len; ++start)
    {
        bool match = true;
        for (std::size_t offset = 0; offset < query_len; ++offset)
        {
            const auto lhs = static_cast<unsigned char>(text[start + offset]);
            const auto rhs = static_cast<unsigned char>(query[offset]);
            if (std::tolower(lhs) != std::tolower(rhs))
            {
                match = false;
                break;
            }
        }
        if (match)
        {
            return true;
        }
    }
    return false;
}

bool announce_matches_search(const rtdir::AnnounceRecord& announce)
{
    if (g_state.search_query[0] == '\0')
    {
        return true;
    }
    char destination[kHashTextLen] = {};
    char identity[kHashTextLen] = {};
    format_hash_hex(announce.destination_hash, destination, sizeof(destination));
    format_hash_hex(announce.identity_hash, identity, sizeof(identity));
    return contains_ci(announce.display_name, g_state.search_query) ||
           contains_ci(destination, g_state.search_query) ||
           contains_ci(identity, g_state.search_query) ||
           contains_ci(aspect_label(announce.aspect), g_state.search_query);
}

bool address_matches_search(const rtdir::LxmfAddressRecord& address)
{
    if (g_state.search_query[0] == '\0')
    {
        return true;
    }
    char destination[kHashTextLen] = {};
    char identity[kHashTextLen] = {};
    format_hash_hex(address.destination_hash, destination, sizeof(destination));
    format_hash_hex(address.identity_hash, identity, sizeof(identity));
    return contains_ci(address.display_name, g_state.search_query) ||
           contains_ci(destination, g_state.search_query) ||
           contains_ci(identity, g_state.search_query);
}

bool reticulum_active()
{
    if (!app::hasAppFacade())
    {
        return false;
    }
    const app::AppConfig& config = app::configFacade().getConfig();
    return chat::infra::isReticulumMeshProtocol(config.mesh_protocol);
}

void set_current_address(const char* address)
{
    copy_text(g_state.current_address, sizeof(g_state.current_address),
              (address && address[0] != '\0') ? address : "home:/");
    if (g_state.address_area && lv_obj_is_valid(g_state.address_area))
    {
        lv_textarea_set_text(g_state.address_area, g_state.current_address);
    }
}

void push_history(const char* address)
{
    if (!address || address[0] == '\0' || g_state.suppress_history)
    {
        return;
    }
    if (g_state.history_count != 0 &&
        std::strncmp(g_state.history[g_state.history_pos].address,
                     address,
                     sizeof(g_state.history[g_state.history_pos].address)) == 0)
    {
        return;
    }
    if (g_state.history_count < g_state.history.size())
    {
        ++g_state.history_count;
        g_state.history_pos = g_state.history_count - 1U;
    }
    else
    {
        for (std::size_t i = 1; i < g_state.history.size(); ++i)
        {
            g_state.history[i - 1U] = g_state.history[i];
        }
        g_state.history_pos = g_state.history.size() - 1U;
    }
    copy_text(g_state.history[g_state.history_pos].address,
              sizeof(g_state.history[g_state.history_pos].address),
              address);
}

bool extract_destination_text(const char* address, char* out, std::size_t out_len)
{
    if (!address || !out || out_len < kHashTextLen)
    {
        return false;
    }
    const char* start = address;
    if (std::strncmp(start, "rn://", 5) == 0)
    {
        start += 5;
    }
    if (std::strncmp(start, "reticulum://", 12) == 0)
    {
        start += 12;
    }
    if (!is_hex_text(start, rtdir::kReticulumHashSize * 2U))
    {
        return false;
    }
    std::snprintf(out, out_len, "%.*s",
                  static_cast<int>(rtdir::kReticulumHashSize * 2U),
                  start);
    return true;
}

const rtdir::AnnounceRecord* find_announce_by_destination_text(const char* hash_text)
{
    if (!hash_text)
    {
        return nullptr;
    }
    for (std::size_t i = 0; i < g_state.announce_count; ++i)
    {
        const auto& announce = g_state.announces[i];
        if (announce.valid && hash_matches_text(announce.destination_hash, hash_text))
        {
            return &announce;
        }
    }
    return nullptr;
}

const rtdir::LxmfAddressRecord* find_address_by_destination_text(const char* hash_text)
{
    if (!hash_text)
    {
        return nullptr;
    }
    for (std::size_t i = 0; i < g_state.address_count; ++i)
    {
        const auto& address = g_state.addresses[i];
        if (address.valid && hash_matches_text(address.destination_hash, hash_text))
        {
            return &address;
        }
    }
    return nullptr;
}

std::size_t favourite_count()
{
    std::size_t count = 0;
    for (std::size_t i = 0; i < g_state.address_count; ++i)
    {
        if (g_state.addresses[i].valid && g_state.addresses[i].favorite)
        {
            ++count;
        }
    }
    return count;
}

void render_current_page();
void render_directory_list();
void page_shortcut_event_cb(lv_event_t* event);
void rebuild_focus_group(lv_obj_t* preferred = nullptr);
void focus_browser_viewport();
void focus_directory_panel();
void apply_layout_state();
lv_coord_t resolve_directory_width();
void open_network_help_modal();
void close_network_help_modal();

bool object_in_subtree(lv_obj_t* root, lv_obj_t* obj)
{
    if (!root || !obj || !lv_obj_is_valid(root) || !lv_obj_is_valid(obj))
    {
        return false;
    }
    for (lv_obj_t* cursor = obj; cursor; cursor = lv_obj_get_parent(cursor))
    {
        if (cursor == root)
        {
            return true;
        }
    }
    return false;
}

bool object_visible(lv_obj_t* obj)
{
    if (!obj || !lv_obj_is_valid(obj))
    {
        return false;
    }
    for (lv_obj_t* cursor = obj; cursor; cursor = lv_obj_get_parent(cursor))
    {
        if (lv_obj_has_flag(cursor, LV_OBJ_FLAG_HIDDEN))
        {
            return false;
        }
    }
    return true;
}

void add_visible_to_group(lv_obj_t* obj)
{
    if (app_g && object_visible(obj))
    {
        lv_group_add_obj(app_g, obj);
    }
}

void add_directory_focusables()
{
    if (g_state.immersive || g_state.directory_collapsed)
    {
        return;
    }
    add_visible_to_group(g_state.favourites_tab);
    add_visible_to_group(g_state.announces_tab);
    add_visible_to_group(g_state.directory_search_btn);
    if (!g_state.directory_list || !lv_obj_is_valid(g_state.directory_list))
    {
        return;
    }
    const uint32_t child_count = lv_obj_get_child_count(g_state.directory_list);
    for (uint32_t index = 0; index < child_count; ++index)
    {
        lv_obj_t* child = lv_obj_get_child(g_state.directory_list, index);
        if (child && lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE))
        {
            add_visible_to_group(child);
        }
    }
}

void add_viewport_links_to_group()
{
    if (!g_state.viewport || !lv_obj_is_valid(g_state.viewport))
    {
        return;
    }
    const uint32_t child_count = lv_obj_get_child_count(g_state.viewport);
    for (uint32_t index = 0; index < child_count; ++index)
    {
        lv_obj_t* child = lv_obj_get_child(g_state.viewport, index);
        if (child && lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE))
        {
            add_visible_to_group(child);
        }
    }
}

void add_browser_focusables()
{
    if (g_state.browser_collapsed && !g_state.immersive)
    {
        return;
    }
    if (!g_state.immersive)
    {
        add_visible_to_group(g_state.browser_back_btn);
        add_visible_to_group(g_state.home_btn);
        add_visible_to_group(g_state.refresh_btn);
        add_visible_to_group(g_state.address_area);
        add_visible_to_group(g_state.go_btn);
    }
    add_visible_to_group(g_state.viewport);
    add_viewport_links_to_group();
}

bool focus_preferred_or_fallback(lv_obj_t* preferred, lv_obj_t* previous)
{
    lv_obj_t* target = nullptr;
    if (object_visible(preferred))
    {
        target = preferred;
    }
    else if (object_visible(previous))
    {
        target = previous;
    }
    else if (object_visible(g_state.viewport))
    {
        target = g_state.viewport;
    }

    if (!target || !app_g)
    {
        return false;
    }
    lv_group_focus_obj(target);
    return true;
}

void rebuild_focus_group(lv_obj_t* preferred)
{
    if (!app_g ||
        ::ui::components::floating_search_box::is_open(g_state.search_box) ||
        ::ui::components::shortcut_help_modal::is_open(g_state.help_modal))
    {
        return;
    }

    lv_obj_t* previous = lv_group_get_focused(app_g);
    lv_group_remove_all_objs(app_g);

    if (!g_state.immersive)
    {
        add_visible_to_group(g_state.top_bar.back_btn);
    }
    add_directory_focusables();
    add_browser_focusables();
    set_default_group(app_g);
    (void)focus_preferred_or_fallback(preferred, previous);
}

void focus_browser_viewport()
{
    rebuild_focus_group(g_state.viewport);
}

void focus_directory_panel()
{
    if (g_state.directory_collapsed)
    {
        return;
    }
    lv_obj_t* target = nullptr;
    if (g_state.directory_list && lv_obj_is_valid(g_state.directory_list))
    {
        const uint32_t child_count = lv_obj_get_child_count(g_state.directory_list);
        for (uint32_t index = 0; index < child_count; ++index)
        {
            lv_obj_t* child = lv_obj_get_child(g_state.directory_list, index);
            if (child && lv_obj_has_flag(child, LV_OBJ_FLAG_CLICKABLE))
            {
                target = child;
                break;
            }
        }
    }
    if (!target)
    {
        target = g_state.directory_mode == DirectoryMode::Favourites
                     ? g_state.favourites_tab
                     : g_state.announces_tab;
    }
    rebuild_focus_group(target);
}

void navigate_to_address(const char* address, bool push)
{
    const char* target = (address && address[0] != '\0') ? address : "home:/";
    if (push)
    {
        push_history(target);
    }
    set_current_address(target);
    render_current_page();
    rebuild_focus_group(g_state.viewport);
}

void resolve_link_target(const char* target, char* out, std::size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!target || target[0] == '\0')
    {
        copy_text(out, out_len, "home:/");
        return;
    }
    if (target[0] == '/')
    {
        char hash_text[kHashTextLen] = {};
        if (extract_destination_text(g_state.current_address, hash_text, sizeof(hash_text)))
        {
            std::snprintf(out, out_len, "%s:%s", hash_text, target);
            return;
        }
    }
    copy_text(out, out_len, target);
}

void link_event_cb(lv_event_t* event)
{
    if (!activation_event(event))
    {
        return;
    }
    auto* context = static_cast<LinkContext*>(lv_event_get_user_data(event));
    if (!context)
    {
        return;
    }
    char resolved[kAddressTextLen] = {};
    resolve_link_target(context->target, resolved, sizeof(resolved));
    navigate_to_address(resolved, true);
    lv_event_stop_processing(event);
}

void clear_viewport()
{
    if (!g_state.viewport)
    {
        return;
    }
    lv_obj_clean(g_state.viewport);
    g_state.link_context_count = 0;
}

lv_obj_t* add_terminal_label(const char* text,
                             uint32_t color = kTerminalText,
                             const lv_font_t* font = nullptr,
                             lv_label_long_mode_t mode = LV_LABEL_LONG_WRAP)
{
    lv_obj_t* label = lv_label_create(g_state.viewport);
    set_label(label, text ? text : "", color, font ? font : caption_font());
    lv_label_set_long_mode(label, mode);
    lv_obj_set_width(label, LV_PCT(100));
    return label;
}

void add_terminal_spacer(lv_coord_t height = 3)
{
    lv_obj_t* spacer = lv_obj_create(g_state.viewport);
    lv_obj_set_size(spacer, LV_PCT(100), height);
    style_plain_container(spacer, kTerminalBg, LV_OPA_TRANSP);
    ::ui::components::two_pane_layout::make_non_scrollable(spacer);
}

void add_terminal_pair(const char* key, const char* value)
{
    char line[192] = {};
    std::snprintf(line, sizeof(line), "%s %s", key ? key : "", value ? value : "");
    add_terminal_label(line, kTerminalDim, tiny_font(), LV_LABEL_LONG_DOT);
}

void add_terminal_link(const char* target, const char* label)
{
    if (!g_state.viewport || g_state.link_context_count >= g_state.link_contexts.size())
    {
        return;
    }
    LinkContext& context = g_state.link_contexts[g_state.link_context_count++];
    copy_text(context.target, sizeof(context.target), target);

    lv_obj_t* btn = lv_btn_create(g_state.viewport);
    lv_obj_set_width(btn, LV_PCT(100));
    lv_obj_set_height(btn, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(btn, lv_color_hex(kTerminalBg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1B2A12), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1B2A12), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_outline_width(btn, 1, LV_STATE_FOCUS_KEY);
    lv_obj_set_style_outline_color(btn, lv_color_hex(kTerminalGreen), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_radius(btn, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_left(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_right(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_top(btn, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(btn, 2, LV_PART_MAIN);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, link_event_cb, LV_EVENT_CLICKED, &context);
    lv_obj_add_event_cb(btn, link_event_cb, LV_EVENT_KEY, &context);
    lv_obj_add_event_cb(btn, page_shortcut_event_cb, LV_EVENT_KEY, nullptr);
    add_to_group(btn);

    char text[192] = {};
    std::snprintf(text, sizeof(text), "=> %s", label && label[0] ? label : target);
    lv_obj_t* link_label = lv_label_create(btn);
    set_label(link_label, text, kTerminalGreen, caption_font());
    lv_label_set_long_mode(link_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(link_label, LV_PCT(100));
    lv_obj_center(link_label);
}

void render_micron_line(const char* line)
{
    if (!line)
    {
        return;
    }
    if (line[0] == '\0')
    {
        add_terminal_spacer();
        return;
    }
    if (std::strncmp(line, "# ", 2) == 0)
    {
        add_terminal_label(line + 2, kTerminalAmber, body_font(), LV_LABEL_LONG_WRAP);
        return;
    }
    if (std::strncmp(line, "## ", 3) == 0)
    {
        add_terminal_label(line + 3, kTerminalAmber, caption_font(), LV_LABEL_LONG_WRAP);
        return;
    }
    if (std::strncmp(line, "=>", 2) == 0)
    {
        const char* cursor = line + 2;
        while (*cursor == ' ' || *cursor == '\t')
        {
            ++cursor;
        }
        char target[kAddressTextLen] = {};
        char label[96] = {};
        std::size_t target_len = 0;
        while (cursor[target_len] != '\0' && cursor[target_len] != ' ' &&
               cursor[target_len] != '\t' && target_len + 1U < sizeof(target))
        {
            target[target_len] = cursor[target_len];
            ++target_len;
        }
        target[target_len] = '\0';
        cursor += target_len;
        while (*cursor == ' ' || *cursor == '\t')
        {
            ++cursor;
        }
        copy_text(label, sizeof(label), cursor[0] != '\0' ? cursor : target);
        add_terminal_link(target, label);
        return;
    }
    add_terminal_label(line, kTerminalText, caption_font(), LV_LABEL_LONG_WRAP);
}

void render_home_page()
{
    clear_viewport();
    add_terminal_label("Nomad Network", kTerminalAmber, body_font(), LV_LABEL_LONG_WRAP);
    add_terminal_spacer();
    char line[96] = {};
    std::snprintf(line,
                  sizeof(line),
                  "announces %u  favourites %u",
                  static_cast<unsigned>(g_state.announce_count),
                  static_cast<unsigned>(favourite_count()));
    add_terminal_label(line, kTerminalText, caption_font(), LV_LABEL_LONG_WRAP);

    const bool active = reticulum_active();
    add_terminal_label(active ? "reticulum active" : "reticulum inactive",
                       active ? kTerminalGreen : kTerminalDim,
                       caption_font(),
                       LV_LABEL_LONG_WRAP);
    if (!g_state.announce_status.sd_present && !g_state.address_status.sd_present)
    {
        add_terminal_label("sd unavailable", kTerminalDim, caption_font(), LV_LABEL_LONG_WRAP);
    }
    add_terminal_spacer(6);
    add_terminal_link("home:/announces", "Announces");
    add_terminal_link("home:/favourites", "Favourites");

    if (g_state.announce_count != 0)
    {
        char dest[kHashTextLen] = {};
        format_hash_hex(g_state.announces[0].destination_hash, dest, sizeof(dest));
        char target[kAddressTextLen] = {};
        std::snprintf(target, sizeof(target), "%s:/page/index.mu", dest);
        const char* label = g_state.announces[0].display_name[0] != '\0'
                                ? g_state.announces[0].display_name
                                : "Latest announce";
        add_terminal_link(target, label);
    }
}

void render_collection_page(bool favourites)
{
    clear_viewport();
    add_terminal_label(favourites ? "Favourites" : "Announces",
                       kTerminalAmber,
                       body_font(),
                       LV_LABEL_LONG_WRAP);
    add_terminal_spacer();
    if (favourites)
    {
        std::size_t visible = 0;
        for (std::size_t i = 0; i < g_state.address_count && visible < 12; ++i)
        {
            const auto& address = g_state.addresses[i];
            if (!address.valid || !address.favorite)
            {
                continue;
            }
            char dest[kHashTextLen] = {};
            char target[kAddressTextLen] = {};
            format_hash_hex(address.destination_hash, dest, sizeof(dest));
            std::snprintf(target, sizeof(target), "%s:/page/index.mu", dest);
            add_terminal_link(target,
                              address.display_name[0] != '\0' ? address.display_name
                                                              : dest);
            ++visible;
        }
        if (visible == 0)
        {
            add_terminal_label("no favourites", kTerminalDim, caption_font());
        }
        return;
    }

    for (std::size_t i = 0; i < g_state.announce_count && i < 16; ++i)
    {
        const auto& announce = g_state.announces[i];
        if (!announce.valid)
        {
            continue;
        }
        char dest[kHashTextLen] = {};
        char target[kAddressTextLen] = {};
        format_hash_hex(announce.destination_hash, dest, sizeof(dest));
        std::snprintf(target, sizeof(target), "%s:/page/index.mu", dest);
        add_terminal_link(target,
                          announce.display_name[0] != '\0' ? announce.display_name
                                                           : dest);
    }
    if (g_state.announce_count == 0)
    {
        add_terminal_label("no announces", kTerminalDim, caption_font());
    }
}

void render_node_page(const rtdir::AnnounceRecord* announce,
                      const rtdir::LxmfAddressRecord* address_record,
                      const char* destination_text)
{
    clear_viewport();
    const char* title = nullptr;
    if (address_record && address_record->display_name[0] != '\0')
    {
        title = address_record->display_name;
    }
    else if (announce && announce->display_name[0] != '\0')
    {
        title = announce->display_name;
    }
    else
    {
        title = destination_text;
    }

    add_terminal_label(title ? title : "Nomad Node",
                       kTerminalAmber,
                       body_font(),
                       LV_LABEL_LONG_WRAP);
    add_terminal_spacer();

    if (destination_text)
    {
        add_terminal_pair("dest", destination_text);
    }
    if (announce)
    {
        char identity[kHashTextLen] = {};
        char hops[32] = {};
        format_hash_hex(announce->identity_hash, identity, sizeof(identity));
        std::snprintf(hops,
                      sizeof(hops),
                      "%u  %s",
                      static_cast<unsigned>(announce->hops),
                      aspect_label(announce->aspect));
        add_terminal_pair("identity", identity);
        add_terminal_pair("hops", hops);
    }
    if (address_record)
    {
        char identity[kHashTextLen] = {};
        format_hash_hex(address_record->identity_hash, identity, sizeof(identity));
        add_terminal_pair("identity", identity);
        if (address_record->favorite)
        {
            add_terminal_pair("state", "favourite");
        }
    }

    add_terminal_spacer(6);
    add_terminal_link("/page/index.mu", "/page/index.mu");
    add_terminal_link("/page/about.mu", "/page/about.mu");
    add_terminal_link("/page/peers.mu", "/page/peers.mu");
}

void render_remote_page_shell(const char* address)
{
    clear_viewport();
    add_terminal_label("Nomad Page", kTerminalAmber, body_font(), LV_LABEL_LONG_WRAP);
    add_terminal_spacer();
    add_terminal_pair("address", address ? address : "");
    add_terminal_label("no cached page body", kTerminalDim, caption_font());
}

void render_current_page()
{
    if (!g_state.viewport)
    {
        return;
    }

    const char* address = g_state.current_address;
    if (!address || address[0] == '\0' || std::strcmp(address, "home:/") == 0)
    {
        render_home_page();
        return;
    }
    if (std::strcmp(address, "home:/announces") == 0)
    {
        render_collection_page(false);
        return;
    }
    if (std::strcmp(address, "home:/favourites") == 0)
    {
        render_collection_page(true);
        return;
    }

    char destination[kHashTextLen] = {};
    if (extract_destination_text(address, destination, sizeof(destination)))
    {
        const auto* announce = find_announce_by_destination_text(destination);
        const auto* known_address = find_address_by_destination_text(destination);
        if (announce || known_address)
        {
            render_node_page(announce, known_address, destination);
            return;
        }
    }

    render_remote_page_shell(address);
}

void refresh_directory_data()
{
    if (g_state.announces.capacity() < kMaxVisibleAnnounces)
    {
        g_state.announces.reserve(kMaxVisibleAnnounces);
    }
    if (g_state.addresses.capacity() < kMaxVisibleAddresses)
    {
        g_state.addresses.reserve(kMaxVisibleAddresses);
    }

    std::size_t count = 0;
    g_state.announces.resize(kMaxVisibleAnnounces);
    g_state.announce_status =
        rtdir::load_announces(g_state.announces.data(), g_state.announces.size(), &count);
    g_state.announce_count = count <= g_state.announces.size() ? count : g_state.announces.size();
    g_state.announces.resize(g_state.announce_count);

    count = 0;
    g_state.addresses.resize(kMaxVisibleAddresses);
    g_state.address_status =
        rtdir::load_lxmf_addresses(g_state.addresses.data(), g_state.addresses.size(), &count);
    g_state.address_count = count <= g_state.addresses.size() ? count : g_state.addresses.size();
    g_state.addresses.resize(g_state.address_count);
}

void update_tab_state()
{
    if (!g_state.favourites_tab || !g_state.announces_tab)
    {
        return;
    }
    if (g_state.directory_mode == DirectoryMode::Favourites)
    {
        lv_obj_add_state(g_state.favourites_tab, LV_STATE_CHECKED);
        lv_obj_clear_state(g_state.announces_tab, LV_STATE_CHECKED);
    }
    else
    {
        lv_obj_clear_state(g_state.favourites_tab, LV_STATE_CHECKED);
        lv_obj_add_state(g_state.announces_tab, LV_STATE_CHECKED);
    }
}

void set_directory_mode(DirectoryMode mode)
{
    if (g_state.directory_mode == mode)
    {
        return;
    }
    g_state.directory_mode = mode;
    update_tab_state();
    render_directory_list();
}

void directory_tab_event_cb(lv_event_t* event)
{
    if (event && lv_event_get_code(event) == LV_EVENT_KEY &&
        lv_event_get_key(event) == LV_KEY_RIGHT)
    {
        focus_browser_viewport();
        lv_event_stop_processing(event);
        return;
    }
    if (!activation_event(event))
    {
        return;
    }
    const auto raw = reinterpret_cast<uintptr_t>(lv_event_get_user_data(event));
    set_directory_mode(raw == 0 ? DirectoryMode::Favourites : DirectoryMode::Announces);
    lv_event_stop_processing(event);
}

void open_directory_row(const DirectoryRowContext& context)
{
    char destination[kHashTextLen] = {};
    if (context.kind == DirectoryRowKind::Favourite)
    {
        if (context.index >= g_state.address_count)
        {
            return;
        }
        format_hash_hex(g_state.addresses[context.index].destination_hash,
                        destination,
                        sizeof(destination));
    }
    else
    {
        if (context.index >= g_state.announce_count)
        {
            return;
        }
        format_hash_hex(g_state.announces[context.index].destination_hash,
                        destination,
                        sizeof(destination));
    }

    char address[kAddressTextLen] = {};
    std::snprintf(address, sizeof(address), "%s:/page/index.mu", destination);
    navigate_to_address(address, true);
}

void directory_row_event_cb(lv_event_t* event)
{
    if (!event)
    {
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_KEY &&
        lv_event_get_key(event) == LV_KEY_RIGHT)
    {
        focus_browser_viewport();
        lv_event_stop_processing(event);
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_FOCUSED)
    {
        lv_obj_t* target = lv_event_get_target_obj(event);
        if (target && lv_obj_is_valid(target))
        {
            lv_obj_scroll_to_view(target, LV_ANIM_ON);
        }
        return;
    }
    if (!activation_event(event))
    {
        return;
    }
    auto* context = static_cast<DirectoryRowContext*>(lv_event_get_user_data(event));
    if (context)
    {
        open_directory_row(*context);
    }
    lv_event_stop_processing(event);
}

lv_obj_t* create_directory_row(const char* title,
                               const char* meta,
                               const char* icon,
                               DirectoryRowKind kind,
                               std::size_t index)
{
    if (!g_state.directory_list || g_state.row_context_count >= g_state.row_contexts.size())
    {
        return nullptr;
    }

    DirectoryRowContext& context = g_state.row_contexts[g_state.row_context_count++];
    context.kind = kind;
    context.index = index;

    lv_obj_t* row = lv_btn_create(g_state.directory_list);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(row, lv_color_hex(kPanelBg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(row, lv_color_hex(kAmber), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(row, lv_color_hex(kAmber), LV_STATE_FOCUS_KEY);
    lv_obj_set_style_border_width(row, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(row, lv_color_hex(kLine), LV_PART_MAIN);
    lv_obj_set_style_radius(row, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, ::ui::page_profile::current().dense ? 4 : 6, LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, 5, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    style_focusable(row);
    lv_obj_add_event_cb(row, directory_row_event_cb, LV_EVENT_CLICKED, &context);
    lv_obj_add_event_cb(row, directory_row_event_cb, LV_EVENT_KEY, &context);
    lv_obj_add_event_cb(row, directory_row_event_cb, LV_EVENT_FOCUSED, &context);
    lv_obj_add_event_cb(row, page_shortcut_event_cb, LV_EVENT_KEY, nullptr);
    add_to_group(row);

    lv_obj_t* icon_label = lv_label_create(row);
    set_label(icon_label, icon ? icon : "*", kTextDim, caption_font());
    lv_obj_set_width(icon_label, 16);
    lv_label_set_long_mode(icon_label, LV_LABEL_LONG_CLIP);

    lv_obj_t* texts = lv_obj_create(row);
    lv_obj_set_width(texts, 0);
    lv_obj_set_height(texts, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(texts, 1);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(texts, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(texts, 1, LV_PART_MAIN);
    style_plain_container(texts, kPanelBg, LV_OPA_TRANSP);
    ::ui::components::two_pane_layout::make_non_scrollable(texts);

    lv_obj_t* title_label = lv_label_create(texts);
    set_label(title_label, title ? title : "--", kText, caption_font());
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title_label, LV_PCT(100));

    lv_obj_t* meta_label = lv_label_create(texts);
    set_label(meta_label, meta ? meta : "", kTextDim, tiny_font());
    lv_label_set_long_mode(meta_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(meta_label, LV_PCT(100));
    return row;
}

void render_empty_directory(const char* message)
{
    lv_obj_t* label = lv_label_create(g_state.directory_list);
    set_label(label, safe_tr(message), kTextDim, caption_font());
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_PCT(100));
    lv_obj_set_style_pad_all(label, 6, LV_PART_MAIN);
}

void render_directory_list()
{
    if (!g_state.directory_list)
    {
        return;
    }
    lv_obj_clean(g_state.directory_list);
    g_state.row_context_count = 0;
    update_tab_state();

    std::size_t visible = 0;
    if (g_state.directory_mode == DirectoryMode::Favourites)
    {
        for (std::size_t i = 0; i < g_state.address_count; ++i)
        {
            const auto& address = g_state.addresses[i];
            if (!address.valid || !address.favorite || !address_matches_search(address))
            {
                continue;
            }
            char destination[10] = {};
            format_hash_prefix(address.destination_hash, destination, sizeof(destination));
            char meta[64] = {};
            std::snprintf(meta, sizeof(meta), "%s  LXMF", destination);
            create_directory_row(address.display_name[0] != '\0' ? address.display_name
                                                                 : destination,
                                 meta,
                                 LV_SYMBOL_STAR,
                                 DirectoryRowKind::Favourite,
                                 i);
            ++visible;
        }
    }
    else
    {
        for (std::size_t i = 0; i < g_state.announce_count; ++i)
        {
            const auto& announce = g_state.announces[i];
            if (!announce.valid || !announce_matches_search(announce))
            {
                continue;
            }
            char destination[10] = {};
            format_hash_prefix(announce.destination_hash, destination, sizeof(destination));
            char meta[72] = {};
            std::snprintf(meta,
                          sizeof(meta),
                          "%s  %s  %uh",
                          destination,
                          aspect_label(announce.aspect),
                          static_cast<unsigned>(announce.hops));
            create_directory_row(announce.display_name[0] != '\0' ? announce.display_name
                                                                  : destination,
                                 meta,
                                 "A",
                                 DirectoryRowKind::Announce,
                                 i);
            ++visible;
        }
    }

    if (visible == 0)
    {
        render_empty_directory(g_state.search_query[0] != '\0' ? "No matches"
                                                               : (g_state.directory_mode ==
                                                                          DirectoryMode::Favourites
                                                                      ? "No favourites"
                                                                      : "No announces"));
    }
    rebuild_focus_group(nullptr);
}

void refresh_all()
{
    refresh_directory_data();
    render_directory_list();
    render_current_page();
    rebuild_focus_group(nullptr);
}

void browser_back_event_cb(lv_event_t* event)
{
    if (!activation_event(event))
    {
        return;
    }
    if (g_state.history_count > 1 && g_state.history_pos > 0)
    {
        --g_state.history_pos;
        g_state.suppress_history = true;
        navigate_to_address(g_state.history[g_state.history_pos].address, false);
        g_state.suppress_history = false;
    }
    lv_event_stop_processing(event);
}

void home_event_cb(lv_event_t* event)
{
    if (!activation_event(event))
    {
        return;
    }
    navigate_to_address("home:/", true);
    lv_event_stop_processing(event);
}

void refresh_event_cb(lv_event_t* event)
{
    if (!activation_event(event))
    {
        return;
    }
    refresh_all();
    ::ui::feedback::show_notice(safe_tr("Network refreshed"), 1200);
    lv_event_stop_processing(event);
}

void go_from_address_bar()
{
    const char* text = g_state.address_area ? lv_textarea_get_text(g_state.address_area) : nullptr;
    navigate_to_address(text && text[0] != '\0' ? text : "home:/", true);
}

void go_event_cb(lv_event_t* event)
{
    if (!activation_event(event))
    {
        return;
    }
    go_from_address_bar();
    lv_event_stop_processing(event);
}

void address_key_event_cb(lv_event_t* event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_KEY)
    {
        return;
    }
    const uint32_t key = lv_event_get_key(event);
    if (key == LV_KEY_ENTER || key == LV_KEY_RIGHT)
    {
        go_from_address_bar();
        lv_event_stop_processing(event);
    }
    else if (key == LV_KEY_ESC && app_g)
    {
        lv_group_focus_obj(g_state.viewport ? g_state.viewport : g_state.root);
        lv_event_stop_processing(event);
    }
}

void on_search_apply(const char* text, void* /*user_data*/)
{
    copy_text(g_state.search_query, sizeof(g_state.search_query), text ? text : "");
    render_directory_list();
}

void on_search_clear(void* /*user_data*/)
{
    g_state.search_query[0] = '\0';
    render_directory_list();
}

void on_search_cancel(void* /*user_data*/)
{
}

void open_search_modal()
{
    if (::ui::components::floating_search_box::is_open(g_state.search_box))
    {
        ::ui::components::floating_search_box::focus(g_state.search_box);
        return;
    }
    if (::ui::components::shortcut_help_modal::is_open(g_state.help_modal))
    {
        return;
    }

    ::ui::components::floating_search_box::Config config{};
    config.title = "Search";
    config.initial_text = g_state.search_query;
    config.max_length = static_cast<uint16_t>(sizeof(g_state.search_query) - 1U);
    config.height = ::ui::page_profile::current().dense ? 122 : 150;
    config.restore_group = app_g;
    config.callbacks.apply = on_search_apply;
    config.callbacks.clear = on_search_clear;
    config.callbacks.cancel = on_search_cancel;
    (void)::ui::components::floating_search_box::open(
        g_state.search_box,
        g_state.root ? g_state.root : lv_scr_act(),
        config);
}

void search_button_event_cb(lv_event_t* event)
{
    if (event && lv_event_get_code(event) == LV_EVENT_KEY &&
        lv_event_get_key(event) == LV_KEY_RIGHT)
    {
        focus_browser_viewport();
        lv_event_stop_processing(event);
        return;
    }
    if (!activation_event(event))
    {
        return;
    }
    open_search_modal();
    lv_event_stop_processing(event);
}

void apply_layout_state()
{
    const bool immersive = g_state.immersive;
    const bool hide_directory = immersive || g_state.directory_collapsed;
    const bool hide_browser = !immersive && g_state.browser_collapsed;
    if (g_state.header)
    {
        immersive ? lv_obj_add_flag(g_state.header, LV_OBJ_FLAG_HIDDEN)
                  : lv_obj_clear_flag(g_state.header, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_state.directory_panel)
    {
        hide_directory ? lv_obj_add_flag(g_state.directory_panel, LV_OBJ_FLAG_HIDDEN)
                       : lv_obj_clear_flag(g_state.directory_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(g_state.directory_panel,
                         hide_browser ? LV_PCT(100) : resolve_directory_width());
        lv_obj_set_flex_grow(g_state.directory_panel, hide_browser ? 1 : 0);
    }
    if (g_state.browser_panel)
    {
        hide_browser ? lv_obj_add_flag(g_state.browser_panel, LV_OBJ_FLAG_HIDDEN)
                     : lv_obj_clear_flag(g_state.browser_panel, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_state.browser_toolbar)
    {
        immersive ? lv_obj_add_flag(g_state.browser_toolbar, LV_OBJ_FLAG_HIDDEN)
                  : lv_obj_clear_flag(g_state.browser_toolbar, LV_OBJ_FLAG_HIDDEN);
    }
    if (g_state.content)
    {
        lv_obj_set_style_pad_left(g_state.content, immersive ? 0 : 2, LV_PART_MAIN);
        lv_obj_set_style_pad_right(g_state.content, immersive ? 0 : 2, LV_PART_MAIN);
        lv_obj_set_style_pad_top(g_state.content, immersive ? 0 : 2, LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(g_state.content, immersive ? 0 : 2, LV_PART_MAIN);
        lv_obj_set_style_pad_column(g_state.content,
                                    (hide_directory || hide_browser) ? 0 : 4,
                                    LV_PART_MAIN);
    }
    if (g_state.browser_panel)
    {
        lv_obj_set_style_pad_all(g_state.browser_panel, immersive ? 0 : 2, LV_PART_MAIN);
        lv_obj_set_style_radius(g_state.browser_panel, immersive ? 0 : 8, LV_PART_MAIN);
        lv_obj_set_style_border_width(g_state.browser_panel, immersive ? 0 : 1, LV_PART_MAIN);
    }
    if (g_state.viewport)
    {
        lv_obj_set_style_radius(g_state.viewport, immersive ? 0 : 8, LV_PART_MAIN);
        lv_obj_set_style_border_width(g_state.viewport, immersive ? 0 : 1, LV_PART_MAIN);
        if (app_g && !hide_browser)
        {
            lv_group_focus_obj(g_state.viewport);
        }
    }
    rebuild_focus_group(hide_browser ? g_state.directory_list : g_state.viewport);
}

void toggle_immersive()
{
    g_state.immersive = !g_state.immersive;
    apply_layout_state();
}

void toggle_directory_panel()
{
    if (g_state.immersive)
    {
        g_state.immersive = false;
    }
    const bool hide_directory = !g_state.directory_collapsed;
    g_state.directory_collapsed = hide_directory;
    if (hide_directory)
    {
        g_state.browser_collapsed = false;
    }
    apply_layout_state();
    ::ui::feedback::show_notice(g_state.directory_collapsed ? safe_tr("Directory hidden")
                                                            : safe_tr("Directory shown"),
                                1200);
    if (g_state.directory_collapsed)
    {
        focus_browser_viewport();
    }
    else
    {
        focus_directory_panel();
    }
}

void toggle_browser_panel()
{
    if (g_state.immersive)
    {
        g_state.immersive = false;
    }
    const bool hide_browser = !g_state.browser_collapsed;
    g_state.browser_collapsed = hide_browser;
    if (hide_browser)
    {
        g_state.directory_collapsed = false;
    }
    apply_layout_state();
    ::ui::feedback::show_notice(g_state.browser_collapsed ? safe_tr("Browser hidden")
                                                          : safe_tr("Browser shown"),
                                1200);
    if (g_state.browser_collapsed)
    {
        focus_directory_panel();
    }
    else
    {
        focus_browser_viewport();
    }
}

bool is_help_shortcut_key(uint32_t key)
{
    return key == 'h' || key == 'H';
}

bool is_directory_toggle_shortcut_key(uint32_t key)
{
    return key == 'c' || key == 'C';
}

bool is_browser_toggle_shortcut_key(uint32_t key)
{
    return key == 'b' || key == 'B';
}

void close_network_help_modal()
{
    ::ui::components::shortcut_help_modal::close(g_state.help_modal);
}

void open_network_help_modal()
{
    if (::ui::components::shortcut_help_modal::is_open(g_state.help_modal))
    {
        close_network_help_modal();
        return;
    }
    if (::ui::components::floating_search_box::is_open(g_state.search_box))
    {
        return;
    }

    ::ui::components::shortcut_help_modal::Row rows[11] = {};
    std::size_t row_count = 0;
    rows[row_count++] = {"S", "/", "Search announces"};
    rows[row_count++] = {"C", nullptr, "Show or hide left column"};
    rows[row_count++] = {"B", nullptr, "Show or hide browser"};
    rows[row_count++] = {"I", nullptr, "Immersive browser"};
    rows[row_count++] = {"Left", "Right", "Switch pane focus"};
    rows[row_count++] = {"Enter", nullptr, "Open selected item"};
    rows[row_count++] = {"R", nullptr, "Refresh network"};
    rows[row_count++] = {"F", nullptr, "Favourites"};
    rows[row_count++] = {"A", nullptr, "Announces"};
    rows[row_count++] = {"Back", nullptr, "Return or exit immersive"};
    rows[row_count++] = {"H", nullptr, "Close help"};

    ::ui::components::shortcut_help_modal::Config config{};
    config.title = "Network Help";
    config.rows = rows;
    config.row_count = row_count;
    config.width = 304;
    config.height = 176;
    config.restore_group = app_g;
    (void)::ui::components::shortcut_help_modal::open(
        g_state.help_modal,
        g_state.root ? g_state.root : lv_screen_active(),
        config);
}

void page_shortcut_event_cb(lv_event_t* event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_KEY)
    {
        return;
    }
    const uint32_t key = lv_event_get_key(event);
    lv_obj_t* target = lv_event_get_target_obj(event);
    if (is_help_shortcut_key(key))
    {
        open_network_help_modal();
        lv_event_stop_processing(event);
        return;
    }
    if (is_directory_toggle_shortcut_key(key))
    {
        toggle_directory_panel();
        lv_event_stop_processing(event);
        return;
    }
    if (is_browser_toggle_shortcut_key(key))
    {
        toggle_browser_panel();
        lv_event_stop_processing(event);
        return;
    }
    if (key == LV_KEY_RIGHT && object_in_subtree(g_state.directory_panel, target))
    {
        if (g_state.browser_collapsed)
        {
            g_state.browser_collapsed = false;
            apply_layout_state();
        }
        focus_browser_viewport();
        lv_event_stop_processing(event);
        return;
    }
    if (key == LV_KEY_LEFT && object_in_subtree(g_state.browser_panel, target))
    {
        if (g_state.directory_collapsed)
        {
            g_state.directory_collapsed = false;
            apply_layout_state();
        }
        focus_directory_panel();
        lv_event_stop_processing(event);
        return;
    }
    if (key == 'i' || key == 'I')
    {
        toggle_immersive();
        lv_event_stop_processing(event);
        return;
    }
    if (g_state.immersive && (key == LV_KEY_ESC || key == LV_KEY_BACKSPACE))
    {
        g_state.immersive = false;
        apply_layout_state();
        lv_event_stop_processing(event);
        return;
    }
    if (key == '/' || key == 's' || key == 'S')
    {
        open_search_modal();
        lv_event_stop_processing(event);
        return;
    }
    if (key == 'r' || key == 'R')
    {
        refresh_all();
        ::ui::feedback::show_notice(safe_tr("Network refreshed"), 1200);
        lv_event_stop_processing(event);
        return;
    }
    if (key == 'f' || key == 'F')
    {
        set_directory_mode(DirectoryMode::Favourites);
        lv_event_stop_processing(event);
        return;
    }
    if (key == 'a' || key == 'A')
    {
        set_directory_mode(DirectoryMode::Announces);
        lv_event_stop_processing(event);
        return;
    }
}

void request_exit()
{
    ui_request_exit_to_menu();
}

void top_bar_back_requested(void* /*user_data*/)
{
    if (g_state.immersive)
    {
        g_state.immersive = false;
        apply_layout_state();
        return;
    }
    request_exit();
}

void back_event_cb(lv_event_t* event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_KEY)
    {
        const uint32_t key = lv_event_get_key(event);
        if (key != LV_KEY_ESC && key != LV_KEY_BACKSPACE)
        {
            return;
        }
    }
    else if (code != LV_EVENT_CLICKED)
    {
        return;
    }
    top_bar_back_requested(nullptr);
    lv_event_stop_processing(event);
}

lv_obj_t* create_icon_button(lv_obj_t* parent,
                             const char* text,
                             lv_event_cb_t cb,
                             lv_coord_t size)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, size, size);
    style_chrome_button(btn);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_KEY, nullptr);
    lv_obj_add_event_cb(btn, page_shortcut_event_cb, LV_EVENT_KEY, nullptr);
    lv_obj_t* label = lv_label_create(btn);
    set_label(label, text, kText, caption_font());
    lv_obj_center(label);
    add_to_group(btn);
    return btn;
}

lv_obj_t* create_tab_button(lv_obj_t* parent,
                            const char* text,
                            DirectoryMode mode,
                            lv_coord_t height)
{
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, LV_PCT(50), height);
    style_chrome_button(btn, false);
    lv_obj_set_style_bg_color(btn, lv_color_hex(kAmber), LV_STATE_CHECKED);
    lv_obj_set_style_pad_left(btn, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_right(btn, 3, LV_PART_MAIN);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_add_event_cb(btn,
                        directory_tab_event_cb,
                        LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(mode == DirectoryMode::Favourites ? 0U : 1U));
    lv_obj_add_event_cb(btn,
                        directory_tab_event_cb,
                        LV_EVENT_KEY,
                        reinterpret_cast<void*>(mode == DirectoryMode::Favourites ? 0U : 1U));
    lv_obj_add_event_cb(btn, page_shortcut_event_cb, LV_EVENT_KEY, nullptr);
    add_to_group(btn);

    lv_obj_t* label = lv_label_create(btn);
    set_label(label, safe_tr(text), kText, tiny_font());
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_center(label);
    return btn;
}

lv_coord_t resolve_directory_width()
{
    lv_coord_t width = lv_display_get_physical_horizontal_resolution(nullptr);
    if (width <= 0)
    {
        width = lv_obj_get_width(lv_scr_act());
    }
    if (width <= 340)
    {
        return 96;
    }
    if (width <= 420)
    {
        return 118;
    }
    return 142;
}

void create_directory_panel(lv_obj_t* parent)
{
    const auto& profile = ::ui::page_profile::current();
    ::ui::components::two_pane_layout::SidePanelSpec side_spec;
    side_spec.width = resolve_directory_width();
    side_spec.pad_row = profile.dense ? 3 : 5;
    side_spec.scrollbar_mode = LV_SCROLLBAR_MODE_OFF;
    g_state.directory_panel =
        ::ui::components::two_pane_layout::create_side_panel(parent, side_spec);
    style_panel(g_state.directory_panel, kWarmBg);
    lv_obj_set_style_pad_all(g_state.directory_panel, profile.dense ? 3 : 5, LV_PART_MAIN);
    lv_obj_set_style_pad_row(g_state.directory_panel, profile.dense ? 3 : 5, LV_PART_MAIN);

    g_state.directory_tabs = lv_obj_create(g_state.directory_panel);
    lv_obj_set_size(g_state.directory_tabs, LV_PCT(100), profile.dense ? 22 : 26);
    style_plain_container(g_state.directory_tabs, kWarmBg, LV_OPA_TRANSP);
    lv_obj_set_flex_flow(g_state.directory_tabs, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(g_state.directory_tabs, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(g_state.directory_tabs, 3, LV_PART_MAIN);
    ::ui::components::two_pane_layout::make_non_scrollable(g_state.directory_tabs);

    g_state.favourites_tab = create_tab_button(g_state.directory_tabs,
                                               "Fav",
                                               DirectoryMode::Favourites,
                                               profile.dense ? 22 : 26);
    g_state.announces_tab = create_tab_button(g_state.directory_tabs,
                                              "Ann",
                                              DirectoryMode::Announces,
                                              profile.dense ? 22 : 26);

    g_state.directory_search_btn = lv_btn_create(g_state.directory_panel);
    lv_obj_set_size(g_state.directory_search_btn, LV_PCT(100), profile.dense ? 22 : 26);
    style_chrome_button(g_state.directory_search_btn, false);
    lv_obj_add_event_cb(g_state.directory_search_btn,
                        search_button_event_cb,
                        LV_EVENT_CLICKED,
                        nullptr);
    lv_obj_add_event_cb(g_state.directory_search_btn,
                        search_button_event_cb,
                        LV_EVENT_KEY,
                        nullptr);
    lv_obj_add_event_cb(g_state.directory_search_btn,
                        page_shortcut_event_cb,
                        LV_EVENT_KEY,
                        nullptr);
    add_to_group(g_state.directory_search_btn);

    lv_obj_t* search_label = lv_label_create(g_state.directory_search_btn);
    set_label(search_label, LV_SYMBOL_SEARCH, kText, caption_font());
    lv_obj_center(search_label);

    g_state.directory_list = lv_obj_create(g_state.directory_panel);
    lv_obj_set_width(g_state.directory_list, LV_PCT(100));
    lv_obj_set_height(g_state.directory_list, 0);
    lv_obj_set_flex_grow(g_state.directory_list, 1);
    style_plain_container(g_state.directory_list, kWarmBg, LV_OPA_TRANSP);
    lv_obj_set_style_pad_all(g_state.directory_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(g_state.directory_list, profile.dense ? 3 : 5, LV_PART_MAIN);
    lv_obj_set_flex_flow(g_state.directory_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(g_state.directory_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_state.directory_list, LV_SCROLLBAR_MODE_AUTO);
}

void create_browser_panel(lv_obj_t* parent)
{
    const auto& profile = ::ui::page_profile::current();
    ::ui::components::two_pane_layout::MainPanelSpec main_spec;
    main_spec.pad_all = profile.dense ? 2 : 3;
    main_spec.pad_row = profile.dense ? 3 : 5;
    main_spec.scrollbar_mode = LV_SCROLLBAR_MODE_OFF;
    g_state.browser_panel =
        ::ui::components::two_pane_layout::create_main_panel(parent, main_spec);
    style_panel(g_state.browser_panel, kPanelBg);

    g_state.browser_toolbar = lv_obj_create(g_state.browser_panel);
    lv_obj_set_width(g_state.browser_toolbar, LV_PCT(100));
    lv_obj_set_height(g_state.browser_toolbar, profile.dense ? 24 : 28);
    style_plain_container(g_state.browser_toolbar, kPanelBg, LV_OPA_TRANSP);
    lv_obj_set_style_pad_all(g_state.browser_toolbar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(g_state.browser_toolbar, profile.dense ? 2 : 4, LV_PART_MAIN);
    lv_obj_set_flex_flow(g_state.browser_toolbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_state.browser_toolbar,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    ::ui::components::two_pane_layout::make_non_scrollable(g_state.browser_toolbar);

    const lv_coord_t button_size = profile.dense ? 22 : 26;
    g_state.browser_back_btn =
        create_icon_button(g_state.browser_toolbar, LV_SYMBOL_LEFT, browser_back_event_cb, button_size);
    g_state.home_btn =
        create_icon_button(g_state.browser_toolbar, LV_SYMBOL_HOME, home_event_cb, button_size);
    g_state.refresh_btn =
        create_icon_button(g_state.browser_toolbar, LV_SYMBOL_REFRESH, refresh_event_cb, button_size);

    g_state.address_area = lv_textarea_create(g_state.browser_toolbar);
    lv_textarea_set_one_line(g_state.address_area, true);
    lv_textarea_set_max_length(g_state.address_area,
                               static_cast<uint16_t>(sizeof(g_state.current_address) - 1U));
    lv_textarea_set_text(g_state.address_area, g_state.current_address);
    lv_obj_set_height(g_state.address_area, button_size);
    lv_obj_set_width(g_state.address_area, 0);
    lv_obj_set_flex_grow(g_state.address_area, 1);
    lv_obj_set_style_bg_color(g_state.address_area, lv_color_hex(0xFFF8E8), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_state.address_area, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_state.address_area, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_state.address_area, lv_color_hex(kLine), LV_PART_MAIN);
    lv_obj_set_style_radius(g_state.address_area, 8, LV_PART_MAIN);
    lv_obj_set_style_text_color(g_state.address_area, lv_color_hex(kText), LV_PART_MAIN);
    lv_obj_set_style_text_font(g_state.address_area, tiny_font(), LV_PART_MAIN);
    lv_obj_set_style_pad_left(g_state.address_area, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_right(g_state.address_area, 6, LV_PART_MAIN);
    lv_obj_set_style_pad_top(g_state.address_area, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(g_state.address_area, 3, LV_PART_MAIN);
    style_focusable(g_state.address_area);
    lv_obj_add_event_cb(g_state.address_area, address_key_event_cb, LV_EVENT_KEY, nullptr);
    add_to_group(g_state.address_area);

    g_state.go_btn =
        create_icon_button(g_state.browser_toolbar, LV_SYMBOL_RIGHT, go_event_cb, button_size);

    g_state.viewport = lv_obj_create(g_state.browser_panel);
    lv_obj_set_width(g_state.viewport, LV_PCT(100));
    lv_obj_set_height(g_state.viewport, 0);
    lv_obj_set_flex_grow(g_state.viewport, 1);
    lv_obj_set_style_bg_color(g_state.viewport, lv_color_hex(kTerminalBg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_state.viewport, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(g_state.viewport, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(g_state.viewport, lv_color_hex(kLine), LV_PART_MAIN);
    lv_obj_set_style_radius(g_state.viewport, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(g_state.viewport, profile.dense ? 5 : 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(g_state.viewport, profile.dense ? 2 : 3, LV_PART_MAIN);
    lv_obj_set_flex_flow(g_state.viewport, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(g_state.viewport, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(g_state.viewport, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(g_state.viewport, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g_state.viewport, page_shortcut_event_cb, LV_EVENT_KEY, nullptr);
    add_to_group(g_state.viewport);
}

} // namespace

namespace network::ui::shell
{

void enter(void* /*user_data*/, lv_obj_t* parent)
{
    if (!parent || (g_state.root && lv_obj_is_valid(g_state.root)))
    {
        return;
    }

    const auto& profile = ::ui::page_profile::current();
    ::ui::components::two_pane_layout::RootSpec root_spec;
    root_spec.pad_row = profile.dense ? 1 : profile.top_content_gap;
    g_state.root = ::ui::components::two_pane_layout::create_root(parent, root_spec);
    lv_obj_set_style_bg_color(g_state.root, lv_color_hex(kWarmBg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_state.root, LV_OPA_COVER, LV_PART_MAIN);

    ::ui::components::two_pane_layout::HeaderSpec header_spec;
    header_spec.height = profile.top_bar_height;
    header_spec.bg_hex = kWarmBg;
    header_spec.pad_all = 0;
    g_state.header =
        ::ui::components::two_pane_layout::create_header_container(g_state.root,
                                                                   header_spec);

    ::ui::widgets::TopBarConfig top_bar_config;
    top_bar_config.height = profile.top_bar_height;
    ::ui::widgets::top_bar_init(g_state.top_bar, g_state.header, top_bar_config);
    ::ui::widgets::top_bar_set_title(g_state.top_bar, safe_tr("Network"));
    ::ui::widgets::top_bar_set_back_callback(g_state.top_bar,
                                             top_bar_back_requested,
                                             nullptr);
    ui_update_top_bar_battery(g_state.top_bar);

    ::ui::components::two_pane_layout::ContentSpec content_spec;
    content_spec.pad_left = 2;
    content_spec.pad_right = 2;
    content_spec.pad_top = 2;
    content_spec.pad_bottom = 2;
    g_state.content =
        ::ui::components::two_pane_layout::create_content_row(g_state.root,
                                                              content_spec);
    lv_obj_set_style_pad_column(g_state.content, 4, LV_PART_MAIN);

    create_directory_panel(g_state.content);
    create_browser_panel(g_state.content);

    lv_obj_add_event_cb(g_state.root, back_event_cb, LV_EVENT_KEY, nullptr);
    lv_obj_add_event_cb(g_state.root, page_shortcut_event_cb, LV_EVENT_KEY, nullptr);
    if (g_state.top_bar.back_btn)
    {
        lv_obj_add_event_cb(g_state.top_bar.back_btn, back_event_cb, LV_EVENT_KEY, nullptr);
        lv_obj_add_event_cb(g_state.top_bar.back_btn, page_shortcut_event_cb, LV_EVENT_KEY, nullptr);
        add_to_group(g_state.top_bar.back_btn);
    }
    if (app_g)
    {
        set_default_group(app_g);
    }

    push_history(g_state.current_address);
    refresh_all();
    update_tab_state();
    if (app_g && g_state.directory_list)
    {
        lv_group_focus_obj(g_state.viewport ? g_state.viewport : g_state.address_area);
    }
}

void exit(void* /*user_data*/, lv_obj_t* /*parent*/)
{
    ::ui::components::floating_search_box::close(g_state.search_box);
    close_network_help_modal();
    if (g_state.root && lv_obj_is_valid(g_state.root))
    {
        lv_obj_del(g_state.root);
    }
    g_state = NetworkPageState{};
}

} // namespace network::ui::shell
