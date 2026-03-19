#include "ui/mono_128x64/runtime.h"

#include "app/app_config.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/infra/meshcore/mc_region_presets.h"
#include "chat/infra/meshtastic/mt_region.h"
#include "chat/runtime/self_identity_policy.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace ui::mono_128x64
{
namespace
{
const char* inputActionName(InputAction action)
{
    switch (action)
    {
    case InputAction::None: return "None";
    case InputAction::Up: return "Up";
    case InputAction::Down: return "Down";
    case InputAction::Left: return "Left";
    case InputAction::Right: return "Right";
    case InputAction::Select: return "Select";
    case InputAction::Back: return "Back";
    case InputAction::Primary: return "Primary";
    case InputAction::Secondary: return "Secondary";
    default: return "?";
    }
}


constexpr const char* kMainMenuItems[] = {
    "CHATS",
    "NEW MESSAGE",
    "SETTINGS",
    "IDENTITY",
    "RADIO",
    "DEVICE",
    "GNSS",
    "ACTIONS",
};

constexpr const char* kSettingsMenuItems[] = {
    "IDENTITY",
    "RADIO",
    "DEVICE",
};

constexpr const char* kIdentityItems[] = {
    "USER NAME",
    "SHORT NAME",
};

constexpr const char* kRadioItems[] = {
    "PROTOCOL",
    "TX POWER",
    "REGION",
    "PRESET",
    "CHANNEL",
    "ENCRYPT",
    "PSK/Name",
};

constexpr const char* kDeviceItems[] = {
    "BLE",
    "TIME ZONE",
    "GPS",
    "GPS INTERVAL",
    "CHAT CHANNEL",
};

constexpr const char* kActionItems[] = {
    "BROADCAST ID",
    "CLEAR NODES",
    "CLEAR MSGS",
    "RESET RADIO",
};

constexpr const char* kMessageMenuItems[] = {
    "INFO",
    "REPLY",
};

constexpr const char* kWeekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
constexpr const char* kComposeCharset =
    " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.,!?-_/:#@+*=()[]";
constexpr const char* kHexCharset = "0123456789ABCDEF";
constexpr const char* kComposeAbcKeys = " ETAOINSHRDLUCMFWYPVBGKQJXZ";
constexpr const char* kComposeNumKeys = "0123456789";
constexpr const char* kComposeSymKeys = " .,!?/-:@#()[]+=";
constexpr const char* kComposeActionLabels[] = {"ABC", "SP", "BACK", "DEL", "SEND"};
struct ComposeGroupDef
{
    const char* input;
    const char* label;
};

constexpr ComposeGroupDef kComposeAbcGroups[] = {
    {"ABC", "ABC"},
    {"DEF", "DEF"},
    {"GHI", "GHI"},
    {"JKL", "JKL"},
    {"!/-", "!/-"},
    {"MNO", "MNO"},
    {"PQRS", "PQRS"},
    {"TUV", "TUV"},
    {"WXYZ", "WXYZ"},
    {".,?", ".,?"},
};
constexpr char kSelectedItemMarker[] = "\xE2\x97\x8F";
constexpr uint32_t kBootMinMs = 1800;
constexpr uint32_t kComposeMultiTapWindowMs = 700;
constexpr int kTimezoneMin = -12 * 60;
constexpr int kTimezoneMax = 14 * 60;
constexpr int kTimezoneStep = 60;
constexpr const char* kHamTerms[] = {
    "73", "88", "AGN", "ALFA", "ATT", "BREAK", "BRAVO", "CALL", "CHARLIE", "COPY", "CQ", "CTCSS", "DE",
    "DELTA", "DIRECT", "DUPLEX", "DX", "ECHO", "ES", "FB", "FM", "FREQ", "FT8", "FT4", "HOTEL", "ID",
    "INDIA", "INFO", "JA", "JULIETT", "KILO", "LIMA", "LOG", "LSB", "MIKE", "NIL", "NOVEMBER", "OM",
    "OSCAR", "OUT", "OVER", "PAPA", "PSE", "QRL", "QRM", "QRN", "QRP", "QRQ", "QRS", "QRT", "QRV",
    "QRZ", "QSB", "QSL", "QSO", "QSP", "QSX", "QSY", "QTC", "QTH", "QTR", "QUEBEC", "RELAY", "REPEATER",
    "RIG", "ROGER", "ROMEO", "RST", "RX", "SEND", "SIERRA", "SIMPLEX", "SOLID", "SPLIT", "STANDBY", "SYM",
    "TANGO", "TKS", "TNX", "TU", "TX", "UNIFORM", "USB", "VICTOR", "WAIT", "WHISKEY", "X-RAY", "XYL",
    "YANKEE", "YL", "ZULU",
};

template <typename T, size_t N>
constexpr size_t arrayCount(const T (&)[N])
{
    return N;
}

template <typename T>
constexpr T clampValue(T value, T low, T high)
{
    return value < low ? low : (value > high ? high : value);
}

template <size_t N>
void copyText(char (&dst)[N], const char* src)
{
    if (!src)
    {
        dst[0] = '\0';
        return;
    }
    std::strncpy(dst, src, N - 1);
    dst[N - 1] = '\0';
}

void appendChar(char* buffer, size_t capacity, size_t& len, char ch)
{
    if (!buffer || capacity == 0 || len + 1 >= capacity)
    {
        return;
    }
    buffer[len++] = ch;
    buffer[len] = '\0';
}

void popChar(char* buffer, size_t& len)
{
    if (!buffer || len == 0)
    {
        return;
    }
    --len;
    buffer[len] = '\0';
}

const char* composeKeysetForMode(ui::mono_128x64::Runtime::ComposeMode mode)
{
    switch (mode)
    {
    case ui::mono_128x64::Runtime::ComposeMode::Num: return kComposeNumKeys;
    case ui::mono_128x64::Runtime::ComposeMode::Sym: return kComposeSymKeys;
    case ui::mono_128x64::Runtime::ComposeMode::AbcUpper:
    case ui::mono_128x64::Runtime::ComposeMode::AbcLower:
    default: return kComposeAbcKeys;
    }
}

const char* composeModeLabel(ui::mono_128x64::Runtime::ComposeMode mode)
{
    switch (mode)
    {
    case ui::mono_128x64::Runtime::ComposeMode::AbcLower: return "abc";
    case ui::mono_128x64::Runtime::ComposeMode::AbcUpper: return "ABC";
    case ui::mono_128x64::Runtime::ComposeMode::Num: return "123";
    case ui::mono_128x64::Runtime::ComposeMode::Sym: return "SYM";
    default: return "ABC";
    }
}

bool isAlphaComposeMode(ui::mono_128x64::Runtime::ComposeMode mode)
{
    return mode == ui::mono_128x64::Runtime::ComposeMode::AbcLower ||
           mode == ui::mono_128x64::Runtime::ComposeMode::AbcUpper;
}

char applyComposeAlphaCase(ui::mono_128x64::Runtime::ComposeMode mode, char ch)
{
    if (std::isalpha(static_cast<unsigned char>(ch)) == 0)
    {
        return ch;
    }

    if (mode == ui::mono_128x64::Runtime::ComposeMode::AbcLower)
    {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
}

char upperAscii(char ch)
{
    return static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
}

bool isAlphaAscii(char ch)
{
    return std::isalpha(static_cast<unsigned char>(ch)) != 0;
}

bool hasPrefixIgnoreCase(const char* text, const char* prefix)
{
    if (!text || !prefix)
    {
        return false;
    }
    while (*prefix)
    {
        if (*text == '\0' || upperAscii(*text) != upperAscii(*prefix))
        {
            return false;
        }
        ++text;
        ++prefix;
    }
    return true;
}

const char* composeAbcGroupLetters(size_t index)
{
    return index < arrayCount(kComposeAbcGroups) ? kComposeAbcGroups[index].input : "";
}

const char* composeAbcGroupLabel(size_t index)
{
    return index < arrayCount(kComposeAbcGroups) ? kComposeAbcGroups[index].label : "";
}

const char* protocolShortLabel(chat::MeshProtocol protocol)
{
    return protocol == chat::MeshProtocol::MeshCore ? "MC" : "MT";
}

template <size_t N>
void appendInfoLine(char (&dst)[N], const char* label, const char* value)
{
    if (!label || !value)
    {
        dst[0] = '\0';
        return;
    }
    std::snprintf(dst, N, "%s:%s", label, value);
}

template <size_t N>
void setInfoSection(char (&dst)[N], const char* title)
{
    if (!title)
    {
        dst[0] = '\0';
        return;
    }
    std::snprintf(dst, N, "[%s]", title);
}

bool encryptEnabled(const app::AppConfig& config)
{
    return config.privacy_encrypt_mode != 0;
}

void setEncryptEnabled(app::AppConfig& config, bool enabled)
{
    config.privacy_encrypt_mode = enabled ? 1 : 0;
}

void bytesToHex(const uint8_t* data, size_t len, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }

    out[0] = '\0';
    if (!data || len == 0)
    {
        return;
    }

    size_t pos = 0;
    for (size_t i = 0; i < len && pos + 2 < out_len; ++i)
    {
        const int written = std::snprintf(out + pos, out_len - pos, "%02X", static_cast<unsigned>(data[i]));
        if (written <= 0)
        {
            break;
        }
        pos += static_cast<size_t>(written);
    }
}

bool hexToBytes(const char* hex, uint8_t* out, size_t out_len)
{
    if (!hex || !out)
    {
        return false;
    }

    const size_t hex_len = std::strlen(hex);
    if (hex_len != out_len * 2U)
    {
        return false;
    }

    for (size_t i = 0; i < out_len; ++i)
    {
        char part[3] = {hex[i * 2U], hex[i * 2U + 1U], '\0'};
        char* end = nullptr;
        const long value = std::strtol(part, &end, 16);
        if (!end || *end != '\0' || value < 0 || value > 255)
        {
            return false;
        }
        out[i] = static_cast<uint8_t>(value);
    }
    return true;
}

} // namespace

Runtime::Runtime(MonoDisplay& display, const HostCallbacks& host)
    : display_(display),
      text_renderer_(host.ui_font ? *host.ui_font : builtin_ui_font()),
      host_(host)
{
}

bool Runtime::begin()
{
    if (initialized_)
    {
        return true;
    }
    initialized_ = display_.begin();
    boot_started_ms_ = nowMs();
    page_entered_ms_ = boot_started_ms_;
    return initialized_;
}

void Runtime::appendBootLog(const char* line)
{
    if (!line || line[0] == '\0')
    {
        return;
    }

    if (boot_log_count_ < kBootLogLines)
    {
        copyText(boot_log_[boot_log_count_], line);
        ++boot_log_count_;
        return;
    }

    for (size_t i = 1; i < kBootLogLines; ++i)
    {
        std::memcpy(boot_log_[i - 1], boot_log_[i], sizeof(boot_log_[i - 1]));
    }
    copyText(boot_log_[kBootLogLines - 1], line);
}

void Runtime::tick(InputAction action)
{
    if (!begin())
    {
        return;
    }

    ensureBootExit();
    handleInput(action);
    render();
}

void Runtime::bindChatObservers()
{
    if (chat_observers_bound_ || !app())
    {
        return;
    }

    app()->getChatService().addIncomingTextObserver(this);
    chat_observers_bound_ = true;
}

void Runtime::onIncomingText(const chat::MeshIncomingText& msg)
{
    MessageMetaEntry& entry = message_meta_[message_meta_cursor_ % kMessageMetaCapacity];
    entry.used = true;
    entry.protocol = app() ? app()->getChatService().getActiveProtocol() : chat::MeshProtocol::Meshtastic;
    entry.channel = msg.channel;
    entry.from = msg.from;
    entry.msg_id = msg.msg_id;
    entry.to = msg.to;
    entry.hop_limit = msg.hop_limit;
    entry.encrypted = msg.encrypted;
    entry.rx_meta = msg.rx_meta;
    ++message_meta_cursor_;
}

void Runtime::handleInput(InputAction action)
{
    if (page_ == Page::Compose && action != InputAction::None && host_.debug_log_fn)
    {
        char preedit[32] = {};
        for (size_t i = 0; i < compose_preedit_len_ && i + 1 < sizeof(preedit); ++i)
        {
            preedit[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(compose_preedit_[i])));
            preedit[i + 1] = '\0';
        }

        char line[160] = {};
        std::snprintf(line, sizeof(line),
                      "[gat562][ui] compose action=%s focus=%u mode=%u group=%u tap=%u preedit=\"%s\" cand=%u body_len=%u\n",
                      inputActionName(action),
                      static_cast<unsigned>(compose_focus_),
                      static_cast<unsigned>(compose_mode_),
                      static_cast<unsigned>(compose_abc_group_index_),
                      static_cast<unsigned>(compose_abc_tap_index_),
                      preedit,
                      static_cast<unsigned>(compose_candidate_count_),
                      static_cast<unsigned>(compose_len_));
        host_.debug_log_fn(line);
    }

    if (action == InputAction::None)
    {
        return;
    }

    if (page_ == Page::BootLog)
    {
        if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            enterPage(Page::MainMenu);
        }
        return;
    }

    if (page_ == Page::Screensaver)
    {
        if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            enterPage(Page::MainMenu);
        }
        return;
    }

    switch (page_)
    {
    case Page::MainMenu:
        if (action == InputAction::Up && main_menu_index_ > 0)
        {
            --main_menu_index_;
        }
        else if (action == InputAction::Down && main_menu_index_ + 1 < arrayCount(kMainMenuItems))
        {
            ++main_menu_index_;
        }
        else if (action == InputAction::Left || action == InputAction::Back)
        {
            enterPage(Page::Screensaver);
        }
        else if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            switch (main_menu_index_)
            {
            case 0: enterPage(Page::ChatList); break;
            case 1:
                active_conversation_ = chat::ConversationId(chat::ChannelId::PRIMARY, 0, app()->getConfig().mesh_protocol);
                openCompose(EditTarget::Message);
                break;
            case 2: enterPage(Page::SettingsMenu); break;
            case 3: enterPage(Page::IdentitySettings); break;
            case 4: enterPage(Page::RadioSettings); break;
            case 5: enterPage(Page::DeviceSettings); break;
            case 6: enterPage(Page::GnssPage); break;
            case 7: enterPage(Page::ActionPage); break;
            default: break;
            }
        }
        break;

    case Page::ChatList:
        if (action == InputAction::Up && chat_list_index_ > 0)
        {
            --chat_list_index_;
        }
        else if (action == InputAction::Down && chat_list_index_ + 1 < conversation_count_)
        {
            ++chat_list_index_;
        }
        else if (action == InputAction::Left || action == InputAction::Back)
        {
            enterPage(Page::MainMenu);
        }
        else if ((action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary) &&
                 chat_list_index_ < conversation_count_)
        {
            active_conversation_ = conversations_[chat_list_index_].id;
            enterPage(Page::Conversation);
        }
        break;

    case Page::Conversation:
        if (action == InputAction::Up && message_index_ > 0)
        {
            --message_index_;
        }
        else if (action == InputAction::Down && message_index_ + 1 < message_count_)
        {
            ++message_index_;
        }
        else if (action == InputAction::Left || action == InputAction::Back)
        {
            enterPage(Page::ChatList);
        }
        else if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            enterPage(Page::MessageMenu);
        }
        break;

    case Page::MessageMenu:
        if (action == InputAction::Up && message_menu_index_ > 0)
        {
            --message_menu_index_;
        }
        else if (action == InputAction::Down && message_menu_index_ + 1 < arrayCount(kMessageMenuItems))
        {
            ++message_menu_index_;
        }
        else if (action == InputAction::Left || action == InputAction::Back)
        {
            enterPage(Page::Conversation);
        }
        else if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            if (message_menu_index_ == 0)
            {
                enterPage(Page::MessageInfo);
            }
            else
            {
                openCompose(EditTarget::Message);
            }
        }
        break;

    case Page::MessageInfo:
        if (action == InputAction::Up && message_info_scroll_ > 0)
        {
            --message_info_scroll_;
        }
        else if (action == InputAction::Down && message_info_scroll_ + 1 < message_info_count_)
        {
            ++message_info_scroll_;
        }
        else if (action == InputAction::Left || action == InputAction::Back ||
                 action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            enterPage(Page::MessageMenu);
        }
        break;

    case Page::Compose:
        if (!usesSmartCompose())
        {
            if (action == InputAction::Up)
            {
                adjustComposeSelection(-1);
            }
            else if (action == InputAction::Down)
            {
                adjustComposeSelection(1);
            }
            else if (action == InputAction::Left || action == InputAction::Back)
            {
                if (compose_len_ > 0)
                {
                    removeComposeChar();
                }
                else
                {
                    finishTextEdit(false);
                }
            }
            else if (action == InputAction::Right || action == InputAction::Primary)
            {
                addComposeChar();
            }
            else if (action == InputAction::Secondary)
            {
                appendChar(compose_buffer_, sizeof(compose_buffer_), compose_len_, ' ');
            }
            else if (action == InputAction::Select)
            {
                if (edit_target_ == EditTarget::Message)
                {
                    sendComposeMessage();
                }
                else
                {
                    finishTextEdit(true);
                }
            }
            break;
        }

        if (action == InputAction::Back)
        {
            finishTextEdit(false);
        }
        else if (compose_focus_ == ComposeFocus::Body)
        {
            if (isAlphaComposeMode(compose_mode_))
            {
                if (action == InputAction::Up)
                {
                    if (compose_abc_group_index_ >= 5U)
                    {
                        compose_abc_group_index_ -= 5U;
                    }
                    else if (compose_candidate_count_ > 0)
                    {
                        compose_focus_ = ComposeFocus::Candidate;
                    }
                }
                else if (action == InputAction::Down)
                {
                    if (compose_abc_group_index_ < 5U)
                    {
                        compose_abc_group_index_ += 5U;
                    }
                    else
                    {
                        compose_focus_ = ComposeFocus::Action;
                    }
                }
                else if (action == InputAction::Left)
                {
                    compose_abc_group_index_ = (compose_abc_group_index_ % 5U == 0U)
                                                   ? (compose_abc_group_index_ + 4U)
                                                   : (compose_abc_group_index_ - 1U);
                }
                else if (action == InputAction::Right)
                {
                    compose_abc_group_index_ = (compose_abc_group_index_ % 5U == 4U)
                                                   ? (compose_abc_group_index_ - 4U)
                                                   : (compose_abc_group_index_ + 1U);
                }
                else if (action == InputAction::Select || action == InputAction::Primary)
                {
                    const char* letters = composeAbcGroupLetters(compose_abc_group_index_);
                    const size_t letter_count = std::strlen(letters);
                    if (letter_count > 0)
                    {
                        const uint32_t now = nowMs();
                        const bool can_cycle = compose_preedit_len_ > 0 &&
                                               compose_abc_last_group_index_ == static_cast<int>(compose_abc_group_index_) &&
                                               (now - compose_abc_last_tap_ms_) <= kComposeMultiTapWindowMs;
                        if (can_cycle)
                        {
                            compose_abc_tap_index_ = (compose_abc_tap_index_ + 1U) % letter_count;
                            compose_preedit_[compose_preedit_len_ - 1U] = letters[compose_abc_tap_index_];
                            compose_abc_last_tap_ms_ = now;
                            rebuildComposeCandidates();
                        }
                        else
                        {
                            compose_abc_tap_index_ = 0;
                            appendComposeChar(letters[0]);
                            compose_abc_last_group_index_ = static_cast<int>(compose_abc_group_index_);
                            compose_abc_last_tap_ms_ = now;
                        }
                    }
                }
                else if (action == InputAction::Secondary)
                {
                    appendComposeChar(' ');
                }
            }
            else if (action == InputAction::Up && compose_candidate_count_ > 0)
            {
                compose_focus_ = ComposeFocus::Candidate;
            }
            else if (action == InputAction::Down)
            {
                compose_focus_ = ComposeFocus::Action;
            }
            else if (action == InputAction::Left)
            {
                adjustComposeSelection(-1);
            }
            else if (action == InputAction::Right)
            {
                adjustComposeSelection(1);
            }
            else if (action == InputAction::Select || action == InputAction::Primary)
            {
                addComposeChar();
            }
            else if (action == InputAction::Secondary)
            {
                appendComposeChar(' ');
            }
        }
        else if (compose_focus_ == ComposeFocus::Candidate)
        {
            if (action == InputAction::Left)
            {
                adjustComposeCandidate(-1);
            }
            else if (action == InputAction::Right)
            {
                adjustComposeCandidate(1);
            }
            else if (action == InputAction::Down)
            {
                compose_focus_ = ComposeFocus::Body;
            }
            else if (action == InputAction::Select || action == InputAction::Primary)
            {
                (void)commitComposeCandidate();
                compose_focus_ = ComposeFocus::Body;
            }
        }
        else
        {
            if (action == InputAction::Left)
            {
                adjustComposeAction(-1);
            }
            else if (action == InputAction::Right)
            {
                adjustComposeAction(1);
            }
            else if (action == InputAction::Up)
            {
                compose_focus_ = compose_candidate_count_ > 0 ? ComposeFocus::Candidate : ComposeFocus::Body;
            }
            else if (action == InputAction::Select || action == InputAction::Primary)
            {
                activateComposeAction();
            }
        }
        break;

    case Page::SettingsMenu:
        if (action == InputAction::Up && settings_menu_index_ > 0)
        {
            --settings_menu_index_;
        }
        else if (action == InputAction::Down && settings_menu_index_ + 1 < arrayCount(kSettingsMenuItems))
        {
            ++settings_menu_index_;
        }
        else if (action == InputAction::Left || action == InputAction::Back)
        {
            enterPage(Page::MainMenu);
        }
        else if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            switch (settings_menu_index_)
            {
            case 0: enterPage(Page::IdentitySettings); break;
            case 1: enterPage(Page::RadioSettings); break;
            case 2: enterPage(Page::DeviceSettings); break;
            default: break;
            }
        }
        break;

    case Page::IdentitySettings:
        if (action == InputAction::Up && identity_index_ > 0)
        {
            --identity_index_;
        }
        else if (action == InputAction::Down && identity_index_ + 1 < arrayCount(kIdentityItems))
        {
            ++identity_index_;
        }
        else if (action == InputAction::Left || action == InputAction::Back)
        {
            enterPage(Page::MainMenu);
        }
        else if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            if (identity_index_ == 0)
            {
                openCompose(EditTarget::UserName, app()->getConfig().node_name);
            }
            else
            {
                openCompose(EditTarget::ShortName, app()->getConfig().short_name);
            }
        }
        break;

    case Page::RadioSettings:
        if (action == InputAction::Up && radio_index_ > 0)
        {
            --radio_index_;
        }
        else if (action == InputAction::Down && radio_index_ + 1 < arrayCount(kRadioItems))
        {
            ++radio_index_;
        }
        else if (action == InputAction::Left)
        {
            adjustRadioSetting(-1);
        }
        else if (action == InputAction::Right)
        {
            adjustRadioSetting(1);
        }
        else if (action == InputAction::Back)
        {
            enterPage(Page::MainMenu);
        }
        else if (action == InputAction::Select || action == InputAction::Primary)
        {
            if (radio_index_ == 6)
            {
                const auto protocol = app()->getConfig().mesh_protocol;
                if (protocol == chat::MeshProtocol::Meshtastic)
                {
                    char hex[33] = {};
                    bytesToHex(app()->getConfig().meshtastic_config.secondary_key, 16, hex, sizeof(hex));
                    openCompose(EditTarget::MeshtasticPsk, hex);
                }
                else
                {
                    openCompose(EditTarget::MeshCoreChannelName, app()->getConfig().meshcore_config.meshcore_channel_name);
                }
            }
            else
            {
                adjustRadioSetting(1);
            }
        }
        break;

    case Page::DeviceSettings:
        if (action == InputAction::Up && device_index_ > 0)
        {
            --device_index_;
        }
        else if (action == InputAction::Down && device_index_ + 1 < arrayCount(kDeviceItems))
        {
            ++device_index_;
        }
        else if (action == InputAction::Left)
        {
            adjustDeviceSetting(-1);
        }
        else if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            adjustDeviceSetting(1);
        }
        else if (action == InputAction::Back)
        {
            enterPage(Page::MainMenu);
        }
        break;

    case Page::GnssPage:
        if (action == InputAction::Left || action == InputAction::Back || action == InputAction::Select)
        {
            enterPage(Page::MainMenu);
        }
        break;

    case Page::ActionPage:
        if (action == InputAction::Up && action_index_ > 0)
        {
            --action_index_;
        }
        else if (action == InputAction::Down && action_index_ + 1 < arrayCount(kActionItems))
        {
            ++action_index_;
        }
        else if (action == InputAction::Left || action == InputAction::Back)
        {
            enterPage(Page::MainMenu);
        }
        else if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            switch (action_index_)
            {
            case 0:
                app()->broadcastNodeInfo();
                appendBootLog("nodeinfo tx");
                break;
            case 1:
                app()->clearNodeDb();
                appendBootLog("nodes cleared");
                break;
            case 2:
                app()->clearMessageDb();
                appendBootLog("messages cleared");
                break;
            case 3:
                if (auto* ble_app = static_cast<app::IAppBleFacade*>(app()))
                {
                    ble_app->resetMeshConfig();
                    appendBootLog("radio reset");
                }
                break;
            default:
                break;
            }
        }
        break;

    default:
        break;
    }
}

