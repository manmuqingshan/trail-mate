#include "ui/screens/network/network_page_shell.h"

#include "app/app_config.h"
#include "app/app_facade_access.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "platform/ui/reticulum_directory_runtime.h"
#include "platform/ui/reticulum_page_runtime.h"
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
#include <cstdlib>
#include <cstring>
#include <vector>

#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#endif

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
#ifndef LV_SYMBOL_LIST
#define LV_SYMBOL_LIST "D"
#endif

namespace
{

namespace rtdir = ::platform::ui::reticulum_directory;
namespace rtpage = ::platform::ui::reticulum_page;

constexpr std::size_t kMaxVisibleAnnounces = 100;
constexpr std::size_t kMaxVisibleAddresses = 100;
constexpr std::size_t kMaxDirectoryRows = 100;
constexpr std::size_t kInitialPageLinks = 8;
constexpr std::size_t kMaxPageLinks = 32;
constexpr std::size_t kMaxHistoryEntries = 8;
constexpr std::size_t kAddressTextLen = 160;
constexpr std::size_t kSearchTextLen = 32;
constexpr std::size_t kHashTextLen = (rtdir::kReticulumHashSize * 2U) + 1U;
constexpr std::size_t kMicronTextChunkBytes = 112;

enum class DirectoryMode : uint8_t
{
    Favourites,
    Announces,
};

enum class DirectoryRowKind : uint8_t
{
    Favourite,
    Announce,
    BuiltInFavourite,
};

enum class MicronAlign : uint8_t
{
    Left,
    Center,
    Right,
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

struct RemotePageAddress
{
    bool valid = false;
    char destination[kHashTextLen] = {};
    char path[rtpage::kReticulumPagePathSize] = {};
};

struct BuiltInFavourite
{
    const char* address = nullptr;
    const char* title = nullptr;
    const char* meta = nullptr;
    const char* icon = nullptr;
};

struct MicronStyle
{
    uint32_t fg = 0xDDDDDD;
    uint32_t bg = 0x050505;
    bool bold = false;
    bool underline = false;
    bool italic = false;
};

struct MicronRenderState
{
    MicronStyle style{};
    uint32_t default_fg = 0xDDDDDD;
    uint32_t default_bg = 0x050505;
    MicronAlign default_align = MicronAlign::Left;
    MicronAlign align = MicronAlign::Left;
    MicronStyle table_restore_style{};
    uint8_t depth = 0;
    bool literal = false;
    bool table_mode = false;
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
    lv_obj_t* browser_body = nullptr;
    lv_obj_t* browser_rail = nullptr;
    lv_obj_t* browser_back_btn = nullptr;
    lv_obj_t* home_btn = nullptr;
    lv_obj_t* refresh_btn = nullptr;
    lv_obj_t* rail_directory_btn = nullptr;
    lv_obj_t* rail_search_btn = nullptr;
    lv_obj_t* address_area = nullptr;
    lv_obj_t* go_btn = nullptr;
    lv_obj_t* viewport = nullptr;
    lv_timer_t* page_load_timer = nullptr;
    ::ui::widgets::TopBar top_bar;
    ::ui::components::floating_search_box::State search_box;
    ::ui::components::shortcut_help_modal::State help_modal;
    std::vector<rtdir::AnnounceRecord> announces;
    std::vector<rtdir::LxmfAddressRecord> addresses;
    std::array<DirectoryRowContext, kMaxDirectoryRows> row_contexts{};
    LinkContext* link_contexts = nullptr;
    std::array<HistoryEntry, kMaxHistoryEntries> history{};
    std::array<char, rtpage::kReticulumPageBodyMaxBytes + 1U> page_body{};
    std::size_t announce_count = 0;
    std::size_t address_count = 0;
    std::size_t row_context_count = 0;
    std::size_t link_context_capacity = 0;
    std::size_t link_context_count = 0;
    std::size_t history_count = 0;
    std::size_t history_pos = 0;
    DirectoryMode directory_mode = DirectoryMode::Announces;
    bool immersive = false;
    bool directory_collapsed = true;
    bool browser_collapsed = false;
    bool suppress_history = false;
    bool cached_page_body_valid = false;
    bool page_cache_load_requested = false;
    rtdir::Status announce_status{};
    rtdir::Status address_status{};
    rtpage::Status cached_page_status{};
    std::size_t cached_page_body_len = 0;
    char cached_page_destination[kHashTextLen] = {};
    char cached_page_path[rtpage::kReticulumPagePathSize] = {};
    char current_address[kAddressTextLen] = "home:/";
    char rendered_shell_address[kAddressTextLen] = {};
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
constexpr uint32_t kMicronDefaultFg = 0xDDDDDD;
constexpr uint32_t kMicronHeading1Fg = 0x222222;
constexpr uint32_t kMicronHeading1Bg = 0xBBBBBB;
constexpr uint32_t kMicronHeading2Fg = 0x111111;
constexpr uint32_t kMicronHeading2Bg = 0x999999;
constexpr uint32_t kMicronHeading3Fg = 0x000000;
constexpr uint32_t kMicronHeading3Bg = 0x777777;
constexpr uint32_t kPageLoadPollMs = 500;
constexpr uint32_t kPagerRotateUpKey = 19;
constexpr uint32_t kPagerRotateDownKey = 20;
constexpr lv_coord_t kViewportScrollStep = 28;
constexpr lv_coord_t kViewportPageScrollPadding = 12;
constexpr BuiltInFavourite kBuiltInFavourites[] = {{
    "47850a3b99243cfb1147e8856bab2691:/page/index.mu",
    "47850A3B /page",
    "built-in Nomad",
    LV_SYMBOL_STAR,
}};

#ifndef NETWORK_PAGE_TRACE
#define NETWORK_PAGE_TRACE 0
#endif

#if NETWORK_PAGE_TRACE
#define NETWORK_PAGE_LOG(...)                             \
    do                                                    \
    {                                                     \
        std::printf("[UI][Network][Nomad] " __VA_ARGS__); \
    } while (0)
#else
#define NETWORK_PAGE_LOG(...) \
    do                        \
    {                         \
    } while (0)
#endif

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

void* network_page_alloc(std::size_t size)
{
    if (size == 0)
    {
        return nullptr;
    }
#if defined(ESP_PLATFORM)
    return heap_caps_malloc_prefer(size,
                                   2,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
    return std::malloc(size);
#endif
}

void network_page_free(void* ptr)
{
    if (!ptr)
    {
        return;
    }
#if defined(ESP_PLATFORM)
    heap_caps_free(ptr);
#else
    std::free(ptr);
#endif
}

void release_link_context_pool()
{
    network_page_free(g_state.link_contexts);
    g_state.link_contexts = nullptr;
    g_state.link_context_capacity = 0;
    g_state.link_context_count = 0;
}

bool reserve_link_contexts(std::size_t required)
{
    if (required <= g_state.link_context_capacity)
    {
        return true;
    }
    if (required > kMaxPageLinks)
    {
        return false;
    }

    std::size_t next_capacity =
        g_state.link_context_capacity == 0 ? kInitialPageLinks
                                           : g_state.link_context_capacity * 2U;
    if (next_capacity < required)
    {
        next_capacity = required;
    }
    if (next_capacity > kMaxPageLinks)
    {
        next_capacity = kMaxPageLinks;
    }

    void* storage = network_page_alloc(next_capacity * sizeof(LinkContext));
    if (!storage)
    {
        return false;
    }
    auto* next_contexts = static_cast<LinkContext*>(storage);
    std::memset(next_contexts, 0, next_capacity * sizeof(LinkContext));
    if (g_state.link_contexts && g_state.link_context_count > 0)
    {
        std::memcpy(next_contexts,
                    g_state.link_contexts,
                    g_state.link_context_count * sizeof(LinkContext));
    }
    network_page_free(g_state.link_contexts);
    g_state.link_contexts = next_contexts;
    g_state.link_context_capacity = next_capacity;
    return true;
}

LinkContext* claim_link_context()
{
    if (g_state.link_context_count >= kMaxPageLinks)
    {
        return nullptr;
    }
    if (!reserve_link_contexts(g_state.link_context_count + 1U))
    {
        return nullptr;
    }
    LinkContext* context = &g_state.link_contexts[g_state.link_context_count++];
    std::memset(context, 0, sizeof(*context));
    return context;
}

const char* log_text(const char* text)
{
    return text ? text : "";
}

bool request_progress_retryable_failed(const rtpage::RequestProgress& progress)
{
    return progress.failure ==
           rtpage::RequestProgress::FailureKind::Retryable;
}

bool request_progress_terminal_failed(const rtpage::RequestProgress& progress)
{
    return progress.failure == rtpage::RequestProgress::FailureKind::Terminal;
}

const char* directory_mode_label(DirectoryMode mode)
{
    return mode == DirectoryMode::Favourites ? "favourites" : "announces";
}

void log_page_status(const char* stage,
                     const RemotePageAddress& page_address,
                     const rtpage::Status& status,
                     std::size_t body_len,
                     uint32_t elapsed_ms)
{
    NETWORK_PAGE_LOG(
        "%s dest=%s path=%s elapsed_ms=%lu supported=%u sd=%u file=%u checked=%u loaded=%u saved=%u request=%u busy=%u progress=%d truncated=%u body=%lu message=\"%s\" detail=\"%s\"\n",
        log_text(stage),
        page_address.destination,
        page_address.path,
        static_cast<unsigned long>(elapsed_ms),
        status.supported ? 1U : 0U,
        status.sd_present ? 1U : 0U,
        status.file_present ? 1U : 0U,
        status.cache_checked ? 1U : 0U,
        status.loaded ? 1U : 0U,
        status.saved ? 1U : 0U,
        status.request_started ? 1U : 0U,
        status.busy ? 1U : 0U,
        status.progress_percent,
        status.truncated ? 1U : 0U,
        static_cast<unsigned long>(body_len),
        status.message,
        status.detail);
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
    case rtdir::AnnounceAspect::NomadNetworkNode:
        return "Nomad";
    case rtdir::AnnounceAspect::Unknown:
    default:
        return "Node";
    }
}

const char* announce_display_label(const rtdir::AnnounceRecord& announce,
                                   const char* fallback)
{
    if (announce.display_name[0] != '\0')
    {
        return announce.display_name;
    }
    if (announce.aspect == rtdir::AnnounceAspect::NomadNetworkNode)
    {
        return "Anonymous Node";
    }
    if (announce.aspect == rtdir::AnnounceAspect::LxmfDelivery ||
        announce.aspect == rtdir::AnnounceAspect::CallAudio)
    {
        return "Anonymous Peer";
    }
    return fallback ? fallback : "";
}

const char* address_display_label(const rtdir::LxmfAddressRecord& address,
                                  const char* fallback)
{
    if (address.display_name[0] != '\0')
    {
        return address.display_name;
    }
    return fallback ? fallback : "Anonymous Peer";
}

bool announce_visible_in_directory(const rtdir::AnnounceRecord& announce)
{
    return announce.valid && announce.aspect == rtdir::AnnounceAspect::NomadNetworkNode;
}

std::size_t visible_announce_count()
{
    std::size_t count = 0;
    for (const auto& announce : g_state.announces)
    {
        if (announce_visible_in_directory(announce))
        {
            ++count;
        }
    }
    return count;
}

const rtdir::AnnounceRecord* first_visible_announce()
{
    for (const auto& announce : g_state.announces)
    {
        if (announce_visible_in_directory(announce))
        {
            return &announce;
        }
    }
    return nullptr;
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
    return contains_ci(announce_display_label(announce, ""), g_state.search_query) ||
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
    return contains_ci(address_display_label(address, ""), g_state.search_query) ||
           contains_ci(destination, g_state.search_query) ||
           contains_ci(identity, g_state.search_query);
}

bool built_in_favourite_matches_search(const BuiltInFavourite& favourite)
{
    if (g_state.search_query[0] == '\0')
    {
        return true;
    }
    return contains_ci(favourite.title, g_state.search_query) ||
           contains_ci(favourite.address, g_state.search_query) ||
           contains_ci(favourite.meta, g_state.search_query);
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

void page_load_timer_cb(lv_timer_t* timer);

void clear_loaded_page_body()
{
    g_state.cached_page_body_valid = false;
    g_state.page_cache_load_requested = false;
    g_state.cached_page_status = {};
    g_state.cached_page_body_len = 0;
    std::memset(g_state.cached_page_destination,
                0,
                sizeof(g_state.cached_page_destination));
    std::memset(g_state.cached_page_path, 0, sizeof(g_state.cached_page_path));
    g_state.page_body[0] = '\0';
}

bool cached_page_matches(const RemotePageAddress& page_address)
{
    return g_state.cached_page_body_valid &&
           std::strcmp(g_state.cached_page_destination,
                       page_address.destination) == 0 &&
           std::strcmp(g_state.cached_page_path, page_address.path) == 0;
}

bool parse_remote_page_address(const char* address, RemotePageAddress& out);

void clear_request_progress_for_address(const char* address)
{
    RemotePageAddress page_address{};
    if (parse_remote_page_address(address, page_address) && page_address.valid)
    {
        rtpage::clear_request_progress(page_address.destination,
                                       page_address.path);
    }
}

void stop_page_load_timer()
{
    if (g_state.page_load_timer)
    {
        lv_timer_pause(g_state.page_load_timer);
    }
}

void ensure_page_load_timer()
{
    if (g_state.page_load_timer)
    {
        lv_timer_resume(g_state.page_load_timer);
        return;
    }
    g_state.page_load_timer =
        lv_timer_create(page_load_timer_cb, kPageLoadPollMs, nullptr);
    if (g_state.page_load_timer)
    {
        lv_timer_set_repeat_count(g_state.page_load_timer, -1);
    }
}

void set_current_address(const char* address)
{
    const char* next_address =
        (address && address[0] != '\0') ? address : "home:/";
    if (std::strncmp(g_state.current_address,
                     next_address,
                     sizeof(g_state.current_address)) != 0)
    {
        clear_request_progress_for_address(g_state.current_address);
        clear_loaded_page_body();
    }
    copy_text(g_state.current_address, sizeof(g_state.current_address),
              next_address);
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

const char* remote_address_body(const char* address)
{
    if (!address)
    {
        return nullptr;
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
    return start;
}

bool parse_remote_page_address(const char* address, RemotePageAddress& out)
{
    out = RemotePageAddress{};
    const char* start = remote_address_body(address);
    if (!start || !is_hex_text(start, rtdir::kReticulumHashSize * 2U))
    {
        return false;
    }

    std::snprintf(out.destination,
                  sizeof(out.destination),
                  "%.*s",
                  static_cast<int>(rtdir::kReticulumHashSize * 2U),
                  start);

    const char* cursor = start + (rtdir::kReticulumHashSize * 2U);
    if (*cursor == ':')
    {
        ++cursor;
    }
    const char* path = (*cursor != '\0') ? cursor : "/page/index.mu";
    if (!rtpage::normalize_path(path, out.path, sizeof(out.path)))
    {
        return false;
    }
    out.valid = true;
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
    std::size_t count = sizeof(kBuiltInFavourites) / sizeof(kBuiltInFavourites[0]);
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
void page_load_timer_cb(lv_timer_t* timer);
void rebuild_focus_group(lv_obj_t* preferred = nullptr);
void focus_browser_viewport();
void focus_directory_panel();
void apply_layout_state();
void open_node_list_page();
lv_coord_t resolve_directory_width();
void open_network_help_modal();
void close_network_help_modal();
void clear_loaded_page_body();
void ensure_page_load_timer();
void stop_page_load_timer();

void reset_state_after_destroy()
{
    g_state.root = nullptr;
    g_state.header = nullptr;
    g_state.content = nullptr;
    g_state.directory_panel = nullptr;
    g_state.directory_tabs = nullptr;
    g_state.favourites_tab = nullptr;
    g_state.announces_tab = nullptr;
    g_state.directory_search_btn = nullptr;
    g_state.directory_list = nullptr;
    g_state.browser_panel = nullptr;
    g_state.browser_toolbar = nullptr;
    g_state.browser_body = nullptr;
    g_state.browser_rail = nullptr;
    g_state.browser_back_btn = nullptr;
    g_state.home_btn = nullptr;
    g_state.refresh_btn = nullptr;
    g_state.rail_directory_btn = nullptr;
    g_state.rail_search_btn = nullptr;
    g_state.address_area = nullptr;
    g_state.go_btn = nullptr;
    g_state.viewport = nullptr;
    g_state.page_load_timer = nullptr;
    g_state.top_bar = {};
    g_state.search_box = {};
    g_state.help_modal = {};
    std::vector<rtdir::AnnounceRecord>().swap(g_state.announces);
    std::vector<rtdir::LxmfAddressRecord>().swap(g_state.addresses);
    release_link_context_pool();
    std::memset(g_state.row_contexts.data(),
                0,
                g_state.row_contexts.size() * sizeof(g_state.row_contexts[0]));
    std::memset(g_state.history.data(),
                0,
                g_state.history.size() * sizeof(g_state.history[0]));
    std::memset(g_state.page_body.data(), 0, g_state.page_body.size());
    g_state.announce_count = 0;
    g_state.address_count = 0;
    g_state.row_context_count = 0;
    g_state.link_context_capacity = 0;
    g_state.link_context_count = 0;
    g_state.history_count = 0;
    g_state.history_pos = 0;
    g_state.directory_mode = DirectoryMode::Announces;
    g_state.immersive = false;
    g_state.directory_collapsed = true;
    g_state.browser_collapsed = false;
    g_state.suppress_history = false;
    g_state.cached_page_body_valid = false;
    g_state.page_cache_load_requested = false;
    g_state.announce_status = {};
    g_state.address_status = {};
    g_state.cached_page_status = {};
    g_state.cached_page_body_len = 0;
    std::memset(g_state.cached_page_destination,
                0,
                sizeof(g_state.cached_page_destination));
    std::memset(g_state.cached_page_path, 0, sizeof(g_state.cached_page_path));
    copy_text(g_state.current_address, sizeof(g_state.current_address), "home:/");
    std::memset(g_state.rendered_shell_address, 0, sizeof(g_state.rendered_shell_address));
    std::memset(g_state.search_query, 0, sizeof(g_state.search_query));
}

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
        add_visible_to_group(g_state.address_area);
        add_visible_to_group(g_state.go_btn);
    }
    add_visible_to_group(g_state.viewport);
    add_viewport_links_to_group();
    if (!g_state.immersive)
    {
        add_visible_to_group(g_state.browser_back_btn);
        add_visible_to_group(g_state.home_btn);
        add_visible_to_group(g_state.refresh_btn);
        add_visible_to_group(g_state.rail_directory_btn);
        add_visible_to_group(g_state.rail_search_btn);
    }
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
    lv_obj_set_style_bg_color(g_state.viewport, lv_color_hex(kTerminalBg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_state.viewport, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clean(g_state.viewport);
    release_link_context_pool();
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

void add_terminal_link_text(const char* target, const char* label)
{
    char text[192] = {};
    const char* value = label && label[0] ? label : (target ? target : "");
    std::snprintf(text, sizeof(text), "=> %s", value);
    add_terminal_label(text, kTerminalGreen, caption_font(), LV_LABEL_LONG_DOT);
}

void add_terminal_link(const char* target, const char* label, bool interactive = true)
{
    if (!g_state.viewport)
    {
        return;
    }
    if (!interactive)
    {
        add_terminal_link_text(target, label);
        return;
    }
    LinkContext* context = claim_link_context();
    if (!context)
    {
        add_terminal_link_text(target, label);
        return;
    }
    copy_text(context->target, sizeof(context->target), target);

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
    lv_obj_add_event_cb(btn, link_event_cb, LV_EVENT_CLICKED, context);
    lv_obj_add_event_cb(btn, link_event_cb, LV_EVENT_KEY, context);
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

bool micron_color_from_text(const char* text,
                            std::size_t len,
                            uint32_t* out)
{
    if (!text || !out)
    {
        return false;
    }
    if (len == 3 && (text[0] == 'g' || text[0] == 'G') &&
        std::isdigit(static_cast<unsigned char>(text[1])) &&
        std::isdigit(static_cast<unsigned char>(text[2])))
    {
        const uint32_t value =
            static_cast<uint32_t>((text[1] - '0') * 10 + (text[2] - '0'));
        const uint32_t byte = value > 99U ? 255U : (value * 255U) / 99U;
        *out = (byte << 16U) | (byte << 8U) | byte;
        return true;
    }
    if (len == 3 && is_hex_text(text, len))
    {
        const uint32_t r = static_cast<uint32_t>(hex_nibble(text[0]) * 17U);
        const uint32_t g = static_cast<uint32_t>(hex_nibble(text[1]) * 17U);
        const uint32_t b = static_cast<uint32_t>(hex_nibble(text[2]) * 17U);
        *out = (r << 16U) | (g << 8U) | b;
        return true;
    }
    if (len == 6 && is_hex_text(text, len))
    {
        uint32_t value = 0;
        for (std::size_t i = 0; i < 6; ++i)
        {
            value = (value << 4U) | hex_nibble(text[i]);
        }
        *out = value;
        return true;
    }
    return false;
}

bool parse_micron_color_token(const char* line,
                              std::size_t len,
                              std::size_t index,
                              uint32_t* out,
                              std::size_t* consumed)
{
    if (!line || !out || !consumed || index >= len)
    {
        return false;
    }
    if (index + 8U <= len && line[index + 1U] == 'T' &&
        micron_color_from_text(line + index + 2U, 6, out))
    {
        *consumed = 8;
        return true;
    }
    if (index + 9U <= len && line[index + 4U] == '`' &&
        line[index + 5U] == line[index] &&
        is_hex_text(line + index + 1U, 3) &&
        is_hex_text(line + index + 6U, 3))
    {
        char expanded[6] = {
            line[index + 6U],
            line[index + 1U],
            line[index + 7U],
            line[index + 2U],
            line[index + 8U],
            line[index + 3U],
        };
        if (micron_color_from_text(expanded, sizeof(expanded), out))
        {
            *consumed = 9;
            return true;
        }
    }
    if (index + 4U <= len &&
        micron_color_from_text(line + index + 1U, 3, out))
    {
        *consumed = 4;
        return true;
    }
    return false;
}

void copy_range(char* out,
                std::size_t out_len,
                const char* start,
                std::size_t len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    if (!start)
    {
        out[0] = '\0';
        return;
    }
    const std::size_t copy_len = len < out_len - 1U ? len : out_len - 1U;
    std::memcpy(out, start, copy_len);
    out[copy_len] = '\0';
}

bool micron_ascii_space(char ch)
{
    return ch == ' ' || ch == '\t';
}

std::size_t utf8_step_len(unsigned char lead)
{
    if ((lead & 0x80U) == 0)
    {
        return 1;
    }
    if ((lead & 0xE0U) == 0xC0U)
    {
        return 2;
    }
    if ((lead & 0xF0U) == 0xE0U)
    {
        return 3;
    }
    if ((lead & 0xF8U) == 0xF0U)
    {
        return 4;
    }
    return 1;
}

std::size_t bounded_utf8_chunk_len(const char* text, std::size_t remaining)
{
    if (!text || remaining == 0)
    {
        return 0;
    }
    if (remaining <= kMicronTextChunkBytes)
    {
        return remaining;
    }

    std::size_t last_space = 0;
    std::size_t last_boundary = 0;
    std::size_t index = 0;
    while (index < remaining && index < kMicronTextChunkBytes)
    {
        const unsigned char lead = static_cast<unsigned char>(text[index]);
        const std::size_t step = utf8_step_len(lead);
        if (index + step > remaining || index + step > kMicronTextChunkBytes)
        {
            break;
        }
        index += step;
        last_boundary = index;
        if (micron_ascii_space(text[index - 1U]))
        {
            last_space = index;
        }
    }

    if (last_space > 0)
    {
        return last_space;
    }
    return last_boundary > 0 ? last_boundary : 1U;
}

MicronAlign micron_align_from_state(const MicronRenderState& state)
{
    return state.align;
}

lv_flex_align_t micron_flex_align(MicronAlign align)
{
    switch (align)
    {
    case MicronAlign::Center:
        return LV_FLEX_ALIGN_CENTER;
    case MicronAlign::Right:
        return LV_FLEX_ALIGN_END;
    case MicronAlign::Left:
    default:
        return LV_FLEX_ALIGN_START;
    }
}

void apply_micron_row_align(lv_obj_t* row, MicronAlign align)
{
    if (!row)
    {
        return;
    }
    lv_obj_set_flex_align(row,
                          micron_flex_align(align),
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
}

void apply_micron_label_style(lv_obj_t* label,
                              const MicronStyle& style,
                              uint32_t default_bg,
                              const lv_font_t* font = nullptr)
{
    if (!label)
    {
        return;
    }
    lv_obj_set_style_text_color(label, lv_color_hex(style.fg), LV_PART_MAIN);
    lv_obj_set_style_text_font(label,
                               font ? font : (style.bold ? body_font() : caption_font()),
                               LV_PART_MAIN);
    if (style.bg != default_bg)
    {
        lv_obj_set_style_bg_color(label, lv_color_hex(style.bg), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(label, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(label, 1, LV_PART_MAIN);
        lv_obj_set_style_pad_left(label, 1, LV_PART_MAIN);
        lv_obj_set_style_pad_right(label, 1, LV_PART_MAIN);
    }
    else
    {
        lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_pad_left(label, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_right(label, 0, LV_PART_MAIN);
    }
    if (style.underline)
    {
        lv_obj_set_style_border_width(label, 1, LV_PART_MAIN);
        lv_obj_set_style_border_side(label, LV_BORDER_SIDE_BOTTOM, LV_PART_MAIN);
        lv_obj_set_style_border_color(label, lv_color_hex(style.fg), LV_PART_MAIN);
        lv_obj_set_style_pad_bottom(label, 1, LV_PART_MAIN);
    }
}

lv_obj_t* create_micron_row(const MicronRenderState& state,
                            bool use_line_background = false,
                            uint32_t line_bg = kTerminalBg)
{
    lv_obj_t* row = lv_obj_create(g_state.viewport);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    style_plain_container(row,
                          use_line_background ? line_bg : state.default_bg,
                          use_line_background ? LV_OPA_COVER : LV_OPA_TRANSP);
    lv_obj_set_style_pad_top(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_left(
        row,
        state.depth > 1U ? static_cast<lv_coord_t>((state.depth - 1U) * 8U) : 0,
        LV_PART_MAIN);
    lv_obj_set_style_pad_right(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, 0, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    apply_micron_row_align(row, micron_align_from_state(state));
    ::ui::components::two_pane_layout::make_non_scrollable(row);
    return row;
}

void emit_micron_text_token(lv_obj_t* row,
                            const char* text,
                            const MicronStyle& style,
                            uint32_t default_bg,
                            const lv_font_t* font = nullptr)
{
    if (!row || !text || text[0] == '\0')
    {
        return;
    }
    lv_obj_t* label = lv_label_create(row);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(label, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(label, LV_PCT(100), LV_PART_MAIN);
    apply_micron_label_style(label, style, default_bg, font);
    ::ui::fonts::apply_localized_font(label,
                                      text,
                                      font ? font : (style.bold ? body_font() : caption_font()));
}

void emit_micron_text(lv_obj_t* row,
                      const char* text,
                      const MicronStyle& style,
                      uint32_t default_bg,
                      const lv_font_t* font = nullptr)
{
    if (!row || !text || text[0] == '\0')
    {
        return;
    }
    const std::size_t text_len = std::strlen(text);
    const char* cursor = text;
    std::size_t remaining = text_len;
    while (remaining > 0)
    {
        char chunk[kMicronTextChunkBytes + 1U] = {};
        const std::size_t chunk_len = bounded_utf8_chunk_len(cursor, remaining);
        if (chunk_len == 0)
        {
            break;
        }
        copy_range(chunk, sizeof(chunk), cursor, chunk_len);
        for (char* ch = chunk; *ch != '\0'; ++ch)
        {
            if (*ch == '\t')
            {
                *ch = ' ';
            }
        }
        emit_micron_text_token(row, chunk, style, default_bg, font);
        cursor += chunk_len;
        remaining -= chunk_len;
    }
}

void reset_micron_style(MicronRenderState& state)
{
    state.style.fg = state.default_fg;
    state.style.bg = state.default_bg;
    state.style.bold = false;
    state.style.underline = false;
    state.style.italic = false;
    state.align = state.default_align;
}

void normalize_micron_link_target(const char* target,
                                  char* out,
                                  std::size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    const char* value = target ? target : "";
    static constexpr const char* kNomadScheme = "nomadnetwork://";
    static constexpr const char* kLxmfScheme = "lxmf://";
    if (std::strncmp(value, kNomadScheme, std::strlen(kNomadScheme)) == 0)
    {
        value += std::strlen(kNomadScheme);
    }
    else if (std::strncmp(value, kLxmfScheme, std::strlen(kLxmfScheme)) == 0)
    {
        value += std::strlen(kLxmfScheme);
    }

    if (value[0] == ':' && value[1] == '/')
    {
        char hash_text[kHashTextLen] = {};
        if (extract_destination_text(g_state.current_address,
                                     hash_text,
                                     sizeof(hash_text)))
        {
            std::snprintf(out, out_len, "%s%s", hash_text, value);
            return;
        }
    }
    copy_text(out, out_len, value);
}

void emit_micron_link(lv_obj_t* row,
                      const char* target,
                      const char* label,
                      const MicronStyle& style,
                      uint32_t default_bg)
{
    if (!row || !target || target[0] == '\0')
    {
        return;
    }
    if (target[0] == '#')
    {
        emit_micron_text(row, label && label[0] ? label : target, style, default_bg);
        return;
    }
    LinkContext* context = claim_link_context();
    if (!context)
    {
        emit_micron_text(row, label && label[0] ? label : target, style, default_bg);
        return;
    }

    normalize_micron_link_target(target, context->target, sizeof(context->target));

    lv_obj_t* btn = lv_btn_create(row);
    lv_obj_set_height(btn, LV_SIZE_CONTENT);
    lv_obj_set_width(btn, LV_SIZE_CONTENT);
    lv_obj_set_style_min_height(btn, 16, LV_PART_MAIN);
    lv_obj_set_style_pad_left(btn, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_right(btn, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_top(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 2, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, lv_color_hex(style.fg), LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        btn,
        lv_color_hex(style.bg != default_bg ? style.bg : kTerminalBg),
        LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(kTerminalGreen), LV_STATE_PRESSED);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(btn, link_event_cb, LV_EVENT_CLICKED, context);
    lv_obj_add_event_cb(btn, link_event_cb, LV_EVENT_KEY, context);

    lv_obj_t* text = lv_label_create(btn);
    const char* visible = label && label[0] ? label : target;
    lv_label_set_text(text, visible);
    lv_label_set_long_mode(text, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(text, LV_SIZE_CONTENT);
    apply_micron_label_style(text, style, default_bg);
    ::ui::fonts::apply_localized_font(text,
                                      visible,
                                      style.bold ? body_font() : caption_font());
    lv_obj_center(text);
}

void emit_micron_field(lv_obj_t* row,
                       const char* spec,
                       const char* data,
                       const MicronStyle& style,
                       uint32_t default_bg)
{
    if (!row || !spec)
    {
        return;
    }
    bool checkbox = false;
    bool radio = false;
    bool masked = false;
    bool checked = false;
    uint16_t width_chars = 16;
    const char* name = spec;
    char name_buf[48] = {};
    char value_buf[64] = {};

    const char* first_sep = std::strchr(spec, '|');
    if (first_sep)
    {
        char flags[16] = {};
        copy_range(flags, sizeof(flags), spec, static_cast<std::size_t>(first_sep - spec));
        checkbox = std::strchr(flags, '?') != nullptr;
        radio = std::strchr(flags, '^') != nullptr;
        masked = std::strchr(flags, '!') != nullptr;
        const char* cursor = flags;
        while (*cursor != '\0')
        {
            if (std::isdigit(static_cast<unsigned char>(*cursor)))
            {
                width_chars = static_cast<uint16_t>(
                    width_chars * 0U + static_cast<uint16_t>(std::atoi(cursor)));
                break;
            }
            ++cursor;
        }
        const char* second_sep = std::strchr(first_sep + 1, '|');
        if (second_sep)
        {
            copy_range(name_buf,
                       sizeof(name_buf),
                       first_sep + 1,
                       static_cast<std::size_t>(second_sep - first_sep - 1));
            const char* third_sep = std::strchr(second_sep + 1, '|');
            if (third_sep)
            {
                copy_range(value_buf,
                           sizeof(value_buf),
                           second_sep + 1,
                           static_cast<std::size_t>(third_sep - second_sep - 1));
                checked = std::strcmp(third_sep + 1, "*") == 0;
            }
            else
            {
                copy_text(value_buf, sizeof(value_buf), second_sep + 1);
            }
        }
        else
        {
            copy_text(name_buf, sizeof(name_buf), first_sep + 1);
        }
        name = name_buf[0] ? name_buf : spec;
    }

    if (checkbox || radio)
    {
        lv_obj_t* cb = lv_checkbox_create(row);
        lv_checkbox_set_text(cb, data && data[0] ? data : (value_buf[0] ? value_buf : name));
        if (checked)
        {
            lv_obj_add_state(cb, LV_STATE_CHECKED);
        }
        lv_obj_set_style_text_color(cb, lv_color_hex(style.fg), LV_PART_MAIN);
        lv_obj_set_style_text_font(cb, caption_font(), LV_PART_MAIN);
        if (style.bg != default_bg)
        {
            lv_obj_set_style_bg_color(cb, lv_color_hex(style.bg), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(cb, LV_OPA_COVER, LV_PART_MAIN);
        }
        return;
    }

    lv_obj_t* field = lv_textarea_create(row);
    lv_textarea_set_one_line(field, true);
    lv_textarea_set_text(field, data ? data : "");
    lv_textarea_set_password_mode(field, masked);
    const lv_coord_t field_w =
        static_cast<lv_coord_t>((width_chars < 6 ? 6 : width_chars) * 6U);
    lv_obj_set_size(field, field_w > 160 ? 160 : field_w, 18);
    lv_obj_set_style_bg_color(
        field,
        lv_color_hex(style.bg != default_bg ? style.bg : 0x222222),
        LV_PART_MAIN);
    lv_obj_set_style_bg_opa(field, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(field, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(field, lv_color_hex(style.fg), LV_PART_MAIN);
    lv_obj_set_style_radius(field, 2, LV_PART_MAIN);
    lv_obj_set_style_text_color(field, lv_color_hex(style.fg), LV_PART_MAIN);
    lv_obj_set_style_text_font(field, caption_font(), LV_PART_MAIN);
    lv_obj_set_style_pad_all(field, 1, LV_PART_MAIN);
}

void flush_micron_part(lv_obj_t* row,
                       char* part,
                       std::size_t& part_len,
                       const MicronRenderState& state,
                       const lv_font_t* font = nullptr)
{
    if (part_len == 0)
    {
        return;
    }
    part[part_len] = '\0';
    emit_micron_text(row, part, state.style, state.default_bg, font);
    part_len = 0;
}

void append_micron_part_char(lv_obj_t* row,
                             char* part,
                             std::size_t& part_len,
                             char ch,
                             const MicronRenderState& state,
                             const lv_font_t* font = nullptr)
{
    if (part_len + 1U >= 384U)
    {
        flush_micron_part(row, part, part_len, state, font);
    }
    part[part_len++] = ch;
}

bool parse_micron_link(lv_obj_t* row,
                       const char* line,
                       std::size_t line_len,
                       std::size_t start,
                       const MicronRenderState& state,
                       std::size_t* consumed)
{
    if (!row || !line || !consumed)
    {
        return false;
    }
    const char* close = std::strchr(line + start + 1U, ']');
    if (!close)
    {
        return false;
    }
    const std::size_t data_len =
        static_cast<std::size_t>(close - (line + start + 1U));
    char data[224] = {};
    copy_range(data, sizeof(data), line + start + 1U, data_len);

    char label[96] = {};
    char target[kAddressTextLen] = {};
    const char* first_tick = std::strchr(data, '`');
    if (!first_tick)
    {
        copy_text(target, sizeof(target), data);
        copy_text(label, sizeof(label), data);
    }
    else
    {
        copy_range(label,
                   sizeof(label),
                   data,
                   static_cast<std::size_t>(first_tick - data));
        const char* second_tick = std::strchr(first_tick + 1, '`');
        if (second_tick)
        {
            copy_range(target,
                       sizeof(target),
                       first_tick + 1,
                       static_cast<std::size_t>(second_tick - first_tick - 1));
        }
        else
        {
            copy_text(target, sizeof(target), first_tick + 1);
        }
        if (label[0] == '\0')
        {
            copy_text(label, sizeof(label), target);
        }
    }

    emit_micron_link(row, target, label, state.style, state.default_bg);
    *consumed = static_cast<std::size_t>(close - (line + start)) + 1U;
    (void)line_len;
    return true;
}

bool parse_micron_field(lv_obj_t* row,
                        const char* line,
                        std::size_t start,
                        const MicronRenderState& state,
                        std::size_t* consumed)
{
    if (!row || !line || !consumed)
    {
        return false;
    }
    const char* tick = std::strchr(line + start + 1U, '`');
    if (!tick)
    {
        return false;
    }
    const char* close = std::strchr(tick + 1U, '>');
    if (!close)
    {
        return false;
    }
    char spec[96] = {};
    char data[96] = {};
    copy_range(spec,
               sizeof(spec),
               line + start + 1U,
               static_cast<std::size_t>(tick - (line + start + 1U)));
    copy_range(data,
               sizeof(data),
               tick + 1U,
               static_cast<std::size_t>(close - (tick + 1U)));
    emit_micron_field(row, spec, data, state.style, state.default_bg);
    *consumed = static_cast<std::size_t>(close - (line + start)) + 1U;
    return true;
}

void render_micron_inline(lv_obj_t* row,
                          const char* line,
                          MicronRenderState& state,
                          const lv_font_t* font = nullptr)
{
    if (!row || !line)
    {
        return;
    }
    char part[384] = {};
    std::size_t part_len = 0;
    const std::size_t line_len = std::strlen(line);
    bool escape = false;

    for (std::size_t i = 0; i < line_len;)
    {
        const char ch = line[i];
        if (escape)
        {
            append_micron_part_char(row, part, part_len, ch, state, font);
            escape = false;
            ++i;
            continue;
        }
        if (ch == '\\')
        {
            escape = true;
            ++i;
            continue;
        }
        if (ch != '`')
        {
            append_micron_part_char(row, part, part_len, ch, state, font);
            ++i;
            continue;
        }

        if (i + 1U < line_len && line[i + 1U] == '`')
        {
            flush_micron_part(row, part, part_len, state, font);
            reset_micron_style(state);
            i += 2U;
            apply_micron_row_align(row, state.align);
            continue;
        }

        flush_micron_part(row, part, part_len, state, font);
        ++i;
        if (i >= line_len)
        {
            break;
        }
        const char tag = line[i];
        switch (tag)
        {
        case '!':
            state.style.bold = !state.style.bold;
            ++i;
            break;
        case '_':
            state.style.underline = !state.style.underline;
            ++i;
            break;
        case '*':
            state.style.italic = !state.style.italic;
            ++i;
            break;
        case 'F':
        case 'B':
        {
            uint32_t color = 0;
            std::size_t consumed = 0;
            if (parse_micron_color_token(line, line_len, i, &color, &consumed))
            {
                if (tag == 'F')
                {
                    state.style.fg = color;
                }
                else
                {
                    state.style.bg = color;
                }
                i += consumed;
            }
            else
            {
                ++i;
            }
            break;
        }
        case 'f':
            state.style.fg = state.default_fg;
            ++i;
            break;
        case 'b':
            state.style.bg = state.default_bg;
            ++i;
            break;
        case 'c':
            state.align = MicronAlign::Center;
            apply_micron_row_align(row, state.align);
            ++i;
            break;
        case 'l':
            state.align = MicronAlign::Left;
            apply_micron_row_align(row, state.align);
            ++i;
            break;
        case 'r':
            state.align = MicronAlign::Right;
            apply_micron_row_align(row, state.align);
            ++i;
            break;
        case 'a':
            state.align = state.default_align;
            apply_micron_row_align(row, state.align);
            ++i;
            break;
        case '<':
        {
            std::size_t consumed = 0;
            if (parse_micron_field(row, line, i, state, &consumed))
            {
                i += consumed;
            }
            else
            {
                ++i;
            }
            break;
        }
        case '[':
        {
            std::size_t consumed = 0;
            if (parse_micron_link(row, line, line_len, i, state, &consumed))
            {
                i += consumed;
            }
            else
            {
                ++i;
            }
            break;
        }
        case ':':
            while (i < line_len &&
                   (std::isalnum(static_cast<unsigned char>(line[i])) ||
                    line[i] == '_' || line[i] == '-' || line[i] == ':'))
            {
                ++i;
            }
            break;
        case '{':
            emit_micron_text(row, "[partial]", state.style, state.default_bg, font);
            while (i < line_len && line[i] != '}')
            {
                ++i;
            }
            if (i < line_len)
            {
                ++i;
            }
            break;
        case '`':
            reset_micron_style(state);
            apply_micron_row_align(row, state.align);
            ++i;
            break;
        default:
            ++i;
            break;
        }
    }
    flush_micron_part(row, part, part_len, state, font);
}

MicronStyle micron_heading_style(uint8_t depth)
{
    MicronStyle style{};
    if (depth <= 1U)
    {
        style.fg = kMicronHeading1Fg;
        style.bg = kMicronHeading1Bg;
    }
    else if (depth == 2U)
    {
        style.fg = kMicronHeading2Fg;
        style.bg = kMicronHeading2Bg;
    }
    else
    {
        style.fg = kMicronHeading3Fg;
        style.bg = kMicronHeading3Bg;
    }
    style.bold = depth <= 2U;
    return style;
}

void render_micron_divider(MicronRenderState& state, const char* line)
{
    char divider[96] = {};
    const char fill = line && line[1] != '\0' ? line[1] : '-';
    for (std::size_t i = 0; i + 1U < sizeof(divider); ++i)
    {
        divider[i] = fill >= 32 ? fill : '-';
    }
    lv_obj_t* row = create_micron_row(state);
    emit_micron_text(row, divider, state.style, state.default_bg, caption_font());
}

void render_micron_empty_line(const MicronRenderState& state)
{
    lv_obj_t* spacer = lv_obj_create(g_state.viewport);
    lv_obj_set_size(spacer, LV_PCT(100), 8);
    style_plain_container(spacer,
                          state.style.bg != state.default_bg ? state.style.bg
                                                             : state.default_bg,
                          state.style.bg != state.default_bg ? LV_OPA_COVER
                                                             : LV_OPA_TRANSP);
    ::ui::components::two_pane_layout::make_non_scrollable(spacer);
}

void render_micron_line(const char* line, MicronRenderState& state)
{
    if (!line)
    {
        return;
    }
    if (line[0] == '\0')
    {
        render_micron_empty_line(state);
        return;
    }
    if (line[0] == '#')
    {
        return;
    }
    if (!state.literal && line[0] == '`' && line[1] == '=' && line[2] == '\0')
    {
        state.literal = true;
        return;
    }
    if (state.literal)
    {
        if (line[0] == '`' && line[1] == '=' && line[2] == '\0')
        {
            state.literal = false;
            return;
        }
        const char* text = line;
        if (line[0] == '\\' && line[1] == '`' && line[2] == '=' && line[3] == '\0')
        {
            text = line + 1;
        }
        lv_obj_t* row = create_micron_row(state);
        emit_micron_text(row, text, state.style, state.default_bg, caption_font());
        return;
    }
    if (line[0] == '<')
    {
        state.depth = 0;
        if (line[1] == '\0')
        {
            return;
        }
        render_micron_line(line + 1, state);
        return;
    }
    if (line[0] == '>')
    {
        uint8_t depth = 0;
        const char* text = line;
        while (*text == '>' && depth < 8U)
        {
            ++depth;
            ++text;
        }
        state.depth = depth;
        if (*text == '\0')
        {
            return;
        }
        const MicronStyle saved_style = state.style;
        state.style = micron_heading_style(depth);
        lv_obj_t* row = create_micron_row(state, true, state.style.bg);
        render_micron_inline(row, text, state, depth <= 1U ? body_font() : caption_font());
        state.style = saved_style;
        return;
    }
    if (line[0] == '-' && (line[1] == '\0' || line[2] == '\0'))
    {
        render_micron_divider(state, line);
        return;
    }
    if (line[0] == '`' && line[1] == '{')
    {
        lv_obj_t* row = create_micron_row(state);
        emit_micron_text(row, "[partial]", state.style, state.default_bg, caption_font());
        return;
    }

    lv_obj_t* row = create_micron_row(state);
    render_micron_inline(row, line, state, caption_font());
}

void begin_micron_table(MicronRenderState& state)
{
    state.table_restore_style = state.style;
    state.style.bg = state.style.bg != state.default_bg ? state.style.bg : 0x151515;
    state.table_mode = true;
}

void finish_micron_table(MicronRenderState& state)
{
    state.style = state.table_restore_style;
    state.table_mode = false;
}

void render_micron_table_row(MicronRenderState& state, const char* row_text)
{
    lv_obj_t* row = create_micron_row(state, true, state.style.bg);
    render_micron_inline(row, row_text ? row_text : "", state, tiny_font());
}

void parse_micron_header_colors(const char* body,
                                std::size_t body_len,
                                uint32_t* fg,
                                uint32_t* bg)
{
    if (!body || !fg || !bg)
    {
        return;
    }
    const char* cursor = body;
    const char* end = body + body_len;
    while (cursor < end)
    {
        const char* line_start = cursor;
        while (cursor < end && *cursor != '\n')
        {
            ++cursor;
        }
        const char* line_end = cursor;
        while (line_start < line_end &&
               (*line_start == ' ' || *line_start == '\t' || *line_start == '\r'))
        {
            ++line_start;
        }
        while (line_end > line_start &&
               (line_end[-1] == ' ' || line_end[-1] == '\t' ||
                line_end[-1] == '\r'))
        {
            --line_end;
        }
        if (line_start == line_end)
        {
            if (cursor < end)
            {
                ++cursor;
            }
            continue;
        }
        if (line_end - line_start < 2 || line_start[0] != '#' ||
            line_start[1] != '!')
        {
            break;
        }
        if (line_end - line_start >= 6 &&
            std::strncmp(line_start, "#!fg=", 5) == 0)
        {
            (void)micron_color_from_text(line_start + 5,
                                         static_cast<std::size_t>(line_end - line_start - 5),
                                         fg);
        }
        else if (line_end - line_start >= 6 &&
                 std::strncmp(line_start, "#!bg=", 5) == 0)
        {
            (void)micron_color_from_text(line_start + 5,
                                         static_cast<std::size_t>(line_end - line_start - 5),
                                         bg);
        }
        if (cursor < end)
        {
            ++cursor;
        }
    }
}

void render_micron_body(char* body, std::size_t body_len, bool truncated)
{
    if (!body)
    {
        return;
    }
    if (body_len == 0)
    {
        add_terminal_label("empty page", kTerminalDim, caption_font(), LV_LABEL_LONG_WRAP);
        return;
    }

    MicronRenderState state{};
    state.default_fg = kMicronDefaultFg;
    state.default_bg = kTerminalBg;
    parse_micron_header_colors(body, body_len, &state.default_fg, &state.default_bg);
    state.style.fg = state.default_fg;
    state.style.bg = state.default_bg;
    lv_obj_set_style_bg_color(g_state.viewport, lv_color_hex(state.default_bg), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g_state.viewport, LV_OPA_COVER, LV_PART_MAIN);

    char* line = body;
    char* cursor = body;
    char* end = body + body_len;
    while (cursor <= end)
    {
        if (cursor == end || *cursor == '\n')
        {
            const char saved = cursor == end ? '\0' : *cursor;
            *cursor = '\0';
            const std::size_t len = std::strlen(line);
            if (len != 0 && line[len - 1U] == '\r')
            {
                line[len - 1U] = '\0';
            }
            if (!state.literal && line[0] == '`' && line[1] == 't')
            {
                if (state.table_mode)
                {
                    finish_micron_table(state);
                }
                else
                {
                    begin_micron_table(state);
                }
            }
            else if (state.table_mode)
            {
                render_micron_table_row(state, line);
            }
            else
            {
                render_micron_line(line, state);
            }
            if (saved == '\0')
            {
                break;
            }
            line = cursor + 1;
        }
        ++cursor;
    }

    if (state.table_mode)
    {
        finish_micron_table(state);
    }

    if (truncated)
    {
        add_terminal_spacer(4);
        add_terminal_label("page truncated", kTerminalDim, caption_font(), LV_LABEL_LONG_WRAP);
    }
}

void render_home_page()
{
    clear_viewport();
    g_state.rendered_shell_address[0] = '\0';
    add_terminal_label("Nomad Network", kTerminalAmber, body_font(), LV_LABEL_LONG_WRAP);
    add_terminal_spacer();
    char line[96] = {};
    std::snprintf(line,
                  sizeof(line),
                  "nodes %u  favourites %u",
                  static_cast<unsigned>(visible_announce_count()),
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
    add_terminal_link("home:/announces", "Nodes");
    add_terminal_link("home:/favourites", "Favourites");

    if (const rtdir::AnnounceRecord* latest = first_visible_announce())
    {
        char dest[kHashTextLen] = {};
        format_hash_hex(latest->destination_hash, dest, sizeof(dest));
        char target[kAddressTextLen] = {};
        std::snprintf(target, sizeof(target), "%s:/page/index.mu", dest);
        const char* label = announce_display_label(*latest, "Latest announce");
        add_terminal_link(target, label);
    }
}

void render_collection_page(bool favourites)
{
    clear_viewport();
    g_state.rendered_shell_address[0] = '\0';
    add_terminal_label(favourites ? "Favourites" : "Nodes",
                       kTerminalAmber,
                       body_font(),
                       LV_LABEL_LONG_WRAP);
    add_terminal_spacer();
    if (favourites)
    {
        std::size_t visible = 0;
        for (const auto& favourite : kBuiltInFavourites)
        {
            add_terminal_link(favourite.address, favourite.title);
            ++visible;
        }
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
            add_terminal_link(target, address_display_label(address, "Anonymous Peer"));
            ++visible;
        }
        if (visible == 0)
        {
            add_terminal_label("no favourites", kTerminalDim, caption_font());
        }
        return;
    }

    std::size_t visible = 0;
    for (std::size_t i = 0; i < g_state.announce_count && visible < 16; ++i)
    {
        const auto& announce = g_state.announces[i];
        if (!announce_visible_in_directory(announce))
        {
            continue;
        }
        char dest[kHashTextLen] = {};
        char target[kAddressTextLen] = {};
        format_hash_hex(announce.destination_hash, dest, sizeof(dest));
        std::snprintf(target, sizeof(target), "%s:/page/index.mu", dest);
        add_terminal_link(target, announce_display_label(announce, dest));
        ++visible;
    }
    if (visible == 0)
    {
        add_terminal_label("no Nomad nodes", kTerminalDim, caption_font());
    }
}

void render_remote_page_shell(const char* address,
                              const RemotePageAddress* page_address,
                              const rtdir::AnnounceRecord* announce,
                              const rtdir::LxmfAddressRecord* address_record,
                              const rtpage::Status* cache_status,
                              const rtpage::Status* request_status,
                              const rtpage::RequestProgress* progress)
{
    const bool preserve_scroll =
        address && address[0] != '\0' &&
        std::strcmp(g_state.rendered_shell_address, address) == 0;
    const lv_coord_t scroll_y =
        preserve_scroll && g_state.viewport ? lv_obj_get_scroll_y(g_state.viewport) : 0;
    clear_viewport();
    copy_text(g_state.rendered_shell_address,
              sizeof(g_state.rendered_shell_address),
              address ? address : "");
    const char* title = "Nomad Page";
    if (address_record)
    {
        title = address_display_label(*address_record, title);
    }
    else if (announce)
    {
        title = announce_display_label(*announce, title);
    }
    add_terminal_label(title, kTerminalAmber, body_font(), LV_LABEL_LONG_WRAP);
    add_terminal_spacer();

    char status_line[128] = {};
    uint32_t status_color = kTerminalDim;
    if (progress && request_progress_terminal_failed(*progress))
    {
        std::snprintf(status_line,
                      sizeof(status_line),
                      "%s",
                      progress->message[0] != '\0'
                          ? progress->message
                          : "Nomad page request failed");
        status_color = kTerminalAmber;
    }
    else if (progress && request_progress_retryable_failed(*progress))
    {
        std::snprintf(status_line,
                      sizeof(status_line),
                      "%s",
                      progress->message[0] != '\0'
                          ? progress->message
                          : "Retrying Nomad page request");
    }
    else if (progress && (progress->active || progress->complete ||
                          progress->message[0] != '\0'))
    {
        if (progress->progress_percent >= 0)
        {
            std::snprintf(status_line,
                          sizeof(status_line),
                          "%s %d%%",
                          progress->message[0] != '\0'
                              ? progress->message
                              : "Loading Nomad page",
                          progress->progress_percent);
        }
        else
        {
            std::snprintf(status_line,
                          sizeof(status_line),
                          "%s",
                          progress->message[0] != '\0'
                              ? progress->message
                              : "Loading Nomad page");
        }
    }
    else if (cache_status && cache_status->busy)
    {
        if (cache_status->progress_percent >= 0)
        {
            std::snprintf(status_line,
                          sizeof(status_line),
                          "Checking cache %d%%",
                          cache_status->progress_percent);
        }
        else
        {
            copy_text(status_line, sizeof(status_line), "Checking cache");
        }
    }
    else if (request_status && request_status->message[0] != '\0')
    {
        copy_text(status_line, sizeof(status_line), request_status->message);
    }
    else if (cache_status && cache_status->cache_checked &&
             !cache_status->loaded)
    {
        copy_text(status_line, sizeof(status_line), "Waiting for Nomad page");
    }
    else
    {
        copy_text(status_line, sizeof(status_line), "Opening Nomad page");
    }

    add_terminal_label(status_line, status_color, caption_font(), LV_LABEL_LONG_WRAP);
    if (preserve_scroll && g_state.viewport && scroll_y > 0)
    {
        lv_obj_update_layout(g_state.viewport);
        lv_obj_scroll_to_y(g_state.viewport, scroll_y, LV_ANIM_OFF);
    }
}

void render_cached_page(const rtpage::Status& status,
                        std::size_t body_len)
{
    clear_viewport();
    g_state.rendered_shell_address[0] = '\0';
    render_micron_body(g_state.page_body.data(), body_len, status.truncated);
}

void page_load_timer_cb(lv_timer_t* /*timer*/)
{
    if (!g_state.root || !lv_obj_is_valid(g_state.root))
    {
        stop_page_load_timer();
        return;
    }
    render_current_page();
}

void render_current_page()
{
    if (!g_state.viewport)
    {
        return;
    }

    const char* address = g_state.current_address;
    NETWORK_PAGE_LOG("render begin address=%s\n", log_text(address));
    if (!address || address[0] == '\0' || std::strcmp(address, "home:/") == 0)
    {
        stop_page_load_timer();
        NETWORK_PAGE_LOG("render route=home announces=%lu favourites=%lu reticulum=%u\n",
                         static_cast<unsigned long>(visible_announce_count()),
                         static_cast<unsigned long>(favourite_count()),
                         reticulum_active() ? 1U : 0U);
        render_home_page();
        return;
    }
    if (std::strcmp(address, "home:/announces") == 0)
    {
        stop_page_load_timer();
        NETWORK_PAGE_LOG("render route=collection type=announces\n");
        render_collection_page(false);
        return;
    }
    if (std::strcmp(address, "home:/favourites") == 0)
    {
        stop_page_load_timer();
        NETWORK_PAGE_LOG("render route=collection type=favourites\n");
        render_collection_page(true);
        return;
    }

    char destination[kHashTextLen] = {};
    if (extract_destination_text(address, destination, sizeof(destination)))
    {
        RemotePageAddress page_address{};
        const auto* announce =
            find_announce_by_destination_text(destination);
        const auto* known_address =
            find_address_by_destination_text(destination);
        NETWORK_PAGE_LOG("address parsed address=%s dest=%s announce=%u contact=%u\n",
                         address,
                         destination,
                         announce ? 1U : 0U,
                         known_address ? 1U : 0U);
        if (!parse_remote_page_address(address, page_address))
        {
            NETWORK_PAGE_LOG("parse failed address=%s dest=%s\n", address, destination);
            rtpage::Status invalid_status{};
            copy_text(invalid_status.message,
                      sizeof(invalid_status.message),
                      "Invalid Nomad page path");
            copy_text(invalid_status.detail, sizeof(invalid_status.detail), address);
            render_remote_page_shell(address,
                                     nullptr,
                                     announce,
                                     known_address,
                                     &invalid_status,
                                     nullptr,
                                     nullptr);
            return;
        }

        std::size_t body_len = 0;
        if (cached_page_matches(page_address))
        {
            NETWORK_PAGE_LOG("render cached-local dest=%s path=%s body=%lu truncated=%u\n",
                             page_address.destination,
                             page_address.path,
                             static_cast<unsigned long>(g_state.cached_page_body_len),
                             g_state.cached_page_status.truncated ? 1U : 0U);
            rtpage::clear_request_progress(page_address.destination,
                                           page_address.path);
            stop_page_load_timer();
            render_cached_page(g_state.cached_page_status,
                               g_state.cached_page_body_len);
            return;
        }

        NETWORK_PAGE_LOG("cache poll begin dest=%s path=%s\n",
                         page_address.destination,
                         page_address.path);
        const rtpage::Status cache_status =
            rtpage::poll_cached_page_load(page_address.destination,
                                          page_address.path,
                                          g_state.page_body.data(),
                                          g_state.page_body.size(),
                                          &body_len);
        log_page_status("cache poll result",
                        page_address,
                        cache_status,
                        body_len,
                        0);
        if (cache_status.cache_checked && !cache_status.busy)
        {
            g_state.page_cache_load_requested = false;
        }
        if (cache_status.loaded)
        {
            g_state.cached_page_body_valid = true;
            g_state.page_cache_load_requested = false;
            g_state.cached_page_status = cache_status;
            g_state.cached_page_body_len = body_len;
            copy_text(g_state.cached_page_destination,
                      sizeof(g_state.cached_page_destination),
                      page_address.destination);
            copy_text(g_state.cached_page_path,
                      sizeof(g_state.cached_page_path),
                      page_address.path);
            NETWORK_PAGE_LOG("render cached-async dest=%s path=%s body=%lu truncated=%u\n",
                             page_address.destination,
                             page_address.path,
                             static_cast<unsigned long>(body_len),
                             cache_status.truncated ? 1U : 0U);
            rtpage::clear_request_progress(page_address.destination,
                                           page_address.path);
            stop_page_load_timer();
            render_cached_page(cache_status, body_len);
            return;
        }

        rtpage::Status active_cache_status = cache_status;
        rtpage::Status request_status{};
        rtpage::RequestProgress progress =
            rtpage::get_request_progress(page_address.destination,
                                         page_address.path);
        bool cache_poll_requested = false;

        if (!cache_status.cache_checked && !cache_status.busy &&
            !g_state.page_cache_load_requested)
        {
            NETWORK_PAGE_LOG("cache async request dest=%s path=%s\n",
                             page_address.destination,
                             page_address.path);
            active_cache_status =
                rtpage::request_cached_page_load(page_address.destination,
                                                 page_address.path);
            g_state.page_cache_load_requested = active_cache_status.busy;
            log_page_status("cache async request result",
                            page_address,
                            active_cache_status,
                            0,
                            0);
            cache_poll_requested = active_cache_status.busy;
        }
        else if (cache_status.busy)
        {
            cache_poll_requested = true;
        }

        if (active_cache_status.cache_checked && !active_cache_status.loaded &&
            !progress.active && !progress.complete &&
            progress.failure == rtpage::RequestProgress::FailureKind::None)
        {
            NETWORK_PAGE_LOG("request begin dest=%s path=%s\n",
                             page_address.destination,
                             page_address.path);
            const uint32_t request_started_ms = lv_tick_get();
            request_status =
                rtpage::request_page(page_address.destination, page_address.path);
            log_page_status("request result",
                            page_address,
                            request_status,
                            0,
                            lv_tick_elaps(request_started_ms));
            progress = rtpage::get_request_progress(page_address.destination,
                                                    page_address.path);
        }

        if (progress.complete && !g_state.page_cache_load_requested)
        {
            active_cache_status =
                rtpage::request_cached_page_load(page_address.destination,
                                                 page_address.path,
                                                 true);
            g_state.page_cache_load_requested = active_cache_status.busy;
            cache_poll_requested = cache_poll_requested ||
                                   active_cache_status.busy;
        }

        const bool progress_needs_poll =
            progress.active || progress.complete ||
            request_progress_retryable_failed(progress);
        const bool terminal_failed =
            request_progress_terminal_failed(progress);
        const bool should_poll =
            cache_poll_requested || g_state.page_cache_load_requested ||
            request_status.request_started || progress_needs_poll;
        if (terminal_failed && !cache_poll_requested &&
            !g_state.page_cache_load_requested)
        {
            stop_page_load_timer();
        }
        else if (should_poll)
        {
            ensure_page_load_timer();
        }
        else
        {
            stop_page_load_timer();
        }

        NETWORK_PAGE_LOG("render shell reason=cache_miss dest=%s path=%s\n",
                         page_address.destination,
                         page_address.path);
        render_remote_page_shell(address,
                                 &page_address,
                                 announce,
                                 known_address,
                                 &active_cache_status,
                                 request_status.message[0] != '\0' ? &request_status : nullptr,
                                 &progress);
        return;
    }

    stop_page_load_timer();
    NETWORK_PAGE_LOG("render shell reason=unrecognized_address address=%s\n", address);
    render_remote_page_shell(address, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
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
    if (context.kind == DirectoryRowKind::BuiltInFavourite)
    {
        if (context.index < (sizeof(kBuiltInFavourites) / sizeof(kBuiltInFavourites[0])))
        {
            navigate_to_address(kBuiltInFavourites[context.index].address, true);
        }
        return;
    }

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
        NETWORK_PAGE_LOG("directory render skipped reason=no_list\n");
        return;
    }
    lv_obj_clean(g_state.directory_list);
    g_state.row_context_count = 0;
    update_tab_state();

    std::size_t visible = 0;
    if (g_state.directory_mode == DirectoryMode::Favourites)
    {
        for (std::size_t i = 0; i < (sizeof(kBuiltInFavourites) / sizeof(kBuiltInFavourites[0]));
             ++i)
        {
            const auto& favourite = kBuiltInFavourites[i];
            if (!built_in_favourite_matches_search(favourite))
            {
                continue;
            }
            create_directory_row(favourite.title,
                                 favourite.meta,
                                 favourite.icon,
                                 DirectoryRowKind::BuiltInFavourite,
                                 i);
            ++visible;
        }
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
            create_directory_row(address_display_label(address, "Anonymous Peer"),
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
            if (!announce_visible_in_directory(announce) ||
                !announce_matches_search(announce))
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
            create_directory_row(announce_display_label(announce, destination),
                                 meta,
                                 "A",
                                 DirectoryRowKind::Announce,
                                 i);
            ++visible;
        }
    }

    NETWORK_PAGE_LOG(
        "directory render mode=%s total=%lu visible=%lu search=\"%s\" announce_status=\"%s\" address_status=\"%s\"\n",
        directory_mode_label(g_state.directory_mode),
        static_cast<unsigned long>(g_state.directory_mode == DirectoryMode::Favourites
                                       ? favourite_count()
                                       : g_state.announce_count),
        static_cast<unsigned long>(visible),
        g_state.search_query,
        g_state.announce_status.message,
        g_state.address_status.message);

    if (visible == 0)
    {
        render_empty_directory(g_state.search_query[0] != '\0'
                                   ? "No matches"
                                   : (g_state.directory_mode == DirectoryMode::Favourites
                                          ? "No favourites"
                                          : "No Nomad nodes"));
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

void directory_rail_event_cb(lv_event_t* event)
{
    if (event && lv_event_get_code(event) == LV_EVENT_KEY &&
        lv_event_get_key(event) == LV_KEY_LEFT)
    {
        focus_browser_viewport();
        lv_event_stop_processing(event);
        return;
    }
    if (!activation_event(event))
    {
        return;
    }
    open_node_list_page();
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
    if (event && lv_event_get_code(event) == LV_EVENT_KEY)
    {
        const uint32_t key = lv_event_get_key(event);
        lv_obj_t* target = lv_event_get_target_obj(event);
        if ((key == LV_KEY_RIGHT && object_in_subtree(g_state.directory_panel, target)) ||
            (key == LV_KEY_LEFT && object_in_subtree(g_state.browser_rail, target)))
        {
            focus_browser_viewport();
            lv_event_stop_processing(event);
            return;
        }
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
    if (g_state.browser_rail)
    {
        immersive ? lv_obj_add_flag(g_state.browser_rail, LV_OBJ_FLAG_HIDDEN)
                  : lv_obj_clear_flag(g_state.browser_rail, LV_OBJ_FLAG_HIDDEN);
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
    if (g_state.browser_body)
    {
        lv_obj_set_style_pad_column(g_state.browser_body, immersive ? 0 : 3, LV_PART_MAIN);
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

void open_node_list_page()
{
    if (g_state.immersive)
    {
        g_state.immersive = false;
    }
    g_state.directory_collapsed = true;
    g_state.browser_collapsed = false;
    apply_layout_state();
    navigate_to_address("home:/announces", true);
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
    (void)key;
    return false;
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

    ::ui::components::shortcut_help_modal::Row rows[10] = {};
    std::size_t row_count = 0;
    rows[row_count++] = {"S", "/", "Search announces"};
    rows[row_count++] = {"C", nullptr, "Nodes"};
    rows[row_count++] = {"I", nullptr, "Immersive browser"};
    rows[row_count++] = {"Rotary", nullptr, "Move focus"};
    rows[row_count++] = {"Enter", nullptr, "Open or lock page scroll"};
    rows[row_count++] = {"Back", nullptr, "Unlock scroll or return"};
    rows[row_count++] = {"R", nullptr, "Refresh network"};
    rows[row_count++] = {"F", nullptr, "Favourites"};
    rows[row_count++] = {"A", nullptr, "Nodes"};
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

lv_coord_t viewport_page_scroll_step()
{
    if (!g_state.viewport || !lv_obj_is_valid(g_state.viewport))
    {
        return kViewportScrollStep;
    }
    const lv_coord_t height = lv_obj_get_height(g_state.viewport);
    const lv_coord_t page_step = height - kViewportPageScrollPadding;
    return page_step > kViewportScrollStep ? page_step : kViewportScrollStep;
}

bool viewport_scroll_delta_for_key(uint32_t key, lv_coord_t* delta)
{
    if (!delta)
    {
        return false;
    }
    if (key == LV_KEY_UP || key == kPagerRotateUpKey)
    {
        *delta = -kViewportScrollStep;
        return true;
    }
    if (key == LV_KEY_DOWN || key == kPagerRotateDownKey)
    {
        *delta = kViewportScrollStep;
        return true;
    }
    if (key == LV_KEY_PREV)
    {
        *delta = -viewport_page_scroll_step();
        return true;
    }
    if (key == LV_KEY_NEXT)
    {
        *delta = viewport_page_scroll_step();
        return true;
    }
    return false;
}

bool scroll_viewport_by_key(lv_event_t* event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_KEY || !g_state.viewport ||
        !lv_obj_is_valid(g_state.viewport))
    {
        return false;
    }
    lv_coord_t delta = 0;
    if (!viewport_scroll_delta_for_key(lv_event_get_key(event), &delta) || delta == 0)
    {
        return false;
    }
    lv_obj_scroll_by(g_state.viewport, 0, delta, LV_ANIM_OFF);
    lv_event_stop_bubbling(event);
    lv_event_stop_processing(event);
    return true;
}

bool scroll_viewport_by_rotary(lv_event_t* event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_ROTARY || !g_state.viewport ||
        !lv_obj_is_valid(g_state.viewport))
    {
        return false;
    }
    const int32_t diff = lv_event_get_rotary_diff(event);
    if (diff == 0)
    {
        return false;
    }
    const lv_coord_t step =
        static_cast<lv_coord_t>(kViewportScrollStep *
                                (diff > 0 ? diff : -diff));
    lv_obj_scroll_by(g_state.viewport, 0, diff > 0 ? -step : step, LV_ANIM_OFF);
    lv_event_stop_bubbling(event);
    lv_event_stop_processing(event);
    return true;
}

void lock_viewport_scroll(lv_event_t* event)
{
    if (!app_g || !g_state.viewport || !lv_obj_is_valid(g_state.viewport))
    {
        return;
    }
    lv_group_focus_obj(g_state.viewport);
    lv_group_set_editing(app_g, true);
    if (event)
    {
        lv_event_stop_bubbling(event);
        lv_event_stop_processing(event);
    }
}

bool pager_focus_key(uint32_t key)
{
    return key == kPagerRotateUpKey || key == kPagerRotateDownKey;
}

bool move_focus_by_pager_key(lv_event_t* event)
{
    if (!event || lv_event_get_code(event) != LV_EVENT_KEY || !app_g ||
        lv_group_get_editing(app_g))
    {
        return false;
    }
    const uint32_t key = lv_event_get_key(event);
    if (!pager_focus_key(key))
    {
        return false;
    }
    if (key == kPagerRotateUpKey)
    {
        lv_group_focus_prev(app_g);
    }
    else
    {
        lv_group_focus_next(app_g);
    }
    lv_event_stop_bubbling(event);
    lv_event_stop_processing(event);
    return true;
}

bool viewport_is_editing()
{
    return app_g && lv_group_get_focused(app_g) == g_state.viewport &&
           lv_group_get_editing(app_g);
}

void viewport_event_cb(lv_event_t* event)
{
    if (!event)
    {
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_FOCUSED)
    {
        if (app_g)
        {
            lv_group_set_editing(app_g, false);
        }
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_DEFOCUSED)
    {
        if (app_g)
        {
            lv_group_set_editing(app_g, false);
        }
        return;
    }
    if ((lv_event_get_code(event) == LV_EVENT_CLICKED ||
         lv_event_get_code(event) == LV_EVENT_PRESSED) &&
        lv_event_get_target_obj(event) == g_state.viewport)
    {
        lock_viewport_scroll(event);
        return;
    }
    if (lv_event_get_code(event) == LV_EVENT_ROTARY)
    {
        if (viewport_is_editing())
        {
            (void)scroll_viewport_by_rotary(event);
        }
        return;
    }
    if (lv_event_get_code(event) != LV_EVENT_KEY || !app_g)
    {
        return;
    }

    const uint32_t key = lv_event_get_key(event);
    if (key == LV_KEY_ENTER)
    {
        lock_viewport_scroll(event);
        return;
    }
    if (viewport_is_editing() && (key == LV_KEY_BACKSPACE || key == LV_KEY_ESC))
    {
        lv_group_set_editing(app_g, false);
        lv_event_stop_bubbling(event);
        lv_event_stop_processing(event);
        return;
    }
    if (viewport_is_editing())
    {
        (void)scroll_viewport_by_key(event);
    }
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
    if (move_focus_by_pager_key(event))
    {
        return;
    }
    if (is_directory_toggle_shortcut_key(key))
    {
        open_node_list_page();
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
        if (!g_state.directory_collapsed)
        {
            focus_directory_panel();
            lv_event_stop_processing(event);
        }
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
        navigate_to_address("home:/favourites", true);
        lv_event_stop_processing(event);
        return;
    }
    if (key == 'a' || key == 'A')
    {
        open_node_list_page();
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
    const lv_coord_t rail_button_size = profile.dense ? 20 : 22;

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
    lv_obj_add_event_cb(g_state.address_area, page_shortcut_event_cb, LV_EVENT_KEY, nullptr);
    add_to_group(g_state.address_area);

    g_state.go_btn =
        create_icon_button(g_state.browser_toolbar, LV_SYMBOL_RIGHT, go_event_cb, button_size);

    g_state.browser_body = lv_obj_create(g_state.browser_panel);
    lv_obj_set_width(g_state.browser_body, LV_PCT(100));
    lv_obj_set_height(g_state.browser_body, 0);
    lv_obj_set_flex_grow(g_state.browser_body, 1);
    style_plain_container(g_state.browser_body, kPanelBg, LV_OPA_TRANSP);
    lv_obj_set_style_pad_all(g_state.browser_body, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(g_state.browser_body, 3, LV_PART_MAIN);
    lv_obj_set_flex_flow(g_state.browser_body, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_state.browser_body,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    ::ui::components::two_pane_layout::make_non_scrollable(g_state.browser_body);

    g_state.viewport = lv_obj_create(g_state.browser_body);
    lv_obj_set_width(g_state.viewport, 0);
    lv_obj_set_height(g_state.viewport, LV_PCT(100));
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
    style_focusable(g_state.viewport);
    lv_obj_add_event_cb(g_state.viewport, viewport_event_cb, LV_EVENT_FOCUSED, nullptr);
    lv_obj_add_event_cb(g_state.viewport, viewport_event_cb, LV_EVENT_DEFOCUSED, nullptr);
    lv_obj_add_event_cb(g_state.viewport, viewport_event_cb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(g_state.viewport, viewport_event_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(g_state.viewport, viewport_event_cb, LV_EVENT_ROTARY, nullptr);
    lv_obj_add_event_cb(g_state.viewport, viewport_event_cb, LV_EVENT_KEY, nullptr);
    lv_obj_add_event_cb(g_state.viewport, page_shortcut_event_cb, LV_EVENT_KEY, nullptr);
    add_to_group(g_state.viewport);

    g_state.browser_rail = lv_obj_create(g_state.browser_body);
    lv_obj_set_width(g_state.browser_rail, rail_button_size);
    lv_obj_set_height(g_state.browser_rail, LV_PCT(100));
    style_plain_container(g_state.browser_rail, kPanelBg, LV_OPA_TRANSP);
    lv_obj_set_style_pad_all(g_state.browser_rail, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(g_state.browser_rail, profile.dense ? 2 : 3, LV_PART_MAIN);
    lv_obj_set_flex_flow(g_state.browser_rail, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(g_state.browser_rail,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    ::ui::components::two_pane_layout::make_non_scrollable(g_state.browser_rail);

    g_state.browser_back_btn = create_icon_button(g_state.browser_rail,
                                                  LV_SYMBOL_LEFT,
                                                  browser_back_event_cb,
                                                  rail_button_size);
    g_state.home_btn = create_icon_button(g_state.browser_rail,
                                          LV_SYMBOL_HOME,
                                          home_event_cb,
                                          rail_button_size);
    g_state.refresh_btn = create_icon_button(g_state.browser_rail,
                                             LV_SYMBOL_REFRESH,
                                             refresh_event_cb,
                                             rail_button_size);
    g_state.rail_directory_btn = create_icon_button(g_state.browser_rail,
                                                    LV_SYMBOL_LIST,
                                                    directory_rail_event_cb,
                                                    rail_button_size);
    g_state.rail_search_btn = create_icon_button(g_state.browser_rail,
                                                 LV_SYMBOL_SEARCH,
                                                 search_button_event_cb,
                                                 rail_button_size);
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
    apply_layout_state();

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
    NETWORK_PAGE_LOG("exit begin root=%u rows=%u links=%u history=%u\n",
                     g_state.root && lv_obj_is_valid(g_state.root) ? 1U : 0U,
                     static_cast<unsigned>(g_state.row_context_count),
                     static_cast<unsigned>(g_state.link_context_count),
                     static_cast<unsigned>(g_state.history_count));
    if (g_state.page_load_timer)
    {
        lv_timer_del(g_state.page_load_timer);
        g_state.page_load_timer = nullptr;
    }
    ::ui::components::floating_search_box::close(g_state.search_box);
    close_network_help_modal();
    if (app_g)
    {
        lv_group_remove_all_objs(app_g);
    }
    if (g_state.root && lv_obj_is_valid(g_state.root))
    {
        lv_obj_del(g_state.root);
    }
    reset_state_after_destroy();
    NETWORK_PAGE_LOG("exit done\n");
}

} // namespace network::ui::shell
