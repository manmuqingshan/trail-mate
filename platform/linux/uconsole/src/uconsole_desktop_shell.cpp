#include "uconsole/uconsole_desktop_shell.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <future>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "app/input_event.h"
#include "app/linux_app_services.h"
#include "core/canvas.h"
#include "lvgl.h"
#include "uconsole/uconsole_chat_workspace_model.h"
#include "uconsole/uconsole_dashboard_model.h"
#include "uconsole/uconsole_map_workspace_model.h"
#include "ui_map_runtime/map_tiles/map_tile_types.h"

namespace trailmate::uconsole
{
namespace
{

using clock = std::chrono::steady_clock;
using InputEvent = cardputer_zero::app::InputEvent;
using InputKey = cardputer_zero::app::InputKey;
using Canvas = cardputer_zero::core::Canvas;

constexpr int kConversationRows = 6;
constexpr int kContactRows = 7;
constexpr int kMetricCount = 4;
constexpr int kCapabilityRows = 5;
constexpr int kChatConversationRows = 7;
constexpr int kChatMessageRows = 7;
constexpr std::uint32_t kLvglFunctionKeyF1 = 0x110001U;
constexpr int kMapTileDisplaySize = 150;
constexpr std::size_t kMaxMapPreviewTiles = 121;

namespace embedded_palette
{

constexpr std::uint32_t kPageBg = 0xFFF3DF;
constexpr std::uint32_t kSurface = 0xFFF7E9;
constexpr std::uint32_t kSurfaceAlt = 0xFFF0D3;
constexpr std::uint32_t kPanelBg = 0xFAF0D8;
constexpr std::uint32_t kBorder = 0xD9B06A;
constexpr std::uint32_t kSeparator = 0xE8D2AB;
constexpr std::uint32_t kAccent = 0xEBA341;
constexpr std::uint32_t kAccentDark = 0xC98118;
constexpr std::uint32_t kHeaderText = 0x2A1A05;
constexpr std::uint32_t kText = 0x3A2A1A;
constexpr std::uint32_t kTextWarm = 0x6B4A1E;
constexpr std::uint32_t kTextMuted = 0x6A5646;
constexpr std::uint32_t kTextDim = 0x8A6A3A;
constexpr std::uint32_t kWhite = 0xFFFFFF;
constexpr std::uint32_t kStatusGreen = 0x5BAF4A;
constexpr std::uint32_t kStatusBlue = 0x2F6FD6;
constexpr std::uint32_t kWarn = 0xB94A2C;
constexpr std::uint32_t kError = 0xCC0000;
constexpr std::uint32_t kSoftAmber = 0xF3D39C;
constexpr std::uint32_t kSoftBlue = 0xDCE8F7;
constexpr std::uint32_t kSoftGreen = 0xDCEFD8;
constexpr std::uint32_t kSoftWarn = 0xF5D9D1;
constexpr std::uint32_t kMapBg = 0xF6E7C8;
constexpr std::uint32_t kPlotBg = 0xF2E4C8;
constexpr std::uint32_t kMapTile1 = 0xEAD9B2;
constexpr std::uint32_t kMapTile2 = 0xF2E4C8;
constexpr std::uint32_t kMapTile3 = 0xE4D2AA;
constexpr std::uint32_t kMapTile4 = 0xE0C894;
constexpr std::uint32_t kMapTile5 = 0xEEDBB4;
constexpr std::uint32_t kMapTile6 = 0xE7D3A4;
constexpr std::uint32_t kMapTile7 = 0xF3E2BE;
constexpr std::uint32_t kMapTile8 = 0xDEC58F;

} // namespace embedded_palette

constexpr std::array<const char*, 13> kNavLabels{
    "Overview",
    "Chat",
    "Map",
    "Contacts",
    "GPS & sky plot",
    "Team",
    "Tracker",
    "Radio tools",
    "Hardware",
    "Data & maps",
    "Extensions",
    "Logs",
    "Settings",
};

std::chrono::steady_clock::time_point g_lvgl_start_time = clock::now();

enum class Section : std::uint8_t
{
    Overview = 0,
    Chat,
    Map,
    Contacts,
    Gps,
    Team,
    Tracker,
    RadioTools,
    Hardware,
    Data,
    Extensions,
    Logs,
    Settings,
};

struct QueuedKeyEvent
{
    std::uint32_t key = 0;
    lv_indev_state_t state = LV_INDEV_STATE_RELEASED;
};

[[nodiscard]] std::uint32_t tickNow() noexcept
{
    return static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            clock::now() - g_lvgl_start_time)
            .count());
}

[[nodiscard]] std::uint32_t mapInputEvent(const InputEvent& event) noexcept
{
    switch (event.key)
    {
    case InputKey::Character:
        return event.text == '\0' ? 0U : static_cast<std::uint8_t>(event.text);
    case InputKey::Backspace:
        return LV_KEY_BACKSPACE;
    case InputKey::Enter:
        return LV_KEY_ENTER;
    case InputKey::Tab:
        return LV_KEY_NEXT;
    case InputKey::Home:
        return LV_KEY_ESC;
    case InputKey::Next:
        return LV_KEY_NEXT;
    case InputKey::Power:
        return LV_KEY_ESC;
    case InputKey::Left:
        return LV_KEY_LEFT;
    case InputKey::Right:
        return LV_KEY_RIGHT;
    case InputKey::Up:
        return LV_KEY_UP;
    case InputKey::Down:
        return LV_KEY_DOWN;
    case InputKey::F1:
        return kLvglFunctionKeyF1;
    case InputKey::Unknown:
    case InputKey::Fn:
    case InputKey::Ctrl:
    case InputKey::Alt:
    case InputKey::Shift:
        return 0U;
    }
    return 0U;
}

[[nodiscard]] std::uint8_t expand5(std::uint16_t value) noexcept
{
    return static_cast<std::uint8_t>((value * 255U) / 31U);
}

[[nodiscard]] std::uint8_t expand6(std::uint16_t value) noexcept
{
    return static_cast<std::uint8_t>((value * 255U) / 63U);
}

[[nodiscard]] cardputer_zero::core::Color rgb565ToColor(
    std::uint16_t pixel) noexcept
{
    const auto red = static_cast<std::uint16_t>((pixel >> 11U) & 0x1FU);
    const auto green = static_cast<std::uint16_t>((pixel >> 5U) & 0x3FU);
    const auto blue = static_cast<std::uint16_t>(pixel & 0x1FU);
    return cardputer_zero::core::rgba(expand5(red), expand6(green),
                                      expand5(blue));
}

[[nodiscard]] lv_color_t color(std::uint32_t hex) noexcept
{
    return lv_color_hex(hex);
}