void Runtime::render()
{
    display_.clear();
    switch (page_)
    {
    case Page::BootLog: renderBootLog(); break;
    case Page::Screensaver: renderScreensaver(); break;
    case Page::MainMenu: renderMainMenu(); break;
    case Page::ChatList: renderChatList(); break;
    case Page::Conversation: renderConversation(); break;
    case Page::MessageMenu: renderMessageMenu(); break;
    case Page::MessageInfo: renderMessageInfo(); break;
    case Page::Compose: renderCompose(); break;
    case Page::SettingsMenu: renderSettingsMenu(); break;
    case Page::IdentitySettings: renderIdentitySettings(); break;
    case Page::RadioSettings: renderRadioSettings(); break;
    case Page::DeviceSettings: renderDeviceSettings(); break;
    case Page::GnssPage: renderGnssPage(); break;
    case Page::ActionPage: renderActionPage(); break;
    default: renderScreensaver(); break;
    }
    display_.present();
}

void Runtime::renderBootLog()
{
    drawTitleBar("BOOT", nullptr);
    const int line_h = text_renderer_.lineHeight();
    const int start_y = 10;
    const size_t visible = std::min(boot_log_count_, static_cast<size_t>(6));
    for (size_t i = 0; i < visible; ++i)
    {
        drawTextClipped(0, start_y + static_cast<int>(i * line_h), display_.width(), boot_log_[boot_log_count_ - visible + i]);
    }
}

void Runtime::renderScreensaver()
{
    char protocol[8] = {};
    char freq[20] = {};
    char time_buf[16] = {};
    char date_buf[24] = {};
    char node_buf[12] = {};
    formatProtocol(protocol, sizeof(protocol));
    formatNodeLabel(node_buf, sizeof(node_buf));
    if (host_.format_frequency_fn)
    {
        host_.format_frequency_fn(host_.active_lora_frequency_hz_fn ? host_.active_lora_frequency_hz_fn() : 0U,
                                  freq,
                                  sizeof(freq));
    }
    formatTime(time_buf, sizeof(time_buf), date_buf, sizeof(date_buf));

    drawTitleBar(protocol, freq[0] != '\0' ? freq : nullptr);

    const int time_w = text_renderer_.measureTextWidth(time_buf);
    const int time_x = std::max(0, (display_.width() - time_w) / 2);
    text_renderer_.drawText(display_, time_x, 18, time_buf);

    const int date_w = text_renderer_.measureTextWidth(date_buf);
    const int date_x = std::max(0, (display_.width() - date_w) / 2);
    text_renderer_.drawText(display_, date_x, 34, date_buf);

    const int node_w = text_renderer_.measureTextWidth(node_buf);
    const int node_x = std::max(0, (display_.width() - node_w) / 2);
    text_renderer_.drawText(display_, node_x, 50, node_buf);
}