void resetBox(lv_obj_t* obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

void applyPanel(lv_obj_t* obj,
                std::uint32_t bg,
                std::uint32_t border = embedded_palette::kBorder)
{
    resetBox(obj);
    lv_obj_set_style_bg_color(obj, color(bg), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, color(border), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_radius(obj, 6, 0);
    lv_obj_set_style_pad_all(obj, 14, 0);
}

void applyTransparent(lv_obj_t* obj)
{
    resetBox(obj);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
}

lv_obj_t* createLabel(lv_obj_t* parent,
                      const char* text,
                      const lv_font_t* font,
                      std::uint32_t text_color,
                      lv_label_long_mode_t long_mode = LV_LABEL_LONG_DOT)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_style_text_color(label, color(text_color), 0);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_pad_all(label, 0, 0);
    lv_label_set_long_mode(label, long_mode);
    lv_label_set_text(label, text);
    return label;
}

void setLabel(lv_obj_t* label, const std::string& text)
{
    if (label != nullptr)
    {
        lv_label_set_text(label, text.c_str());
    }
}

void setLabel(lv_obj_t* label, const char* text)
{
    if (label != nullptr)
    {
        lv_label_set_text(label, text ? text : "");
    }
}

[[nodiscard]] const char* sectionTitle(Section section) noexcept
{
    switch (section)
    {
    case Section::Overview:
        return "Operational workspace";
    case Section::Chat:
        return "Chat workspace";
    case Section::Contacts:
        return "Contacts & nodes";
    case Section::Map:
        return "Offline map workspace";
    case Section::Gps:
        return "GPS & sky plot";
    case Section::Team:
        return "Team operations";
    case Section::Tracker:
        return "Track recorder";
    case Section::RadioTools:
        return "Radio tools";
    case Section::Hardware:
        return "Hardware status";
    case Section::Data:
        return "Data & offline maps";
    case Section::Extensions:
        return "Extensions";
    case Section::Logs:
        return "Packet and runtime logs";
    case Section::Settings:
        return "Settings workspace";
    }
    return "Operational workspace";
}

[[nodiscard]] const char* sectionSubtitle(Section section) noexcept
{
    switch (section)
    {
    case Section::Overview:
        return "Live service snapshot for the Linux handheld target.";
    case Section::Chat:
        return "Conversation list and message activity preview.";
    case Section::Contacts:
        return "Saved identities, nearby peers, trust and node actions.";
    case Section::Map:
        return "Cached tiles, background downloads, contours and field overlays.";
    case Section::Gps:
        return "Live receiver health, satellite geometry and fix details.";
    case Section::Team:
        return "Shared field state, team chat and location coordination.";
    case Section::Tracker:
        return "Persistent GPX, CSV or binary recording on Linux storage.";
    case Section::RadioTools:
        return "Energy sweep, SSTV receiver and walkie controls in one workbench.";
    case Section::Hardware:
        return "uConsole modules and shared capability status.";
    case Section::Data:
        return "Map cache, downloads, tracks and local application storage.";
    case Section::Extensions:
        return "Install and remove packages from the Trail Mate catalog.";
    case Section::Logs:
        return "Inspect decoded LoRa, GPS and MQTT traffic.";
    case Section::Settings:
        return "Grouped configuration entrypoint for Linux targets.";
    }
    return "";
}

[[nodiscard]] std::string formatCount(std::size_t value)
{
    char buffer[24] = {};
    std::snprintf(buffer, sizeof(buffer), "%lu",
                  static_cast<unsigned long>(value));
    return buffer;
}

[[nodiscard]] std::string formatCount(int value)
{
    char buffer[24] = {};
    std::snprintf(buffer, sizeof(buffer), "%d", value);
    return buffer;
}

[[nodiscard]] UConsoleShellOptions validateOptions(UConsoleShellOptions options)
{
    if (options.width <= 0 || options.height <= 0)
    {
        throw std::runtime_error("uConsole shell dimensions must be positive.");
    }
    if (options.frame_time_ms <= 0)
    {
        options.frame_time_ms = 16;
    }
    return options;
}

class UConsoleDesktopShell
{
  public:
    UConsoleDesktopShell()
        : services_(),
          dashboard_model_(services_),
          chat_model_(services_),
          map_model_(services_)
    {
    }

    ~UConsoleDesktopShell()
    {
        services_.shutdown();
    }

    bool begin()
    {
        if (initialized_) return true;
        if (!services_.initialize()) return false;

        group_ = lv_group_create();
        if (group_ == nullptr)
        {
            return false;
        }
        lv_group_set_default(group_);

        buildUi();
        initialized_ = true;
        refreshDashboard(true);
        return true;
    }

    void releaseLvglObjects() noexcept
    {
        if (group_ != nullptr)
        {
            lv_group_del(group_);
            group_ = nullptr;
        }
    }

    void tick()
    {
        if (!initialized_) return;

        services_.tick();
        refreshMapPreview(false);
        const auto now = clock::now();
        if ((now - last_refresh_) >= std::chrono::milliseconds(500))
        {
            refreshDashboard(false);
        }
    }

    void enqueueInputs(const std::vector<InputEvent>& events)
    {
        for (const auto& event : events)
        {
            if (handleGlobalShortcut(event))
            {
                continue;
            }
            const std::uint32_t mapped = mapInputEvent(event);
            if (mapped == 0U) continue;
            key_events_.push_back({mapped, LV_INDEV_STATE_PRESSED});
            key_events_.push_back({mapped, LV_INDEV_STATE_RELEASED});
        }
    }

    [[nodiscard]] lv_group_t* inputGroup() const noexcept
    {
        return group_;
    }

    bool dequeueKeyEvent(std::uint32_t* key, lv_indev_state_t* state)
    {
        if (key_events_.empty()) return false;
        const QueuedKeyEvent event = key_events_.front();
        key_events_.pop_front();
        *key = event.key;
        *state = event.state;
        return true;
    }

    [[nodiscard]] bool hasPendingKeyEvent() const noexcept
    {
        return !key_events_.empty();
    }

  private:
    struct NavBinding
    {
        UConsoleDesktopShell* shell = nullptr;
        Section section = Section::Overview;
    };

    struct ChatConversationBinding
    {
        UConsoleDesktopShell* shell = nullptr;
        std::size_t index = 0;
    };

    static void navEventCb(lv_event_t* event)
    {
        const lv_event_code_t code = lv_event_get_code(event);
        if (code != LV_EVENT_CLICKED && code != LV_EVENT_KEY)
        {
            return;
        }

        if (code == LV_EVENT_KEY)
        {
            const std::uint32_t key = lv_event_get_key(event);
            if (key != LV_KEY_ENTER && key != LV_KEY_RIGHT)
            {
                return;
            }
        }

        auto* binding =
            static_cast<NavBinding*>(lv_event_get_user_data(event));
        if (binding == nullptr || binding->shell == nullptr)
        {
            return;
        }
        binding->shell->selectSection(binding->section);
    }

    static void chatConversationEventCb(lv_event_t* event)
    {
        const lv_event_code_t code = lv_event_get_code(event);
        if (code != LV_EVENT_CLICKED && code != LV_EVENT_KEY)
        {
            return;
        }

        if (code == LV_EVENT_KEY)
        {
            const std::uint32_t key = lv_event_get_key(event);
            if (key != LV_KEY_ENTER && key != LV_KEY_RIGHT)
            {
                return;
            }
        }

        auto* binding =
            static_cast<ChatConversationBinding*>(lv_event_get_user_data(event));
        if (binding == nullptr || binding->shell == nullptr)
        {
            return;
        }

        if (binding->shell->chat_model_.selectConversationAt(
                binding->index, kChatConversationRows))
        {
            binding->shell->refreshDashboard(true);
        }
    }

    static void chatSendEventCb(lv_event_t* event)
    {
        const lv_event_code_t code = lv_event_get_code(event);
        if (code != LV_EVENT_CLICKED && code != LV_EVENT_KEY)
        {
            return;
        }

        if (code == LV_EVENT_KEY)
        {
            const std::uint32_t key = lv_event_get_key(event);
            if (key != LV_KEY_ENTER)
            {
                return;
            }
        }

        auto* shell =
            static_cast<UConsoleDesktopShell*>(lv_event_get_user_data(event));
        if (shell == nullptr || shell->chat_input_ == nullptr)
        {
            return;
        }

        const char* text = lv_textarea_get_text(shell->chat_input_);
        const bool sent = shell->chat_model_.sendText(text == nullptr ? "" : text);
        if (sent)
        {
            lv_textarea_set_text(shell->chat_input_, "");
        }
        shell->refreshDashboard(true);
        shell->refreshChatWorkspace(true);
    }

    bool handleGlobalShortcut(const InputEvent& event)
    {
        if (event.key == InputKey::F1)
        {
            toggleShortcutOverlay();
            return true;
        }

        if (event.key == InputKey::Power)
        {
            if (shortcuts_visible_)
            {
                toggleShortcutOverlay();
                return true;
            }
            if (active_section_ != Section::Overview)
            {
                selectSection(Section::Overview);
                return true;
            }
            return false;
        }

        if (event.key != InputKey::Character || event.text == '\0')
        {
            return false;
        }

        const bool editing_chat =
            active_section_ == Section::Chat && group_ != nullptr &&
            lv_group_get_focused(group_) == chat_input_;
        if (editing_chat)
        {
            return false;
        }

        const char key = static_cast<char>(
            std::tolower(static_cast<unsigned char>(event.text)));
        if (shortcuts_visible_)
        {
            if (key == 'h' || key == '?')
            {
                toggleShortcutOverlay();
            }
            return true;
        }

        if (active_section_ == Section::Map &&
            handleMapShortcut(key))
        {
            return true;
        }

        switch (key)
        {
        case 'h':
        case '?':
            toggleShortcutOverlay();
            return true;
        case '\\':
            toggleSidebar();
            return true;
        case '[':
            cycleSection(-1);
            return true;
        case ']':
            cycleSection(1);
            return true;
        case 'o':
            selectSection(Section::Overview);
            return true;
        case 'c':
            selectSection(Section::Chat);
            return true;
        case 'm':
            selectSection(Section::Map);
            return true;
        case 'n':
            selectSection(Section::Contacts);
            return true;
        case 'g':
            selectSection(Section::Gps);
            return true;
        case 't':
            selectSection(Section::Team);
            return true;
        case 'k':
            selectSection(Section::Tracker);
            return true;
        case 'r':
            selectSection(Section::RadioTools);
            return true;
        case 'w':
            selectSection(Section::Hardware);
            return true;
        case 'd':
            selectSection(Section::Data);
            return true;
        case 'e':
            selectSection(Section::Extensions);
            return true;
        case 'l':
            selectSection(Section::Logs);
            return true;
        case 's':
            selectSection(Section::Settings);
            return true;
        default:
            return false;
        }
    }

    bool handleMapShortcut(char key)
    {
        const auto snapshot = map_model_.snapshot();
        constexpr int kMapDisplayWidth = 750;
        constexpr int kMapDisplayHeight = 450;
        constexpr double kPanStep = 64.0;

        switch (key)
        {
        case 'w':
            map_model_.panByDisplayDelta(0.0,
                                         kPanStep,
                                         kMapDisplayWidth,
                                         kMapDisplayHeight,
                                         snapshot.lat,
                                         snapshot.lon,
                                         snapshot.zoom,
                                         true);
            break;
        case 'a':
            map_model_.panByDisplayDelta(kPanStep,
                                         0.0,
                                         kMapDisplayWidth,
                                         kMapDisplayHeight,
                                         snapshot.lat,
                                         snapshot.lon,
                                         snapshot.zoom,
                                         true);
            break;
        case 's':
            map_model_.panByDisplayDelta(0.0,
                                         -kPanStep,
                                         kMapDisplayWidth,
                                         kMapDisplayHeight,
                                         snapshot.lat,
                                         snapshot.lon,
                                         snapshot.zoom,
                                         true);
            break;
        case 'd':
            map_model_.panByDisplayDelta(-kPanStep,
                                         0.0,
                                         kMapDisplayWidth,
                                         kMapDisplayHeight,
                                         snapshot.lat,
                                         snapshot.lon,
                                         snapshot.zoom,
                                         true);
            break;
        case 'q':
            map_model_.zoomOut();
            break;
        case 'e':
            map_model_.zoomIn();
            break;
        case 'c':
            map_model_.clearManualCenter();
            break;
        case 'l':
            if (snapshot.source_label == "OSM")
            {
                map_model_.setSource(
                    ::platform::linux_runtime::MapBaseSource::Terrain);
            }
            else if (snapshot.source_label == "Terrain")
            {
                map_model_.setSource(
                    ::platform::linux_runtime::MapBaseSource::Satellite);
            }
            else
            {
                map_model_.setSource(
                    ::platform::linux_runtime::MapBaseSource::Osm);
            }
            break;
        case 'o':
            map_model_.setContourEnabled(!snapshot.contour_enabled);
            break;
        case 'm':
        {
            const auto active_tool =
                snapshot.presentation_workspace.active_tool;
            map_model_.presentationModel().setActiveTool(
                active_tool == ::ui::map::MapToolKind::MeasureDistance
                    ? ::ui::map::MapToolKind::Pan
                    : ::ui::map::MapToolKind::MeasureDistance);
            break;
        }
        default:
            return false;
        }

        refreshMapPreview(true);
        refreshFooter();
        return true;
    }

    void cycleSection(int delta)
    {
        const int section_count = static_cast<int>(kNavLabels.size());
        const int current = static_cast<int>(active_section_);
        const int next = (current + delta + section_count) % section_count;
        selectSection(static_cast<Section>(next));
    }

    void toggleSidebar()
    {
        if (sidebar_ == nullptr)
        {
            return;
        }
        sidebar_collapsed_ = !sidebar_collapsed_;
        if (sidebar_collapsed_)
        {
            lv_obj_add_flag(sidebar_, LV_OBJ_FLAG_HIDDEN);
        }
        else
        {
            lv_obj_clear_flag(sidebar_, LV_OBJ_FLAG_HIDDEN);
            const auto index = static_cast<std::size_t>(active_section_);
            if (index < nav_buttons_.size() && nav_buttons_[index] != nullptr)
            {
                lv_group_focus_obj(nav_buttons_[index]);
            }
        }
        refreshFooter();
    }

    void toggleShortcutOverlay()
    {
        if (shortcut_overlay_ == nullptr)
        {
            return;
        }
        if (shortcuts_visible_)
        {
            shortcuts_visible_ = false;
            lv_obj_add_flag(shortcut_overlay_, LV_OBJ_FLAG_HIDDEN);
            return;
        }

        lv_obj_del(shortcut_overlay_);
        shortcut_overlay_ = nullptr;
        buildShortcutOverlay(lv_screen_active());
        shortcuts_visible_ = true;
        if (shortcut_overlay_ != nullptr)
        {
            lv_obj_clear_flag(shortcut_overlay_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(shortcut_overlay_);
        }
    }

    void selectSection(Section section)
    {
        active_section_ = section;
        refreshNavStyles();
        setLabel(workspace_title_, sectionTitle(active_section_));
        setLabel(workspace_subtitle_, sectionSubtitle(active_section_));
        if (active_section_ != Section::Overview &&
            active_section_ != Section::Chat)
        {
            rebuildDesktopPage();
        }
        refreshSectionVisibility();
        if (active_section_ == Section::Chat)
        {
            refreshChatWorkspace(true);
        }
        refreshFooter();
    }

    void buildUi()
    {
        lv_obj_t* root = lv_scr_act();
        lv_obj_clean(root);
        resetBox(root);
        lv_obj_set_style_bg_color(root, color(embedded_palette::kPageBg), 0);
        lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(root, 0, 0);
        lv_obj_set_flex_flow(root, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);

        buildTopBar(root);

        lv_obj_t* body = lv_obj_create(root);
        applyTransparent(body);
        lv_obj_set_width(body, LV_PCT(100));
        lv_obj_set_flex_grow(body, 1);
        lv_obj_set_style_pad_all(body, 10, 0);
        lv_obj_set_style_pad_column(body, 10, 0);
        lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);

        buildSidebar(body);
        buildWorkspace(body);
        buildStatusPanel(body);
        buildBottomBar(root);
        buildShortcutOverlay(root);

        selectSection(Section::Overview);
    }

    void buildTopBar(lv_obj_t* root)
    {
        lv_obj_t* bar = lv_obj_create(root);
        resetBox(bar);
        lv_obj_set_width(bar, LV_PCT(100));
        lv_obj_set_height(bar, 44);
        lv_obj_set_style_bg_color(bar, color(embedded_palette::kAccent), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_left(bar, 14, 0);
        lv_obj_set_style_pad_right(bar, 14, 0);
        lv_obj_set_style_pad_top(bar, 5, 0);
        lv_obj_set_style_pad_bottom(bar, 5, 0);
        lv_obj_set_style_pad_column(bar, 8, 0);
        lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);

        lv_obj_t* title_wrap = lv_obj_create(bar);
        applyTransparent(title_wrap);
        lv_obj_set_height(title_wrap, LV_PCT(100));
        lv_obj_set_flex_grow(title_wrap, 1);
        lv_obj_set_flex_flow(title_wrap, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(title_wrap, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        createLabel(title_wrap, "Trail Mate uConsole", &lv_font_montserrat_16,
                    embedded_palette::kHeaderText);
    }

    void buildBottomBar(lv_obj_t* root)
    {
        lv_obj_t* bar = lv_obj_create(root);
        resetBox(bar);
        lv_obj_set_width(bar, LV_PCT(100));
        lv_obj_set_height(bar, 30);
        lv_obj_set_style_bg_color(bar,
                                  color(embedded_palette::kSurfaceAlt), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
        lv_obj_set_style_border_width(bar, 1, 0);
        lv_obj_set_style_border_color(bar,
                                      color(embedded_palette::kBorder), 0);
        lv_obj_set_style_pad_left(bar, 12, 0);
        lv_obj_set_style_pad_right(bar, 12, 0);
        lv_obj_set_style_pad_column(bar, 10, 0);
        lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        footer_context_label_ =
            createLabel(bar, "Overview", &lv_font_montserrat_12,
                        embedded_palette::kTextWarm);
        lv_obj_set_width(footer_context_label_, 130);

        footer_status_label_ =
            createLabel(bar, "Starting services...", &lv_font_montserrat_12,
                        embedded_palette::kTextMuted);
        lv_obj_set_width(footer_status_label_, 260);
        lv_obj_set_style_text_align(footer_status_label_,
                                    LV_TEXT_ALIGN_CENTER, 0);

        footer_shortcuts_row_ = lv_obj_create(bar);
        resetBox(footer_shortcuts_row_);
        applyTransparent(footer_shortcuts_row_);
        lv_obj_set_width(footer_shortcuts_row_, 0);
        lv_obj_set_height(footer_shortcuts_row_, LV_PCT(100));
        lv_obj_set_flex_grow(footer_shortcuts_row_, 1);
        lv_obj_set_style_pad_column(footer_shortcuts_row_, 0, 0);
        lv_obj_set_flex_flow(footer_shortcuts_row_, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(footer_shortcuts_row_,
                              LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);
    }

    lv_obj_t* createFooterKeycap(lv_obj_t* parent,
                                 const char* text,
                                 lv_coord_t width)
    {
        lv_obj_t* keycap = lv_label_create(parent);
        lv_obj_set_size(keycap, width, 18);
        lv_obj_set_style_bg_color(keycap, color(0xF8E6C3), 0);
        lv_obj_set_style_bg_opa(keycap, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(keycap, color(0x8A6E43), 0);
        lv_obj_set_style_border_width(keycap, 1, 0);
        lv_obj_set_style_radius(keycap, 3, 0);
        lv_obj_set_style_text_color(keycap, color(0x25170D), 0);
        lv_obj_set_style_text_font(keycap, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_align(keycap, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(keycap, LV_LABEL_LONG_CLIP);
        lv_label_set_text(keycap, text ? text : "");
        return keycap;
    }

    void addFooterShortcut(const char* primary,
                           const char* secondary,
                           const char* description)
    {
        if (footer_shortcuts_row_ == nullptr)
        {
            return;
        }

        lv_obj_t* group = lv_obj_create(footer_shortcuts_row_);
        resetBox(group);
        applyTransparent(group);
        lv_obj_set_height(group, 22);
        lv_obj_set_style_pad_column(group, 0, 0);
        lv_obj_set_flex_flow(group, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(group,
                              LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);

        if (primary != nullptr && std::strcmp(primary, "WASD") == 0)
        {
            for (const char key : std::string("WASD"))
            {
                char label[2] = {key, '\0'};
                createFooterKeycap(group, label, 16);
            }
        }
        else
        {
            const lv_coord_t primary_width =
                primary != nullptr && std::strlen(primary) > 2 ? 28 : 16;
            createFooterKeycap(group, primary, primary_width);
        }
        if (secondary != nullptr && secondary[0] != '\0')
        {
            const lv_coord_t secondary_width =
                std::strlen(secondary) > 2 ? 28 : 16;
            createFooterKeycap(group, secondary, secondary_width);
        }

        lv_obj_t* label =
            createLabel(group, description, &lv_font_montserrat_10,
                        embedded_palette::kTextMuted);
        lv_obj_set_width(label, LV_SIZE_CONTENT);
    }

    void refreshFooterShortcuts()
    {
        if (footer_shortcuts_row_ == nullptr)
        {
            return;
        }
        lv_obj_clean(footer_shortcuts_row_);

        switch (active_section_)
        {
        case Section::Chat:
            addFooterShortcut("\\", nullptr, "Nav");
            addFooterShortcut("Tab", nullptr, "Focus");
            addFooterShortcut("Enter", nullptr, "Send");
            addFooterShortcut("F11", nullptr, "Full");
            addFooterShortcut("Ctrl-M", nullptr, "Min");
            addFooterShortcut("Ctrl-Q", nullptr, "Quit");
            break;
        case Section::Map:
            addFooterShortcut("WASD", nullptr, "Pan");
            addFooterShortcut("Q", "E", "Zoom");
            addFooterShortcut("C", nullptr, "Ctr");
            addFooterShortcut("L", nullptr, "Layer");
            addFooterShortcut("O", nullptr, "Cnt");
            addFooterShortcut("M", nullptr, "Meas");
            addFooterShortcut("F1", nullptr, "Help");
            break;
        case Section::Overview:
            addFooterShortcut("\\", nullptr, "Nav");
            addFooterShortcut("C", nullptr, "Chat");
            addFooterShortcut("M", nullptr, "Map");
            addFooterShortcut("[", "]", "Page");
            addFooterShortcut("F1", nullptr, "Help");
            break;
        default:
            addFooterShortcut("\\", nullptr, "Nav");
            addFooterShortcut("Enter", nullptr, "Open");
            addFooterShortcut("[", "]", "Page");
            addFooterShortcut("F1", nullptr, "Help");
            addFooterShortcut("F11", nullptr, "Full");
            break;
        }
    }

    void buildShortcutOverlay(lv_obj_t* root)
    {
        shortcut_overlay_ = lv_obj_create(root);
        resetBox(shortcut_overlay_);
        lv_obj_add_flag(shortcut_overlay_, LV_OBJ_FLAG_FLOATING);
        lv_obj_set_size(shortcut_overlay_, LV_PCT(100), LV_PCT(100));
        lv_obj_set_pos(shortcut_overlay_, 0, 0);
        lv_obj_set_style_bg_color(shortcut_overlay_,
                                  color(embedded_palette::kHeaderText), 0);
        lv_obj_set_style_bg_opa(shortcut_overlay_, LV_OPA_60, 0);

        lv_obj_t* card = lv_obj_create(shortcut_overlay_);
        applyPanel(card, embedded_palette::kSurface,
                   embedded_palette::kBorder);
        lv_obj_set_size(card, 760, 560);
        lv_obj_set_style_pad_all(card, 10, 0);
        lv_obj_set_style_pad_row(card, 4, 0);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_center(card);

        createLabel(card, "uConsole keyboard shortcuts",
                    &lv_font_montserrat_16, embedded_palette::kText);
        createLabel(card,
                    "Global shortcuts are disabled while typing in the chat "
                    "composer.",
                    &lv_font_montserrat_12, embedded_palette::kTextMuted);

        auto addKeycap = [](lv_obj_t* parent,
                            const char* text,
                            lv_coord_t width)
        {
            lv_obj_t* keycap = lv_label_create(parent);
            lv_obj_set_size(keycap, width, 18);
            lv_obj_set_style_bg_color(keycap, lv_color_hex(0xF8E6C3), 0);
            lv_obj_set_style_bg_opa(keycap, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(keycap, 1, 0);
            lv_obj_set_style_border_color(keycap, lv_color_hex(0x8A6E43), 0);
            lv_obj_set_style_radius(keycap, 3, 0);
            lv_obj_set_style_text_font(keycap, &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_color(keycap, lv_color_hex(0x25170D), 0);
            lv_obj_set_style_text_align(keycap, LV_TEXT_ALIGN_CENTER, 0);
            lv_label_set_long_mode(keycap, LV_LABEL_LONG_CLIP);
            lv_label_set_text(keycap, text ? text : "");
            return keycap;
        };

        auto addHelpRow = [this, &addKeycap](lv_obj_t* parent,
                                             const char* primary,
                                             const char* secondary,
                                             const char* description)
        {
            lv_obj_t* row = lv_obj_create(parent);
            resetBox(row);
            lv_obj_set_width(row, LV_PCT(100));
            lv_obj_set_height(row, 22);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(row,
                                  LV_FLEX_ALIGN_START,
                                  LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER);
            lv_obj_set_style_pad_column(row, 4, 0);

            lv_obj_t* keys = lv_obj_create(row);
            resetBox(keys);
            lv_obj_set_size(keys, 112, 20);
            lv_obj_set_style_pad_column(keys, 3, 0);
            lv_obj_set_flex_flow(keys, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(keys,
                                  LV_FLEX_ALIGN_START,
                                  LV_FLEX_ALIGN_CENTER,
                                  LV_FLEX_ALIGN_CENTER);
            if (primary != nullptr && std::strcmp(primary, "WASD") == 0)
            {
                for (const char key : std::string("WASD"))
                {
                    char key_label[2] = {key, '\0'};
                    addKeycap(keys, key_label, 24);
                }
            }
            else if (secondary != nullptr && secondary[0] != '\0')
            {
                addKeycap(keys, primary, 52);
                addKeycap(keys, secondary, 52);
            }
            else
            {
                addKeycap(keys, primary, 106);
            }

            lv_obj_t* text = lv_label_create(row);
            lv_obj_set_width(text, 0);
            lv_obj_set_flex_grow(text, 1);
            lv_obj_set_style_text_font(text, &lv_font_montserrat_12, 0);
            lv_obj_set_style_text_color(text, lv_color_hex(0x3E2B18), 0);
            lv_label_set_long_mode(text, LV_LABEL_LONG_DOT);
            lv_label_set_text(text, description ? description : "");
        };

        lv_obj_t* columns = lv_obj_create(card);
        applyTransparent(columns);
        lv_obj_set_width(columns, LV_PCT(100));
        lv_obj_set_flex_grow(columns, 1);
        lv_obj_set_style_pad_column(columns, 30, 0);
        lv_obj_set_flex_flow(columns, LV_FLEX_FLOW_ROW);

        lv_obj_t* navigation = lv_obj_create(columns);
        applyTransparent(navigation);
        lv_obj_set_height(navigation, LV_PCT(100));
        lv_obj_set_flex_grow(navigation, 1);
        lv_obj_set_style_pad_row(navigation, 10, 0);
        lv_obj_set_flex_flow(navigation, LV_FLEX_FLOW_COLUMN);
        createLabel(navigation, "NAVIGATION", &lv_font_montserrat_12,
                    embedded_palette::kTextDim);
        addHelpRow(navigation, "Up", "Down", "Move focus");
        addHelpRow(navigation, "Tab", nullptr, "Next focus target");
        addHelpRow(navigation, "Enter", "Right", "Open or activate");
        addHelpRow(navigation, "Esc", nullptr, "Return to overview");
        addHelpRow(navigation, "[", "]", "Previous / next workspace");
        addHelpRow(navigation, "\\", nullptr, "Collapse navigation");
        addHelpRow(navigation, "F1", "H", "Toggle this help");
        addHelpRow(navigation, "F11", nullptr, "Toggle fullscreen");
        addHelpRow(navigation, "Ctrl-M", nullptr, "Minimize window");
        addHelpRow(navigation, "Ctrl-Q", nullptr, "Quit application");

        lv_obj_t* workspaces = lv_obj_create(columns);
        applyTransparent(workspaces);
        lv_obj_set_height(workspaces, LV_PCT(100));
        lv_obj_set_flex_grow(workspaces, 1);
        lv_obj_set_style_pad_row(workspaces, 10, 0);
        lv_obj_set_flex_flow(workspaces, LV_FLEX_FLOW_COLUMN);
        const auto page_index = static_cast<std::size_t>(active_section_);
        createLabel(workspaces,
                    page_index < kNavLabels.size() ? kNavLabels[page_index]
                                                   : "PAGE",
                    &lv_font_montserrat_12, embedded_palette::kTextDim);
        switch (active_section_)
        {
        case Section::Overview:
            addHelpRow(workspaces, "O", nullptr, "Open overview");
            addHelpRow(workspaces, "C", nullptr, "Open chat");
            addHelpRow(workspaces, "M", nullptr, "Open map");
            addHelpRow(workspaces, "N", nullptr, "Open contacts");
            addHelpRow(workspaces, "G", nullptr, "Open GPS page");
            break;
        case Section::Chat:
            addHelpRow(workspaces, "Tab", nullptr, "Move focus");
            addHelpRow(workspaces, "Enter", nullptr, "Send / activate");
            addHelpRow(workspaces, "[", "]", "Previous / next workspace");
            break;
        case Section::Map:
            addHelpRow(workspaces, "WASD", nullptr, "Pan map");
            addHelpRow(workspaces, "Q", "E", "Zoom out / in");
            addHelpRow(workspaces, "C", nullptr, "Recenter map");
            addHelpRow(workspaces, "L", nullptr, "Cycle base layer");
            addHelpRow(workspaces, "O", nullptr, "Toggle contours");
            addHelpRow(workspaces, "M", nullptr, "Toggle measure tool");
            addHelpRow(workspaces, "[", "]", "Previous / next workspace");
            break;
        default:
            addHelpRow(workspaces, "Up", "Down", "Move focus");
            addHelpRow(workspaces, "Enter", "Right", "Open or activate");
            addHelpRow(workspaces, "[", "]", "Previous / next workspace");
            break;
        }

        createLabel(card,
                    "Virtual keycaps follow the Pager/T-Deck keyboard-first "
                    "interaction style.",
                    &lv_font_montserrat_12, embedded_palette::kTextMuted);

        lv_obj_add_flag(shortcut_overlay_, LV_OBJ_FLAG_HIDDEN);
    }

    void refreshFooter()
    {
        const auto index = static_cast<std::size_t>(active_section_);
        std::string context =
            index < kNavLabels.size() ? kNavLabels[index] : "Workspace";
        if (sidebar_collapsed_)
        {
            context += " / navigation hidden";
        }
        setLabel(footer_context_label_, context);
        refreshFooterShortcuts();
    }

    lv_obj_t* createChip(lv_obj_t* parent,
                         const char* text,
                         std::uint32_t bg,
                         std::uint32_t fg)
    {
        lv_obj_t* chip = lv_obj_create(parent);
        resetBox(chip);
        lv_obj_set_size(chip, LV_SIZE_CONTENT, 26);
        lv_obj_set_style_bg_color(chip, color(bg), 0);
        lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(chip, 6, 0);
        lv_obj_set_style_pad_left(chip, 8, 0);
        lv_obj_set_style_pad_right(chip, 8, 0);
        lv_obj_t* label =
            createLabel(chip, text, &lv_font_montserrat_12, fg);
        lv_obj_center(label);
        return label;
    }

    void buildSidebar(lv_obj_t* parent)
    {
        sidebar_ = lv_obj_create(parent);
        applyPanel(sidebar_, embedded_palette::kSurfaceAlt,
                   embedded_palette::kBorder);
        lv_obj_set_width(sidebar_, 168);
        lv_obj_set_height(sidebar_, LV_PCT(100));
        lv_obj_set_style_pad_all(sidebar_, 7, 0);
        lv_obj_set_style_pad_row(sidebar_, 2, 0);
        lv_obj_set_flex_flow(sidebar_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(sidebar_, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        createLabel(sidebar_, "TRAIL MATE", &lv_font_montserrat_16,
                    embedded_palette::kText);

        for (std::size_t index = 0; index < kNavLabels.size(); ++index)
        {
            if (index == 0)
            {
                createLabel(sidebar_, "WORKSPACES", &lv_font_montserrat_10,
                            embedded_palette::kTextDim);
            }
            else if (index == 3)
            {
                createLabel(sidebar_, "FIELD", &lv_font_montserrat_10,
                            embedded_palette::kTextDim);
            }
            else if (index == 8)
            {
                createLabel(sidebar_, "SYSTEM", &lv_font_montserrat_10,
                            embedded_palette::kTextDim);
            }
            nav_bindings_[index] = {this, static_cast<Section>(index)};
            nav_buttons_[index] =
                createNavButton(sidebar_, kNavLabels[index],
                                &nav_bindings_[index]);
        }
    }

    lv_obj_t* createNavButton(lv_obj_t* parent,
                              const char* label_text,
                              NavBinding* binding)
    {
        lv_obj_t* button = lv_btn_create(parent);
        resetBox(button);
        lv_obj_set_width(button, LV_PCT(100));
        lv_obj_set_height(button, 25);
        lv_obj_set_style_radius(button, 5, 0);
        lv_obj_set_style_pad_left(button, 8, 0);
        lv_obj_set_style_pad_right(button, 8, 0);
        lv_obj_set_style_bg_color(button, color(embedded_palette::kSurface), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_add_event_cb(button, navEventCb, LV_EVENT_CLICKED, binding);
        lv_obj_add_event_cb(button, navEventCb, LV_EVENT_KEY, binding);
        lv_group_add_obj(group_, button);

        lv_obj_t* label = createLabel(button, label_text,
                                      &lv_font_montserrat_12,
                                      embedded_palette::kText);
        lv_obj_set_width(label, LV_PCT(100));
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_center(label);
        return button;
    }

    void buildWorkspace(lv_obj_t* parent)
    {
        lv_obj_t* workspace = lv_obj_create(parent);
        applyTransparent(workspace);
        lv_obj_set_height(workspace, LV_PCT(100));
        lv_obj_set_flex_grow(workspace, 1);
        lv_obj_set_style_pad_row(workspace, 8, 0);
        lv_obj_set_flex_flow(workspace, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(workspace, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        lv_obj_t* header = lv_obj_create(workspace);
        applyTransparent(header);
        lv_obj_set_width(header, LV_PCT(100));
        lv_obj_set_height(header, 36);
        lv_obj_set_style_pad_column(header, 10, 0);
        lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(header, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        workspace_title_ =
            createLabel(header, sectionTitle(active_section_),
                        &lv_font_montserrat_20, embedded_palette::kText);
        workspace_subtitle_ =
            createLabel(header, sectionSubtitle(active_section_),
                        &lv_font_montserrat_12,
                        embedded_palette::kTextMuted);

        metrics_panel_ = lv_obj_create(workspace);
        applyTransparent(metrics_panel_);
        lv_obj_set_width(metrics_panel_, LV_PCT(100));
        lv_obj_set_height(metrics_panel_, 58);
        lv_obj_set_style_pad_column(metrics_panel_, 8, 0);
        lv_obj_set_flex_flow(metrics_panel_, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(metrics_panel_, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        createMetric(metrics_panel_, 0, "Threads");
        createMetric(metrics_panel_, 1, "Unread");
        createMetric(metrics_panel_, 2, "Contacts");
        createMetric(metrics_panel_, 3, "Nearby");

        conversation_panel_ = lv_obj_create(workspace);
        applyPanel(conversation_panel_, embedded_palette::kSurface);
        lv_obj_set_width(conversation_panel_, LV_PCT(100));
        lv_obj_set_height(conversation_panel_, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_row(conversation_panel_, 2, 0);
        lv_obj_set_flex_flow(conversation_panel_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(conversation_panel_, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        createLabel(conversation_panel_, "Recent conversations",
                    &lv_font_montserrat_16, embedded_palette::kText);
        for (int index = 0; index < kConversationRows; ++index)
        {
            buildConversationRow(index);
        }

        buildChatWorkspace(workspace);
        buildDesktopPages(workspace);
    }

    void createMetric(lv_obj_t* parent, int index, const char* title)
    {
        lv_obj_t* box = lv_obj_create(parent);
        applyPanel(box, embedded_palette::kSurface);
        lv_obj_set_height(box, LV_PCT(100));
        lv_obj_set_flex_grow(box, 1);
        lv_obj_set_style_pad_left(box, 10, 0);
        lv_obj_set_style_pad_right(box, 10, 0);
        lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(box, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        createLabel(box, title, &lv_font_montserrat_12,
                    embedded_palette::kTextMuted);
        metric_value_labels_[index] =
            createLabel(box, "0", &lv_font_montserrat_20,
                        embedded_palette::kText);
    }

    void buildConversationRow(int index)
    {
        lv_obj_t* row = lv_obj_create(conversation_panel_);
        applyTransparent(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 42);
        lv_obj_set_style_pad_column(row, 10, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        conversation_rows_[index] = row;
        conversation_title_labels_[index] =
            createLabel(row, "-", &lv_font_montserrat_14,
                        embedded_palette::kText, LV_LABEL_LONG_DOT);
        lv_obj_set_width(conversation_title_labels_[index], 150);
        conversation_preview_labels_[index] =
            createLabel(row, "", &lv_font_montserrat_12,
                        embedded_palette::kTextMuted, LV_LABEL_LONG_DOT);
        lv_obj_set_flex_grow(conversation_preview_labels_[index], 1);
        conversation_meta_labels_[index] =
            createLabel(row, "", &lv_font_montserrat_12,
                        embedded_palette::kTextDim, LV_LABEL_LONG_DOT);
        lv_obj_set_width(conversation_meta_labels_[index], 86);
        lv_obj_set_style_text_align(conversation_meta_labels_[index],
                                    LV_TEXT_ALIGN_RIGHT, 0);
    }

    void buildChatWorkspace(lv_obj_t* parent)
    {
        chat_panel_ = lv_obj_create(parent);
        applyPanel(chat_panel_, embedded_palette::kSurface);
        lv_obj_set_width(chat_panel_, LV_PCT(100));
        lv_obj_set_flex_grow(chat_panel_, 1);
        lv_obj_set_style_pad_column(chat_panel_, 10, 0);
        lv_obj_set_flex_flow(chat_panel_, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(chat_panel_, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        lv_obj_t* conversation_list = lv_obj_create(chat_panel_);
        applyTransparent(conversation_list);
        lv_obj_set_width(conversation_list, 280);
        lv_obj_set_height(conversation_list, LV_PCT(100));
        lv_obj_set_style_pad_row(conversation_list, 5, 0);
        lv_obj_set_flex_flow(conversation_list, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(conversation_list, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        createLabel(conversation_list, "Conversations", &lv_font_montserrat_16,
                    embedded_palette::kText);
        for (int index = 0; index < kChatConversationRows; ++index)
        {
            buildChatConversationRow(conversation_list, index);
        }

        lv_obj_t* thread = lv_obj_create(chat_panel_);
        applyTransparent(thread);
        lv_obj_set_height(thread, LV_PCT(100));
        lv_obj_set_flex_grow(thread, 1);
        lv_obj_set_style_pad_row(thread, 6, 0);
        lv_obj_set_flex_flow(thread, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(thread, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        lv_obj_t* thread_header = lv_obj_create(thread);
        applyTransparent(thread_header);
        lv_obj_set_width(thread_header, LV_PCT(100));
        lv_obj_set_height(thread_header, 34);
        lv_obj_set_style_pad_column(thread_header, 8, 0);
        lv_obj_set_flex_flow(thread_header, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(thread_header, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        chat_title_label_ = createLabel(thread_header, "-",
                                        &lv_font_montserrat_16,
                                        embedded_palette::kText);
        chat_meta_label_ = createLabel(thread_header, "-",
                                       &lv_font_montserrat_12,
                                       embedded_palette::kTextMuted);
        lv_obj_set_width(chat_title_label_, 190);
        lv_obj_set_flex_grow(chat_meta_label_, 1);

        chat_messages_panel_ = lv_obj_create(thread);
        applyPanel(chat_messages_panel_, embedded_palette::kPanelBg);
        lv_obj_set_width(chat_messages_panel_, LV_PCT(100));
        lv_obj_set_flex_grow(chat_messages_panel_, 1);
        lv_obj_set_style_pad_row(chat_messages_panel_, 5, 0);
        lv_obj_set_flex_flow(chat_messages_panel_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(chat_messages_panel_, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        chat_empty_label_ = createLabel(
            chat_messages_panel_, "No messages yet.", &lv_font_montserrat_14,
            embedded_palette::kTextMuted);
        lv_obj_set_width(chat_empty_label_, LV_PCT(100));
        for (int index = 0; index < kChatMessageRows; ++index)
        {
            buildChatMessageRow(index);
        }

        lv_obj_t* input_row = lv_obj_create(thread);
        applyTransparent(input_row);
        lv_obj_set_width(input_row, LV_PCT(100));
        lv_obj_set_height(input_row, 50);
        lv_obj_set_style_pad_column(input_row, 8, 0);
        lv_obj_set_flex_flow(input_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(input_row, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        chat_input_ = lv_textarea_create(input_row);
        lv_obj_set_height(chat_input_, 40);
        lv_obj_set_flex_grow(chat_input_, 1);
        lv_textarea_set_one_line(chat_input_, true);
        lv_textarea_set_placeholder_text(chat_input_, "Type a message");
        lv_obj_set_style_text_font(chat_input_, &lv_font_montserrat_14, 0);
        lv_obj_set_style_radius(chat_input_, 6, 0);
        lv_group_add_obj(group_, chat_input_);

        chat_send_button_ = lv_btn_create(input_row);
        resetBox(chat_send_button_);
        lv_obj_set_size(chat_send_button_, 76, 40);
        lv_obj_set_style_radius(chat_send_button_, 6, 0);
        lv_obj_set_style_bg_color(chat_send_button_,
                                  color(embedded_palette::kAccent), 0);
        lv_obj_set_style_bg_opa(chat_send_button_, LV_OPA_COVER, 0);
        lv_obj_add_event_cb(chat_send_button_, chatSendEventCb,
                            LV_EVENT_CLICKED, this);
        lv_obj_add_event_cb(chat_send_button_, chatSendEventCb, LV_EVENT_KEY,
                            this);
        lv_group_add_obj(group_, chat_send_button_);
        lv_obj_t* send_label = createLabel(
            chat_send_button_, "Send", &lv_font_montserrat_14,
            embedded_palette::kHeaderText);
        lv_obj_center(send_label);

        chat_status_label_ =
            createLabel(thread, "Ready.", &lv_font_montserrat_12,
                        embedded_palette::kTextMuted);
        lv_obj_set_width(chat_status_label_, LV_PCT(100));
    }

    lv_obj_t* createPreviewPanel(lv_obj_t* parent,
                                 const char* title,
                                 int width = 0,
                                 std::uint32_t background =
                                     embedded_palette::kSurface,
                                 bool fill_height = false)
    {
        lv_obj_t* panel = lv_obj_create(parent);
        applyPanel(panel, background);
        if (width > 0)
        {
            lv_obj_set_width(panel, width);
        }
        else
        {
            lv_obj_set_flex_grow(panel, 1);
        }
        lv_obj_set_height(panel,
                          fill_height ? LV_PCT(100) : LV_SIZE_CONTENT);
        lv_obj_set_style_pad_all(panel, 10, 0);
        lv_obj_set_style_pad_row(panel, 7, 0);
        lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        createLabel(panel, title, &lv_font_montserrat_16,
                    embedded_palette::kText);
        return panel;
    }

    lv_obj_t* addPreviewRow(lv_obj_t* parent,
                            const char* title,
                            const char* detail,
                            std::uint32_t accent =
                                embedded_palette::kStatusGreen)
    {
        lv_obj_t* row = lv_obj_create(parent);
        applyPanel(row, embedded_palette::kPanelBg,
                   embedded_palette::kBorder);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 54);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_width(row, 4, 0);
        lv_obj_set_style_border_color(row, color(accent), 0);
        lv_obj_set_style_pad_all(row, 7, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        createLabel(row, title, &lv_font_montserrat_14,
                    embedded_palette::kText);
        lv_obj_t* meta = createLabel(
            row, detail, &lv_font_montserrat_12,
            embedded_palette::kTextMuted, LV_LABEL_LONG_DOT);
        lv_obj_set_width(meta, LV_PCT(100));
        return meta;
    }

    lv_obj_t* addActionPill(lv_obj_t* parent,
                            const char* text,
                            std::uint32_t background =
                                embedded_palette::kSoftGreen,
                            std::uint32_t foreground =
                                embedded_palette::kText)
    {
        lv_obj_t* pill = lv_obj_create(parent);
        resetBox(pill);
        lv_obj_set_size(pill, LV_SIZE_CONTENT, 28);
        lv_obj_set_style_bg_color(pill, color(background), 0);
        lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(pill, 5, 0);
        lv_obj_set_style_pad_left(pill, 9, 0);
        lv_obj_set_style_pad_right(pill, 9, 0);
        lv_obj_t* label =
            createLabel(pill, text, &lv_font_montserrat_12, foreground);
        lv_obj_center(label);
        return pill;
    }

    void buildDesktopPages(lv_obj_t* parent)
    {
        desktop_page_panel_ = lv_obj_create(parent);
        applyTransparent(desktop_page_panel_);
        lv_obj_set_width(desktop_page_panel_, LV_PCT(100));
        lv_obj_set_flex_grow(desktop_page_panel_, 1);
        lv_obj_set_style_pad_column(desktop_page_panel_, 10, 0);
        lv_obj_set_flex_flow(desktop_page_panel_, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(desktop_page_panel_, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    }

    static std::string mapSummary(const MapWorkspaceSnapshot& snapshot)
    {
        char line[160] = {};
        if (snapshot.has_center)
        {
            std::snprintf(line, sizeof(line),
                          "%.4f N  /  %.4f E  /  zoom %d  /  %s",
                          snapshot.lat, snapshot.lon, snapshot.zoom,
                          snapshot.source_label.c_str());
        }
        else
        {
            std::snprintf(line, sizeof(line), "No map center  /  %s",
                          snapshot.fix_label.c_str());
        }
        return line;
    }

    static std::string mapCacheSummary(const MapWorkspaceSnapshot& snapshot)
    {
        char line[128] = {};
        std::snprintf(line, sizeof(line), "%llu tiles / %llu KB",
                      static_cast<unsigned long long>(
                          snapshot.cache_stats.cached_tiles),
                      static_cast<unsigned long long>(
                          snapshot.cache_stats.total_bytes / 1024U));
        return line;
    }

    static std::string mapDownloadSummary(
        const MapWorkspaceSnapshot& snapshot)
    {
        std::size_t missing = 0;
        for (const auto& tile : snapshot.tiles)
        {
            if (!tile.available) ++missing;
        }
        char line[128] = {};
        std::snprintf(line, sizeof(line), "%zu visible / %zu pending",
                      snapshot.tiles.size() - missing, missing);
        return line;
    }

    static std::string mapNodeSummary(const MapWorkspaceSnapshot& snapshot)
    {
        char line[128] = {};
        std::snprintf(line, sizeof(line), "%zu nodes / %zu via MQTT",
                      snapshot.visible_node_count,
                      snapshot.visible_mqtt_node_count);
        return line;
    }

    void buildMapPreview()
    {
        const auto snapshot = map_model_.snapshot();
        map_tile_cells_.fill(nullptr);
        map_tile_images_.fill(nullptr);
        map_tile_placeholder_labels_.fill(nullptr);
        map_tile_paths_.fill(std::string{});
        lv_obj_t* map = createPreviewPanel(
            desktop_page_panel_, "Field map", 0, embedded_palette::kMapBg,
            true);

        lv_obj_t* toolbar = lv_obj_create(map);
        applyTransparent(toolbar);
        lv_obj_set_width(toolbar, LV_PCT(100));
        lv_obj_set_height(toolbar, 30);
        lv_obj_set_style_pad_column(toolbar, 5, 0);
        lv_obj_set_flex_flow(toolbar, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(toolbar, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        addActionPill(toolbar, "Terrain", embedded_palette::kSoftBlue,
                      embedded_palette::kText);
        addActionPill(toolbar, "Contours");
        addActionPill(toolbar, "Recenter");
        addActionPill(toolbar, "Measure");

        lv_obj_t* grid = lv_obj_create(map);
        resetBox(grid);
        lv_obj_set_width(grid, LV_PCT(100));
        lv_obj_set_flex_grow(grid, 1);
        lv_obj_set_style_pad_row(grid, 0, 0);
        lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        constexpr std::array<std::uint32_t, 9> tile_colors{
            embedded_palette::kMapTile1, embedded_palette::kMapTile2,
            embedded_palette::kMapTile3, embedded_palette::kMapBg,
            embedded_palette::kMapTile4, embedded_palette::kMapTile5,
            embedded_palette::kMapTile6, embedded_palette::kMapTile7,
            embedded_palette::kMapTile8};
        const int preview_columns =
            static_cast<int>(std::max<std::size_t>(1U, snapshot.columns));
        const int preview_rows =
            static_cast<int>(std::max<std::size_t>(1U, snapshot.rows));
        for (int row_index = 0; row_index < preview_rows; ++row_index)
        {
            lv_obj_t* row = lv_obj_create(grid);
            resetBox(row);
            lv_obj_set_width(row, kMapTileDisplaySize * preview_columns);
            lv_obj_set_height(row, kMapTileDisplaySize);
            lv_obj_set_style_pad_column(row, 0, 0);
            lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
            for (int column = 0; column < preview_columns; ++column)
            {
                const int tile_index =
                    (row_index * preview_columns) + column;
                lv_obj_t* tile = lv_obj_create(row);
                resetBox(tile);
                lv_obj_set_size(tile, kMapTileDisplaySize,
                                kMapTileDisplaySize);
                lv_obj_set_style_bg_color(
                    tile,
                    color(tile_colors[static_cast<std::size_t>(tile_index) %
                                      tile_colors.size()]),
                    0);
                lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
                // Tile cells are a compositing grid, not cards.  Keep their
                // edges transparent so adjacent 256px images share the exact
                // boundary produced by the common map-tile geometry.
                lv_obj_set_style_border_width(tile, 0, 0);
                map_tile_cells_[static_cast<std::size_t>(tile_index)] = tile;
                lv_obj_t* placeholder =
                    createLabel(tile, "", &lv_font_montserrat_10,
                                embedded_palette::kTextDim,
                                LV_LABEL_LONG_WRAP);
                lv_obj_set_width(placeholder, LV_PCT(100));
                lv_obj_set_height(placeholder, LV_PCT(100));
                lv_obj_set_style_text_align(placeholder,
                                            LV_TEXT_ALIGN_CENTER, 0);
                lv_obj_set_style_text_line_space(placeholder, 1, 0);
                lv_obj_center(placeholder);
                map_tile_placeholder_labels_
                    [static_cast<std::size_t>(tile_index)] = placeholder;
                const bool available =
                    tile_index < static_cast<int>(snapshot.tiles.size()) &&
                    snapshot.tiles[static_cast<std::size_t>(tile_index)]
                        .available;
                lv_obj_set_style_bg_color(
                    tile,
                    color(available
                              ? tile_colors[static_cast<std::size_t>(tile_index) %
                                            tile_colors.size()]
                              : embedded_palette::kSurfaceAlt),
                    0);
                if (available)
                {
                    auto* image = lv_image_create(tile);
                    resetBox(image);
                    lv_obj_set_size(image, LV_PCT(100), LV_PCT(100));
                    map_tile_images_[static_cast<std::size_t>(tile_index)] =
                        image;
                    map_tile_paths_[static_cast<std::size_t>(tile_index)] =
                        "A:" + snapshot
                                   .tiles[static_cast<std::size_t>(tile_index)]
                                   .path.string();
                    lv_image_set_inner_align(image, LV_IMAGE_ALIGN_STRETCH);
                    lv_image_set_src(
                        image,
                        map_tile_paths_[static_cast<std::size_t>(tile_index)]
                            .c_str());
                    lv_obj_add_flag(placeholder, LV_OBJ_FLAG_HIDDEN);
                }
                else
                {
                    char xyz[48] = {};
                    if (tile_index < static_cast<int>(snapshot.tiles.size()))
                    {
                        const auto& id =
                            snapshot.tiles[static_cast<std::size_t>(tile_index)]
                                .id;
                        ::ui::map_tiles::formatMapTileCoordinateLabel(
                            static_cast<std::uint8_t>(id.z),
                            static_cast<std::uint32_t>(id.x),
                            static_cast<std::uint32_t>(id.y),
                            xyz,
                            sizeof(xyz));
                    }
                    lv_label_set_text(placeholder, xyz);
                    map_tile_paths_[static_cast<std::size_t>(tile_index)]
                        .clear();
                }
            }
        }
        map_meta_label_ = createLabel(
            map, mapSummary(snapshot).c_str(), &lv_font_montserrat_12,
            embedded_palette::kTextMuted);

        lv_obj_t* download = createPreviewPanel(
            desktop_page_panel_, "Map downloads", 220,
            embedded_palette::kSurface);
        map_cache_label_ =
            addPreviewRow(download, "Cache", mapCacheSummary(snapshot).c_str(),
                          embedded_palette::kStatusGreen);
        map_download_label_ =
            addPreviewRow(download, "Visible tiles",
                          mapDownloadSummary(snapshot).c_str(),
                          embedded_palette::kAccentDark);
        map_retry_label_ =
            addPreviewRow(download, "Nodes", mapNodeSummary(snapshot).c_str(),
                          embedded_palette::kStatusBlue);
        addActionPill(download, "Open cache directory");
        requestMissingMapTiles(snapshot);
    }

    void buildGpsPreview()
    {
        lv_obj_t* sky = createPreviewPanel(
            desktop_page_panel_, "Satellite sky plot", 0,
            embedded_palette::kSurface, true);
        lv_obj_t* stage = lv_obj_create(sky);
        resetBox(stage);
        lv_obj_set_width(stage, LV_PCT(100));
        lv_obj_set_flex_grow(stage, 1);
        lv_obj_set_style_bg_color(stage, color(embedded_palette::kMapBg), 0);
        lv_obj_set_style_bg_opa(stage, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(stage,
                                      color(embedded_palette::kBorder), 0);
        lv_obj_set_style_border_width(stage, 1, 0);
        lv_obj_set_style_radius(stage, 6, 0);

        constexpr int sky_x = 28;
        constexpr int sky_y = 38;
        constexpr int sky_size = 430;
        constexpr int sky_radius = sky_size / 2;
        constexpr int center_x = sky_x + sky_radius;
        constexpr int center_y = sky_y + sky_radius;
        constexpr std::uint32_t grid_color = 0xC9943F;

        lv_obj_t* vertical_axis = lv_obj_create(stage);
        resetBox(vertical_axis);
        lv_obj_set_pos(vertical_axis, center_x, sky_y);
        lv_obj_set_size(vertical_axis, 1, sky_size);
        lv_obj_set_style_bg_color(vertical_axis, color(grid_color), 0);
        lv_obj_set_style_bg_opa(vertical_axis, LV_OPA_COVER, 0);

        lv_obj_t* horizontal_axis = lv_obj_create(stage);
        resetBox(horizontal_axis);
        lv_obj_set_pos(horizontal_axis, sky_x, center_y);
        lv_obj_set_size(horizontal_axis, sky_size, 1);
        lv_obj_set_style_bg_color(horizontal_axis, color(grid_color), 0);
        lv_obj_set_style_bg_opa(horizontal_axis, LV_OPA_COVER, 0);

        constexpr std::array<int, 3> ring_sizes{
            sky_size, (sky_size * 2) / 3, sky_size / 3};
        for (std::size_t index = 0; index < ring_sizes.size(); ++index)
        {
            const int ring_size = ring_sizes[index];
            lv_obj_t* ring = lv_obj_create(stage);
            resetBox(ring);
            lv_obj_set_size(ring, ring_size, ring_size);
            lv_obj_set_pos(ring, center_x - ring_size / 2,
                           center_y - ring_size / 2);
            lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_color(ring, color(grid_color), 0);
            lv_obj_set_style_border_width(ring, index == 0 ? 2 : 1, 0);
            lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        }

        lv_obj_t* center_dot = lv_obj_create(stage);
        resetBox(center_dot);
        lv_obj_set_size(center_dot, 7, 7);
        lv_obj_set_pos(center_dot, center_x - 3, center_y - 3);
        lv_obj_set_style_bg_color(
            center_dot, color(embedded_palette::kTextWarm), 0);
        lv_obj_set_style_bg_opa(center_dot, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(center_dot, LV_RADIUS_CIRCLE, 0);

        struct PlotLabel
        {
            int x;
            int y;
            const char* text;
            const lv_font_t* font;
            std::uint32_t text_color;
        };
        constexpr std::array<PlotLabel, 8> plot_labels{{
            {center_x - 6, sky_y - 26, "N", &lv_font_montserrat_16,
             embedded_palette::kText},
            {sky_x + sky_size + 11, center_y - 8, "E",
             &lv_font_montserrat_16, embedded_palette::kText},
            {center_x - 5, sky_y + sky_size + 8, "S",
             &lv_font_montserrat_16, embedded_palette::kText},
            {sky_x - 22, center_y - 8, "W", &lv_font_montserrat_16,
             embedded_palette::kText},
            {sky_x + 34, sky_y + 37, "90 deg", &lv_font_montserrat_10,
             embedded_palette::kTextDim},
            {sky_x + 105, sky_y + 108, "60 deg", &lv_font_montserrat_10,
             embedded_palette::kTextDim},
            {center_x + sky_radius / 3 + 8, center_y - 18, "30 deg",
             &lv_font_montserrat_10, embedded_palette::kTextDim},
            {center_x + 9, center_y + 8, "Horizon",
             &lv_font_montserrat_10, embedded_palette::kTextDim},
        }};
        for (const auto& item : plot_labels)
        {
            lv_obj_t* label =
                createLabel(stage, item.text, item.font, item.text_color);
            lv_obj_set_pos(label, item.x, item.y);
        }

        struct SatelliteDot
        {
            int id;
            const char* system;
            float azimuth;
            float elevation;
            int snr;
            bool used;
            std::uint32_t fill_color;
            std::uint32_t border_color;
            std::uint32_t text_color;
        };
        constexpr std::array<SatelliteDot, 8> satellites{{
            {12, "GPS", 332.0F, 67.0F, 42, true, 0xE3B11F, 0x3E7D3E,
             embedded_palette::kHeaderText},
            {31, "GPS", 296.0F, 52.0F, 39, true, 0xE3B11F, 0x3E7D3E,
             embedded_palette::kHeaderText},
            {7, "BDS", 8.0F, 61.0F, 38, true, 0xB94A2C, 0x3E7D3E,
             embedded_palette::kWhite},
            {4, "GAL", 158.0F, 48.0F, 36, true, 0x3E7D3E, 0x3E7D3E,
             embedded_palette::kWhite},
            {11, "GAL", 132.0F, 35.0F, 33, true, 0x3E7D3E, 0x8FBF4D,
             embedded_palette::kWhite},
            {19, "BDS", 226.0F, 29.0F, 30, true, 0xB94A2C, 0x8FBF4D,
             embedded_palette::kWhite},
            {22, "GPS", 82.0F, 23.0F, 28, false, 0xE3B11F, 0xB94A2C,
             embedded_palette::kHeaderText},
            {5, "GPS", 278.0F, 16.0F, 18, false, 0xE3B11F, 0x6E6E6E,
             embedded_palette::kHeaderText},
        }};
        for (const auto& sat : satellites)
        {
            constexpr int dot_size = 31;
            const float radius =
                static_cast<float>(sky_radius) *
                (1.0F - std::clamp(sat.elevation, 0.0F, 90.0F) / 90.0F);
            const float radians =
                sat.azimuth * 3.1415926535F / 180.0F;
            const int satellite_x = static_cast<int>(std::round(
                static_cast<float>(center_x) + radius * std::sin(radians)));
            const int satellite_y = static_cast<int>(std::round(
                static_cast<float>(center_y) - radius * std::cos(radians)));

            lv_obj_t* dot = lv_obj_create(stage);
            resetBox(dot);
            lv_obj_set_size(dot, dot_size, dot_size);
            lv_obj_set_pos(dot, satellite_x - dot_size / 2,
                           satellite_y - dot_size / 2);
            lv_obj_set_style_bg_color(dot, color(sat.fill_color), 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(dot, color(sat.border_color), 0);
            lv_obj_set_style_border_width(dot, 3, 0);
            lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);

            char id[8];
            std::snprintf(id, sizeof(id), "%d", sat.id);
            lv_obj_t* label = createLabel(
                dot, id, &lv_font_montserrat_10, sat.text_color);
            lv_obj_center(label);

            if (sat.used)
            {
                lv_obj_t* use_tag = lv_obj_create(stage);
                resetBox(use_tag);
                lv_obj_set_size(use_tag, 28, 16);
                lv_obj_set_pos(use_tag, satellite_x + 8, satellite_y + 8);
                lv_obj_set_style_bg_color(
                    use_tag, color(embedded_palette::kStatusGreen), 0);
                lv_obj_set_style_bg_opa(use_tag, LV_OPA_COVER, 0);
                lv_obj_set_style_radius(use_tag, 8, 0);
                lv_obj_t* use_label =
                    createLabel(use_tag, "USE", &lv_font_montserrat_10,
                                embedded_palette::kWhite);
                lv_obj_center(use_label);
            }
        }

        constexpr int legend_x = 494;
        lv_obj_t* constellation_title =
            createLabel(stage, "CONSTELLATION", &lv_font_montserrat_10,
                        embedded_palette::kTextDim);
        lv_obj_set_pos(constellation_title, legend_x, 76);

        struct LegendItem
        {
            const char* label;
            std::uint32_t color;
        };
        constexpr std::array<LegendItem, 4> systems{{
            {"GPS", 0xE3B11F},
            {"GLONASS", 0x2D6FB6},
            {"Galileo", 0x3E7D3E},
            {"BeiDou", 0xB94A2C},
        }};
        constexpr std::array<LegendItem, 4> signals{{
            {"Good", 0x3E7D3E},
            {"Weak", 0xC18B2C},
            {"Not used", 0xB94A2C},
            {"In view", 0x6E6E6E},
        }};
        const auto build_legend =
            [stage](const std::array<LegendItem, 4>& items, int x, int y)
        {
            for (std::size_t index = 0; index < items.size(); ++index)
            {
                lv_obj_t* swatch = lv_obj_create(stage);
                resetBox(swatch);
                lv_obj_set_size(swatch, 13, 13);
                lv_obj_set_pos(swatch, x, y + static_cast<int>(index) * 28);
                lv_obj_set_style_bg_color(
                    swatch, color(items[index].color), 0);
                lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
                lv_obj_set_style_radius(swatch, 4, 0);

                lv_obj_t* label =
                    createLabel(stage, items[index].label,
                                &lv_font_montserrat_12,
                                embedded_palette::kTextMuted);
                lv_obj_set_pos(label, x + 21,
                               y - 1 + static_cast<int>(index) * 28);
            }
        };
        build_legend(systems, legend_x, 96);

        lv_obj_t* signal_title =
            createLabel(stage, "SIGNAL BORDER", &lv_font_montserrat_10,
                        embedded_palette::kTextDim);
        lv_obj_set_pos(signal_title, legend_x, 228);
        build_legend(signals, legend_x, 250);

        lv_obj_t* legend_note =
            createLabel(stage, "Fill = constellation\nRing = signal state",
                        &lv_font_montserrat_10,
                        embedded_palette::kTextDim, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(legend_note, 170);
        lv_obj_set_style_text_line_space(legend_note, 3, 0);
        lv_obj_set_pos(legend_note, legend_x, 376);

        createLabel(sky, "North up  /  live 1 Hz  /  altitude 18.4 m",
                    &lv_font_montserrat_12, embedded_palette::kTextWarm);

        lv_obj_t* receiver = createPreviewPanel(
            desktop_page_panel_, "Satellite status", 330,
            embedded_palette::kSurface, true);

        lv_obj_t* receiver_summary = lv_obj_create(receiver);
        applyPanel(receiver_summary, embedded_palette::kAccent,
                   embedded_palette::kAccentDark);
        lv_obj_set_width(receiver_summary, LV_PCT(100));
        lv_obj_set_height(receiver_summary, 42);
        lv_obj_set_style_pad_all(receiver_summary, 8, 0);
        lv_obj_t* summary_label =
            createLabel(receiver_summary, "USE 6/8   HDOP 0.8   FIX 3D",
                        &lv_font_montserrat_12,
                        embedded_palette::kHeaderText);
        lv_obj_center(summary_label);

        lv_obj_t* table_header = lv_obj_create(receiver);
        resetBox(table_header);
        lv_obj_set_width(table_header, LV_PCT(100));
        lv_obj_set_height(table_header, 28);
        lv_obj_set_style_bg_color(
            table_header, color(embedded_palette::kSurfaceAlt), 0);
        lv_obj_set_style_bg_opa(table_header, LV_OPA_COVER, 0);

        constexpr std::array<int, 5> column_x{8, 45, 112, 176, 227};
        constexpr std::array<const char*, 5> column_names{
            "ID", "SYS", "ELEV", "SNR", "USE"};
        for (std::size_t column = 0; column < column_names.size(); ++column)
        {
            lv_obj_t* label =
                createLabel(table_header, column_names[column],
                            &lv_font_montserrat_10,
                            embedded_palette::kTextDim);
            lv_obj_set_pos(label, column_x[column], 7);
        }

        for (std::size_t row_index = 0; row_index < satellites.size();
             ++row_index)
        {
            const auto& sat = satellites[row_index];
            lv_obj_t* row = lv_obj_create(receiver);
            resetBox(row);
            lv_obj_set_width(row, LV_PCT(100));
            lv_obj_set_height(row, 37);
            lv_obj_set_style_bg_color(
                row,
                color(row_index % 2 == 0 ? embedded_palette::kPanelBg
                                         : embedded_palette::kSurface),
                0);
            lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
            lv_obj_set_style_border_color(
                row, color(embedded_palette::kSeparator), 0);
            lv_obj_set_style_border_width(row, 1, 0);
            lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);

            char id[8];
            char elevation[12];
            char snr[8];
            std::snprintf(id, sizeof(id), "%d", sat.id);
            std::snprintf(elevation, sizeof(elevation), "%.0f deg",
                          static_cast<double>(sat.elevation));
            std::snprintf(snr, sizeof(snr), "%d", sat.snr);
            const std::array<const char*, 5> values{
                id, sat.system, elevation, snr, sat.used ? "YES" : "NO"};
            for (std::size_t column = 0; column < values.size(); ++column)
            {
                const std::uint32_t value_color =
                    column == 1
                        ? sat.fill_color
                        : (column == 4
                               ? (sat.used ? embedded_palette::kStatusGreen
                                           : embedded_palette::kWarn)
                               : embedded_palette::kText);
                lv_obj_t* label =
                    createLabel(row, values[column],
                                &lv_font_montserrat_12, value_color);
                lv_obj_set_pos(label, column_x[column], 10);
            }
        }

        lv_obj_t* receiver_meta =
            createLabel(receiver, "Live / 31.2304, 121.4737 / GPS UTC",
                        &lv_font_montserrat_10,
                        embedded_palette::kTextMuted);
        lv_obj_set_width(receiver_meta, LV_PCT(100));
        addActionPill(receiver, "Open map");
    }

    void requestMissingMapTiles(const MapWorkspaceSnapshot& snapshot)
    {
        if (map_download_jobs_.size() >= 6U) return;
        for (const auto& tile : snapshot.tiles)
        {
            if (tile.available || map_download_jobs_.size() >= 6U) continue;
            map_download_jobs_.push_back(std::async(
                std::launch::async,
                [this, id = tile.id]()
                {
                    return map_model_.ensureTile(id);
                }));
        }
    }

    void refreshMapPreview(bool force)
    {
        for (auto it = map_download_jobs_.begin();
             it != map_download_jobs_.end();)
        {
            if (it->wait_for(std::chrono::milliseconds(0)) ==
                std::future_status::ready)
            {
                it = map_download_jobs_.erase(it);
            }
            else
            {
                ++it;
            }
        }

        if (map_meta_label_ == nullptr) return;
        if (!force && active_section_ != Section::Map) return;
        const auto now = clock::now();
        if (!force &&
            (now - last_map_preview_refresh_) <
                std::chrono::milliseconds(100))
        {
            return;
        }
        last_map_preview_refresh_ = now;

        const auto snapshot = map_model_.snapshot();
        setLabel(map_meta_label_, mapSummary(snapshot));
        setLabel(map_cache_label_, mapCacheSummary(snapshot));
        setLabel(map_download_label_, mapDownloadSummary(snapshot));
        setLabel(map_retry_label_, mapNodeSummary(snapshot));
        for (std::size_t index = 0;
             index < map_tile_cells_.size() && index < snapshot.tiles.size();
             ++index)
        {
            auto* tile = map_tile_cells_[index];
            if (tile == nullptr) continue;
            if (snapshot.tiles[index].available)
            {
                if (map_tile_placeholder_labels_[index] != nullptr)
                {
                    lv_obj_add_flag(map_tile_placeholder_labels_[index],
                                    LV_OBJ_FLAG_HIDDEN);
                }
                if (map_tile_images_[index] == nullptr)
                {
                    auto* image = lv_image_create(tile);
                    resetBox(image);
                    lv_obj_set_size(image, LV_PCT(100), LV_PCT(100));
                    map_tile_images_[index] = image;
                }
                const std::string path =
                    "A:" + snapshot.tiles[index].path.string();
                if (map_tile_paths_[index] != path)
                {
                    map_tile_paths_[index] = path;
                    lv_image_set_inner_align(map_tile_images_[index],
                                             LV_IMAGE_ALIGN_STRETCH);
                    lv_image_set_src(map_tile_images_[index],
                                     map_tile_paths_[index].c_str());
                }
                lv_obj_set_style_bg_color(
                    tile, color(embedded_palette::kMapTile2), 0);
            }
            else
            {
                if (map_tile_images_[index] != nullptr)
                {
                    lv_obj_del(map_tile_images_[index]);
                    map_tile_images_[index] = nullptr;
                }
                if (map_tile_placeholder_labels_[index] != nullptr)
                {
                    char xyz[48] = {};
                    const auto& id = snapshot.tiles[index].id;
                    ::ui::map_tiles::formatMapTileCoordinateLabel(
                        static_cast<std::uint8_t>(id.z),
                        static_cast<std::uint32_t>(id.x),
                        static_cast<std::uint32_t>(id.y),
                        xyz,
                        sizeof(xyz));
                    lv_label_set_text(map_tile_placeholder_labels_[index],
                                      xyz);
                    lv_obj_clear_flag(map_tile_placeholder_labels_[index],
                                      LV_OBJ_FLAG_HIDDEN);
                }
                lv_obj_set_style_bg_color(
                    tile, color(embedded_palette::kSurfaceAlt), 0);
            }
        }
        if (map_download_jobs_.empty())
        {
            requestMissingMapTiles(snapshot);
        }
    }

    void buildRadioToolsPreview()
    {
        lv_obj_t* spectrum = createPreviewPanel(
            desktop_page_panel_, "Energy sweep", 0,
            embedded_palette::kSurface);
        lv_obj_t* chart = lv_obj_create(spectrum);
        applyPanel(chart, embedded_palette::kPlotBg,
                   embedded_palette::kBorder);
        lv_obj_set_width(chart, LV_PCT(100));
        lv_obj_set_height(chart, 280);
        lv_obj_set_style_pad_all(chart, 12, 0);
        lv_obj_set_style_pad_column(chart, 3, 0);
        lv_obj_set_flex_flow(chart, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(chart, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END,
                              LV_FLEX_ALIGN_END);
        constexpr std::array<int, 28> levels{
            42, 48, 44, 55, 50, 62, 71, 95, 128, 176,
            112, 76, 59, 52, 48, 66, 88, 142, 196, 154,
            92, 68, 58, 49, 45, 54, 73, 98};
        for (std::size_t index = 0; index < levels.size(); ++index)
        {
            lv_obj_t* bar = lv_obj_create(chart);
            resetBox(bar);
            lv_obj_set_width(bar, 0);
            lv_obj_set_height(bar, levels[index]);
            lv_obj_set_flex_grow(bar, 1);
            lv_obj_set_style_bg_color(
                bar,
                color(levels[index] > 150 ? embedded_palette::kAccent
                                          : embedded_palette::kStatusBlue),
                0);
            lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(bar, 2, 0);
        }
        createLabel(spectrum,
                    "433.050 - 434.790 MHz / strongest 434.225 MHz at -69.1 dBm",
                    &lv_font_montserrat_12, embedded_palette::kTextWarm);
        addActionPill(spectrum, "Run sweep");

        lv_obj_t* sessions = lv_obj_create(desktop_page_panel_);
        applyTransparent(sessions);
        lv_obj_set_width(sessions, 330);
        lv_obj_set_height(sessions, LV_PCT(100));
        lv_obj_set_style_pad_row(sessions, 10, 0);
        lv_obj_set_flex_flow(sessions, LV_FLEX_FLOW_COLUMN);

        lv_obj_t* sstv = createPreviewPanel(
            sessions, "SSTV receiver", 330, embedded_palette::kSurface);
        addPreviewRow(sstv, "Mode", "Martin M1 / waiting for sync",
                      embedded_palette::kStatusBlue);
        addPreviewRow(sstv, "Last image", "No decoded image yet");
        addActionPill(sstv, "Start receiver");

        lv_obj_t* walkie = createPreviewPanel(
            sessions, "Walkie", 330, embedded_palette::kSurface);
        addPreviewRow(walkie, "Session", "Stopped / capability: Simulated",
                      embedded_palette::kWarn);
        addPreviewRow(walkie, "Channel", "433.175 MHz / monitor off");
        lv_obj_t* actions = lv_obj_create(walkie);
        applyTransparent(actions);
        lv_obj_set_width(actions, LV_PCT(100));
        lv_obj_set_height(actions, 30);
        lv_obj_set_style_pad_column(actions, 6, 0);
        lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
        addActionPill(actions, "Start session");
        addActionPill(actions, "Press PTT", embedded_palette::kSoftBlue,
                      embedded_palette::kText);
    }

    void buildExtensionsPreview()
    {
        lv_obj_t* catalog = createPreviewPanel(
            desktop_page_panel_, "Available packages", 0,
            embedded_palette::kSurface);
        addPreviewRow(catalog, "English language pack",
                      "v1.4 / locale / 86 KB / installed",
                      embedded_palette::kStatusGreen);
        addPreviewRow(catalog, "Noto Sans CJK field font",
                      "v2.1 / font / 4.8 MB / update available",
                      embedded_palette::kAccentDark);
        addPreviewRow(catalog, "Pinyin input method",
                      "v1.2 / IME / 612 KB / available",
                      embedded_palette::kStatusBlue);
        addPreviewRow(catalog, "Emergency symbol set",
                      "v1.0 / assets / 138 KB / available",
                      embedded_palette::kTextWarm);
        addPreviewRow(catalog, "Topographic map style",
                      "v0.9 / map style / 244 KB / available",
                      embedded_palette::kStatusGreen);

        lv_obj_t* details = createPreviewPanel(
            desktop_page_panel_, "Package details", 320,
            embedded_palette::kSurface);
        createLabel(details, "Noto Sans CJK field font",
                    &lv_font_montserrat_16, embedded_palette::kText);
        createLabel(details, "Typography for multilingual field messages,",
                    &lv_font_montserrat_12, embedded_palette::kTextMuted);
        createLabel(details, "maps, node names and team coordination.",
                    &lv_font_montserrat_12, embedded_palette::kTextMuted);
        addPreviewRow(details, "Compatibility", "Desktop / full memory profile",
                      embedded_palette::kStatusGreen);
        addPreviewRow(details, "Storage", "linux-local / 4.8 MB");
        addPreviewRow(details, "Status", "Update available",
                      embedded_palette::kAccentDark);
        addActionPill(details, "Install update");
    }

    void buildGenericFieldPreview(Section section)
    {
        lv_obj_t* primary = createPreviewPanel(
            desktop_page_panel_, sectionTitle(section), 0,
            embedded_palette::kSurface);
        lv_obj_t* secondary = createPreviewPanel(
            desktop_page_panel_, "Desktop actions", 320,
            embedded_palette::kSurface);
        switch (section)
        {
        case Section::Contacts:
            addPreviewRow(primary, "Base Camp",
                          "!435a1002 / Meshtastic / online",
                          embedded_palette::kStatusGreen);
            addPreviewRow(primary, "Trail Scout",
                          "!435a1003 / Meshtastic / 2 min ago",
                          embedded_palette::kStatusBlue);
            addPreviewRow(primary, "MQTT Relay",
                          "!435a1010 / MQTT / trusted",
                          embedded_palette::kTextWarm);
            addPreviewRow(secondary, "Selected node",
                          "Base Camp / key verified");
            addActionPill(secondary, "Open chat");
            addActionPill(secondary, "Request NodeInfo");
            addActionPill(secondary, "Ignore node",
                          embedded_palette::kSoftWarn,
                          embedded_palette::kWarn);
            break;
        case Section::Team:
            addPreviewRow(primary, "Alpha team",
                          "4 members / leader Base Camp",
                          embedded_palette::kTextWarm);
            addPreviewRow(primary, "Trail Scout shared position",
                          "31.2308, 121.4741 / 2 min ago",
                          embedded_palette::kStatusBlue);
            addPreviewRow(primary, "Waypoint updated",
                          "North checkpoint / synced",
                          embedded_palette::kStatusGreen);
            addActionPill(secondary, "Open team chat");
            addActionPill(secondary, "Share current position");
            break;
        case Section::Tracker:
            addPreviewRow(primary, "Recorder", "Recording / GPX",
                          embedded_palette::kWarn);
            addPreviewRow(primary, "Current file",
                          "track-20260723-142830.gpx",
                          embedded_palette::kStatusBlue);
            addPreviewRow(primary, "Sampling", "10 s / GPS 3D fix");
            addActionPill(secondary, "Stop recording",
                          embedded_palette::kSoftWarn,
                          embedded_palette::kWarn);
            addActionPill(secondary, "Open track directory");
            break;
        case Section::Hardware:
            addPreviewRow(primary, "AIO2", "Available / USB serial",
                          embedded_palette::kStatusGreen);
            addPreviewRow(primary, "LoRa SX1262",
                          "Available / spidev1.0",
                          embedded_palette::kStatusGreen);
            addPreviewRow(primary, "GPS", "Available / 3D fix",
                          embedded_palette::kStatusGreen);
            addPreviewRow(primary, "Audio", "Simulated backend",
                          embedded_palette::kWarn);
            addPreviewRow(secondary, "Runtime", "Linux desktop / uConsole");
            addPreviewRow(secondary, "Protocol", "Native Meshtastic");
            break;
        case Section::Data:
            addPreviewRow(primary, "Map cache",
                          "2,418 tiles / 386 MB / healthy",
                          embedded_palette::kStatusGreen);
            addPreviewRow(primary, "Background downloads",
                          "4 active / 2 retrying",
                          embedded_palette::kAccentDark);
            addPreviewRow(primary, "Track files", "18 files / 42 MB");
            addPreviewRow(primary, "Messages", "SQLite / 3,284 records");
            addActionPill(secondary, "Open map workspace");
            addActionPill(secondary, "Retry failed tiles");
            break;
        case Section::Logs:
            addPreviewRow(primary, "14:28:31  LoRa RX",
                          "Meshtastic text / !435a1002 / RSSI -81",
                          embedded_palette::kStatusGreen);
            addPreviewRow(primary, "14:28:30  GPS",
                          "GGA / 3D fix / 8 satellites",
                          embedded_palette::kStatusBlue);
            addPreviewRow(primary, "14:28:28  MQTT RX",
                          "msh/CN/2/e/LongFast / decoded",
                          embedded_palette::kTextWarm);
            addPreviewRow(secondary, "Source filter", "LoRa");
            addActionPill(secondary, "GPS");
            addActionPill(secondary, "LoRa");
            addActionPill(secondary, "MQTT");
            break;
        case Section::Settings:
            addPreviewRow(primary, "Radio protocol", "Meshtastic");
            addPreviewRow(primary, "Region and preset",
                          "CN / LongFast / 433.175 MHz");
            addPreviewRow(primary, "GPS", "Enabled / 5 s interval");
            addPreviewRow(primary, "Map", "Terrain / zoom 14 / auto download");
            addPreviewRow(secondary, "Configuration", "Unsaved changes: 0");
            addActionPill(secondary, "Apply settings");
            addActionPill(secondary, "Reload");
            break;
        default:
            break;
        }
    }

    void rebuildDesktopPage()
    {
        if (desktop_page_panel_ == nullptr)
        {
            return;
        }
        lv_obj_clean(desktop_page_panel_);
        switch (active_section_)
        {
        case Section::Map:
            buildMapPreview();
            break;
        case Section::Gps:
            buildGpsPreview();
            break;
        case Section::RadioTools:
            buildRadioToolsPreview();
            break;
        case Section::Extensions:
            buildExtensionsPreview();
            break;
        case Section::Contacts:
        case Section::Team:
        case Section::Tracker:
        case Section::Hardware:
        case Section::Data:
        case Section::Logs:
        case Section::Settings:
            buildGenericFieldPreview(active_section_);
            break;
        case Section::Overview:
        case Section::Chat:
            break;
        }
    }

    void buildChatConversationRow(lv_obj_t* parent, int index)
    {
        chat_conversation_bindings_[index] = {this,
                                              static_cast<std::size_t>(index)};
        lv_obj_t* button = lv_btn_create(parent);
        resetBox(button);
        lv_obj_set_width(button, LV_PCT(100));
        lv_obj_set_height(button, 74);
        lv_obj_set_style_radius(button, 6, 0);
        lv_obj_set_style_pad_all(button, 8, 0);
        lv_obj_set_style_bg_color(button,
                                  color(embedded_palette::kPanelBg), 0);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
        lv_obj_set_flex_flow(button, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_add_event_cb(button, chatConversationEventCb, LV_EVENT_CLICKED,
                            &chat_conversation_bindings_[index]);
        lv_obj_add_event_cb(button, chatConversationEventCb, LV_EVENT_KEY,
                            &chat_conversation_bindings_[index]);
        lv_group_add_obj(group_, button);

        chat_conversation_buttons_[index] = button;
        chat_conversation_title_labels_[index] =
            createLabel(button, "-", &lv_font_montserrat_14,
                        embedded_palette::kText);
        chat_conversation_preview_labels_[index] =
            createLabel(button, "", &lv_font_montserrat_12,
                        embedded_palette::kTextMuted);
        chat_conversation_meta_labels_[index] =
            createLabel(button, "", &lv_font_montserrat_12,
                        embedded_palette::kTextDim);
        lv_obj_set_width(chat_conversation_title_labels_[index], LV_PCT(100));
        lv_obj_set_width(chat_conversation_preview_labels_[index], LV_PCT(100));
        lv_obj_set_width(chat_conversation_meta_labels_[index], LV_PCT(100));
    }

    void buildChatMessageRow(int index)
    {
        lv_obj_t* row = lv_obj_create(chat_messages_panel_);
        applyPanel(row, embedded_palette::kSurface);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 72);
        lv_obj_set_style_pad_all(row, 8, 0);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);

        chat_message_rows_[index] = row;
        chat_message_sender_labels_[index] =
            createLabel(row, "-", &lv_font_montserrat_12,
                        embedded_palette::kTextWarm);
        chat_message_text_labels_[index] =
            createLabel(row, "", &lv_font_montserrat_14,
                        embedded_palette::kText, LV_LABEL_LONG_WRAP);
        chat_message_meta_labels_[index] =
            createLabel(row, "", &lv_font_montserrat_12,
                        embedded_palette::kTextMuted);
        lv_obj_set_width(chat_message_sender_labels_[index], LV_PCT(100));
        lv_obj_set_width(chat_message_text_labels_[index], LV_PCT(100));
        lv_obj_set_width(chat_message_meta_labels_[index], LV_PCT(100));
    }

    void buildStatusPanel(lv_obj_t* parent)
    {
        status_panel_ = lv_obj_create(parent);
        applyPanel(status_panel_, embedded_palette::kSurface);
        lv_obj_set_width(status_panel_, 330);
        lv_obj_set_height(status_panel_, LV_PCT(100));
        lv_obj_set_style_pad_row(status_panel_, 12, 0);
        lv_obj_set_flex_flow(status_panel_, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(status_panel_, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

        createLabel(status_panel_, "Runtime and capabilities",
                    &lv_font_montserrat_16, embedded_palette::kText);
        for (int index = 0; index < kCapabilityRows; ++index)
        {
            capability_labels_[index] =
                createLabel(status_panel_, "-", &lv_font_montserrat_12,
                            embedded_palette::kTextMuted,
                            LV_LABEL_LONG_WRAP);
            lv_obj_set_width(capability_labels_[index], LV_PCT(100));
        }

        lv_obj_t* divider = lv_obj_create(status_panel_);
        resetBox(divider);
        lv_obj_set_width(divider, LV_PCT(100));
        lv_obj_set_height(divider, 1);
        lv_obj_set_style_bg_color(divider,
                                  color(embedded_palette::kSeparator), 0);
        lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);

        createLabel(status_panel_, "Contacts and nearby nodes",
                    &lv_font_montserrat_16, embedded_palette::kText);
        for (int index = 0; index < kContactRows; ++index)
        {
            buildContactRow(status_panel_, index);
        }
    }

    void buildContactRow(lv_obj_t* parent, int index)
    {
        lv_obj_t* row = lv_obj_create(parent);
        applyTransparent(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 48);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START,
                              LV_FLEX_ALIGN_START);

        contact_rows_[index] = row;
        contact_name_labels_[index] =
            createLabel(row, "-", &lv_font_montserrat_14,
                        embedded_palette::kText);
        lv_obj_set_width(contact_name_labels_[index], LV_PCT(100));
        contact_meta_labels_[index] =
            createLabel(row, "", &lv_font_montserrat_12,
                        embedded_palette::kTextMuted);
        lv_obj_set_width(contact_meta_labels_[index], LV_PCT(100));
    }

    void refreshNavStyles()
    {
        const auto active_index = static_cast<std::size_t>(active_section_);
        for (std::size_t index = 0; index < nav_buttons_.size(); ++index)
        {
            lv_obj_t* button = nav_buttons_[index];
            if (button == nullptr) continue;
            const bool active = index == active_index;
            lv_obj_set_style_bg_color(
                button,
                color(active ? embedded_palette::kAccent
                             : embedded_palette::kSurface),
                0);
            lv_obj_set_style_border_width(button, active ? 1 : 0, 0);
            lv_obj_set_style_border_color(
                button, color(embedded_palette::kBorder), 0);
            lv_obj_t* label = lv_obj_get_child(button, 0);
            if (label != nullptr)
            {
                lv_obj_set_style_text_color(
                    label,
                    color(active ? embedded_palette::kHeaderText
                                 : embedded_palette::kText),
                    0);
            }
        }
    }

    void refreshSectionVisibility()
    {
        const bool overview_active = active_section_ == Section::Overview;
        const bool chat_active = active_section_ == Section::Chat;
        const bool desktop_page_active = !overview_active && !chat_active;
        if (metrics_panel_ != nullptr)
        {
            if (!overview_active)
                lv_obj_add_flag(metrics_panel_, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_clear_flag(metrics_panel_, LV_OBJ_FLAG_HIDDEN);
        }
        if (conversation_panel_ != nullptr)
        {
            if (!overview_active)
                lv_obj_add_flag(conversation_panel_, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_clear_flag(conversation_panel_, LV_OBJ_FLAG_HIDDEN);
        }
        if (chat_panel_ != nullptr)
        {
            if (chat_active)
                lv_obj_clear_flag(chat_panel_, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(chat_panel_, LV_OBJ_FLAG_HIDDEN);
        }
        if (desktop_page_panel_ != nullptr)
        {
            if (desktop_page_active)
                lv_obj_clear_flag(desktop_page_panel_, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_add_flag(desktop_page_panel_, LV_OBJ_FLAG_HIDDEN);
        }
        if (status_panel_ != nullptr)
        {
            lv_obj_add_flag(status_panel_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    void refreshDashboard(bool force)
    {
        const auto now = clock::now();
        if (!force && (now - last_refresh_) < std::chrono::milliseconds(500))
        {
            return;
        }
        last_refresh_ = now;

        const UConsoleDashboardSnapshot snapshot = dashboard_model_.snapshot();

        setLabel(top_mesh_label_, "Mesh: " + snapshot.mesh_protocol);
        setLabel(top_node_label_, "Node: " + snapshot.self_node);
        setLabel(top_unread_label_, "Unread: " + formatCount(snapshot.unread_count));
        setLabel(footer_status_label_,
                 snapshot.mesh_protocol + "  /  node " + snapshot.self_node +
                     "  /  " + formatCount(snapshot.unread_count) +
                     " unread");

        setLabel(metric_value_labels_[0],
                 formatCount(snapshot.conversation_count));
        setLabel(metric_value_labels_[1], formatCount(snapshot.unread_count));
        setLabel(metric_value_labels_[2], formatCount(snapshot.contact_count));
        setLabel(metric_value_labels_[3], formatCount(snapshot.nearby_count));

        for (int index = 0; index < kConversationRows; ++index)
        {
            const bool visible =
                index < static_cast<int>(snapshot.conversations.size());
            if (visible)
            {
                lv_obj_clear_flag(conversation_rows_[index],
                                  LV_OBJ_FLAG_HIDDEN);
                const auto& item = snapshot.conversations[index];
                setLabel(conversation_title_labels_[index], item.title);
                setLabel(conversation_preview_labels_[index], item.preview);
                setLabel(conversation_meta_labels_[index], item.meta);
                lv_obj_set_style_text_color(
                    conversation_meta_labels_[index],
                    color(item.unread > 0 ? embedded_palette::kAccentDark
                                          : embedded_palette::kTextDim),
                    0);
            }
            else
            {
                lv_obj_add_flag(conversation_rows_[index],
                                LV_OBJ_FLAG_HIDDEN);
            }
        }

        for (int index = 0; index < kCapabilityRows; ++index)
        {
            if (index < static_cast<int>(snapshot.capability_lines.size()))
            {
                setLabel(capability_labels_[index],
                         snapshot.capability_lines[index]);
            }
            else
            {
                setLabel(capability_labels_[index], "");
            }
        }

        for (int index = 0; index < kContactRows; ++index)
        {
            const bool visible = index < static_cast<int>(snapshot.contacts.size());
            if (visible)
            {
                lv_obj_clear_flag(contact_rows_[index], LV_OBJ_FLAG_HIDDEN);
                const auto& contact = snapshot.contacts[index];
                setLabel(contact_name_labels_[index], contact.name);
                setLabel(contact_meta_labels_[index],
                         contact.node_id + " / " + contact.protocol + " / " +
                             contact.status);
            }
            else
            {
                lv_obj_add_flag(contact_rows_[index], LV_OBJ_FLAG_HIDDEN);
            }
        }

        refreshChatWorkspace(force);
    }

    void refreshChatWorkspace(bool force)
    {
        if (chat_panel_ == nullptr) return;
        if (!force && active_section_ != Section::Chat) return;

        const ChatWorkspaceSnapshot snapshot =
            chat_model_.snapshot(kChatConversationRows, kChatMessageRows);

        setLabel(chat_title_label_, snapshot.active_title);
        setLabel(chat_meta_label_, snapshot.active_meta);
        setLabel(chat_status_label_,
                 snapshot.action_status.empty() ? "Ready."
                                                : snapshot.action_status);

        for (int index = 0; index < kChatConversationRows; ++index)
        {
            const bool visible =
                index < static_cast<int>(snapshot.conversations.size());
            if (!visible)
            {
                lv_obj_add_flag(chat_conversation_buttons_[index],
                                LV_OBJ_FLAG_HIDDEN);
                continue;
            }

            lv_obj_clear_flag(chat_conversation_buttons_[index],
                              LV_OBJ_FLAG_HIDDEN);
            const auto& item = snapshot.conversations[index];
            setLabel(chat_conversation_title_labels_[index], item.title);
            setLabel(chat_conversation_preview_labels_[index], item.preview);
            setLabel(chat_conversation_meta_labels_[index], item.meta);
            lv_obj_set_style_bg_color(
                chat_conversation_buttons_[index],
                color(item.active ? embedded_palette::kSoftAmber
                                  : embedded_palette::kPanelBg),
                0);
            lv_obj_set_style_border_width(chat_conversation_buttons_[index],
                                          item.active ? 1 : 0, 0);
            lv_obj_set_style_border_color(
                chat_conversation_buttons_[index],
                color(embedded_palette::kBorder), 0);
            lv_obj_set_style_text_color(
                chat_conversation_meta_labels_[index],
                color(item.unread > 0 ? embedded_palette::kAccentDark
                                      : embedded_palette::kTextMuted),
                0);
        }

        const bool has_messages = !snapshot.messages.empty();
        if (chat_empty_label_ != nullptr)
        {
            if (has_messages)
                lv_obj_add_flag(chat_empty_label_, LV_OBJ_FLAG_HIDDEN);
            else
                lv_obj_clear_flag(chat_empty_label_, LV_OBJ_FLAG_HIDDEN);
        }

        for (int index = 0; index < kChatMessageRows; ++index)
        {
            const bool visible =
                index < static_cast<int>(snapshot.messages.size());
            if (!visible)
            {
                lv_obj_add_flag(chat_message_rows_[index], LV_OBJ_FLAG_HIDDEN);
                continue;
            }

            lv_obj_clear_flag(chat_message_rows_[index], LV_OBJ_FLAG_HIDDEN);
            const auto& item = snapshot.messages[index];
            setLabel(chat_message_sender_labels_[index], item.sender);
            setLabel(chat_message_text_labels_[index], item.text);
            setLabel(chat_message_meta_labels_[index], item.meta);
            lv_obj_set_style_bg_color(
                chat_message_rows_[index],
                color(item.failed
                          ? embedded_palette::kSoftWarn
                          : (item.outgoing ? embedded_palette::kSoftGreen
                                           : embedded_palette::kSurface)),
                0);
            lv_obj_set_style_text_color(
                chat_message_meta_labels_[index],
                color(item.failed ? embedded_palette::kError
                                  : embedded_palette::kTextMuted),
                0);
        }
    }

    linux_app::LinuxAppServices services_;
    UConsoleDashboardModel dashboard_model_;
    UConsoleChatWorkspaceModel chat_model_;
    UConsoleMapWorkspaceModel map_model_;
    bool initialized_ = false;
    bool sidebar_collapsed_ = false;
    bool shortcuts_visible_ = false;
    Section active_section_ = Section::Overview;
    lv_group_t* group_ = nullptr;
    std::deque<QueuedKeyEvent> key_events_{};
    clock::time_point last_refresh_{};

    lv_obj_t* sidebar_ = nullptr;
    lv_obj_t* workspace_title_ = nullptr;
    lv_obj_t* workspace_subtitle_ = nullptr;
    lv_obj_t* top_mesh_label_ = nullptr;
    lv_obj_t* top_node_label_ = nullptr;
    lv_obj_t* top_unread_label_ = nullptr;
    lv_obj_t* footer_context_label_ = nullptr;
    lv_obj_t* footer_status_label_ = nullptr;
    lv_obj_t* footer_shortcuts_row_ = nullptr;
    lv_obj_t* shortcut_overlay_ = nullptr;
    lv_obj_t* metrics_panel_ = nullptr;
    lv_obj_t* conversation_panel_ = nullptr;
    lv_obj_t* chat_panel_ = nullptr;
    lv_obj_t* desktop_page_panel_ = nullptr;
    lv_obj_t* status_panel_ = nullptr;
    lv_obj_t* chat_messages_panel_ = nullptr;
    lv_obj_t* chat_title_label_ = nullptr;
    lv_obj_t* chat_meta_label_ = nullptr;
    lv_obj_t* chat_empty_label_ = nullptr;
    lv_obj_t* chat_input_ = nullptr;
    lv_obj_t* chat_send_button_ = nullptr;
    lv_obj_t* chat_status_label_ = nullptr;
    lv_obj_t* map_meta_label_ = nullptr;
    lv_obj_t* map_cache_label_ = nullptr;
    lv_obj_t* map_download_label_ = nullptr;
    lv_obj_t* map_retry_label_ = nullptr;
    std::array<lv_obj_t*, kMaxMapPreviewTiles> map_tile_cells_{};
    std::array<lv_obj_t*, kMaxMapPreviewTiles> map_tile_images_{};
    std::array<lv_obj_t*, kMaxMapPreviewTiles>
        map_tile_placeholder_labels_{};
    std::array<std::string, kMaxMapPreviewTiles> map_tile_paths_{};
    std::vector<std::future<::platform::linux_runtime::MapTileResult>>
        map_download_jobs_{};
    clock::time_point last_map_preview_refresh_{};

    std::array<NavBinding, kNavLabels.size()> nav_bindings_{};
    std::array<lv_obj_t*, kNavLabels.size()> nav_buttons_{};
    std::array<lv_obj_t*, kMetricCount> metric_value_labels_{};
    std::array<lv_obj_t*, kConversationRows> conversation_rows_{};
    std::array<lv_obj_t*, kConversationRows> conversation_title_labels_{};
    std::array<lv_obj_t*, kConversationRows> conversation_preview_labels_{};
    std::array<lv_obj_t*, kConversationRows> conversation_meta_labels_{};
    std::array<lv_obj_t*, kCapabilityRows> capability_labels_{};
    std::array<lv_obj_t*, kContactRows> contact_rows_{};
    std::array<lv_obj_t*, kContactRows> contact_name_labels_{};
    std::array<lv_obj_t*, kContactRows> contact_meta_labels_{};
    std::array<ChatConversationBinding, kChatConversationRows>
        chat_conversation_bindings_{};
    std::array<lv_obj_t*, kChatConversationRows> chat_conversation_buttons_{};
    std::array<lv_obj_t*, kChatConversationRows>
        chat_conversation_title_labels_{};
    std::array<lv_obj_t*, kChatConversationRows>
        chat_conversation_preview_labels_{};
    std::array<lv_obj_t*, kChatConversationRows>
        chat_conversation_meta_labels_{};
    std::array<lv_obj_t*, kChatMessageRows> chat_message_rows_{};
    std::array<lv_obj_t*, kChatMessageRows> chat_message_sender_labels_{};
    std::array<lv_obj_t*, kChatMessageRows> chat_message_text_labels_{};
    std::array<lv_obj_t*, kChatMessageRows> chat_message_meta_labels_{};
};

class UConsoleLvglHost
{
  public:
    UConsoleLvglHost(UConsoleDesktopShell& shell,
                     ::trailmate::cardputer_zero::platform::SurfacePresenter&
                         presenter,
                     UConsoleShellOptions options)
        : shell_(shell),
          presenter_(presenter),
          options_(validateOptions(options)),
          canvas_(options_.width, options_.height),
          frame_buffer_(static_cast<std::size_t>(options_.width) *
                            static_cast<std::size_t>(options_.height),
                        0)
    {
        g_lvgl_start_time = clock::now();
        lv_init();
        lv_tick_set_cb(tickNow);

        display_ = lv_display_create(options_.width, options_.height);
        if (display_ == nullptr)
        {
            throw std::runtime_error(
                "Failed to create LVGL display for uConsole shell.");
        }
        lv_display_set_default(display_);
        lv_display_set_user_data(display_, this);
        lv_display_set_color_format(display_, LV_COLOR_FORMAT_RGB565);
        lv_display_set_buffers(
            display_, frame_buffer_.data(), nullptr,
            static_cast<std::uint32_t>(frame_buffer_.size() *
                                       sizeof(std::uint16_t)),
            LV_DISPLAY_RENDER_MODE_FULL);
        lv_display_set_flush_cb(display_, flushCb);

        if (!shell_.begin())
        {
            throw std::runtime_error("Failed to begin uConsole desktop shell.");
        }

        keypad_ = lv_indev_create();
        if (keypad_ == nullptr)
        {
            throw std::runtime_error(
                "Failed to create LVGL keypad input for uConsole shell.");
        }
        lv_indev_set_type(keypad_, LV_INDEV_TYPE_KEYPAD);
        lv_indev_set_display(keypad_, display_);
        lv_indev_set_user_data(keypad_, this);
        lv_indev_set_read_cb(keypad_, readInputCb);
        if (shell_.inputGroup() != nullptr)
        {
            lv_indev_set_group(keypad_, shell_.inputGroup());
        }

        if (presenter_.supportsPointer())
        {
            pointer_ = lv_indev_create();
            if (pointer_ == nullptr)
            {
                throw std::runtime_error(
                    "Failed to create pointer input for uConsole shell.");
            }
            lv_indev_set_type(pointer_, LV_INDEV_TYPE_POINTER);
            lv_indev_set_display(pointer_, display_);
            lv_indev_set_user_data(pointer_, this);
            lv_indev_set_read_cb(pointer_, readPointerCb);
        }

        tick();
    }

    ~UConsoleLvglHost()
    {
        if (keypad_ != nullptr)
        {
            lv_indev_delete(keypad_);
            keypad_ = nullptr;
        }
        if (pointer_ != nullptr)
        {
            lv_indev_delete(pointer_);
            pointer_ = nullptr;
        }
        shell_.releaseLvglObjects();
        if (display_ != nullptr)
        {
            lv_display_delete(display_);
            display_ = nullptr;
        }
        lv_deinit();
    }

    void tick()
    {
        shell_.tick();
        lv_timer_handler();
        if (dirty_)
        {
            copyFrameBufferToCanvas();
            dirty_ = false;
        }
    }

    [[nodiscard]] const Canvas& canvas() const noexcept
    {
        return canvas_;
    }

    static void flushCb(lv_display_t* display,
                        const lv_area_t* /*area*/,
                        std::uint8_t* /*px_map*/)
    {
        auto* host =
            static_cast<UConsoleLvglHost*>(lv_display_get_user_data(display));
        if (host != nullptr) host->dirty_ = true;
        lv_display_flush_ready(display);
    }

    static void readInputCb(lv_indev_t* indev, lv_indev_data_t* data)
    {
        auto* host =
            static_cast<UConsoleLvglHost*>(lv_indev_get_user_data(indev));
        if (host == nullptr || data == nullptr) return;

        data->state = LV_INDEV_STATE_RELEASED;
        data->key = 0U;

        std::uint32_t key = 0U;
        lv_indev_state_t state = LV_INDEV_STATE_RELEASED;
        if (!host->shell_.dequeueKeyEvent(&key, &state)) return;

        data->state = state;
        data->key = key;
        data->continue_reading = host->shell_.hasPendingKeyEvent();
    }

    static void readPointerCb(lv_indev_t* indev, lv_indev_data_t* data)
    {
        auto* host =
            static_cast<UConsoleLvglHost*>(lv_indev_get_user_data(indev));
        if (host == nullptr || data == nullptr) return;

        const auto pointer = host->presenter_.pointerState();
        data->point.x = pointer.x;
        data->point.y = pointer.y;
        data->state = pointer.pressed ? LV_INDEV_STATE_PRESSED
                                      : LV_INDEV_STATE_RELEASED;
    }

  private:
    void copyFrameBufferToCanvas()
    {
        for (int y = 0; y < options_.height; ++y)
        {
            for (int x = 0; x < options_.width; ++x)
            {
                const auto index =
                    static_cast<std::size_t>((y * options_.width) + x);
                canvas_.setPixel(x, y, rgb565ToColor(frame_buffer_[index]));
            }
        }
    }

    UConsoleDesktopShell& shell_;
    ::trailmate::cardputer_zero::platform::SurfacePresenter& presenter_;
    UConsoleShellOptions options_{};
    lv_display_t* display_ = nullptr;
    lv_indev_t* keypad_ = nullptr;
    lv_indev_t* pointer_ = nullptr;
    Canvas canvas_;
    std::vector<std::uint16_t> frame_buffer_{};
    bool dirty_ = true;
};

} // namespace

void runUConsoleShell(::trailmate::cardputer_zero::platform::SurfacePresenter& presenter,
                      UConsoleShellOptions options)
{
    options = validateOptions(options);
    UConsoleDesktopShell shell;
    UConsoleLvglHost host{shell, presenter, options};

    auto next_frame = clock::now();
    const auto frame_time = std::chrono::milliseconds(options.frame_time_ms);

    while (presenter.pump())
    {
        shell.enqueueInputs(presenter.drainInput());
        host.tick();
        presenter.present(host.canvas());

        next_frame += frame_time;
        std::this_thread::sleep_until(next_frame);

        if (clock::now() > next_frame + std::chrono::milliseconds(250))
        {
            next_frame = clock::now();
        }
    }
}

} // namespace trailmate::uconsole