void Runtime::renderMainMenu()
{
    drawMenuList("MENU", kMainMenuItems, arrayCount(kMainMenuItems), main_menu_index_);
}

void Runtime::renderChatList()
{
    rebuildConversationList();
    drawTitleBar("CHATS", nullptr);
    if (conversation_count_ == 0)
    {
        text_renderer_.drawText(display_, 0, 18, "NO CONVERSATIONS");
        return;
    }

    const int line_h = text_renderer_.lineHeight();
    const int marker_w = text_renderer_.measureTextWidth(kSelectedItemMarker) + 2;
    for (size_t i = 0; i < conversation_count_ && i < 6; ++i)
    {
        const bool selected = (i == chat_list_index_);
        char line[32] = {};
        const auto& conv = conversations_[i];
        std::snprintf(line, sizeof(line), "%s%s",
                      conv.unread > 0 ? "*" : "",
                      conv.name.c_str());
        const int y = 10 + static_cast<int>(i * line_h);
        if (selected)
        {
            text_renderer_.drawText(display_, 0, y, kSelectedItemMarker);
        }
        drawTextClipped(marker_w, y, display_.width() - marker_w, line, false);
    }
}

void Runtime::renderConversation()
{
    rebuildMessages();
    char title[20] = {};
    if (active_conversation_.peer == 0)
    {
        copyText(title, "BROADCAST");
    }
    else
    {
        std::snprintf(title, sizeof(title), "%08lX", static_cast<unsigned long>(active_conversation_.peer));
    }
    drawTitleBar(title, nullptr);

    if (message_count_ == 0)
    {
        text_renderer_.drawText(display_, 0, 18, "NO MESSAGES");
        return;
    }

    const int line_h = text_renderer_.lineHeight();
    const int marker_w = text_renderer_.measureTextWidth(kSelectedItemMarker) + 2;
    const size_t selected_index = std::min(message_index_, message_count_ - 1U);
    const size_t visible = std::min(message_count_, static_cast<size_t>(5));
    size_t start = 0;
    if (message_count_ > visible)
    {
        start = (selected_index + 1 > visible) ? (selected_index + 1 - visible) : 0;
        if (start + visible > message_count_)
        {
            start = message_count_ - visible;
        }
    }
    for (size_t i = 0; i < visible; ++i)
    {
        const size_t msg_index = start + i;
        const auto& msg = messages_[msg_index];
        const bool selected = (msg_index == selected_index);
        char line[64] = {};
        char sender[12] = {};
        if (msg.from == 0)
        {
            copyText(sender, "SELF");
        }
        else
        {
            std::snprintf(sender, sizeof(sender), "%04lX", static_cast<unsigned long>(msg.from & 0xFFFFUL));
        }
        std::snprintf(line, sizeof(line), "%s>%s", sender, msg.text.c_str());

        const int y = 10 + static_cast<int>(i * line_h);
        if (selected)
        {
            text_renderer_.drawText(display_, 0, y, kSelectedItemMarker);
        }
        drawTextClipped(marker_w, y, display_.width() - marker_w, line, false);
    }
}

void Runtime::renderMessageMenu()
{
    drawMenuList("MESSAGE", kMessageMenuItems, arrayCount(kMessageMenuItems), message_menu_index_);
}

void Runtime::renderMessageInfo()
{
    buildMessageInfo();
    char pos[12] = {};
    if (message_info_count_ > 0)
    {
        std::snprintf(pos, sizeof(pos), "%u/%u",
                      static_cast<unsigned>(std::min(message_info_scroll_ + 1, message_info_count_)),
                      static_cast<unsigned>(message_info_count_));
    }
    drawTitleBar("INFO", pos[0] != '\0' ? pos : nullptr);
    if (message_info_count_ == 0)
    {
        text_renderer_.drawText(display_, 0, 18, "NO INFO");
        return;
    }

    const int line_h = text_renderer_.lineHeight();
    const size_t visible = std::min(message_info_count_, static_cast<size_t>(6));
    const size_t start = std::min(message_info_scroll_,
                                  message_info_count_ > visible ? message_info_count_ - visible : 0U);
    for (size_t i = 0; i < visible && (start + i) < message_info_count_; ++i)
    {
        drawTextClipped(0, 10 + static_cast<int>(i * line_h), display_.width(), message_info_lines_[start + i], false);
    }
}

void Runtime::renderCompose()
{
    if (!usesSmartCompose())
    {
        drawTitleBar(edit_target_ == EditTarget::Message ? "COMPOSE" : "EDIT", nullptr);
        drawTextClipped(0, 12, display_.width(), compose_buffer_);

        const char* charset = editUsesHexCharset() ? kHexCharset : kComposeCharset;
        const size_t charset_len = std::strlen(charset);
        const char current = charset[compose_charset_index_ % charset_len];
        char pick[8] = {};
        std::snprintf(pick, sizeof(pick), "[%c]", current);
        text_renderer_.drawText(display_, 0, 34, pick);

        text_renderer_.drawText(display_, 40, 34, "U/D PICK");
        text_renderer_.drawText(display_, 40, 44, "R ADD");
        text_renderer_.drawText(display_, 40, 54, "L DEL OK");
        return;
    }

    const int line_h = text_renderer_.lineHeight();

    char target[16] = {};
    formatComposeTarget(target, sizeof(target));
    char to_line[24] = {};
    std::snprintf(to_line, sizeof(to_line), "TO:%s", target[0] != '\0' ? target : "-");
    drawTextClipped(0, 0, display_.width(), to_line, false);

    constexpr size_t kBodyVisiblePerLine = 18;
    char body_text[kComposeMax + 1] = {};
    if (compose_len_ > 0)
    {
        copyText(body_text, compose_buffer_);
    }

    const size_t body_text_len = std::strlen(body_text);
    const size_t body_window = kBodyVisiblePerLine * 2U;
    const size_t body_start = body_text_len > body_window ? (body_text_len - body_window) : 0U;
    const size_t visible_len = body_text_len - body_start;
    const size_t split = visible_len > kBodyVisiblePerLine ? (visible_len - kBodyVisiblePerLine) : 0U;

    char body_line_1[kBodyVisiblePerLine + 1] = {};
    char body_line_2[kBodyVisiblePerLine + 1] = {};
    if (visible_len == 0)
    {
        std::snprintf(body_line_1, sizeof(body_line_1), "_");
    }
    else
    {
        const size_t first_len = std::min(split, kBodyVisiblePerLine);
        if (first_len > 0)
        {
            std::memcpy(body_line_1, body_text + body_start, first_len);
            body_line_1[first_len] = '\0';
        }

        const size_t second_start = body_start + split;
        const size_t second_len = std::min(body_text_len - second_start, kBodyVisiblePerLine);
        if (second_len > 0)
        {
            std::memcpy(body_line_2, body_text + second_start, second_len);
            body_line_2[second_len] = '\0';
        }
    }

    drawTextClipped(0, line_h, display_.width(), body_line_1, false);
    drawTextClipped(0, line_h * 2, display_.width(), body_line_2, false);

    char candidate_line[48] = {};
    size_t pos = 0;
    if (compose_preedit_len_ > 0)
    {
        for (size_t i = 0; i < compose_preedit_len_ && pos + 2 < sizeof(candidate_line); ++i)
        {
            candidate_line[pos++] = static_cast<char>(std::tolower(static_cast<unsigned char>(compose_preedit_[i])));
        }
        candidate_line[pos++] = '_';
        candidate_line[pos] = '\0';
        if (compose_candidate_count_ > 0 && pos + 1 < sizeof(candidate_line))
        {
            candidate_line[pos++] = ' ';
            candidate_line[pos] = '\0';
        }
    }

    if (compose_candidate_count_ == 0)
    {
        if (pos == 0)
        {
            std::snprintf(candidate_line, sizeof(candidate_line), "-");
        }
    }
    else
    {
        for (size_t i = 0; i < compose_candidate_count_ && pos + 4 < sizeof(candidate_line); ++i)
        {
            const bool selected = (i == compose_candidate_index_);
            pos += static_cast<size_t>(std::snprintf(candidate_line + pos, sizeof(candidate_line) - pos,
                                                     selected ? "[%s]" : "%s",
                                                     compose_candidates_[i]));
            if (i + 1 < compose_candidate_count_ && pos + 1 < sizeof(candidate_line))
            {
                candidate_line[pos++] = ' ';
                candidate_line[pos] = '\0';
            }
        }
    }
    drawTextClipped(0, line_h * 3, display_.width(), candidate_line, false);

    if (isAlphaComposeMode(compose_mode_))
    {
        constexpr int kGroupCols = 5;
        constexpr int kGroupX[kGroupCols] = {0, 26, 52, 78, 104};
        constexpr int kGroupCellW[kGroupCols] = {26, 26, 26, 26, 24};
        for (int row = 0; row < 2; ++row)
        {
            for (int col = 0; col < kGroupCols; ++col)
            {
                const size_t group_index = static_cast<size_t>(row * kGroupCols + col);
                if (group_index >= arrayCount(kComposeAbcGroups))
                {
                    continue;
                }

                char cell[8] = {};
                std::snprintf(cell, sizeof(cell), "%s", composeAbcGroupLabel(group_index));
                const bool selected = compose_focus_ == ComposeFocus::Body && compose_abc_group_index_ == group_index;
                drawTextClipped(kGroupX[col], line_h * (5 + row), kGroupCellW[col], cell, selected);
            }
        }
    }
    else
    {
        const char* keyset = composeKeysetForMode(compose_mode_);
        const size_t key_count = std::strlen(keyset);
        const size_t page_start = key_count == 0 ? 0U : ((compose_charset_index_ / 12U) * 12U);
        constexpr int kKeyCols = 6;
        constexpr int kKeyCellW = 20;
        for (int row = 0; row < 2; ++row)
        {
            for (int col = 0; col < kKeyCols; ++col)
            {
                const size_t key_index = page_start + static_cast<size_t>(row * kKeyCols + col);
                if (key_index >= key_count)
                {
                    continue;
                }

                const bool selected = compose_focus_ == ComposeFocus::Body && key_index == compose_charset_index_;
                const char key_char = keyset[key_index];
                char cell[8] = {};
                std::snprintf(cell, sizeof(cell), selected ? "[%c]" : " %c ",
                              key_char == ' ' ? '_' : key_char);
                text_renderer_.drawText(display_, col * kKeyCellW, line_h * (5 + row), cell, false);
            }
        }
    }

    const int action_y = line_h * 7;
    const int action_x[5] = {0, 24, 48, 72, 96};
    const char* action_labels[5] = {composeModeLabel(compose_mode_), "  ", "\xE2\x86\x90", "\xE2\x87\xA4", "SEND"};
    for (size_t i = 0; i < 5; ++i)
    {
        char action_cell[10] = {};
        std::snprintf(action_cell, sizeof(action_cell), "[%s]", action_labels[i]);
        const bool selected = compose_focus_ == ComposeFocus::Action && compose_action_index_ == i;
        text_renderer_.drawText(display_, action_x[i], action_y, action_cell, selected);
    }
}

void Runtime::renderSettingsMenu()
{
    drawMenuList("SETTINGS", kSettingsMenuItems, arrayCount(kSettingsMenuItems), settings_menu_index_);
}

void Runtime::renderIdentitySettings()
{
    drawTitleBar("IDENTITY", nullptr);
    char value[40] = {};
    for (size_t i = 0; i < arrayCount(kIdentityItems); ++i)
    {
        if (i == 0)
        {
            copyText(value, app()->getConfig().node_name);
        }
        else
        {
            copyText(value, app()->getConfig().short_name);
        }

        char line[48] = {};
        std::snprintf(line, sizeof(line), "%s: %s", kIdentityItems[i], value[0] ? value : "-");
        drawTextClipped(0, 10 + static_cast<int>(i * text_renderer_.lineHeight()),
                        display_.width(), line, i == identity_index_);
    }
}

void Runtime::renderRadioSettings()
{
    drawTitleBar("RADIO", protocolShortLabel(app()->getConfig().mesh_protocol));
    char value[40] = {};
    auto& cfg = app()->getConfig();
    for (size_t i = 0; i < arrayCount(kRadioItems); ++i)
    {
        value[0] = '\0';
        switch (i)
        {
        case 0:
            copyText(value, cfg.mesh_protocol == chat::MeshProtocol::MeshCore ? "MeshCore" : "Meshtastic");
            break;
        case 1:
            std::snprintf(value, sizeof(value), "%ddBm", static_cast<int>(cfg.activeMeshConfig().tx_power));
            break;
        case 2:
            if (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
            {
                if (const auto* region = chat::meshtastic::findRegion(
                        static_cast<meshtastic_Config_LoRaConfig_RegionCode>(cfg.meshtastic_config.region)))
                {
                    copyText(value, region->label);
                }
            }
            else if (const auto* preset = chat::meshcore::findRegionPresetById(cfg.meshcore_config.meshcore_region_preset))
            {
                copyText(value, preset->title);
            }
            else
            {
                copyText(value, "Custom");
            }
            break;
        case 3:
            if (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
            {
                copyText(value,
                         chat::meshtastic::presetDisplayName(
                             static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(cfg.meshtastic_config.modem_preset)));
            }
            else
            {
                std::snprintf(value, sizeof(value), "%.3f/%.0f",
                              static_cast<double>(cfg.meshcore_config.meshcore_freq_mhz),
                              static_cast<double>(cfg.meshcore_config.meshcore_bw_khz));
            }
            break;
        case 4:
            if (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
            {
                std::snprintf(value, sizeof(value), "Slot %u", static_cast<unsigned>(cfg.meshtastic_config.channel_num));
            }
            else
            {
                std::snprintf(value, sizeof(value), "%s/%u",
                              cfg.meshcore_config.meshcore_channel_name,
                              static_cast<unsigned>(cfg.meshcore_config.meshcore_channel_slot));
            }
            break;
        case 5:
            copyText(value, encryptEnabled(cfg) ? "On" : "Off");
            break;
        case 6:
            if (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
            {
                char hex[33] = {};
                bytesToHex(cfg.meshtastic_config.secondary_key, 16, hex, sizeof(hex));
                copyText(value, hex[0] ? hex : "0000...");
            }
            else
            {
                copyText(value, cfg.meshcore_config.meshcore_channel_name);
            }
            break;
        }

        char line[48] = {};
        std::snprintf(line, sizeof(line), "%s: %s", kRadioItems[i], value);
        drawTextClipped(0, 10 + static_cast<int>(i * text_renderer_.lineHeight()),
                        display_.width(), line, i == radio_index_);
    }
}

void Runtime::renderDeviceSettings()
{
    drawTitleBar("DEVICE", nullptr);
    char line[48] = {};
    for (size_t i = 0; i < arrayCount(kDeviceItems); ++i)
    {
        if (i == 0)
        {
            std::snprintf(line, sizeof(line), "BLE: %s", app()->isBleEnabled() ? "ON" : "OFF");
        }
        else if (i == 1)
        {
            const int tz = host_.timezone_offset_min_fn ? host_.timezone_offset_min_fn() : 0;
            std::snprintf(line, sizeof(line), "TIME ZONE: UTC%+d", tz / 60);
        }
        else if (i == 2)
        {
            std::snprintf(line, sizeof(line), "GPS: %s", app()->getConfig().gps_mode != 0 ? "ON" : "OFF");
        }
        else if (i == 3)
        {
            std::snprintf(line, sizeof(line), "GPS INT: %lus",
                          static_cast<unsigned long>(app()->getConfig().gps_interval_ms / 1000UL));
        }
        else
        {
            std::snprintf(line, sizeof(line), "CHAT CH: %s",
                          app()->getConfig().chat_channel == 0 ? "PRIMARY" : "SECONDARY");
        }
        drawTextClipped(0, 10 + static_cast<int>(i * text_renderer_.lineHeight()),
                        display_.width(), line, i == device_index_);
    }
}

void Runtime::renderGnssPage()
{
    drawTitleBar("GNSS", nullptr);
    const auto state = host_.gps_data_fn ? host_.gps_data_fn() : platform::ui::gps::GpsState{};
    char line[40] = {};
    std::snprintf(line, sizeof(line), "ENABLED: %s", (host_.gps_enabled_fn && host_.gps_enabled_fn()) ? "YES" : "NO");
    text_renderer_.drawText(display_, 0, 12, line);
    std::snprintf(line, sizeof(line), "POWERED: %s", (host_.gps_powered_fn && host_.gps_powered_fn()) ? "YES" : "NO");
    text_renderer_.drawText(display_, 0, 22, line);
    std::snprintf(line, sizeof(line), "FIX: %s", state.valid ? "YES" : "NO");
    text_renderer_.drawText(display_, 0, 32, line);
    if (state.valid)
    {
        std::snprintf(line, sizeof(line), "LAT %.4f", state.lat);
        text_renderer_.drawText(display_, 0, 42, line);
        std::snprintf(line, sizeof(line), "LNG %.4f", state.lng);
        text_renderer_.drawText(display_, 0, 52, line);
    }
}

void Runtime::renderActionPage()
{
    drawMenuList("ACTIONS", kActionItems, arrayCount(kActionItems), action_index_);
}

void Runtime::enterPage(Page page)
{
    page_ = page;
    page_entered_ms_ = nowMs();
    if (page == Page::ChatList)
    {
        rebuildConversationList();
        chat_list_index_ = std::min(chat_list_index_, conversation_count_ == 0 ? 0U : conversation_count_ - 1U);
    }
    else if (page == Page::Conversation)
    {
        if (app())
        {
            app()->getChatService().markConversationRead(active_conversation_);
        }
        rebuildMessages();
        message_menu_index_ = 0;
        message_info_scroll_ = 0;
    }
    else if (page == Page::MessageMenu)
    {
        message_menu_index_ = 0;
    }
    else if (page == Page::MessageInfo)
    {
        message_info_scroll_ = 0;
        buildMessageInfo();
    }
}

void Runtime::openCompose(EditTarget target, const char* seed_text)
{
    edit_target_ = target;
    page_before_compose_ = page_;
    compose_buffer_[0] = '\0';
    compose_len_ = 0;
    compose_charset_index_ = 0;
    compose_mode_ = ComposeMode::AbcLower;
    compose_focus_ = ComposeFocus::Body;
    compose_action_index_ = 0;
    compose_abc_group_index_ = 0;
    compose_abc_tap_index_ = 0;
    compose_abc_last_tap_ms_ = 0;
    compose_abc_last_group_index_ = -1;
    compose_preedit_[0] = '\0';
    compose_preedit_len_ = 0;
    compose_candidate_count_ = 0;
    compose_candidate_index_ = 0;
    for (size_t i = 0; i < kComposeCandidateMax; ++i)
    {
        compose_candidates_[i][0] = '\0';
    }
    if (seed_text)
    {
        copyText(compose_buffer_, seed_text);
        compose_len_ = std::strlen(compose_buffer_);
    }
    rebuildComposeCandidates();
    page_ = Page::Compose;
    page_entered_ms_ = nowMs();
}

void Runtime::finishTextEdit(bool accept)
{
    if (accept)
    {
        saveEditedTextToConfig();
    }
    edit_target_ = EditTarget::None;
    enterPage(page_before_compose_);
}

void Runtime::rebuildConversationList()
{
    conversation_count_ = 0;
    conversation_total_ = 0;
    if (!app())
    {
        return;
    }

    size_t total = 0;
    const auto list = app()->getChatService().getConversations(0, kMaxConversationItems, &total);
    conversation_total_ = total;
    conversation_count_ = std::min(list.size(), static_cast<size_t>(kMaxConversationItems));
    for (size_t i = 0; i < conversation_count_; ++i)
    {
        conversations_[i] = list[i];
    }
}

void Runtime::rebuildMessages()
{
    message_count_ = 0;
    if (!app())
    {
        return;
    }

    const auto list = app()->getChatService().getRecentMessages(active_conversation_, kMaxMessageItems);
    message_count_ = std::min(list.size(), static_cast<size_t>(kMaxMessageItems));
    for (size_t i = 0; i < message_count_; ++i)
    {
        messages_[i] = list[i];
    }
    if (message_count_ == 0)
    {
        message_index_ = 0;
    }
    else if (message_index_ >= message_count_)
    {
        message_index_ = message_count_ - 1U;
    }
}

void Runtime::buildMessageInfo()
{
    message_info_count_ = 0;
    const chat::ChatMessage* msg = selectedMessage();
    if (!msg)
    {
        return;
    }

    auto push_line = [this](const char* text)
    {
        if (!text || text[0] == '\0' || message_info_count_ >= kMessageInfoLines)
        {
            return;
        }
        copyText(message_info_lines_[message_info_count_], text);
        ++message_info_count_;
    };

    auto push_kv = [this, &push_line](const char* key, const char* value)
    {
        if (message_info_count_ >= kMessageInfoLines)
        {
            return;
        }
        appendInfoLine(message_info_lines_[message_info_count_], key, value ? value : "-");
        ++message_info_count_;
    };

    char value[40] = {};
    auto push_section = [this](const char* title)
    {
        if (!title || message_info_count_ >= kMessageInfoLines)
        {
            return;
        }
        setInfoSection(message_info_lines_[message_info_count_], title);
        ++message_info_count_;
    };

    auto formatInfoTime = [this](uint32_t timestamp_s, char* out, size_t out_len)
    {
        if (!out || out_len == 0)
        {
            return;
        }
        out[0] = '\0';
        if (timestamp_s == 0)
        {
            return;
        }

        const int tz_offset_s = (host_.timezone_offset_min_fn ? host_.timezone_offset_min_fn() : 0) * 60;
        const time_t adjusted = static_cast<time_t>(timestamp_s + tz_offset_s);
        const std::tm* tm = std::gmtime(&adjusted);
        if (!tm)
        {
            std::snprintf(out, out_len, "%lu", static_cast<unsigned long>(timestamp_s));
            return;
        }

        std::snprintf(out, out_len, "%02d-%02d %02d:%02d",
                      tm->tm_mon + 1,
                      tm->tm_mday,
                      tm->tm_hour,
                      tm->tm_min);
    };

    std::snprintf(value, sizeof(value), "%s", msg->protocol == chat::MeshProtocol::MeshCore ? "MC" : "MT");
    push_section("MSG");
    push_kv("P", value);

    std::snprintf(value, sizeof(value), "%s", msg->channel == chat::ChannelId::SECONDARY ? "SECONDARY" : "PRIMARY");
    push_kv("CH", value);

    if (msg->from == 0)
    {
        push_kv("FR", "SELF");
    }
    else
    {
        std::snprintf(value, sizeof(value), "%08lX", static_cast<unsigned long>(msg->from));
        push_kv("FR", value);
    }

    std::snprintf(value, sizeof(value), "%08lX", static_cast<unsigned long>(msg->peer));
    push_kv("PEER", value);

    std::snprintf(value, sizeof(value), "%08lX", static_cast<unsigned long>(msg->msg_id));
    push_kv("ID", value);

    formatInfoTime(msg->timestamp, value, sizeof(value));
    push_kv("TIME", value[0] != '\0' ? value : "-");

    std::snprintf(value, sizeof(value), "%u", static_cast<unsigned>(msg->text.size()));
    push_kv("LEN", value);

    if (!msg->text.empty())
    {
        push_kv("TXT", msg->text.c_str());
    }

    const MessageMetaEntry* meta = nullptr;
    for (size_t i = 0; i < kMessageMetaCapacity; ++i)
    {
        const MessageMetaEntry& candidate = message_meta_[i];
        if (!candidate.used)
        {
            continue;
        }
        if (candidate.protocol == msg->protocol &&
            candidate.channel == msg->channel &&
            candidate.from == msg->from &&
            candidate.msg_id == msg->msg_id)
        {
            meta = &candidate;
            break;
        }
    }

    if (meta)
    {
        push_section("RX");
        std::snprintf(value, sizeof(value), "%08lX", static_cast<unsigned long>(meta->to));
        push_kv("TO", value);

        std::snprintf(value, sizeof(value), "%u", static_cast<unsigned>(meta->hop_limit));
        push_kv("HOP", value);

        push_kv("ENC", meta->encrypted ? "YES" : "NO");

        std::snprintf(value, sizeof(value), "%d.%01d",
                      meta->rx_meta.rssi_dbm_x10 / 10,
                      std::abs(meta->rx_meta.rssi_dbm_x10 % 10));
        push_kv("RS", value);

        std::snprintf(value, sizeof(value), "%d.%01d",
                      meta->rx_meta.snr_db_x10 / 10,
                      std::abs(meta->rx_meta.snr_db_x10 % 10));
        push_kv("SN", value);

        std::snprintf(value, sizeof(value), "%u", static_cast<unsigned>(meta->rx_meta.hop_count));
        push_kv("HP", value);

        std::snprintf(value, sizeof(value), "%02X", static_cast<unsigned>(meta->rx_meta.channel_hash));
        push_kv("HS", value);

        std::snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(meta->rx_meta.relay_node));
        push_kv("RY", value);
    }

    if (msg->from != 0 && app())
    {
        if (const auto* node = app()->getContactService().getNodeInfo(msg->from))
        {
            push_section("NODE");
            push_kv("NM", node->display_name.empty() ? "-" : node->display_name.c_str());
            push_kv("SH", node->short_name[0] != '\0' ? node->short_name : "-");
            push_kv("LN", node->long_name[0] != '\0' ? node->long_name : "-");

            formatInfoTime(node->last_seen, value, sizeof(value));
            push_kv("SEEN", value[0] != '\0' ? value : "-");

            std::snprintf(value, sizeof(value), "%.1f", static_cast<double>(node->snr));
            push_kv("SN", value);

            std::snprintf(value, sizeof(value), "%.1f", static_cast<double>(node->rssi));
            push_kv("RS", value);

            std::snprintf(value, sizeof(value), "%u", static_cast<unsigned>(node->hops_away));
            push_kv("HP", value);

            std::snprintf(value, sizeof(value), "%u", static_cast<unsigned>(node->channel));
            push_kv("CH", value);
        }
    }
}

void Runtime::sendComposeMessage()
{
    if (usesSmartCompose())
    {
        (void)commitComposePreedit(true);
    }

    if (!app() || compose_len_ == 0)
    {
        finishTextEdit(false);
        return;
    }

    app()->getChatService().sendText(active_conversation_.channel, compose_buffer_, active_conversation_.peer);
    finishTextEdit(false);
    enterPage(Page::Conversation);
}

void Runtime::commitConfig()
{
    if (!app())
    {
        return;
    }
    app()->saveConfig();
}

void Runtime::ensureBootExit()
{
    if (page_ == Page::BootLog && (nowMs() - boot_started_ms_) >= kBootMinMs)
    {
        enterPage(Page::Screensaver);
    }
}

void Runtime::adjustRadioSetting(int delta)
{
    if (!app())
    {
        return;
    }

    auto& cfg = app()->getConfig();
    switch (radio_index_)
    {
    case 0:
        app()->switchMeshProtocol(cfg.mesh_protocol == chat::MeshProtocol::Meshtastic
                                      ? chat::MeshProtocol::MeshCore
                                      : chat::MeshProtocol::Meshtastic,
                                  false);
        break;
    case 1:
        cfg.activeMeshConfig().tx_power = static_cast<int8_t>(clampValue<int>(
            static_cast<int>(cfg.activeMeshConfig().tx_power) + delta,
            static_cast<int>(app::AppConfig::kTxPowerMinDbm),
            static_cast<int>(app::AppConfig::kTxPowerMaxDbm)));
        break;
    case 2:
        if (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
        {
            size_t count = 0;
            const auto* table = chat::meshtastic::getRegionTable(&count);
            if (count > 0)
            {
                size_t index = 0;
                for (size_t i = 0; i < count; ++i)
                {
                    if (table[i].code ==
                        static_cast<meshtastic_Config_LoRaConfig_RegionCode>(cfg.meshtastic_config.region))
                    {
                        index = i;
                        break;
                    }
                }
                index = static_cast<size_t>(clampValue<int>(static_cast<int>(index) + delta, 0, static_cast<int>(count) - 1));
                cfg.meshtastic_config.region = static_cast<uint8_t>(table[index].code);
            }
        }
        else
        {
            size_t count = 0;
            const auto* table = chat::meshcore::getRegionPresetTable(&count);
            if (count > 0)
            {
                int index = -1;
                for (size_t i = 0; i < count; ++i)
                {
                    if (table[i].id == cfg.meshcore_config.meshcore_region_preset)
                    {
                        index = static_cast<int>(i);
                        break;
                    }
                }
                index = clampValue(index + delta, 0, static_cast<int>(count) - 1);
                cfg.meshcore_config.meshcore_region_preset = table[index].id;
            }
        }
        break;
    case 3:
        if (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
        {
            constexpr int kPresetMin = static_cast<int>(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST);
            constexpr int kPresetMax = static_cast<int>(meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO);
            cfg.meshtastic_config.modem_preset = static_cast<uint8_t>(clampValue(
                static_cast<int>(cfg.meshtastic_config.modem_preset) + delta, kPresetMin, kPresetMax));
            cfg.meshtastic_config.use_preset = true;
        }
        else
        {
            size_t count = 0;
            const auto* table = chat::meshcore::getRegionPresetTable(&count);
            if (count > 0)
            {
                int index = -1;
                for (size_t i = 0; i < count; ++i)
                {
                    if (table[i].id == cfg.meshcore_config.meshcore_region_preset)
                    {
                        index = static_cast<int>(i);
                        break;
                    }
                }
                index = clampValue(index + delta, 0, static_cast<int>(count) - 1);
                cfg.meshcore_config.meshcore_region_preset = table[index].id;
                cfg.meshcore_config.meshcore_freq_mhz = table[index].freq_mhz;
                cfg.meshcore_config.meshcore_bw_khz = table[index].bw_khz;
                cfg.meshcore_config.meshcore_sf = table[index].sf;
                cfg.meshcore_config.meshcore_cr = table[index].cr;
            }
        }
        break;
    case 4:
        if (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
        {
            cfg.meshtastic_config.channel_num = static_cast<uint16_t>(clampValue<int>(
                static_cast<int>(cfg.meshtastic_config.channel_num) + delta, 0, 255));
        }
        else
        {
            cfg.meshcore_config.meshcore_channel_slot = static_cast<uint8_t>(clampValue<int>(
                static_cast<int>(cfg.meshcore_config.meshcore_channel_slot) + delta, 0, 15));
        }
        break;
    case 5:
        setEncryptEnabled(cfg, !encryptEnabled(cfg));
        break;
    default:
        break;
    }

    commitConfig();
}

void Runtime::adjustDeviceSetting(int delta)
{
    if (!app())
    {
        return;
    }

    if (device_index_ == 0)
    {
        app()->setBleEnabled(!app()->isBleEnabled());
    }
    else if (device_index_ == 1 && host_.timezone_offset_min_fn && host_.set_timezone_offset_min_fn)
    {
        const int current = host_.timezone_offset_min_fn();
        const int next = clampValue(current + delta * kTimezoneStep, kTimezoneMin, kTimezoneMax);
        host_.set_timezone_offset_min_fn(next);
    }
    else if (device_index_ == 2)
    {
        app()->getConfig().gps_mode = (app()->getConfig().gps_mode == 0) ? 1 : 0;
        commitConfig();
    }
    else if (device_index_ == 3)
    {
        static constexpr uint32_t kGpsIntervals[] = {15000UL, 30000UL, 60000UL, 300000UL, 600000UL};
        size_t index = 0;
        while (index + 1 < arrayCount(kGpsIntervals) &&
               kGpsIntervals[index] < app()->getConfig().gps_interval_ms)
        {
            ++index;
        }
        const int next = clampValue<int>(static_cast<int>(index) + delta, 0, static_cast<int>(arrayCount(kGpsIntervals)) - 1);
        app()->getConfig().gps_interval_ms = kGpsIntervals[next];
        commitConfig();
    }
    else if (device_index_ == 4)
    {
        app()->getConfig().chat_channel = app()->getConfig().chat_channel == 0 ? 1 : 0;
        commitConfig();
    }
}

void Runtime::adjustComposeSelection(int delta)
{
    if (usesSmartCompose())
    {
        const char* charset = composeKeysetForMode(compose_mode_);
        const size_t len = std::strlen(charset);
        if (len == 0)
        {
            compose_charset_index_ = 0;
            return;
        }
        const int next = static_cast<int>(compose_charset_index_) + delta;
        if (next < 0)
        {
            compose_charset_index_ = len - 1;
        }
        else
        {
            compose_charset_index_ = static_cast<size_t>(next) % len;
        }
        return;
    }

    const char* charset = editUsesHexCharset() ? kHexCharset : kComposeCharset;
    const size_t len = std::strlen(charset);
    if (len == 0)
    {
        compose_charset_index_ = 0;
        return;
    }
    const int next = static_cast<int>(compose_charset_index_) + delta;
    if (next < 0)
    {
        compose_charset_index_ = len - 1;
    }
    else
    {
        compose_charset_index_ = static_cast<size_t>(next) % len;
    }
}

void Runtime::adjustComposeCandidate(int delta)
{
    if (compose_candidate_count_ == 0)
    {
        compose_candidate_index_ = 0;
        return;
    }
    const int next = static_cast<int>(compose_candidate_index_) + delta;
    if (next < 0)
    {
        compose_candidate_index_ = compose_candidate_count_ - 1;
    }
    else
    {
        compose_candidate_index_ = static_cast<size_t>(next) % compose_candidate_count_;
    }
}

void Runtime::adjustComposeAction(int delta)
{
    constexpr size_t kActionCount = 5;
    const int next = static_cast<int>(compose_action_index_) + delta;
    if (next < 0)
    {
        compose_action_index_ = kActionCount - 1;
    }
    else
    {
        compose_action_index_ = static_cast<size_t>(next) % kActionCount;
    }
}

void Runtime::addComposeChar()
{
    if (usesSmartCompose())
    {
        const char* keyset = composeKeysetForMode(compose_mode_);
        const size_t len = std::strlen(keyset);
        if (len == 0)
        {
            return;
        }
        appendComposeChar(keyset[compose_charset_index_ % len]);
        return;
    }

    const char* charset = editUsesHexCharset() ? kHexCharset : kComposeCharset;
    const size_t len = std::strlen(charset);
    if (len == 0)
    {
        return;
    }
    appendChar(compose_buffer_, sizeof(compose_buffer_), compose_len_, charset[compose_charset_index_ % len]);
}

void Runtime::removeComposeChar()
{
    if (usesSmartCompose())
    {
        compose_abc_last_group_index_ = -1;
        compose_abc_last_tap_ms_ = 0;
        compose_abc_tap_index_ = 0;
        if (compose_preedit_len_ > 0)
        {
            popChar(compose_preedit_, compose_preedit_len_);
            rebuildComposeCandidates();
        }
        else
        {
            popChar(compose_buffer_, compose_len_);
        }
        return;
    }

    popChar(compose_buffer_, compose_len_);
}

void Runtime::cycleComposeMode()
{
    if (compose_mode_ == ComposeMode::AbcLower)
    {
        compose_mode_ = ComposeMode::AbcUpper;
    }
    else if (compose_mode_ == ComposeMode::AbcUpper)
    {
        compose_mode_ = ComposeMode::Num;
    }
    else if (compose_mode_ == ComposeMode::Num)
    {
        compose_mode_ = ComposeMode::Sym;
    }
    else
    {
        compose_mode_ = ComposeMode::AbcLower;
    }
    compose_charset_index_ = 0;
    compose_abc_tap_index_ = 0;
    compose_abc_last_tap_ms_ = 0;
    compose_abc_last_group_index_ = -1;
    rebuildComposeCandidates();
}

void Runtime::rebuildComposeCandidates()
{
    compose_candidate_count_ = 0;
    compose_candidate_index_ = 0;
    for (size_t i = 0; i < kComposeCandidateMax; ++i)
    {
        compose_candidates_[i][0] = '\0';
    }

    if (!usesSmartCompose() || !isAlphaComposeMode(compose_mode_) || compose_preedit_len_ == 0)
    {
        return;
    }

    for (const char* term : kHamTerms)
    {
        if (!hasPrefixIgnoreCase(term, compose_preedit_))
        {
            continue;
        }
        copyText(compose_candidates_[compose_candidate_count_], term);
        ++compose_candidate_count_;
        if (compose_candidate_count_ >= kComposeCandidateMax)
        {
            break;
        }
    }
}

bool Runtime::commitComposeCandidate()
{
    if (compose_candidate_count_ == 0 || compose_candidate_index_ >= compose_candidate_count_)
    {
        return false;
    }
    appendComposeWord(compose_candidates_[compose_candidate_index_]);
    compose_preedit_[0] = '\0';
    compose_preedit_len_ = 0;
    compose_abc_last_group_index_ = -1;
    compose_abc_last_tap_ms_ = 0;
    compose_abc_tap_index_ = 0;
    rebuildComposeCandidates();
    return true;
}

bool Runtime::commitComposePreedit(bool prefer_candidate)
{
    if (compose_preedit_len_ == 0)
    {
        return false;
    }
    if (prefer_candidate && commitComposeCandidate())
    {
        return true;
    }
    appendComposeWord(compose_preedit_);
    compose_preedit_[0] = '\0';
    compose_preedit_len_ = 0;
    compose_abc_last_group_index_ = -1;
    compose_abc_last_tap_ms_ = 0;
    compose_abc_tap_index_ = 0;
    rebuildComposeCandidates();
    return true;
}

void Runtime::appendComposeChar(char ch)
{
    if (!usesSmartCompose())
    {
        appendChar(compose_buffer_, sizeof(compose_buffer_), compose_len_, ch);
        return;
    }

    if (isAlphaComposeMode(compose_mode_) && isAlphaAscii(ch))
    {
        appendChar(compose_preedit_, sizeof(compose_preedit_), compose_preedit_len_, upperAscii(ch));
        rebuildComposeCandidates();
        return;
    }

    if (ch == ' ')
    {
        (void)commitComposePreedit(true);
        compose_abc_last_group_index_ = -1;
        compose_abc_last_tap_ms_ = 0;
        compose_abc_tap_index_ = 0;
        if (compose_len_ > 0 && compose_buffer_[compose_len_ - 1] != ' ')
        {
            appendChar(compose_buffer_, sizeof(compose_buffer_), compose_len_, ' ');
        }
        return;
    }

    (void)commitComposePreedit(true);
    compose_abc_last_group_index_ = -1;
    compose_abc_last_tap_ms_ = 0;
    compose_abc_tap_index_ = 0;
    appendChar(compose_buffer_, sizeof(compose_buffer_), compose_len_, ch);
}

void Runtime::appendComposeWord(const char* text)
{
    if (!text || text[0] == '\0')
    {
        return;
    }

    if (compose_len_ > 0 && compose_buffer_[compose_len_ - 1] != ' ')
    {
        appendChar(compose_buffer_, sizeof(compose_buffer_), compose_len_, ' ');
    }

    for (size_t i = 0; text[i] != '\0'; ++i)
    {
        appendChar(compose_buffer_, sizeof(compose_buffer_), compose_len_, applyComposeAlphaCase(compose_mode_, text[i]));
    }
}

void Runtime::activateComposeAction()
{
    switch (static_cast<ComposeAction>(compose_action_index_))
    {
    case ComposeAction::Mode:
        cycleComposeMode();
        compose_focus_ = ComposeFocus::Action;
        break;
    case ComposeAction::Space:
        appendComposeChar(' ');
        compose_focus_ = ComposeFocus::Body;
        break;
    case ComposeAction::Back:
        finishTextEdit(false);
        break;
    case ComposeAction::Delete:
        removeComposeChar();
        compose_focus_ = ComposeFocus::Body;
        break;
    case ComposeAction::Send:
        sendComposeMessage();
        break;
    }
}

void Runtime::saveEditedTextToConfig()
{
    if (!app())
    {
        return;
    }

    auto& cfg = app()->getConfig();
    switch (edit_target_)
    {
    case EditTarget::UserName:
        copyText(cfg.node_name, compose_buffer_);
        break;
    case EditTarget::ShortName:
        copyText(cfg.short_name, compose_buffer_);
        break;
    case EditTarget::MeshtasticPsk:
        (void)hexToBytes(compose_buffer_, cfg.meshtastic_config.secondary_key, 16);
        break;
    case EditTarget::MeshCoreChannelName:
        copyText(cfg.meshcore_config.meshcore_channel_name, compose_buffer_);
        break;
    default:
        break;
    }
    commitConfig();
}

void Runtime::formatTime(char* out_time, size_t out_len, char* out_date, size_t date_len) const
{
    if (out_time && out_len > 0)
    {
        out_time[0] = '\0';
    }
    if (out_date && date_len > 0)
    {
        out_date[0] = '\0';
    }

    if (!host_.utc_now_fn && !host_.millis_fn)
    {
        return;
    }

    time_t now = host_.utc_now_fn ? host_.utc_now_fn() : 0;
    const bool has_valid_wall_clock = now >= static_cast<time_t>(1700000000);

    if (has_valid_wall_clock && host_.timezone_offset_min_fn)
    {
        now += static_cast<time_t>(host_.timezone_offset_min_fn()) * 60;
    }

    if (!has_valid_wall_clock)
    {
        const uint32_t uptime_s = host_.millis_fn ? (host_.millis_fn() / 1000U) : 0U;
        const uint32_t hours = uptime_s / 3600U;
        const uint32_t minutes = (uptime_s / 60U) % 60U;
        const uint32_t seconds = uptime_s % 60U;

        if (out_time && out_len > 0)
        {
            std::snprintf(out_time, out_len, "%02lu:%02lu:%02lu",
                          static_cast<unsigned long>(hours),
                          static_cast<unsigned long>(minutes),
                          static_cast<unsigned long>(seconds));
        }
        if (out_date && date_len > 0)
        {
            std::snprintf(out_date, date_len, "TIME UNSYNC");
        }
        return;
    }

    const tm* local = gmtime(&now);
    if (!local)
    {
        return;
    }

    if (out_time && out_len > 0)
    {
        std::snprintf(out_time, out_len, "%02d:%02d:%02d", local->tm_hour, local->tm_min, local->tm_sec);
    }
    if (out_date && date_len > 0)
    {
        const char* weekday = (local->tm_wday >= 0 && local->tm_wday < 7) ? kWeekdays[local->tm_wday] : "---";
        std::snprintf(out_date, date_len, "%04d-%02d-%02d %s",
                      local->tm_year + 1900, local->tm_mon + 1, local->tm_mday, weekday);
    }
}

void Runtime::formatProtocol(char* out, size_t out_len) const
{
    if (!out || out_len == 0 || !app())
    {
        return;
    }
    std::snprintf(out, out_len, "%s", protocolShortLabel(app()->getConfig().mesh_protocol));
}

void Runtime::formatNodeLabel(char* out, size_t out_len) const
{
    if (!out || out_len == 0 || !app())
    {
        return;
    }
    chat::runtime::formatScreenNodeLabel(app()->getSelfNodeId(), out, out_len);
}

void Runtime::formatComposeTarget(char* out, size_t out_len) const
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';

    if (active_conversation_.peer == 0)
    {
        std::snprintf(out, out_len, "BCST");
        return;
    }

    std::snprintf(out, out_len, "%04lX", static_cast<unsigned long>(active_conversation_.peer & 0xFFFFUL));
}

void Runtime::drawTitleBar(const char* left, const char* right)
{
    if (left && left[0] != '\0')
    {
        text_renderer_.drawText(display_, 0, 0, left);
    }
    if (right && right[0] != '\0')
    {
        const int w = text_renderer_.measureTextWidth(right);
        text_renderer_.drawText(display_, std::max(0, display_.width() - w), 0, right);
    }
    display_.drawHLine(0, 8, display_.width());
}

void Runtime::drawMenuList(const char* title, const char* const* items, size_t count, size_t selected)
{
    drawTitleBar(title, nullptr);
    const int line_h = text_renderer_.lineHeight();
    for (size_t i = 0; i < count && i < 5; ++i)
    {
        drawTextClipped(0, 10 + static_cast<int>(i * line_h), display_.width(), items[i], i == selected);
    }
}

void Runtime::drawFooterHint(const char* hint)
{
    if (!hint)
    {
        return;
    }
    drawTextClipped(0, 56, display_.width(), hint);
}

void Runtime::drawTextClipped(int x, int y, int w, const char* text, bool inverse)
{
    if (!text || w <= 0)
    {
        return;
    }

    char clipped[48] = {};
    if (text_renderer_.measureTextWidth(text) <= w)
    {
        copyText(clipped, text);
    }
    else if (w > text_renderer_.ellipsisWidth())
    {
        const size_t keep_bytes = text_renderer_.clipTextToWidth(text, w - text_renderer_.ellipsisWidth());
        std::memcpy(clipped, text, std::min(keep_bytes, sizeof(clipped) - 4));
        clipped[std::min(keep_bytes, sizeof(clipped) - 4)] = '\0';
        std::strcat(clipped, "...");
    }
    else
    {
        const size_t keep_bytes = text_renderer_.clipTextToWidth(text, w);
        std::memcpy(clipped, text, std::min(keep_bytes, sizeof(clipped) - 1));
        clipped[std::min(keep_bytes, sizeof(clipped) - 1)] = '\0';
    }
    text_renderer_.drawText(display_, x, y, clipped, inverse);
}

bool Runtime::editUsesHexCharset() const
{
    return edit_target_ == EditTarget::MeshtasticPsk;
}

bool Runtime::usesSmartCompose() const
{
    return edit_target_ == EditTarget::Message;
}

uint32_t Runtime::nowMs() const
{
    return host_.millis_fn ? host_.millis_fn() : 0U;
}

app::IAppFacade* Runtime::app() const
{
    return host_.app;
}

const chat::ChatMessage* Runtime::selectedMessage() const
{
    if (message_count_ == 0)
    {
        return nullptr;
    }
    const size_t index = std::min(message_index_, message_count_ - 1U);
    return &messages_[index];
}

} // namespace ui::mono_128x64
