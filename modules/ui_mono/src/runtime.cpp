#include "ui/mono/runtime.h"

#include "app/app_config.h"
#include "ble/ble_manager.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/infra/meshcore/mc_region_presets.h"
#include "chat/infra/meshtastic/mt_protocol_helpers.h"
#include "chat/infra/meshtastic/mt_region.h"
#include "chat/runtime/mesh_adapter_protocol_effect_executor.h"
#include "chat/runtime/meshtastic_runtime.h"
#include "chat/runtime/protocol_runtime_factory.h"
#include "chat/runtime/self_identity_policy.h"
#include "chat/usecase/chat_service.h"
#include "chat/usecase/contact_service.h"
#include "generated/meshtastic/mesh.pb.h"
#include "platform/ui/screen_runtime.h"
#include "ui/mono/assets/trailmate_sleep_logo.h"
#include "ui/mono/screens/screensaver_layout.h"

#ifndef TRAIL_MATE_MONO_PINYIN_LOOKUP_ENABLED
#if (defined(GAT562_NO_PINYIN_IME) && GAT562_NO_PINYIN_IME) || (defined(GAT562_NO_CJK) && GAT562_NO_CJK)
#define TRAIL_MATE_MONO_PINYIN_LOOKUP_ENABLED 0
#else
#define TRAIL_MATE_MONO_PINYIN_LOOKUP_ENABLED 1
#endif
#endif

#if TRAIL_MATE_MONO_PINYIN_LOOKUP_ENABLED
#include "ui/widgets/ime/pinyin_lookup.h"
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <variant>

#ifndef TRAILMATE_NRF52_BLE_DISABLED
#define TRAILMATE_NRF52_BLE_DISABLED 0
#endif

#ifndef TRAILMATE_NRF_MONO_TARGET_DEVICE_IO_SETTINGS
#define TRAILMATE_NRF_MONO_TARGET_DEVICE_IO_SETTINGS 0
#endif

#ifndef TRAILMATE_NRF_MONO_TARGET_MESSAGE_SOUND_SETTING
#define TRAILMATE_NRF_MONO_TARGET_MESSAGE_SOUND_SETTING TRAILMATE_NRF_MONO_TARGET_DEVICE_IO_SETTINGS
#endif

namespace ui::mono
{
namespace
{
class NoopProtocolEffectExecutor final : public chat::runtime::IProtocolEffectExecutor
{
  public:
    bool execute(const chat::runtime::ProtocolEffect&) override
    {
        return false;
    }
};

const char* inputActionName(InputAction action)
{
    switch (action)
    {
    case InputAction::None:
        return "None";
    case InputAction::Up:
        return "Up";
    case InputAction::Down:
        return "Down";
    case InputAction::Left:
        return "Left";
    case InputAction::Right:
        return "Right";
    case InputAction::Select:
        return "Select";
    case InputAction::Back:
        return "Back";
    case InputAction::Primary:
        return "Primary";
    case InputAction::Secondary:
        return "Secondary";
    default:
        return "?";
    }
}

void drawBitmap1bpp(MonoDisplay& display,
                    int x,
                    int y,
                    int width,
                    int height,
                    int stride,
                    const uint8_t* bitmap)
{
    if (!bitmap || width <= 0 || height <= 0 || stride <= 0)
    {
        return;
    }

    for (int row = 0; row < height; ++row)
    {
        for (int col = 0; col < width; ++col)
        {
            const uint8_t byte = bitmap[row * stride + col / 8];
            if ((byte & (0x80U >> (col % 8))) != 0)
            {
                display.drawPixel(x + col, y + row, true);
            }
        }
    }
}

constexpr const char* kMainMenuItems[] = {
    "CHATS",
    "NODES",
    "GPS",
    "SETTINGS",
    "INFO",
};

constexpr const char* kMeshCoreMainMenuItems[] = {
    "CHATS",
    "NODES",
    "GPS",
    "DISCOVER",
    "SETTINGS",
    "INFO",
};

constexpr const char* kDiscoverItems[] = {
    "SCAN LOCAL",
    "ID LOCAL",
    "ID BROADCAST",
    "CANCEL",
};

constexpr const char* kSettingsMenuItems[] = {
    "PROTO",
    "DEVICE",
    "ACTIONS",
};

enum class RadioSettingItem
{
    Protocol,
    MtRegion,
    MtMode,
    MtPreset,
    MtBandwidth,
    MtSpreadFactor,
    MtCodingRate,
    MtTxPower,
    MtChannelSlot,
    MtPrimaryKey,
    MtOverrideFrequency,
    MtFrequencyOffset,
    McRegion,
    McFrequency,
    McBandwidth,
    McSpreadFactor,
    McCodingRate,
    McTxPower,
    McChannelSlot,
    McChannelName,
    McChannelKey,
    Encrypt,
};

struct RadioSettingDef
{
    RadioSettingItem item;
    const char* label;
};

constexpr RadioSettingDef kRadioItems[] = {
    {RadioSettingItem::Protocol, "PROTO"},
};

constexpr uint16_t kMeshtasticChannelNumMax = 16;
constexpr float kRadioFrequencyMinMhz = 400.0f;
constexpr float kRadioFrequencyMaxMhz = 2500.0f;
constexpr float kRadioFrequencyStepMhz = 0.1f;
constexpr float kMeshtasticFrequencyOffsetStepMhz = 0.025f;
constexpr float kMeshtasticFrequencyOffsetMinMhz = -5.0f;
constexpr float kMeshtasticFrequencyOffsetMaxMhz = 5.0f;
constexpr float kLoRaBandwidthOptionsKhz[] = {31.25f, 62.5f, 125.0f, 250.0f, 500.0f};
constexpr uint32_t kScreenTimeoutAlwaysMs = 300000UL;
constexpr uint32_t kScreenTimeoutOptionsMs[] = {15000UL, 30000UL, 60000UL, kScreenTimeoutAlwaysMs};

uint16_t normalizedMeshtasticChannelNum(uint16_t channel_num)
{
    return std::min<uint16_t>(channel_num, kMeshtasticChannelNumMax);
}

void sanitizeMeshtasticChannelNum(app::AppConfig& cfg)
{
    cfg.meshtastic_config.channel_num = normalizedMeshtasticChannelNum(cfg.meshtastic_config.channel_num);
}

void formatMeshtasticChannelSlot(char* out, size_t out_len, uint16_t channel_num)
{
    if (!out || out_len == 0)
    {
        return;
    }

    const unsigned slot = static_cast<unsigned>(normalizedMeshtasticChannelNum(channel_num));
    if (slot == 0U)
    {
        std::snprintf(out, out_len, "Auto");
        return;
    }

    std::snprintf(out, out_len, "%u", slot);
}

enum class DeviceSettingItem
{
#if !TRAILMATE_NRF52_BLE_DISABLED
    Ble,
#endif
#if TRAILMATE_NRF_MONO_TARGET_MESSAGE_SOUND_SETTING
    MessageTone,
#endif
#if TRAILMATE_NRF_MONO_TARGET_DEVICE_IO_SETTINGS
    MessageLight,
    LedColor,
    KeyboardLight,
#endif
    Gps,
    Sats,
    GpsInterval,
    ScreenOff,
    TimeZone,
};

constexpr const char* kDeviceItems[] = {
#if !TRAILMATE_NRF52_BLE_DISABLED
    "BLE",
#endif
#if TRAILMATE_NRF_MONO_TARGET_MESSAGE_SOUND_SETTING
    "MSG SOUND",
#endif
#if TRAILMATE_NRF_MONO_TARGET_DEVICE_IO_SETTINGS
    "MSG LIGHT",
    "LED COLOR",
    "KEY LIGHT",
#endif
    "GPS",
    "SATS",
    "GPS INT",
    "SCREEN OFF",
    "TIME ZONE",
};

constexpr DeviceSettingItem kDeviceItemIds[] = {
#if !TRAILMATE_NRF52_BLE_DISABLED
    DeviceSettingItem::Ble,
#endif
#if TRAILMATE_NRF_MONO_TARGET_MESSAGE_SOUND_SETTING
    DeviceSettingItem::MessageTone,
#endif
#if TRAILMATE_NRF_MONO_TARGET_DEVICE_IO_SETTINGS
    DeviceSettingItem::MessageLight,
    DeviceSettingItem::LedColor,
    DeviceSettingItem::KeyboardLight,
#endif
    DeviceSettingItem::Gps,
    DeviceSettingItem::Sats,
    DeviceSettingItem::GpsInterval,
    DeviceSettingItem::ScreenOff,
    DeviceSettingItem::TimeZone,
};

constexpr const char* kActionItems[] = {
    "BROADCAST ID",
    "CLEAR NODES",
    "CLEAR MSGS",
    "CLEAR CACHE",
};

bool actionNeedsConfirm(size_t index)
{
    return index == 1 || index == 2 || index == 3;
}

constexpr const char* kMessageMenuItems[] = {
    "INFO",
    "SEND",
    "RETRY",
};

constexpr const char* kNewChatItems[] = {
    "PRIMARY",
    "SECONDARY",
    "CANCEL",
};

constexpr size_t kNodeActionItemCount = 7;

constexpr const char* kWeekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
constexpr const char* kComposeCharset =
    " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.,!?-_/:#@+*=%&$~()[]{}<>\\|;\"'`";
constexpr const char* kHexCharset = "0123456789ABCDEF";
constexpr const char* kComposeAlphaKeys = "QWERTYUIOPASDFGHJKLZXCVBNM";
constexpr const char* kComposeAbcKeys = " ETAOINSHRDLUCMFWYPVBGKQJXZ";
constexpr const char* kComposeNumKeys = "0123456789";
constexpr const char* kComposeSymKeys = " .,!?/-:@#()[]{}+=_*%&$~\\|;\"'`<>";
constexpr const char* kComposeActionLabels[] = {"ABC", "SP", "BACK", "DEL", "SEND"};
constexpr size_t kVirtualKeyboardCols = 3;
constexpr size_t kVirtualKeyboardRows = 3;
constexpr size_t kVirtualKeyboardPageSize = kVirtualKeyboardCols * kVirtualKeyboardRows;
constexpr size_t kDefaultComposeCandidatePageSize = 5;
constexpr const char* kFullAlphaRows[] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
constexpr const char* kFullNumRows[] = {"1234567890"};
constexpr const char* kFullSymRows[] = {".,!?/-:@#", "()[]{}+=", "_*%&$~\\|"};

constexpr uint32_t kConversationScrollStartPauseMs = 500;
constexpr uint32_t kConversationScrollStepMs = 250;
constexpr uint32_t kConversationScrollEndPauseMs = 800;
constexpr uint32_t kBootMinMs = 1800;
constexpr uint32_t kComposeMultiTapWindowMs = 1500;
constexpr uint32_t kLargeScreensaverRefreshMs = 60000;
constexpr uint32_t kGnssSnapshotRefreshMs = 5000;
constexpr size_t kKeyTextMax = 96;
constexpr int kTimezoneMin = -12 * 60;
constexpr int kTimezoneMax = 14 * 60;
constexpr int kTimezoneStep = 60;
constexpr const char* kHamTerms[] = {
    "73",
    "88",
    "AGN",
    "ALFA",
    "ATT",
    "BREAK",
    "BRAVO",
    "CALL",
    "CHARLIE",
    "COPY",
    "CQ",
    "CTCSS",
    "DE",
    "DELTA",
    "DIRECT",
    "DUPLEX",
    "DX",
    "ECHO",
    "ES",
    "FB",
    "FM",
    "FREQ",
    "FT8",
    "FT4",
    "HOTEL",
    "ID",
    "INDIA",
    "INFO",
    "JA",
    "JULIETT",
    "KILO",
    "LIMA",
    "LOG",
    "LSB",
    "MIKE",
    "NIL",
    "NOVEMBER",
    "OM",
    "OSCAR",
    "OUT",
    "OVER",
    "PAPA",
    "PSE",
    "QRL",
    "QRM",
    "QRN",
    "QRP",
    "QRQ",
    "QRS",
    "QRT",
    "QRV",
    "QRZ",
    "QSB",
    "QSL",
    "QSO",
    "QSP",
    "QSX",
    "QSY",
    "QTC",
    "QTH",
    "QTR",
    "QUEBEC",
    "RELAY",
    "REPEATER",
    "RIG",
    "ROGER",
    "ROMEO",
    "RST",
    "RX",
    "SEND",
    "SIERRA",
    "SIMPLEX",
    "SOLID",
    "SPLIT",
    "STANDBY",
    "SYM",
    "TANGO",
    "TKS",
    "TNX",
    "TU",
    "TX",
    "UNIFORM",
    "USB",
    "VICTOR",
    "WAIT",
    "WHISKEY",
    "X-RAY",
    "XYL",
    "YANKEE",
    "YL",
    "ZULU",
};

template <typename T, size_t N>
constexpr size_t arrayCount(const T (&)[N])
{
    return N;
}

static_assert(arrayCount(kRadioItems) > 0, "radio settings must not be empty");
static_assert(arrayCount(kDeviceItems) == arrayCount(kDeviceItemIds),
              "device settings labels and ids must stay aligned");

const char* meshOperationFailureLabel(chat::MeshOperationFailure failure)
{
    switch (failure)
    {
    case chat::MeshOperationFailure::None:
        return "OK";
    case chat::MeshOperationFailure::InvalidInput:
        return "INVALID INPUT";
    case chat::MeshOperationFailure::Unsupported:
        return "UNSUPPORTED";
    case chat::MeshOperationFailure::NotReady:
        return "NOT READY";
    case chat::MeshOperationFailure::TxDisabled:
        return "TX DISABLED";
    case chat::MeshOperationFailure::RadioOffline:
        return "RADIO OFFLINE";
    case chat::MeshOperationFailure::DutyCycleLimited:
        return "DUTY LIMITED";
    case chat::MeshOperationFailure::LocalIdentityMissing:
        return "NO IDENTITY";
    case chat::MeshOperationFailure::PeerKeyMissing:
        return "NO PEER KEY";
    case chat::MeshOperationFailure::ChannelKeyMissing:
        return "NO CHANNEL KEY";
    case chat::MeshOperationFailure::EncodeFailed:
        return "ENCODE FAILED";
    case chat::MeshOperationFailure::CryptoFailed:
        return "CRYPTO FAILED";
    case chat::MeshOperationFailure::RadioTxFailed:
        return "RADIO TX FAILED";
    case chat::MeshOperationFailure::Busy:
        return "BUSY";
    case chat::MeshOperationFailure::Unknown:
    default:
        return "FAILED";
    }
}

struct VirtualKeyboardRows
{
    const char* const* rows = nullptr;
    size_t row_count = 0;
};

VirtualKeyboardRows fullKeyboardRowsForMode(ui::mono::Runtime::ComposeMode mode)
{
    switch (mode)
    {
    case ui::mono::Runtime::ComposeMode::Num:
        return {kFullNumRows, arrayCount(kFullNumRows)};
    case ui::mono::Runtime::ComposeMode::Sym:
        return {kFullSymRows, arrayCount(kFullSymRows)};
    case ui::mono::Runtime::ComposeMode::Cn:
    case ui::mono::Runtime::ComposeMode::AbcUpper:
    case ui::mono::Runtime::ComposeMode::AbcLower:
    default:
        return {kFullAlphaRows, arrayCount(kFullAlphaRows)};
    }
}

size_t fullKeyboardKeyCount(VirtualKeyboardRows grid)
{
    size_t count = 0;
    for (size_t row = 0; row < grid.row_count; ++row)
    {
        count += std::strlen(grid.rows[row]);
    }
    return count;
}

bool fullKeyboardIndexToCell(VirtualKeyboardRows grid, size_t index, size_t& out_row, size_t& out_col)
{
    size_t base = 0;
    for (size_t row = 0; row < grid.row_count; ++row)
    {
        const size_t len = std::strlen(grid.rows[row]);
        if (index < base + len)
        {
            out_row = row;
            out_col = index - base;
            return true;
        }
        base += len;
    }
    out_row = 0;
    out_col = 0;
    return false;
}

size_t fullKeyboardCellToIndex(VirtualKeyboardRows grid, size_t target_row, size_t target_col)
{
    size_t index = 0;
    for (size_t row = 0; row < grid.row_count; ++row)
    {
        const size_t len = std::strlen(grid.rows[row]);
        if (row == target_row)
        {
            return index + std::min(target_col, len > 0 ? len - 1U : 0U);
        }
        index += len;
    }
    return 0;
}

bool fullKeyboardMoveVertical(ui::mono::Runtime::ComposeMode mode, size_t current, int delta, size_t& next)
{
    const VirtualKeyboardRows grid = fullKeyboardRowsForMode(mode);
    size_t row = 0;
    size_t col = 0;
    if (grid.row_count == 0 || !fullKeyboardIndexToCell(grid, current, row, col))
    {
        next = 0;
        return false;
    }

    if ((delta < 0 && row == 0) || (delta > 0 && row + 1U >= grid.row_count))
    {
        next = current;
        return false;
    }

    const size_t target_row = delta < 0 ? row - 1U : row + 1U;
    next = fullKeyboardCellToIndex(grid, target_row, col);
    return true;
}

size_t radioItemCount()
{
    return arrayCount(kRadioItems);
}

RadioSettingItem radioSettingItem(size_t index)
{
    return index < arrayCount(kRadioItems) ? kRadioItems[index].item : RadioSettingItem::Protocol;
}

const char* radioSettingLabel(size_t index)
{
    return index < arrayCount(kRadioItems) ? kRadioItems[index].label : "LORA";
}

DeviceSettingItem deviceSettingItem(size_t index)
{
    return index < arrayCount(kDeviceItemIds) ? kDeviceItemIds[index] : DeviceSettingItem::Gps;
}

template <typename T>
constexpr T clampValue(T value, T low, T high)
{
    return value < low ? low : (value > high ? high : value);
}

template <size_t N>
float stepOption(float current, const float (&options)[N], int delta)
{
    size_t nearest = 0;
    float nearest_diff = std::fabs(current - options[0]);
    for (size_t i = 1; i < N; ++i)
    {
        const float diff = std::fabs(current - options[i]);
        if (diff < nearest_diff)
        {
            nearest = i;
            nearest_diff = diff;
        }
    }

    const int next = clampValue<int>(static_cast<int>(nearest) + delta, 0, static_cast<int>(N) - 1);
    return options[next];
}

float stepClampedFloat(float current, int delta, float step, float low, float high)
{
    return clampValue(current + (static_cast<float>(delta) * step), low, high);
}

uint32_t normalizedScreenTimeoutMs(uint32_t timeout_ms)
{
    return platform::ui::screen::clamp_timeout_ms(timeout_ms);
}

size_t screenTimeoutOptionIndex(uint32_t timeout_ms)
{
    timeout_ms = normalizedScreenTimeoutMs(timeout_ms);
    size_t index = 0;
    while (index + 1 < arrayCount(kScreenTimeoutOptionsMs) &&
           kScreenTimeoutOptionsMs[index] < timeout_ms)
    {
        ++index;
    }
    return index;
}

uint32_t stepScreenTimeoutMs(uint32_t current_timeout_ms, int delta)
{
    const int current = static_cast<int>(screenTimeoutOptionIndex(current_timeout_ms));
    const int next = clampValue(current + delta, 0, static_cast<int>(arrayCount(kScreenTimeoutOptionsMs)) - 1);
    return kScreenTimeoutOptionsMs[next];
}

void formatScreenTimeoutLabel(char* out, size_t out_len, uint32_t timeout_ms)
{
    if (!out || out_len == 0)
    {
        return;
    }

    const uint32_t normalized = normalizedScreenTimeoutMs(timeout_ms);
    if (normalized >= kScreenTimeoutAlwaysMs)
    {
        std::snprintf(out, out_len, "Always");
        return;
    }
    if (normalized % 60000UL == 0)
    {
        std::snprintf(out, out_len, "%lumin", static_cast<unsigned long>(normalized / 60000UL));
        return;
    }

    std::snprintf(out, out_len, "%lus", static_cast<unsigned long>(normalized / 1000UL));
}

size_t utf8CharLength(unsigned char lead)
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

void appendText(char* buffer, size_t capacity, size_t& len, const char* text)
{
    if (!buffer || capacity == 0 || !text)
    {
        return;
    }

    for (size_t i = 0; text[i] != '\0';)
    {
        const size_t cp_len = utf8CharLength(text[i]);
        size_t actual_len = 0;
        while (actual_len < cp_len && text[i + actual_len] != '\0')
        {
            ++actual_len;
        }
        if (actual_len == 0 || len + actual_len >= capacity)
        {
            return;
        }
        std::memcpy(buffer + len, text + i, actual_len);
        len += actual_len;
        buffer[len] = '\0';
        i += actual_len;
    }
}

void popChar(char* buffer, size_t& len)
{
    if (!buffer || len == 0)
    {
        return;
    }
    size_t start = len - 1U;
    while (start > 0U && (static_cast<unsigned char>(buffer[start]) & 0xC0U) == 0x80U)
    {
        --start;
    }
    len = start;
    buffer[len] = '\0';
}

const char* composeKeysetForMode(ui::mono::Runtime::ComposeMode mode)
{
    switch (mode)
    {
    case ui::mono::Runtime::ComposeMode::Num:
        return kComposeNumKeys;
    case ui::mono::Runtime::ComposeMode::Sym:
        return kComposeSymKeys;
    case ui::mono::Runtime::ComposeMode::Cn:
        return kComposeAlphaKeys;
    case ui::mono::Runtime::ComposeMode::AbcUpper:
    case ui::mono::Runtime::ComposeMode::AbcLower:
    default:
        return kComposeAlphaKeys;
    }
}

const char* composeModeLabel(ui::mono::Runtime::ComposeMode mode)
{
    switch (mode)
    {
    case ui::mono::Runtime::ComposeMode::AbcLower:
        return "en";
    case ui::mono::Runtime::ComposeMode::AbcUpper:
        return "EN";
    case ui::mono::Runtime::ComposeMode::Num:
        return "123";
    case ui::mono::Runtime::ComposeMode::Sym:
        return "SYN";
    case ui::mono::Runtime::ComposeMode::Cn:
        return "CN";
    default:
        return "EN";
    }
}

const char* physicalComposeModeLabel(ui::mono::Runtime::ComposeMode mode)
{
    switch (mode)
    {
    case ui::mono::Runtime::ComposeMode::Num:
        return "123";
    case ui::mono::Runtime::ComposeMode::Sym:
        return "SYN";
    case ui::mono::Runtime::ComposeMode::Cn:
        return "CN";
    case ui::mono::Runtime::ComposeMode::AbcLower:
    case ui::mono::Runtime::ComposeMode::AbcUpper:
    default:
        return "EN";
    }
}

bool isAlphaComposeMode(ui::mono::Runtime::ComposeMode mode)
{
    return mode == ui::mono::Runtime::ComposeMode::AbcLower ||
           mode == ui::mono::Runtime::ComposeMode::AbcUpper;
}

char applyComposeAlphaCase(ui::mono::Runtime::ComposeMode mode, char ch)
{
    if (std::isalpha(static_cast<unsigned char>(ch)) == 0)
    {
        return ch;
    }

    if (mode == ui::mono::Runtime::ComposeMode::AbcLower)
    {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
}

char displayComposeKeyChar(ui::mono::Runtime::ComposeMode mode, char ch)
{
    if (ch == ' ')
    {
        return '_';
    }
    return std::isalpha(static_cast<unsigned char>(ch)) != 0
               ? applyComposeAlphaCase(mode, ch)
               : ch;
}

char upperAscii(char ch)
{
    return static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
}

bool isAlphaAscii(char ch)
{
    return std::isalpha(static_cast<unsigned char>(ch)) != 0;
}

const char* phoneCycleCharsForKey(char ch)
{
    switch (ch)
    {
    case '2':
        return "abc2";
    case '3':
        return "def3";
    case '4':
        return "ghi4";
    case '5':
        return "jkl5";
    case '6':
        return "mno6";
    case '7':
        return "pqrs7";
    case '8':
        return "tuv8";
    case '9':
        return "wxyz9";
    case '1':
        return ".,?!1";
    default:
        return "";
    }
}

const char* phoneSymbolCycleCharsForKey(char ch)
{
    switch (ch)
    {
    case '1':
        return ".,?!1";
    case '2':
        return "@/:;2";
    case '3':
        return "#+-=3";
    case '4':
        return "_$%&4";
    case '5':
        return "()[]5";
    case '6':
        return "{}<>6";
    case '7':
        return "'\"`~7";
    case '8':
        return "\\|*^8";
    case '9':
        return "-+=/9";
    case '0':
        return " 0";
    default:
        return "";
    }
}

char applyPhoneCycleCase(ui::mono::Runtime::ComposeMode mode, char ch)
{
    if (std::isalpha(static_cast<unsigned char>(ch)) == 0)
    {
        return ch;
    }
    return mode == ui::mono::Runtime::ComposeMode::AbcUpper
               ? static_cast<char>(std::toupper(static_cast<unsigned char>(ch)))
               : static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
}

void formatPhoneCycleHint(char* out, size_t out_len,
                          ui::mono::Runtime::ComposeMode mode,
                          char key,
                          size_t tap_index)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';

    const char* chars = mode == ui::mono::Runtime::ComposeMode::Sym
                            ? phoneSymbolCycleCharsForKey(key)
                            : phoneCycleCharsForKey(key);
    const size_t count = std::strlen(chars);
    if (count == 0)
    {
        return;
    }

    char group[8] = {};
    size_t pos = 0;
    for (size_t i = 0; i < count && pos + 1 < sizeof(group); ++i)
    {
        const char ch = chars[i];
        group[pos++] = std::isalpha(static_cast<unsigned char>(ch)) != 0
                           ? static_cast<char>(std::toupper(static_cast<unsigned char>(ch)))
                           : ch;
    }
    group[pos] = '\0';

    const char selected = applyPhoneCycleCase(mode, chars[tap_index % count]);
    std::snprintf(out, out_len, "%c %s:%c", key, group, selected);
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

double degToRad(double deg)
{
    return deg * 3.14159265358979323846 / 180.0;
}

double radToDeg(double rad)
{
    return rad * 180.0 / 3.14159265358979323846;
}

double normalizeBearingDeg(double deg)
{
    while (deg < 0.0)
    {
        deg += 360.0;
    }
    while (deg >= 360.0)
    {
        deg -= 360.0;
    }
    return deg;
}

double haversineMeters(double lat1, double lon1, double lat2, double lon2)
{
    constexpr double kEarthRadiusM = 6371000.0;
    const double dlat = degToRad(lat2 - lat1);
    const double dlon = degToRad(lon2 - lon1);
    const double a = std::sin(dlat / 2.0) * std::sin(dlat / 2.0) +
                     std::cos(degToRad(lat1)) * std::cos(degToRad(lat2)) *
                         std::sin(dlon / 2.0) * std::sin(dlon / 2.0);
    const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(std::max(0.0, 1.0 - a)));
    return kEarthRadiusM * c;
}

double bearingDegrees(double lat1, double lon1, double lat2, double lon2)
{
    const double lat1r = degToRad(lat1);
    const double lat2r = degToRad(lat2);
    const double dlonr = degToRad(lon2 - lon1);
    const double y = std::sin(dlonr) * std::cos(lat2r);
    const double x = std::cos(lat1r) * std::sin(lat2r) -
                     std::sin(lat1r) * std::cos(lat2r) * std::cos(dlonr);
    return normalizeBearingDeg(radToDeg(std::atan2(y, x)));
}

const char* bearingCardinal(double bearing_deg)
{
    static constexpr const char* kDirs[] = {
        "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
        "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
    const int index = static_cast<int>((normalizeBearingDeg(bearing_deg) + 11.25) / 22.5) % 16;
    return kDirs[index];
}

const char* bearingOctant(double bearing_deg)
{
    static constexpr const char* kDirs[] = {
        "N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    const int index = static_cast<int>((normalizeBearingDeg(bearing_deg) + 22.5) / 45.0) % 8;
    return kDirs[index];
}

void formatBearingCompact(char* out, size_t out_len, double bearing_deg)
{
    if (!out || out_len == 0)
    {
        return;
    }

    const int rounded_deg = static_cast<int>(std::lround(normalizeBearingDeg(bearing_deg))) % 360;
    std::snprintf(out, out_len, "%s%d", bearingOctant(bearing_deg), rounded_deg);
}

void drawLine(MonoDisplay& display, int x0, int y0, int x1, int y1, bool on = true)
{
    int dx = std::abs(x1 - x0);
    const int sx = (x0 < x1) ? 1 : -1;
    int dy = -std::abs(y1 - y0);
    const int sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while (true)
    {
        display.drawPixel(x0, y0, on);
        if (x0 == x1 && y0 == y1)
        {
            break;
        }
        const int e2 = err * 2;
        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

void drawCircle(MonoDisplay& display, int cx, int cy, int radius, bool on = true)
{
    if (radius <= 0)
    {
        return;
    }

    int x = radius;
    int y = 0;
    int err = 0;
    while (x >= y)
    {
        display.drawPixel(cx + x, cy + y, on);
        display.drawPixel(cx + y, cy + x, on);
        display.drawPixel(cx - y, cy + x, on);
        display.drawPixel(cx - x, cy + y, on);
        display.drawPixel(cx - x, cy - y, on);
        display.drawPixel(cx - y, cy - x, on);
        display.drawPixel(cx + y, cy - x, on);
        display.drawPixel(cx + x, cy - y, on);
        ++y;
        if (err <= 0)
        {
            err += (2 * y) + 1;
        }
        if (err > 0)
        {
            --x;
            err -= (2 * x) + 1;
        }
    }
}

void drawCompassArrow(MonoDisplay& display, int cx, int cy, double bearing_deg, int length, bool on = true)
{
    const double angle = degToRad(normalizeBearingDeg(bearing_deg) - 90.0);
    const int tip_x = cx + static_cast<int>(std::lround(std::cos(angle) * length));
    const int tip_y = cy + static_cast<int>(std::lround(std::sin(angle) * length));
    const int tail_x = cx - static_cast<int>(std::lround(std::cos(angle) * (length / 4.0)));
    const int tail_y = cy - static_cast<int>(std::lround(std::sin(angle) * (length / 4.0)));
    const double left_angle = angle + degToRad(150.0);
    const double right_angle = angle - degToRad(150.0);
    const int wing = std::max(3, length / 3);
    const int left_x = tip_x + static_cast<int>(std::lround(std::cos(left_angle) * wing));
    const int left_y = tip_y + static_cast<int>(std::lround(std::sin(left_angle) * wing));
    const int right_x = tip_x + static_cast<int>(std::lround(std::cos(right_angle) * wing));
    const int right_y = tip_y + static_cast<int>(std::lround(std::sin(right_angle) * wing));

    drawLine(display, tail_x, tail_y, tip_x, tip_y, on);
    drawLine(display, tip_x, tip_y, left_x, left_y, on);
    drawLine(display, tip_x, tip_y, right_x, right_y, on);
}

void drawFrame(MonoDisplay& display, int x, int y, int w, int h, bool filled = false)
{
    if (w <= 0 || h <= 0)
    {
        return;
    }

    if (filled)
    {
        display.fillRect(x, y, w, h, true);
        return;
    }

    display.fillRect(x, y, w, 1, true);
    display.fillRect(x, y + h - 1, w, 1, true);
    display.fillRect(x, y, 1, h, true);
    display.fillRect(x + w - 1, y, 1, h, true);
}

void formatToggleLabel(char* out, size_t out_len, const char* name, bool enabled)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%s %s", name ? name : "--", enabled ? "ON" : "OFF");
}

void splitClockText(const char* time_text, char* main_out, size_t main_len, char* sec_out, size_t sec_len)
{
    if (main_out && main_len > 0)
    {
        main_out[0] = '\0';
    }
    if (sec_out && sec_len > 0)
    {
        sec_out[0] = '\0';
    }
    if (!time_text || !main_out || main_len == 0)
    {
        return;
    }

    if (std::strlen(time_text) >= 5)
    {
        std::snprintf(main_out, main_len, "%.5s", time_text);
    }
    else
    {
        std::snprintf(main_out, main_len, "%s", time_text);
    }

    if (sec_out && sec_len > 0 && std::strlen(time_text) >= 8)
    {
        std::snprintf(sec_out, sec_len, "%.2s", time_text + 6);
    }
}

const char* signalRatingLabel(float snr, float rssi)
{
    if (snr >= 9.0f || rssi >= -90.0f)
    {
        return "STR";
    }
    if (snr >= 4.0f || rssi >= -102.0f)
    {
        return "OK";
    }
    if (snr > -2.0f || rssi >= -112.0f)
    {
        return "WEAK";
    }
    return "POOR";
}

void formatElapsedShort(time_t now_s, uint32_t then_s, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (then_s == 0 || now_s < static_cast<time_t>(1700000000) || now_s < static_cast<time_t>(then_s))
    {
        std::snprintf(out, out_len, "-");
        return;
    }

    uint32_t delta = static_cast<uint32_t>(now_s - static_cast<time_t>(then_s));
    if (delta < 60U)
    {
        std::snprintf(out, out_len, "%lus", static_cast<unsigned long>(delta));
    }
    else if (delta < 3600U)
    {
        std::snprintf(out, out_len, "%lum", static_cast<unsigned long>(delta / 60U));
    }
    else if (delta < 86400U)
    {
        std::snprintf(out, out_len, "%luh", static_cast<unsigned long>(delta / 3600U));
    }
    else
    {
        std::snprintf(out, out_len, "%lud", static_cast<unsigned long>(delta / 86400U));
    }
}

size_t composeCandidatePageSize(const HostCallbacks& host)
{
    return host.compose_candidate_page_size > 0 ? host.compose_candidate_page_size : kDefaultComposeCandidatePageSize;
}

size_t composeCandidatePageStart(size_t selected_index, size_t candidate_count, size_t page_size)
{
    if (candidate_count == 0 || page_size == 0)
    {
        return 0;
    }
    const size_t clamped_index = std::min(selected_index, candidate_count - 1U);
    return (clamped_index / page_size) * page_size;
}

void appendTextBounded(char* out, size_t out_len, size_t& pos, const char* text)
{
    if (!out || out_len == 0 || !text || pos >= out_len)
    {
        return;
    }

    const int written = std::snprintf(out + pos, out_len - pos, "%s", text);
    if (written <= 0)
    {
        return;
    }

    const size_t available = out_len - pos;
    if (available == 0)
    {
        return;
    }
    pos += std::min(static_cast<size_t>(written), available - 1U);
}

void formatDistanceShort(double meters, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!(meters >= 0.0))
    {
        std::snprintf(out, out_len, "-");
        return;
    }
    if (meters < 1000.0)
    {
        std::snprintf(out, out_len, "%.0fm", meters);
    }
    else if (meters < 10000.0)
    {
        std::snprintf(out, out_len, "%.1fk", meters / 1000.0);
    }
    else
    {
        std::snprintf(out, out_len, "%.0fk", meters / 1000.0);
    }
}

const char* gnssFixLabel(::gps::GnssFix fix)
{
    switch (fix)
    {
    case ::gps::GnssFix::FIX2D:
        return "2D";
    case ::gps::GnssFix::FIX3D:
        return "3D";
    case ::gps::GnssFix::NOFIX:
    default:
        return "NO";
    }
}

const char* gnssSystemLabel(::gps::GnssSystem sys)
{
    switch (sys)
    {
    case ::gps::GnssSystem::GPS:
        return "GPS";
    case ::gps::GnssSystem::GLN:
        return "GLN";
    case ::gps::GnssSystem::GAL:
        return "GAL";
    case ::gps::GnssSystem::BD:
        return "BDS";
    case ::gps::GnssSystem::UNKNOWN:
    default:
        return "UNK";
    }
}

template <size_t N, typename... Args>
void pushFormattedLine(char (&lines)[N][40], size_t& line_count, const char* fmt, Args... args)
{
    if (!fmt || line_count >= N)
    {
        return;
    }
    std::snprintf(lines[line_count], sizeof(lines[line_count]), fmt, args...);
    ++line_count;
}

const char* protocolShortLabel(chat::MeshProtocol protocol)
{
    return protocol == chat::MeshProtocol::MeshCore ? "MC" : "MT";
}

const char* protocolMarker(chat::MeshProtocol protocol)
{
    return protocol == chat::MeshProtocol::MeshCore ? "C" : "T";
}

const char* nodeProtocolMarker(chat::contacts::NodeProtocolType protocol)
{
    switch (protocol)
    {
    case chat::contacts::NodeProtocolType::Meshtastic:
        return "T";
    case chat::contacts::NodeProtocolType::MeshCore:
        return "C";
    default:
        return "?";
    }
}

const char* protocolLabel(chat::MeshProtocol protocol)
{
    return protocol == chat::MeshProtocol::MeshCore ? "MeshCore" : "Meshtastic";
}

bool encryptEnabled(const app::AppConfig& config);

const char* meshtasticRegionLabel(const chat::MeshConfig& config)
{
    const auto* region = chat::meshtastic::findRegion(
        static_cast<meshtastic_Config_LoRaConfig_RegionCode>(config.region));
    return region ? region->label : "-";
}

const char* meshCoreRegionLabel(const chat::MeshConfig& config)
{
    const auto* preset = chat::meshcore::findRegionPresetById(config.meshcore_region_preset);
    return preset ? preset->title : "Custom";
}

const char* radioRegionLabel(const app::AppConfig& cfg)
{
    return cfg.mesh_protocol == chat::MeshProtocol::Meshtastic
               ? meshtasticRegionLabel(cfg.meshtastic_config)
               : meshCoreRegionLabel(cfg.meshcore_config);
}

void applyMeshCoreRegionPreset(chat::MeshConfig& config, const chat::meshcore::RegionPreset& preset)
{
    config.meshcore_region_preset = preset.id;
    config.meshcore_freq_mhz = preset.freq_mhz;
    config.meshcore_bw_khz = preset.bw_khz;
    config.meshcore_sf = preset.sf;
    config.meshcore_cr = preset.cr;
    config.tx_power = preset.tx_power_dbm;
}

void adjustMeshCoreRegionPreset(chat::MeshConfig& config, int delta)
{
    size_t count = 0;
    const auto* table = chat::meshcore::getRegionPresetTable(&count);
    if (!table || count == 0)
    {
        return;
    }

    int index = -1;
    for (size_t i = 0; i < count; ++i)
    {
        if (table[i].id == config.meshcore_region_preset)
        {
            index = static_cast<int>(i);
            break;
        }
    }

    if (index < 0 && delta <= 0)
    {
        config.meshcore_region_preset = 0;
        return;
    }

    const int min_index = -1;
    const int max_index = static_cast<int>(count) - 1;
    index = clampValue(index + delta, min_index, max_index);
    if (index < 0)
    {
        config.meshcore_region_preset = 0;
        return;
    }

    applyMeshCoreRegionPreset(config, table[index]);
}

void formatBandwidth(char* out, size_t out_len, float bw_khz)
{
    if (!out || out_len == 0)
    {
        return;
    }

    const float rounded = std::round(bw_khz);
    if (std::fabs(bw_khz - rounded) < 0.01f)
    {
        std::snprintf(out, out_len, "%.0fk", rounded);
    }
    else if (std::fabs((bw_khz * 10.0f) - std::round(bw_khz * 10.0f)) < 0.01f)
    {
        std::snprintf(out, out_len, "%.1fk", bw_khz);
    }
    else
    {
        std::snprintf(out, out_len, "%.2fk", bw_khz);
    }
}

void formatFrequency(char* out, size_t out_len, float freq_mhz)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%.3f", static_cast<double>(freq_mhz));
}

void formatOffset(char* out, size_t out_len, float offset_mhz)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%+.3f", static_cast<double>(offset_mhz));
}

bool textEqualsIgnoreCase(const char* lhs, const char* rhs)
{
    if (!lhs || !rhs)
    {
        return false;
    }
    while (*lhs && *rhs)
    {
        if (upperAscii(*lhs) != upperAscii(*rhs))
        {
            return false;
        }
        ++lhs;
        ++rhs;
    }
    return *lhs == '\0' && *rhs == '\0';
}

void compactKeyText(const char* input, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!input)
    {
        return;
    }
    size_t pos = 0;
    for (size_t i = 0; input[i] != '\0' && pos + 1 < out_len; ++i)
    {
        const unsigned char ch = static_cast<unsigned char>(input[i]);
        if (std::isspace(ch) != 0)
        {
            continue;
        }
        out[pos++] = static_cast<char>(ch);
    }
    out[pos] = '\0';
}

int hexValue(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f')
    {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F')
    {
        return ch - 'A' + 10;
    }
    return -1;
}

bool decodeHexBytes(const char* text, uint8_t* out, size_t out_capacity, size_t* out_len)
{
    if (out_len)
    {
        *out_len = 0;
    }
    if (!text || !out || out_capacity == 0)
    {
        return false;
    }

    char digits[chat::kMeshtasticChannelKeyMaxLen * 2 + 1] = {};
    size_t digit_count = 0;
    for (size_t i = 0; text[i] != '\0'; ++i)
    {
        const char ch = text[i];
        if (hexValue(ch) >= 0)
        {
            if (digit_count + 1 >= sizeof(digits))
            {
                return false;
            }
            digits[digit_count++] = ch;
            continue;
        }
        if (ch == ':' || ch == '-' || ch == '_')
        {
            continue;
        }
        return false;
    }
    if (digit_count == 0 || (digit_count % 2U) != 0U)
    {
        return false;
    }

    const size_t byte_count = digit_count / 2U;
    if (byte_count > out_capacity)
    {
        return false;
    }
    for (size_t i = 0; i < byte_count; ++i)
    {
        const int hi = hexValue(digits[i * 2U]);
        const int lo = hexValue(digits[i * 2U + 1U]);
        if (hi < 0 || lo < 0)
        {
            return false;
        }
        out[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    if (out_len)
    {
        *out_len = byte_count;
    }
    return true;
}

int base64Value(char ch)
{
    if (ch >= 'A' && ch <= 'Z')
    {
        return ch - 'A';
    }
    if (ch >= 'a' && ch <= 'z')
    {
        return ch - 'a' + 26;
    }
    if (ch >= '0' && ch <= '9')
    {
        return ch - '0' + 52;
    }
    if (ch == '+')
    {
        return 62;
    }
    if (ch == '/')
    {
        return 63;
    }
    return -1;
}

bool decodeBase64Bytes(const char* text, uint8_t* out, size_t out_capacity, size_t* out_len)
{
    if (out_len)
    {
        *out_len = 0;
    }
    if (!text || !out)
    {
        return false;
    }

    size_t out_pos = 0;
    int quartet[4] = {};
    size_t quartet_len = 0;
    bool saw_padding = false;
    for (size_t i = 0;; ++i)
    {
        const char ch = text[i];
        if (ch == '\0' || std::isspace(static_cast<unsigned char>(ch)) != 0)
        {
            if (ch == '\0')
            {
                break;
            }
            continue;
        }

        int value = 0;
        if (ch == '=')
        {
            value = -2;
            saw_padding = true;
        }
        else
        {
            if (saw_padding)
            {
                return false;
            }
            value = base64Value(ch);
            if (value < 0)
            {
                return false;
            }
        }
        quartet[quartet_len++] = value;
        if (quartet_len < 4)
        {
            continue;
        }

        if (quartet[0] < 0 || quartet[1] < 0)
        {
            return false;
        }
        const uint32_t packed = (static_cast<uint32_t>(quartet[0]) << 18) |
                                (static_cast<uint32_t>(quartet[1]) << 12) |
                                (static_cast<uint32_t>(quartet[2] < 0 ? 0 : quartet[2]) << 6) |
                                static_cast<uint32_t>(quartet[3] < 0 ? 0 : quartet[3]);
        const size_t emit_count = quartet[2] < 0 ? 1U : (quartet[3] < 0 ? 2U : 3U);
        if (out_pos + emit_count > out_capacity)
        {
            return false;
        }
        out[out_pos++] = static_cast<uint8_t>((packed >> 16) & 0xFFU);
        if (emit_count >= 2U)
        {
            out[out_pos++] = static_cast<uint8_t>((packed >> 8) & 0xFFU);
        }
        if (emit_count >= 3U)
        {
            out[out_pos++] = static_cast<uint8_t>(packed & 0xFFU);
        }
        quartet_len = 0;
    }

    if (quartet_len != 0)
    {
        return false;
    }
    if (out_len)
    {
        *out_len = out_pos;
    }
    return out_pos > 0;
}

void encodeBase64Bytes(const uint8_t* data, size_t len, char* out, size_t out_len)
{
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
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
    for (size_t i = 0; i < len && pos + 4 < out_len; i += 3U)
    {
        const size_t remain = std::min<size_t>(3U, len - i);
        const uint32_t packed = (static_cast<uint32_t>(data[i]) << 16) |
                                (remain > 1U ? (static_cast<uint32_t>(data[i + 1U]) << 8) : 0U) |
                                (remain > 2U ? static_cast<uint32_t>(data[i + 2U]) : 0U);
        out[pos++] = kAlphabet[(packed >> 18) & 0x3FU];
        out[pos++] = kAlphabet[(packed >> 12) & 0x3FU];
        out[pos++] = remain > 1U ? kAlphabet[(packed >> 6) & 0x3FU] : '=';
        out[pos++] = remain > 2U ? kAlphabet[packed & 0x3FU] : '=';
    }
    out[pos] = '\0';
}

bool meshtasticDefaultPsk(uint8_t* out, size_t out_capacity, size_t* out_len)
{
    if (out_len)
    {
        *out_len = 0;
    }
    if (!out || out_capacity < chat::kMeshtasticChannelKeyDefaultLen)
    {
        return false;
    }
    size_t expanded_len = 0;
    chat::meshtastic::expandShortPsk(1U, out, &expanded_len);
    if (expanded_len == 0 || expanded_len > out_capacity)
    {
        return false;
    }
    if (out_len)
    {
        *out_len = expanded_len;
    }
    return true;
}

bool isMeshtasticDefaultPsk(const uint8_t* key, size_t len)
{
    uint8_t default_key[chat::kMeshtasticChannelKeyDefaultLen] = {};
    size_t default_len = 0;
    return meshtasticDefaultPsk(default_key, sizeof(default_key), &default_len) &&
           len == default_len &&
           std::memcmp(key, default_key, default_len) == 0;
}

uint8_t storedMeshtasticKeyLen(const chat::MeshConfig& mesh)
{
    return chat::normalizeMeshtasticChannelKeyLen(mesh.primary_key,
                                                  sizeof(mesh.primary_key),
                                                  mesh.primary_key_len);
}

void formatMeshtasticPrimaryKeySummary(const chat::MeshConfig& mesh, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    const uint8_t key_len = storedMeshtasticKeyLen(mesh);
    if (key_len == 0 || isMeshtasticDefaultPsk(mesh.primary_key, key_len))
    {
        std::snprintf(out, out_len, "AQ==");
        return;
    }
    std::snprintf(out, out_len, "B64 %uB", static_cast<unsigned>(key_len));
}

void formatMeshtasticPrimaryKeyForEdit(const chat::MeshConfig& mesh, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    const uint8_t key_len = storedMeshtasticKeyLen(mesh);
    if (key_len == 0 || isMeshtasticDefaultPsk(mesh.primary_key, key_len))
    {
        std::snprintf(out, out_len, "AQ==");
        return;
    }
    encodeBase64Bytes(mesh.primary_key, key_len, out, out_len);
}

void formatMeshCoreChannelKeySummary(const chat::MeshConfig& mesh, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    if (chat::isAllZeroKeyBytes(mesh.secondary_key, chat::kMeshCoreChannelKeyLen))
    {
        std::snprintf(out, out_len, "PUBLIC");
        return;
    }
    std::snprintf(out, out_len, "B64 16B");
}

void formatMeshCoreChannelKeyForEdit(const chat::MeshConfig& mesh, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    if (chat::isAllZeroKeyBytes(mesh.secondary_key, chat::kMeshCoreChannelKeyLen))
    {
        out[0] = '\0';
        return;
    }
    encodeBase64Bytes(mesh.secondary_key, chat::kMeshCoreChannelKeyLen, out, out_len);
}

bool applyMeshtasticPrimaryKeyText(chat::MeshConfig& mesh, const char* input)
{
    char compact[kKeyTextMax] = {};
    compactKeyText(input, compact, sizeof(compact));

    std::memset(mesh.primary_key, 0, sizeof(mesh.primary_key));
    mesh.primary_key_len = 0;
    if (compact[0] == '\0' ||
        textEqualsIgnoreCase(compact, "DEFAULT") ||
        textEqualsIgnoreCase(compact, "PUBLIC"))
    {
        return true;
    }

    uint8_t decoded[chat::kMeshtasticChannelKeyMaxLen] = {};
    size_t decoded_len = 0;
    if (decodeHexBytes(compact, decoded, sizeof(decoded), &decoded_len))
    {
        if (decoded_len != chat::kMeshtasticChannelKeyDefaultLen &&
            decoded_len != chat::kMeshtasticChannelKeyMaxLen)
        {
            return false;
        }
        std::memcpy(mesh.primary_key, decoded, decoded_len);
        mesh.primary_key_len = static_cast<uint8_t>(decoded_len);
        return true;
    }

    if (!decodeBase64Bytes(compact, decoded, sizeof(decoded), &decoded_len))
    {
        return false;
    }
    if (decoded_len == 1U)
    {
        size_t expanded_len = 0;
        if (!meshtasticDefaultPsk(mesh.primary_key, sizeof(mesh.primary_key), &expanded_len))
        {
            return false;
        }
        if (decoded[0] != 1U)
        {
            chat::meshtastic::expandShortPsk(decoded[0], mesh.primary_key, &expanded_len);
        }
        mesh.primary_key_len = static_cast<uint8_t>(expanded_len);
        return expanded_len == chat::kMeshtasticChannelKeyDefaultLen;
    }
    if (decoded_len != chat::kMeshtasticChannelKeyDefaultLen &&
        decoded_len != chat::kMeshtasticChannelKeyMaxLen)
    {
        return false;
    }
    std::memcpy(mesh.primary_key, decoded, decoded_len);
    mesh.primary_key_len = static_cast<uint8_t>(decoded_len);
    return true;
}

bool applyMeshCoreChannelKeyText(chat::MeshConfig& mesh, const char* input)
{
    char compact[kKeyTextMax] = {};
    compactKeyText(input, compact, sizeof(compact));

    std::memset(mesh.secondary_key, 0, sizeof(mesh.secondary_key));
    mesh.secondary_key_len = 0;
    if (compact[0] == '\0' ||
        textEqualsIgnoreCase(compact, "PUBLIC") ||
        textEqualsIgnoreCase(compact, "NONE"))
    {
        return true;
    }

    uint8_t decoded[chat::kMeshtasticChannelKeyMaxLen] = {};
    size_t decoded_len = 0;
    if (!decodeHexBytes(compact, decoded, sizeof(decoded), &decoded_len) &&
        !decodeBase64Bytes(compact, decoded, sizeof(decoded), &decoded_len))
    {
        return false;
    }
    if (decoded_len != chat::kMeshCoreChannelKeyLen)
    {
        return false;
    }
    std::memcpy(mesh.secondary_key, decoded, chat::kMeshCoreChannelKeyLen);
    mesh.secondary_key_len = static_cast<uint8_t>(chat::kMeshCoreChannelKeyLen);
    return true;
}

void formatRadioSettingValue(const app::AppConfig& cfg, RadioSettingItem item, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';

    const auto& mt = cfg.meshtastic_config;
    const auto& mc = cfg.meshcore_config;
    switch (item)
    {
    case RadioSettingItem::Protocol:
        std::snprintf(out, out_len, "%s", protocolLabel(cfg.mesh_protocol));
        return;
    case RadioSettingItem::MtRegion:
        std::snprintf(out, out_len, "%s", meshtasticRegionLabel(mt));
        return;
    case RadioSettingItem::MtMode:
        std::snprintf(out, out_len, "%s", mt.use_preset ? "PRESET" : "MANUAL");
        return;
    case RadioSettingItem::MtPreset:
        std::snprintf(out, out_len, "%s",
                      chat::meshtastic::presetDisplayName(
                          static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(mt.modem_preset)));
        return;
    case RadioSettingItem::MtBandwidth:
        formatBandwidth(out, out_len, mt.bandwidth_khz);
        return;
    case RadioSettingItem::MtSpreadFactor:
        std::snprintf(out, out_len, "SF%u", static_cast<unsigned>(mt.spread_factor));
        return;
    case RadioSettingItem::MtCodingRate:
        std::snprintf(out, out_len, "4/%u", static_cast<unsigned>(mt.coding_rate));
        return;
    case RadioSettingItem::MtTxPower:
        std::snprintf(out, out_len, "%ddBm", static_cast<int>(mt.tx_power));
        return;
    case RadioSettingItem::MtChannelSlot:
        formatMeshtasticChannelSlot(out, out_len, mt.channel_num);
        return;
    case RadioSettingItem::MtPrimaryKey:
        formatMeshtasticPrimaryKeySummary(mt, out, out_len);
        return;
    case RadioSettingItem::MtOverrideFrequency:
        if (mt.override_frequency_mhz <= 0.0f)
        {
            std::snprintf(out, out_len, "AUTO");
        }
        else
        {
            formatFrequency(out, out_len, mt.override_frequency_mhz);
        }
        return;
    case RadioSettingItem::MtFrequencyOffset:
        formatOffset(out, out_len, mt.frequency_offset_mhz);
        return;
    case RadioSettingItem::Encrypt:
        std::snprintf(out, out_len, "%s", encryptEnabled(cfg) ? "ON" : "OFF");
        return;
    case RadioSettingItem::McRegion:
        std::snprintf(out, out_len, "%s", meshCoreRegionLabel(mc));
        return;
    case RadioSettingItem::McFrequency:
        formatFrequency(out, out_len, mc.meshcore_freq_mhz);
        return;
    case RadioSettingItem::McBandwidth:
        formatBandwidth(out, out_len, mc.meshcore_bw_khz);
        return;
    case RadioSettingItem::McSpreadFactor:
        std::snprintf(out, out_len, "SF%u", static_cast<unsigned>(mc.meshcore_sf));
        return;
    case RadioSettingItem::McCodingRate:
        std::snprintf(out, out_len, "4/%u", static_cast<unsigned>(mc.meshcore_cr));
        return;
    case RadioSettingItem::McTxPower:
        std::snprintf(out, out_len, "%ddBm", static_cast<int>(mc.tx_power));
        return;
    case RadioSettingItem::McChannelSlot:
        std::snprintf(out, out_len, "%u", static_cast<unsigned>(mc.meshcore_channel_slot));
        return;
    case RadioSettingItem::McChannelName:
        std::snprintf(out, out_len, "%s", mc.meshcore_channel_name);
        return;
    case RadioSettingItem::McChannelKey:
        formatMeshCoreChannelKeySummary(mc, out, out_len);
        return;
    }
}

const char* gpsSatMaskLabel(uint8_t mask)
{
    switch (mask)
    {
    case 0x1:
        return "GPS";
    case 0x1 | 0x8:
        return "GPS+BDS";
    case 0x1 | 0x4:
        return "GPS+GAL";
    case 0x1 | 0x8 | 0x4:
        return "GPS+BDS+GAL";
    case 0x1 | 0x8 | 0x4 | 0x2:
        return "GPS+BDS+GAL+GLO";
    default:
        return "CUSTOM";
    }
}

uint8_t nextGpsSatMask(uint8_t current, int delta)
{
    static constexpr uint8_t kGpsMasks[] = {
        0x1 | 0x8 | 0x4,
        0x1,
        0x1 | 0x8,
        0x1 | 0x4,
        0x1 | 0x8 | 0x4 | 0x2,
    };

    int index = 0;
    for (size_t i = 0; i < arrayCount(kGpsMasks); ++i)
    {
        if (kGpsMasks[i] == current)
        {
            index = static_cast<int>(i);
            break;
        }
    }
    index = clampValue(index + delta, 0, static_cast<int>(arrayCount(kGpsMasks)) - 1);
    return kGpsMasks[index];
}

void formatTimezoneLabel(int offset_min, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }

    const int hours = offset_min / 60;
    const int minutes = std::abs(offset_min % 60);
    if (minutes == 0)
    {
        std::snprintf(out, out_len, "UTC%+d", hours);
    }
    else
    {
        std::snprintf(out, out_len, "UTC%+d:%02d", hours, minutes);
    }
}

time_t applyLocalTimezone(const HostCallbacks& host, time_t utc_seconds)
{
    if (host.apply_timezone_offset_fn)
    {
        return host.apply_timezone_offset_fn(utc_seconds);
    }
    const int tz_offset_s = (host.timezone_offset_min_fn ? host.timezone_offset_min_fn() : 0) * 60;
    return utc_seconds + static_cast<time_t>(tz_offset_s);
}

void formatUptimeLabel(uint32_t uptime_seconds, char* out, size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }

    const uint32_t days = uptime_seconds / 86400U;
    const uint32_t hours = (uptime_seconds / 3600U) % 24U;
    const uint32_t minutes = (uptime_seconds / 60U) % 60U;
    std::snprintf(out,
                  out_len,
                  "%lu d %lu h %lu m",
                  static_cast<unsigned long>(days),
                  static_cast<unsigned long>(hours),
                  static_cast<unsigned long>(minutes));
}

void appendStatus(Runtime* runtime, const char* fmt, ...)
{
    if (!runtime || !fmt)
    {
        return;
    }

    char line[40] = {};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    runtime->appendBootLog(line);
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

#if !TRAILMATE_NRF52_BLE_DISABLED
bool loadBlePairingStatus(app::IAppFacade* app, ble::BlePairingStatus* out)
{
    if (!app || !out)
    {
        return false;
    }
    ble::BleManager* manager = app->getBleManager();
    return manager ? manager->getPairingStatus(out) : false;
}

enum class BleDisplayState
{
    Off,
    On,
    Link,
};

BleDisplayState resolveBleDisplayState(app::IAppFacade* app)
{
    if (!app)
    {
        return BleDisplayState::Off;
    }

    ble::BleManager* manager = app->getBleManager();
    if (!manager || !manager->isEnabled())
    {
        return BleDisplayState::Off;
    }

    ble::BlePairingStatus status{};
    if (manager->getPairingStatus(&status) && status.is_connected)
    {
        return BleDisplayState::Link;
    }

    return BleDisplayState::On;
}

const char* bleDisplayStateLabel(BleDisplayState state)
{
    switch (state)
    {
    case BleDisplayState::Link:
        return "LINK";
    case BleDisplayState::On:
        return "ON";
    case BleDisplayState::Off:
    default:
        return "OFF";
    }
}

void formatBleStateLabel(char* out, size_t out_len, app::IAppFacade* app)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "BLE %s", bleDisplayStateLabel(resolveBleDisplayState(app)));
}

const char* blePairingModeLabel(const ble::BlePairingStatus& status)
{
    if (!status.requires_passkey)
    {
        return "OPEN";
    }
    return status.is_fixed_pin ? "FIXED" : "RANDOM";
}
#endif

} // namespace

Runtime::Runtime(MonoDisplay& display, const HostCallbacks& host)
    : display_(display),
      text_renderer_(host.ui_font ? *host.ui_font : builtin_ui_font()),
      accent_text_renderer_(host.accent_font ? *host.accent_font
                                             : (host.ui_font ? *host.ui_font : builtin_ui_font())),
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
    last_interaction_ms_ = boot_started_ms_;
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

    expireTransientPopup();
    ensureBootExit();
    const chat::runtime::RuntimeContext protocol_context = buildMeshtasticProtocolContext();
    chat::runtime::FixedProtocolRuntimeContextProvider context_provider(protocol_context);
    chat::runtime::ProtocolRuntimeSelection runtime_selection{};
    runtime_selection.meshtastic = &meshtastic_protocol_runtime_;
    if (auto* mesh = app() ? app()->getMeshAdapter() : nullptr)
    {
        chat::runtime::MeshAdapterProtocolEffectExecutor executor(*mesh);
        const auto bundle = chat::runtime::protocolRuntimeFor(chat::MeshProtocol::Meshtastic,
                                                              runtime_selection,
                                                              executor,
                                                              context_provider);
        if (bundle.valid())
        {
            auto facade = bundle.createFacade();
            handleMeshtasticFacadeResult(facade.tick());
        }
    }
    else
    {
        NoopProtocolEffectExecutor executor;
        const auto bundle = chat::runtime::protocolRuntimeFor(chat::MeshProtocol::Meshtastic,
                                                              runtime_selection,
                                                              executor,
                                                              context_provider);
        if (bundle.valid())
        {
            auto facade = bundle.createFacade();
            handleMeshtasticFacadeResult(facade.tick());
        }
    }
    ensureSleepTimeout(action);
    handleInput(action);
    if (shouldRenderForTick(action))
    {
        render();
    }
}

void Runtime::typeText(char ch)
{
    if (ch == '\0' || !begin())
    {
        return;
    }

    expireTransientPopup();
    ensureBootExit();
    ensureSleepTimeout(InputAction::Primary);
    if (page_ == Page::Sleep)
    {
        last_interaction_ms_ = nowMs();
        enterPage(page_before_sleep_);
    }
    if (page_ != Page::Compose)
    {
        return;
    }

    last_interaction_ms_ = nowMs();
    if (usesPhysicalTextInput() && handlePhysicalComposeText(ch))
    {
        render();
        return;
    }

    if (ch == '\b')
    {
        removeComposeChar();
    }
    else if (static_cast<unsigned char>(ch) >= 32U && static_cast<unsigned char>(ch) < 127U)
    {
        appendComposeChar(ch);
    }
    render();
}

void Runtime::bindChatObservers()
{
    if (chat_observers_bound_ || !app())
    {
        return;
    }

    app()->getChatService().addIncomingTextObserver(this);
    app()->getChatService().addIncomingDataObserver(this);
    chat_observers_bound_ = true;
}

void Runtime::showNotice(const char* title, const char* message, uint32_t duration_ms)
{
    if (!begin())
    {
        return;
    }
    showTransientPopup(title, message, duration_ms);
    render();
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
    if (host_.play_message_tone_fn)
    {
        host_.play_message_tone_fn();
    }
    if (host_.play_message_light_fn)
    {
        host_.play_message_light_fn();
    }
}

void Runtime::onIncomingData(const chat::MeshIncomingData& msg)
{
    chat::runtime::IncomingPacket packet{};
    packet.protocol = chat::MeshProtocol::Meshtastic;
    packet.channel = msg.channel;
    packet.from = msg.from;
    packet.to = msg.to;
    packet.packet_id = msg.packet_id;
    packet.request_id = msg.request_id;
    packet.portnum = msg.portnum;
    packet.want_response = msg.want_response;
    packet.payload = msg.payload;
    packet.rx_meta = msg.rx_meta;

    const chat::runtime::RuntimeContext protocol_context = buildMeshtasticProtocolContext();
    chat::runtime::FixedProtocolRuntimeContextProvider context_provider(protocol_context);
    chat::runtime::ProtocolRuntimeSelection runtime_selection{};
    runtime_selection.meshtastic = &meshtastic_protocol_runtime_;
    if (auto* mesh = app() ? app()->getMeshAdapter() : nullptr)
    {
        chat::runtime::MeshAdapterProtocolEffectExecutor executor(*mesh);
        const auto bundle = chat::runtime::protocolRuntimeFor(chat::MeshProtocol::Meshtastic,
                                                              runtime_selection,
                                                              executor,
                                                              context_provider);
        if (bundle.valid())
        {
            auto facade = bundle.createFacade();
            handleMeshtasticFacadeResult(facade.handleIncoming(packet));
        }
    }
    else
    {
        NoopProtocolEffectExecutor executor;
        const auto bundle = chat::runtime::protocolRuntimeFor(chat::MeshProtocol::Meshtastic,
                                                              runtime_selection,
                                                              executor,
                                                              context_provider);
        if (bundle.valid())
        {
            auto facade = bundle.createFacade();
            handleMeshtasticFacadeResult(facade.handleIncoming(packet));
        }
    }
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
                      "[gat562][ui] compose action=%s focus=%u mode=%u group=%u tap=%u preedit=\"%s\" t9=\"%s\" t9_py=\"%s\" cand=%u body_len=%u\n",
                      inputActionName(action),
                      static_cast<unsigned>(compose_focus_),
                      static_cast<unsigned>(compose_mode_),
                      static_cast<unsigned>(compose_abc_group_index_),
                      static_cast<unsigned>(compose_abc_tap_index_),
                      preedit,
                      compose_t9_digits_,
                      compose_t9_selected_pinyin_,
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

    if (page_ == Page::Sleep)
    {
        if (action != InputAction::None)
        {
            last_interaction_ms_ = nowMs();
            enterPage(page_before_sleep_);
        }
        return;
    }

    if (action != InputAction::None)
    {
        last_interaction_ms_ = nowMs();
    }

    if (setting_popup_active_)
    {
        if (action == InputAction::Back)
        {
            cancelSettingPopup();
        }
        else if (action == InputAction::Select || action == InputAction::Primary)
        {
            confirmSettingPopup();
        }
        else if (action == InputAction::Up || action == InputAction::Right)
        {
            adjustSettingPopup(1);
        }
        else if (action == InputAction::Down || action == InputAction::Left)
        {
            adjustSettingPopup(-1);
        }
        return;
    }

    switch (page_)
    {
    case Page::MainMenu:
    {
        const size_t item_count = mainMenuItemCount();
        if (item_count == 0)
        {
            break;
        }
        if (main_menu_index_ >= item_count)
        {
            main_menu_index_ = item_count - 1U;
        }
        if (action == InputAction::Up && main_menu_index_ > 0)
        {
            --main_menu_index_;
        }
        else if (action == InputAction::Down && main_menu_index_ + 1 < item_count)
        {
            ++main_menu_index_;
        }
        else if (action == InputAction::Left || action == InputAction::Back)
        {
            enterPage(Page::Screensaver);
        }
        else if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            enterPage(mainMenuPageForIndex(main_menu_index_));
        }
        break;
    }

    case Page::ChatList:
    {
        const size_t item_count = conversation_count_ + 1U;
        if (chat_list_index_ >= item_count)
        {
            chat_list_index_ = item_count - 1U;
        }
        if (action == InputAction::Up && chat_list_index_ > 0)
        {
            --chat_list_index_;
        }
        else if (action == InputAction::Down && chat_list_index_ + 1 < item_count)
        {
            ++chat_list_index_;
        }
        else if (action == InputAction::Left || action == InputAction::Back)
        {
            enterPage(Page::MainMenu);
        }
        else if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            if (chat_list_index_ == 0)
            {
                enterPage(Page::NewChatPage);
            }
            else if ((chat_list_index_ - 1U) < conversation_count_)
            {
                active_conversation_ = conversations_[chat_list_index_ - 1U].id;
                enterPage(Page::Conversation);
            }
        }
        break;
    }

    case Page::NewChatPage:
        if (action == InputAction::Up && new_chat_index_ > 0)
        {
            --new_chat_index_;
        }
        else if (action == InputAction::Down && new_chat_index_ + 1 < arrayCount(kNewChatItems))
        {
            ++new_chat_index_;
        }
        else if (action == InputAction::Left || action == InputAction::Back)
        {
            enterPage(Page::ChatList);
        }
        else if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            executeNewChatPageItem(new_chat_index_);
        }
        break;

    case Page::NodeList:
        if (action == InputAction::Up && node_list_index_ > 0)
        {
            --node_list_index_;
        }
        else if (action == InputAction::Down && node_list_index_ + 1 < node_count_)
        {
            ++node_list_index_;
        }
        else if (action == InputAction::Left || action == InputAction::Back)
        {
            enterPage(Page::MainMenu);
        }
        else if (action == InputAction::Right)
        {
            enterPage(Page::NodeActionMenu);
        }
        else if (action == InputAction::Select || action == InputAction::Primary)
        {
            enterPage(host_.physical_text_input ? Page::NodeActionMenu : Page::NodeInfo);
        }
        break;

    case Page::NodeActionMenu:
        if (action == InputAction::Up && node_action_index_ > 0)
        {
            --node_action_index_;
        }
        else if (action == InputAction::Down && node_action_index_ + 1 < nodeActionCount())
        {
            ++node_action_index_;
        }
        else if (action == InputAction::Left || action == InputAction::Back)
        {
            enterPage(Page::NodeList);
        }
        else if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            executeNodeAction();
        }
        break;

    case Page::NodeInfo:
    {
        const size_t page_size = visibleRowsFrom(10);
        if (action == InputAction::Up && node_info_scroll_ > 0)
        {
            node_info_scroll_ = (node_info_scroll_ >= page_size)
                                    ? (node_info_scroll_ - page_size)
                                    : 0U;
        }
        else if (action == InputAction::Down && (node_info_scroll_ + page_size) < node_info_count_)
        {
            node_info_scroll_ += page_size;
        }
        else if (action == InputAction::Left || action == InputAction::Back ||
                 action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            enterPage(host_.physical_text_input ? Page::NodeActionMenu : Page::NodeList);
        }
        break;
    }

    case Page::NodeCompass:
        if (action == InputAction::Left || action == InputAction::Back ||
            action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            enterPage(Page::NodeActionMenu);
        }
        break;

    case Page::Conversation:
        if (action == InputAction::Up && message_index_ > 0)
        {
            --message_index_;
            message_focus_started_ms_ = nowMs();
        }
        else if (action == InputAction::Down && message_index_ + 1 < message_count_)
        {
            ++message_index_;
            message_focus_started_ms_ = nowMs();
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
            else if (message_menu_index_ == 1)
            {
                if (!app() || !app()->getChatService().canSendToConversation(active_conversation_))
                {
                    showTransientPopup("MESSAGE", "VIEW ONLY", 2000U);
                    break;
                }
                openCompose(EditTarget::Message);
            }
            else
            {
                retrySelectedMessage();
            }
        }
        break;

    case Page::MessageInfo:
    {
        const size_t page_size = visibleRowsFrom(10);
        if (action == InputAction::Up && message_info_scroll_ > 0)
        {
            message_info_scroll_ = (message_info_scroll_ >= page_size)
                                       ? (message_info_scroll_ - page_size)
                                       : 0U;
        }
        else if (action == InputAction::Down && (message_info_scroll_ + page_size) < message_info_count_)
        {
            message_info_scroll_ += page_size;
        }
        else if (action == InputAction::Left || action == InputAction::Back ||
                 action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            enterPage(Page::MessageMenu);
        }
        break;
    }

    case Page::Compose:
        if (usesPhysicalTextInput())
        {
            if (action == InputAction::Up && compose_mode_ == ComposeMode::Cn && hasPhysicalChinesePreedit())
            {
                if (compose_t9_selected_pinyin_[0] != '\0')
                {
                    adjustComposeCandidate(-1);
                }
                else
                {
                    adjustComposeT9PinyinCandidate(-1);
                }
            }
            else if (action == InputAction::Down && compose_mode_ == ComposeMode::Cn && hasPhysicalChinesePreedit())
            {
                if (compose_t9_selected_pinyin_[0] != '\0')
                {
                    adjustComposeCandidate(1);
                }
                else
                {
                    adjustComposeT9PinyinCandidate(1);
                }
            }
            else if (action == InputAction::Back || action == InputAction::Left)
            {
                if (hasPhysicalChinesePreedit() || compose_preedit_len_ > 0 || compose_len_ > 0)
                {
                    removeComposeChar();
                }
                else
                {
                    finishTextEdit(false);
                }
            }
            else if (action == InputAction::Secondary)
            {
                (void)commitPhysicalComposeToText(true);
                appendComposeChar(' ');
            }
            else if (action == InputAction::Select || action == InputAction::Primary)
            {
                if (commitPhysicalComposePreedit(true))
                {
                    compose_focus_ = ComposeFocus::Body;
                }
                else if (edit_target_ == EditTarget::Message)
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
            const char* keyset = composeKeysetForMode(compose_mode_);
            size_t key_count = std::strlen(keyset);
            if (usesFullVirtualKeyboard())
            {
                key_count = fullKeyboardKeyCount(fullKeyboardRowsForMode(compose_mode_));
            }
            if (key_count == 0)
            {
                compose_charset_index_ = 0;
            }
            else if (usesFullVirtualKeyboard())
            {
                compose_charset_index_ = std::min(compose_charset_index_, key_count - 1U);
                if (action == InputAction::Up)
                {
                    size_t next = compose_charset_index_;
                    if (fullKeyboardMoveVertical(compose_mode_, compose_charset_index_, -1, next))
                    {
                        compose_charset_index_ = next;
                    }
                    else if (compose_candidate_count_ > 0)
                    {
                        compose_focus_ = ComposeFocus::Candidate;
                    }
                }
                else if (action == InputAction::Down)
                {
                    size_t next = compose_charset_index_;
                    if (fullKeyboardMoveVertical(compose_mode_, compose_charset_index_, 1, next))
                    {
                        compose_charset_index_ = next;
                    }
                    else
                    {
                        compose_focus_ = ComposeFocus::Action;
                    }
                }
                else if (action == InputAction::Left)
                {
                    compose_charset_index_ = (compose_charset_index_ == 0) ? (key_count - 1U)
                                                                           : (compose_charset_index_ - 1U);
                }
                else if (action == InputAction::Right)
                {
                    compose_charset_index_ = (compose_charset_index_ + 1U) % key_count;
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
            else
            {
                const size_t page_start = (compose_charset_index_ / kVirtualKeyboardPageSize) * kVirtualKeyboardPageSize;
                const size_t page_end = std::min(key_count, page_start + kVirtualKeyboardPageSize);
                if (action == InputAction::Up)
                {
                    if (compose_charset_index_ >= page_start + kVirtualKeyboardCols)
                    {
                        compose_charset_index_ -= kVirtualKeyboardCols;
                    }
                    else if (compose_candidate_count_ > 0)
                    {
                        compose_focus_ = ComposeFocus::Candidate;
                    }
                }
                else if (action == InputAction::Down)
                {
                    const size_t next = compose_charset_index_ + kVirtualKeyboardCols;
                    if (next < page_end)
                    {
                        compose_charset_index_ = next;
                    }
                    else
                    {
                        compose_focus_ = ComposeFocus::Action;
                    }
                }
                else if (action == InputAction::Left)
                {
                    compose_charset_index_ = (compose_charset_index_ == page_start) ? (page_end - 1U)
                                                                                    : (compose_charset_index_ - 1U);
                }
                else if (action == InputAction::Right)
                {
                    compose_charset_index_ = (compose_charset_index_ + 1U >= page_end) ? page_start
                                                                                       : (compose_charset_index_ + 1U);
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

    case Page::DiscoverPage:
        if (action == InputAction::Up && discover_index_ > 0)
        {
            --discover_index_;
        }
        else if (action == InputAction::Down && discover_index_ + 1 < arrayCount(kDiscoverItems))
        {
            ++discover_index_;
        }
        else if (action == InputAction::Left || action == InputAction::Back)
        {
            enterPage(Page::MainMenu);
        }
        else if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            executeDiscoverPageItem(discover_index_);
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
            case 0:
                enterPage(Page::RadioSettings);
                break;
            case 1:
                enterPage(Page::DeviceSettings);
                break;
            case 2:
                enterPage(Page::ActionPage);
                break;
            default:
                break;
            }
        }
        break;

    case Page::InfoPage:
    {
        const size_t page_size = visibleRowsFrom(10);
        if (action == InputAction::Up && info_scroll_ > 0)
        {
            info_scroll_ = (info_scroll_ >= page_size) ? (info_scroll_ - page_size) : 0U;
        }
        else if (action == InputAction::Down)
        {
            info_scroll_ += page_size;
        }
        else if (action == InputAction::Left || action == InputAction::Back ||
                 action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            enterPage(Page::MainMenu);
        }
        break;
    }

    case Page::RadioSettings:
        if (action == InputAction::Up && radio_index_ > 0)
        {
            --radio_index_;
        }
        else if (action == InputAction::Down && radio_index_ + 1 < radioItemCount())
        {
            ++radio_index_;
        }
        else if (action == InputAction::Left || action == InputAction::Back)
        {
            enterPage(Page::SettingsMenu);
        }
        else if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            const RadioSettingItem item = radioSettingItem(radio_index_);
            if (item == RadioSettingItem::McChannelName)
            {
                openCompose(EditTarget::MeshCoreChannelName, app()->getConfig().meshcore_config.meshcore_channel_name);
            }
            else if (item == RadioSettingItem::MtPrimaryKey)
            {
                char key_text[kKeyTextMax] = {};
                formatMeshtasticPrimaryKeyForEdit(app()->getConfig().meshtastic_config,
                                                  key_text,
                                                  sizeof(key_text));
                openCompose(EditTarget::MeshtasticPrimaryKey, key_text);
            }
            else if (item == RadioSettingItem::McChannelKey)
            {
                char key_text[kKeyTextMax] = {};
                formatMeshCoreChannelKeyForEdit(app()->getConfig().meshcore_config,
                                                key_text,
                                                sizeof(key_text));
                openCompose(EditTarget::MeshCoreChannelKey, key_text);
            }
            else
            {
                beginSettingPopup(Page::RadioSettings, radio_index_);
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
        else if (action == InputAction::Left || action == InputAction::Back)
        {
            enterPage(Page::SettingsMenu);
        }
        else if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            beginSettingPopup(Page::DeviceSettings, device_index_);
        }
        break;

    case Page::GnssPage:
        if (action == InputAction::Up && gnss_page_index_ > 0)
        {
            --gnss_page_index_;
        }
        else if (action == InputAction::Down)
        {
            ++gnss_page_index_;
        }
        else if (action == InputAction::Left || action == InputAction::Back || action == InputAction::Select)
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
            enterPage(Page::SettingsMenu);
        }
        else if (action == InputAction::Right || action == InputAction::Select || action == InputAction::Primary)
        {
            if (actionNeedsConfirm(action_index_))
            {
                beginSettingPopup(Page::ActionPage, action_index_);
            }
            else
            {
                executeActionPageItem(action_index_);
            }
        }
        break;

    default:
        break;
    }
}

void Runtime::render()
{
    if (page_ == Page::Sleep && display_.powerSavesOnSleep())
    {
        display_.setPowerSave(true);
        return;
    }

    display_.setPowerSave(false);
    display_.clear();
    switch (page_)
    {
    case Page::BootLog:
        renderBootLog();
        break;
    case Page::Screensaver:
        renderScreensaver();
        break;
    case Page::Sleep:
        renderSleep();
        break;
    case Page::MainMenu:
        renderMainMenu();
        break;
    case Page::ChatList:
        renderChatList();
        break;
    case Page::NewChatPage:
        renderNewChatPage();
        break;
    case Page::NodeList:
        renderNodeList();
        break;
    case Page::NodeActionMenu:
        renderNodeActionMenu();
        break;
    case Page::NodeInfo:
        renderNodeInfo();
        break;
    case Page::NodeCompass:
        renderNodeCompass();
        break;
    case Page::Conversation:
        renderConversation();
        break;
    case Page::MessageMenu:
        renderMessageMenu();
        break;
    case Page::MessageInfo:
        renderMessageInfo();
        break;
    case Page::Compose:
        renderCompose();
        break;
    case Page::DiscoverPage:
        renderDiscoverPage();
        break;
    case Page::SettingsMenu:
        renderSettingsMenu();
        break;
    case Page::RadioSettings:
        renderRadioSettings();
        break;
    case Page::DeviceSettings:
        renderDeviceSettings();
        break;
    case Page::InfoPage:
        renderInfoPage();
        break;
    case Page::GnssPage:
        renderGnssPage();
        break;
    case Page::ActionPage:
        renderActionPage();
        break;
    default:
        renderScreensaver();
        break;
    }

    if (setting_popup_active_)
    {
        renderSettingPopup();
    }

    if (transient_popup_active_)
    {
        renderTransientPopup();
    }

#if !TRAILMATE_NRF52_BLE_DISABLED
    ble::BlePairingStatus ble_status{};
    if (loadBlePairingStatus(app(), &ble_status) &&
        ble_status.available &&
        ble_status.requires_passkey &&
        ble_status.is_pairing_active)
    {
        display_.fillRect(0, 45, display_.width(), 19, false);
        display_.drawHLine(0, 44, display_.width());
        char line1[24] = {};
        char line2[24] = {};
        std::snprintf(line1, sizeof(line1), "BLE %s PIN", ble_status.is_fixed_pin ? "FIXED" : "PAIR");
        std::snprintf(line2, sizeof(line2), "%06lu", static_cast<unsigned long>(ble_status.passkey));
        text_renderer_.drawText(display_, 0, 46, line1);
        text_renderer_.drawText(display_, 0, 54, line2);
    }
#endif

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

void Runtime::refreshGnssSnapshot(bool force)
{
    const uint32_t now = nowMs();
    const bool stale = !gnss_snapshot_valid_ ||
                       now < gnss_snapshot_updated_ms_ ||
                       (now - gnss_snapshot_updated_ms_) >= kGnssSnapshotRefreshMs;
    if (!force && !stale)
    {
        return;
    }

    gnss_snapshot_state_ = host_.gps_data_fn ? host_.gps_data_fn() : platform::ui::gps::GpsState{};
    gnss_snapshot_status_ = platform::ui::gps::GnssStatus{};
    gnss_snapshot_count_ = 0;
    gnss_snapshot_sats_.fill(platform::ui::gps::GnssSatInfo{});
    (void)platform::ui::gps::get_gnss_snapshot(gnss_snapshot_sats_.data(),
                                               gnss_snapshot_sats_.size(),
                                               &gnss_snapshot_count_,
                                               &gnss_snapshot_status_);
    gnss_snapshot_updated_ms_ = now;
    gnss_snapshot_valid_ = true;
}

void Runtime::renderScreensaver()
{
    char protocol[8] = {};
    char freq[20] = {};
    char ram_buf[24] = {};
    char time_buf[16] = {};
    char time_main_buf[8] = {};
    char time_sec_buf[4] = {};
    char date_buf[24] = {};
    char status_buf[16] = {};
    char node_buf[12] = {};
    char tz_buf[16] = {};
    char unread_buf[8] = {};
    char bat_pct_buf[8] = {};
    char sat_buf[16] = {};
    char left_toggle_buf[12] = {};
    char left_ble_buf[12] = {};
    formatProtocol(protocol, sizeof(protocol));
    formatNodeLabel(node_buf, sizeof(node_buf));
    formatTime(time_buf, sizeof(time_buf), date_buf, sizeof(date_buf));
    splitClockText(time_buf, time_main_buf, sizeof(time_main_buf), time_sec_buf, sizeof(time_sec_buf));
    formatTimezoneLabel(host_.timezone_offset_min_fn ? host_.timezone_offset_min_fn() : 0, tz_buf, sizeof(tz_buf));
    if (host_.format_frequency_fn)
    {
        host_.format_frequency_fn(host_.active_lora_frequency_hz_fn ? host_.active_lora_frequency_hz_fn() : 0U,
                                  freq,
                                  sizeof(freq));
    }

    refreshGnssSnapshot();
    const auto battery = host_.battery_info_fn ? host_.battery_info_fn() : platform::ui::device::BatteryInfo{};
    const auto ram = host_.ram_usage_fn ? host_.ram_usage_fn() : HostCallbacks::ResourceUsage{};
    const auto& gps = gnss_snapshot_state_;
    const auto& gnss_status = gnss_snapshot_status_;
    const bool app_ready = host_.app_ready_fn ? host_.app_ready_fn() : (app() != nullptr);
    const int unread = (app_ready && app()) ? app()->getChatService().getTotalUnread() : 0;
    const bool gps_enabled = host_.gps_enabled_fn && host_.gps_enabled_fn();
    std::snprintf(unread_buf, sizeof(unread_buf), unread > 99 ? "99+" : "%d", unread);
    if (battery.available && battery.level >= 0)
    {
        std::snprintf(bat_pct_buf, sizeof(bat_pct_buf), "BAT:%d%%", battery.level);
    }
    else
    {
        std::snprintf(bat_pct_buf, sizeof(bat_pct_buf), "BAT:--");
    }
    const unsigned satellite_count = gnss_status.sats_in_view > 0
                                         ? static_cast<unsigned>(gnss_status.sats_in_view)
                                         : (gnss_snapshot_count_ > 0
                                                ? static_cast<unsigned>(gnss_snapshot_count_)
                                                : static_cast<unsigned>(gps.satellites));
    if (satellite_count > 0)
    {
        std::snprintf(sat_buf, sizeof(sat_buf), "SAT %u", satellite_count);
    }
    else if (gps_enabled)
    {
        std::snprintf(sat_buf, sizeof(sat_buf), "SAT --");
    }
    else
    {
        std::snprintf(sat_buf, sizeof(sat_buf), "SAT OFF");
    }
    const bool clock_unsynced = std::strcmp(date_buf, "TIME UNSYNC") == 0;
    if (clock_unsynced)
    {
        std::snprintf(status_buf, sizeof(status_buf), "CLOCK UNSYNC");
    }
    else
    {
        std::snprintf(status_buf, sizeof(status_buf), "%s", date_buf);
    }
    formatToggleLabel(left_toggle_buf, sizeof(left_toggle_buf), "GPS", gps_enabled);
#if !TRAILMATE_NRF52_BLE_DISABLED
    formatBleStateLabel(left_ble_buf, sizeof(left_ble_buf), app());
#endif
    if (ram.available && ram.total_bytes > 0)
    {
        std::snprintf(ram_buf, sizeof(ram_buf), "RAM %lu/%luK",
                      static_cast<unsigned long>(ram.used_bytes / 1024U),
                      static_cast<unsigned long>(ram.total_bytes / 1024U));
    }
    else
    {
        std::snprintf(ram_buf, sizeof(ram_buf), "RAM --");
    }

    ScreensaverLayoutModel model{};
    model.protocol = protocol[0] ? protocol : "--";
    model.frequency = freq[0] ? freq : "--";
    model.time_main = time_main_buf;
    model.seconds = time_sec_buf;
    model.status = status_buf[0] ? status_buf : "--";
    model.battery = bat_pct_buf;
    model.satellites = sat_buf;
    model.unread = unread_buf;
    model.node = node_buf[0] ? node_buf : "--";
    model.timezone = tz_buf[0] ? tz_buf : "UTC+0";
    model.gps = left_toggle_buf;
    model.ble = left_ble_buf;
    model.ram = ram_buf;
    model.battery_low = battery.available && battery.level >= 0 && battery.level <= 20;

    renderScreensaverLayout(display_, text_renderer_, accent_text_renderer_, model);
}

void Runtime::renderSleep()
{
    if (usesLargeScreensaverLayout())
    {
        const int x = std::max(0, (display_.width() - assets::kTrailmateSleepLogoWidth) / 2);
        const int y = std::max(0, (display_.height() - assets::kTrailmateSleepLogoHeight) / 2);
        drawBitmap1bpp(display_,
                       x,
                       y,
                       assets::kTrailmateSleepLogoWidth,
                       assets::kTrailmateSleepLogoHeight,
                       assets::kTrailmateSleepLogoStride,
                       assets::kTrailmateSleepLogoBitmap);
        return;
    }

    text_renderer_.drawText(display_, 0, 24, "SLEEP");
}

bool Runtime::mainMenuShowsDiscover() const
{
    return app() && app()->getConfig().mesh_protocol == chat::MeshProtocol::MeshCore;
}

size_t Runtime::mainMenuItemCount() const
{
    return mainMenuShowsDiscover()
               ? arrayCount(kMeshCoreMainMenuItems)
               : arrayCount(kMainMenuItems);
}

const char* const* Runtime::mainMenuItems() const
{
    return mainMenuShowsDiscover() ? kMeshCoreMainMenuItems : kMainMenuItems;
}

Runtime::Page Runtime::mainMenuPageForIndex(size_t index) const
{
    if (mainMenuShowsDiscover())
    {
        switch (index)
        {
        case 0:
            return Page::ChatList;
        case 1:
            return Page::NodeList;
        case 2:
            return Page::GnssPage;
        case 3:
            return Page::DiscoverPage;
        case 4:
            return Page::SettingsMenu;
        case 5:
            return Page::InfoPage;
        default:
            return Page::MainMenu;
        }
    }

    switch (index)
    {
    case 0:
        return Page::ChatList;
    case 1:
        return Page::NodeList;
    case 2:
        return Page::GnssPage;
    case 3:
        return Page::SettingsMenu;
    case 4:
        return Page::InfoPage;
    default:
        return Page::MainMenu;
    }
}

void Runtime::renderMainMenu()
{
    const size_t item_count = mainMenuItemCount();
    if (main_menu_index_ >= item_count && item_count > 0)
    {
        main_menu_index_ = item_count - 1U;
    }
    drawMenuList("MENU", mainMenuItems(), item_count, main_menu_index_);
}

void Runtime::renderChatList()
{
    rebuildConversationList();
    const size_t page_size = visibleRowsFrom(10);
    const size_t item_count = conversation_count_ + 1U;
    const size_t total_pages = std::max<size_t>(1U, (item_count + page_size - 1U) / page_size);
    const size_t selected = std::min(chat_list_index_, item_count - 1U);
    const size_t start = (selected / page_size) * page_size;
    const size_t current_page = (start / page_size) + 1U;
    char pos[16] = {};
    std::snprintf(pos, sizeof(pos), "%u/%u",
                  static_cast<unsigned>(current_page),
                  static_cast<unsigned>(total_pages));
    drawTitleBar("CHATS", pos);

    const int line_h = text_renderer_.lineHeight();
    const size_t visible = std::min(item_count - start, page_size);
    const int marker_w = std::max(8, text_renderer_.measureTextWidth("T") + 2);
    const int marker_x = std::max(0, display_.width() - marker_w);
    const int line_w = std::max(0, marker_x - 1);
    for (size_t i = 0; i < visible; ++i)
    {
        const size_t item_index = start + i;
        const bool selected_row = (item_index == selected);
        char line[32] = {};
        const char* marker = app() ? protocolMarker(app()->getConfig().mesh_protocol) : "";
        if (item_index == 0)
        {
            std::snprintf(line, sizeof(line), "+ NEW CHANNEL");
        }
        else
        {
            const auto& conv = conversations_[item_index - 1U];
            std::snprintf(line, sizeof(line), "%s%s",
                          conv.unread > 0 ? "*" : "",
                          conv.name.c_str());
            marker = protocolMarker(conv.id.protocol);
        }
        const int y = 10 + static_cast<int>(i * line_h);
        drawTextClipped(0, y, line_w, line, selected_row);
        drawTextClipped(marker_x, y, marker_w, marker, selected_row);
    }
}

void Runtime::renderNewChatPage()
{
    drawMenuList("NEW CHAT", kNewChatItems, arrayCount(kNewChatItems), new_chat_index_);
}

void Runtime::renderNodeList()
{
    rebuildNodeList();
    const size_t selected = (node_count_ > 0)
                                ? std::min(node_list_index_, node_count_ - 1U)
                                : 0U;
    const size_t page_size = visibleRowsFrom(10);
    const size_t start = (node_count_ > 0) ? ((selected / page_size) * page_size) : 0U;
    constexpr int kHeaderY = 0;
    const int line_h = text_renderer_.lineHeight();
    constexpr int kRowStartY = 10;
    const bool wide = display_.width() >= 160;
    const int name_x = 0;
    const int proto_w = std::max(8, text_renderer_.measureTextWidth("T") + 2);
    const int proto_x = std::max(0, display_.width() - proto_w);
    const int bars_x = std::max(0, proto_x - 14);
    const int hops_x = wide ? std::max(0, bars_x - 20) : 90;
    const int brg_x = wide ? std::max(0, hops_x - 32) : 60;
    const int dist_x = wide ? std::max(0, brg_x - 34) : 40;
    const int age_x = wide ? std::max(0, dist_x - 24) : 24;
    const int name_w = wide ? std::max(20, age_x - 2) : 22;
    const int age_w = wide ? std::max(14, dist_x - age_x - 2) : 14;
    const int dist_w = wide ? std::max(18, brg_x - dist_x - 2) : 18;
    const int brg_w = wide ? std::max(24, hops_x - brg_x - 2) : 28;
    const int hops_w = wide ? std::max(10, bars_x - hops_x - 2) : 12;

    display_.fillRect(0, kHeaderY, display_.width(), line_h + 1, true);
    drawTextClipped(name_x, kHeaderY, name_w, wide ? "NODE" : "ID", true);
    drawTextClipped(age_x, kHeaderY, age_w, "AGE", true);
    drawTextClipped(dist_x, kHeaderY, dist_w, "DST", true);
    drawTextClipped(brg_x, kHeaderY, brg_w, "BRG", true);
    drawTextClipped(hops_x, kHeaderY, hops_w, "HP", true);
    drawTextClipped(proto_x, kHeaderY, proto_w, "P", true);
    if (node_count_ == 0)
    {
        text_renderer_.drawText(display_, 0, kRowStartY, "NO NODES");
        return;
    }

    const size_t visible = std::min(node_count_ - start, page_size);

    for (size_t i = 0; i < visible; ++i)
    {
        const size_t node_index = start + i;
        const auto& node = nodes_[node_index];
        const int row_y = kRowStartY + static_cast<int>(i * line_h);
        const bool selected_row = (node_index == selected);
        char node_id[8] = {};
        std::snprintf(node_id, sizeof(node_id), "%04lX",
                      static_cast<unsigned long>(node.node_id & 0xFFFFUL));
        char label[32] = {};
        const char* label_source = node_id;
        if (wide)
        {
            if (!node.display_name.empty())
            {
                label_source = node.display_name.c_str();
            }
            else if (node.short_name[0] != '\0')
            {
                label_source = node.short_name;
            }
        }
        copyText(label, label_source);

        if (selected_row)
        {
            display_.fillRect(0, row_y, display_.width(), line_h, true);
        }

        char age_buf[8] = {};
        const time_t now_s = host_.utc_now_fn ? host_.utc_now_fn() : 0;
        formatElapsedShort(now_s, node.last_seen, age_buf, sizeof(age_buf));

        char dist_buf[12] = {};
        const auto gps = host_.gps_data_fn ? host_.gps_data_fn() : platform::ui::gps::GpsState{};
        if (gps.valid && node.position.valid)
        {
            const double node_lat = static_cast<double>(node.position.latitude_i) / 1e7;
            const double node_lon = static_cast<double>(node.position.longitude_i) / 1e7;
            formatDistanceShort(haversineMeters(gps.lat, gps.lng, node_lat, node_lon), dist_buf, sizeof(dist_buf));
        }
        else
        {
            std::snprintf(dist_buf, sizeof(dist_buf), "-");
        }

        bool has_bearing = false;
        double bearing_deg = 0.0;
        if (gps.valid && node.position.valid)
        {
            const double node_lat = static_cast<double>(node.position.latitude_i) / 1e7;
            const double node_lon = static_cast<double>(node.position.longitude_i) / 1e7;
            bearing_deg = bearingDegrees(gps.lat, gps.lng, node_lat, node_lon);
            has_bearing = true;
        }

        const char* sig = signalRatingLabel(node.snr, node.rssi);
        char hops_buf[6] = {};
        if (node.hops_away == 0xFF)
        {
            std::snprintf(hops_buf, sizeof(hops_buf), "-");
        }
        else
        {
            std::snprintf(hops_buf, sizeof(hops_buf), "%u", static_cast<unsigned>(node.hops_away));
        }
        drawTextClipped(name_x, row_y, name_w, label, selected_row);
        drawTextClipped(age_x, row_y, age_w, age_buf, selected_row);
        drawTextClipped(dist_x, row_y, dist_w, dist_buf, selected_row);
        drawTextClipped(hops_x, row_y, hops_w, hops_buf, selected_row);
        drawTextClipped(proto_x, row_y, proto_w, nodeProtocolMarker(node.protocol), selected_row);
        if (has_bearing)
        {
            char bearing_buf[10] = {};
            formatBearingCompact(bearing_buf, sizeof(bearing_buf), bearing_deg);
            drawTextClipped(brg_x, row_y, brg_w, bearing_buf, selected_row);
        }
        else
        {
            drawTextClipped(brg_x, row_y, brg_w, "-", selected_row);
        }

        const int bars = std::strcmp(sig, "STR") == 0 ? 3 : std::strcmp(sig, "OK") == 0 ? 2
                                                        : std::strcmp(sig, "WEAK") == 0 ? 1
                                                                                        : 0;
        for (int bar = 0; bar < 3; ++bar)
        {
            if (bar >= bars)
            {
                continue;
            }
            const int bar_h = 2 + bar * 3;
            const int bar_x = bars_x + bar * 4;
            const int bar_y = row_y + 6 - bar_h;
            display_.fillRect(bar_x, bar_y, 3, bar_h, !selected_row);
        }
    }
}

void Runtime::renderNodeInfo()
{
    buildNodeInfo();
    char pos[24] = {};
    const size_t page_size = visibleRowsFrom(10);
    if (node_info_count_ > 0)
    {
        const size_t total_pages = (node_info_count_ + page_size - 1U) / page_size;
        const size_t current_page = (node_info_scroll_ / page_size) + 1U;
        std::snprintf(pos, sizeof(pos), "%u/%u",
                      static_cast<unsigned>(current_page),
                      static_cast<unsigned>(total_pages));
    }
    drawTitleBar("NODE", pos[0] != '\0' ? pos : nullptr);
    if (node_info_count_ == 0)
    {
        text_renderer_.drawText(display_, 0, 18, "NO INFO");
        return;
    }

    const size_t max_scroll = node_info_count_ > page_size ? (node_info_count_ - page_size) : 0U;
    if (node_info_scroll_ > max_scroll)
    {
        node_info_scroll_ = max_scroll;
    }

    const int line_h = text_renderer_.lineHeight();
    const size_t start = std::min(node_info_scroll_, node_info_count_);
    const size_t visible = std::min(node_info_count_ - start, page_size);
    for (size_t i = 0; i < visible && (start + i) < node_info_count_; ++i)
    {
        drawTextClipped(0, 10 + static_cast<int>(i * line_h), display_.width(), node_info_lines_[start + i], false);
    }
}

void Runtime::renderNodeCompass()
{
    const chat::contacts::NodeInfo* node = selectedNode();
    char right[12] = {};
    if (node != nullptr)
    {
        std::snprintf(right, sizeof(right), "%04lX",
                      static_cast<unsigned long>(node->node_id & 0xFFFFUL));
    }
    drawTitleBar("COMPASS", right[0] != '\0' ? right : nullptr);

    if (node == nullptr)
    {
        text_renderer_.drawText(display_, 0, 18, "NO NODE");
        return;
    }

    const char* title_name = node->display_name.empty()
                                 ? (node->short_name[0] != '\0' ? node->short_name : "NODE")
                                 : node->display_name.c_str();
    drawTextClipped(0, 10, display_.width(), title_name);

    const auto gps = host_.gps_data_fn ? host_.gps_data_fn() : platform::ui::gps::GpsState{};
    if (!gps.valid)
    {
        if (display_.width() >= 160 && display_.height() >= 120)
        {
            drawTextClipped(12, 52, display_.width() - 24, "GPS UNAVAILABLE");
            drawTextClipped(12, 68, display_.width() - 24, "NEED LOCAL FIX");
        }
        else
        {
            text_renderer_.drawText(display_, 0, 26, "GPS UNAVAILABLE");
            text_renderer_.drawText(display_, 0, 36, "NEED LOCAL FIX");
        }
        return;
    }
    if (!node->position.valid)
    {
        if (display_.width() >= 160 && display_.height() >= 120)
        {
            drawTextClipped(12, 52, display_.width() - 24, "NODE POSITION");
            drawTextClipped(12, 68, display_.width() - 24, "UNAVAILABLE");
        }
        else
        {
            text_renderer_.drawText(display_, 0, 26, "NODE POSITION");
            text_renderer_.drawText(display_, 0, 36, "UNAVAILABLE");
        }
        return;
    }

    const double node_lat = static_cast<double>(node->position.latitude_i) / 1e7;
    const double node_lon = static_cast<double>(node->position.longitude_i) / 1e7;
    const double dist_m = haversineMeters(gps.lat, gps.lng, node_lat, node_lon);
    const double abs_bearing_deg = bearingDegrees(gps.lat, gps.lng, node_lat, node_lon);
    const double rel_bearing_deg = gps.has_course ? normalizeBearingDeg(abs_bearing_deg - gps.course_deg) : abs_bearing_deg;
    char line[24] = {};
    char age_buf[12] = {};
    char footer[24] = {};
    const char* proto = node->protocol == chat::contacts::NodeProtocolType::MeshCore     ? "MC"
                        : node->protocol == chat::contacts::NodeProtocolType::Meshtastic ? "MT"
                                                                                         : "?";

    if (display_.width() >= 160 && display_.height() >= 120)
    {
        const int center_x = std::max(42, display_.width() / 3 - 8);
        const int center_y = std::min(display_.height() - 56, std::max(72, display_.height() / 2));
        const int radius = std::min(44, std::min(center_x - 8, display_.height() / 4));
        const int info_x = std::min(display_.width() - 68, center_x + radius + 14);
        const int info_w = std::max(44, display_.width() - info_x - 2);

        auto drawInfo = [this, info_x, info_w](int y, const char* text)
        {
            drawTextClipped(info_x, y, info_w, text);
        };

        drawCircle(display_, center_x, center_y, radius);
        display_.drawPixel(center_x, center_y, true);
        text_renderer_.drawText(display_, center_x - 2, center_y - radius - 10, "N");
        text_renderer_.drawText(display_, center_x - 2, center_y + radius + 3, "S");
        text_renderer_.drawText(display_, center_x - radius - 9, center_y - 3, "W");
        text_renderer_.drawText(display_, center_x + radius + 5, center_y - 3, "E");
        display_.drawPixel(center_x, center_y - radius + 3, true);
        display_.drawPixel(center_x - 1, center_y - radius + 4, true);
        display_.drawPixel(center_x + 1, center_y - radius + 4, true);
        drawCompassArrow(display_,
                         center_x,
                         center_y,
                         gps.has_course ? rel_bearing_deg : abs_bearing_deg,
                         radius - 6,
                         true);

        if (dist_m < 1000.0)
        {
            std::snprintf(line, sizeof(line), "DST %.0fm", dist_m);
        }
        else
        {
            std::snprintf(line, sizeof(line), "DST %.2fkm", dist_m / 1000.0);
        }
        drawInfo(28, line);

        std::snprintf(line, sizeof(line), "BRG %s %.0f", bearingCardinal(abs_bearing_deg), abs_bearing_deg);
        drawInfo(42, line);
        if (gps.has_course)
        {
            std::snprintf(line, sizeof(line), "HDG %.0f", gps.course_deg);
            drawInfo(56, line);
            std::snprintf(line, sizeof(line), "REL %.0f", rel_bearing_deg);
            drawInfo(70, line);
        }
        else
        {
            drawInfo(56, "HDG N/A");
            drawInfo(70, "REL N/A");
        }

        if (node->position.has_altitude)
        {
            std::snprintf(line, sizeof(line), "ALT %ldm", static_cast<long>(node->position.altitude));
        }
        else
        {
            std::snprintf(line, sizeof(line), "ALT -");
        }
        drawInfo(84, line);

        formatElapsedShort(host_.utc_now_fn ? host_.utc_now_fn() : 0, node->last_seen, age_buf, sizeof(age_buf));
        if (node->hops_away == 0xFF)
        {
            std::snprintf(footer, sizeof(footer), "%s  %s", proto, age_buf);
        }
        else
        {
            std::snprintf(footer, sizeof(footer), "%s H%u %s",
                          proto,
                          static_cast<unsigned>(node->hops_away),
                          age_buf);
        }
        drawTextClipped(12, display_.height() - 18, display_.width() - 24, footer);
        return;
    }

    constexpr int kCenterX = 25;
    constexpr int kCenterY = 39;
    constexpr int kRadius = 17;
    constexpr int kInfoRightX = 127;
    constexpr int kInfoW = 58;
    constexpr int kInfoMinX = kInfoRightX - kInfoW + 1;
    auto drawInfoRight = [this, kInfoRightX, kInfoW, kInfoMinX](int y, const char* text)
    {
        if (!text || text[0] == '\0')
        {
            return;
        }

        const char* draw_text = text;
        char clipped[32] = {};
        if (text_renderer_.measureTextWidth(text) > kInfoW)
        {
            if (kInfoW > text_renderer_.ellipsisWidth())
            {
                const size_t keep_bytes = text_renderer_.clipTextToWidth(text, kInfoW - text_renderer_.ellipsisWidth());
                std::memcpy(clipped, text, keep_bytes);
                clipped[keep_bytes] = '\0';
                std::strcat(clipped, "...");
            }
            else
            {
                const size_t keep_bytes = text_renderer_.clipTextToWidth(text, kInfoW);
                std::memcpy(clipped, text, keep_bytes);
                clipped[keep_bytes] = '\0';
            }
            draw_text = clipped;
        }

        const int draw_w = text_renderer_.measureTextWidth(draw_text);
        const int draw_x = std::max(kInfoMinX, kInfoRightX - draw_w);
        text_renderer_.drawText(display_, draw_x, y, draw_text);
    };
    drawCircle(display_, kCenterX, kCenterY, kRadius);
    display_.drawPixel(kCenterX, kCenterY, true);
    text_renderer_.drawText(display_, kCenterX - 2, kCenterY - kRadius - 7, "N");
    text_renderer_.drawText(display_, kCenterX - 2, kCenterY + kRadius + 1, "S");
    text_renderer_.drawText(display_, kCenterX - kRadius - 7, kCenterY - 3, "W");
    text_renderer_.drawText(display_, kCenterX + kRadius + 3, kCenterY - 3, "E");
    display_.drawPixel(kCenterX, kCenterY - kRadius + 3, true);
    display_.drawPixel(kCenterX - 1, kCenterY - kRadius + 4, true);
    display_.drawPixel(kCenterX + 1, kCenterY - kRadius + 4, true);

    if (gps.has_course)
    {
        drawCompassArrow(display_, kCenterX, kCenterY, rel_bearing_deg, kRadius - 4, true);
    }
    else
    {
        drawCompassArrow(display_, kCenterX, kCenterY, abs_bearing_deg, kRadius - 4, true);
    }

    if (dist_m < 1000.0)
    {
        std::snprintf(line, sizeof(line), "DST %.0fm", dist_m);
    }
    else
    {
        std::snprintf(line, sizeof(line), "DST %.2fkm", dist_m / 1000.0);
    }
    drawInfoRight(18, line);

    std::snprintf(line, sizeof(line), "BRG %s %.0f", bearingCardinal(abs_bearing_deg), abs_bearing_deg);
    drawInfoRight(26, line);

    if (gps.has_course)
    {
        std::snprintf(line, sizeof(line), "HDG %.0f", gps.course_deg);
        drawInfoRight(34, line);
        std::snprintf(line, sizeof(line), "REL %.0f", rel_bearing_deg);
        drawInfoRight(42, line);
    }
    else
    {
        drawInfoRight(34, "HDG N/A");
        drawInfoRight(42, "REL N/A");
    }

    if (node->position.has_altitude)
    {
        std::snprintf(line, sizeof(line), "ALT %ldm", static_cast<long>(node->position.altitude));
    }
    else
    {
        std::snprintf(line, sizeof(line), "ALT -");
    }
    drawInfoRight(50, line);

    formatElapsedShort(host_.utc_now_fn ? host_.utc_now_fn() : 0, node->last_seen, age_buf, sizeof(age_buf));
    if (node->hops_away == 0xFF)
    {
        std::snprintf(footer, sizeof(footer), "%s  %s", proto, age_buf);
    }
    else
    {
        std::snprintf(footer, sizeof(footer), "%s H%u %s",
                      proto,
                      static_cast<unsigned>(node->hops_away),
                      age_buf);
    }
    drawInfoRight(58, footer);
}

void Runtime::renderNodeActionMenu()
{
    const auto* node = selectedNode();
    std::array<const char*, kNodeActionItemCount> items{};
    for (size_t i = 0; i < items.size(); ++i)
    {
        items[i] = nodeActionLabel(i);
    }
    char title[20] = {};
    if (node)
    {
        std::snprintf(title, sizeof(title), "%04lX", static_cast<unsigned long>(node->node_id & 0xFFFFUL));
    }
    drawMenuList(title[0] != '\0' ? title : "NODE", items.data(), items.size(), node_action_index_);
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
    constexpr int kConversationStartY = 10;
    constexpr int kBubbleGap = 1;
    constexpr int kBubbleWidth = 168;
    constexpr int kBubbleBodyBottomPadding = 2;
    const int bubble_h = (line_h * 2) + 1 + kBubbleBodyBottomPadding;
    const int row_h = bubble_h + kBubbleGap;
    const int bubble_w = std::max(8, std::min(kBubbleWidth, display_.width() - 2));
    const size_t max_visible = std::max<size_t>(
        1U,
        static_cast<size_t>(std::max(row_h, display_.height() - kConversationStartY) / row_h));
    const size_t visible = std::min(message_count_, max_visible);
    const size_t selected_index = std::min(message_index_, message_count_ - 1U);
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
        char sender[16] = {};
        char time_text[16] = {};
        uint32_t display_timestamp = msg.timestamp;
        if (selected && display_timestamp < 1700000000U)
        {
            for (size_t meta_index = 0; meta_index < kMessageMetaCapacity; ++meta_index)
            {
                const MessageMetaEntry& candidate = message_meta_[meta_index];
                if (!candidate.used)
                {
                    continue;
                }
                if (candidate.protocol == msg.protocol &&
                    candidate.channel == msg.channel &&
                    candidate.from == msg.from &&
                    candidate.msg_id == msg.msg_id &&
                    candidate.rx_meta.rx_timestamp_s >= 1700000000U)
                {
                    display_timestamp = candidate.rx_meta.rx_timestamp_s;
                    break;
                }
            }
        }
        formatConversationSender(sender, sizeof(sender), msg, selected);
        formatConversationTime(time_text, sizeof(time_text), display_timestamp, selected);

        const int y = kConversationStartY + static_cast<int>(i * row_h);
        const bool is_self = (msg.from == 0);
        const int x = is_self ? (display_.width() - bubble_w - 1) : 1;
        drawConversationBubble(x, y, bubble_w, sender, time_text, msg.text.c_str(), selected, is_self);
    }
}

void Runtime::renderMessageMenu()
{
    drawMenuList("MESSAGE", kMessageMenuItems, arrayCount(kMessageMenuItems), message_menu_index_);
}

void Runtime::renderMessageInfo()
{
    buildMessageInfo();
    char pos[24] = {};
    const size_t page_size = visibleRowsFrom(10);
    if (message_info_count_ > 0)
    {
        const size_t total_pages = (message_info_count_ + page_size - 1U) / page_size;
        const size_t current_page = (message_info_scroll_ / page_size) + 1U;
        std::snprintf(pos, sizeof(pos), "%u/%u",
                      static_cast<unsigned>(current_page),
                      static_cast<unsigned>(total_pages));
    }
    drawTitleBar("INFO", pos[0] != '\0' ? pos : nullptr);
    if (message_info_count_ == 0)
    {
        text_renderer_.drawText(display_, 0, 18, "NO INFO");
        return;
    }

    const size_t max_scroll = message_info_count_ > page_size ? (message_info_count_ - page_size) : 0U;
    if (message_info_scroll_ > max_scroll)
    {
        message_info_scroll_ = max_scroll;
    }

    const int line_h = text_renderer_.lineHeight();
    const size_t start = std::min(message_info_scroll_, message_info_count_);
    const size_t visible = std::min(message_info_count_ - start, page_size);
    for (size_t i = 0; i < visible && (start + i) < message_info_count_; ++i)
    {
        drawTextClipped(0, 10 + static_cast<int>(i * line_h), display_.width(), message_info_lines_[start + i], false);
    }
}

void Runtime::renderCompose()
{
    if (usesPhysicalTextInput())
    {
        drawTitleBar(edit_target_ == EditTarget::Message ? "COMPOSE" : "EDIT", physicalComposeModeLabel(compose_mode_));

        char target[16] = {};
        formatComposeTarget(target, sizeof(target));
        if (edit_target_ == EditTarget::Message)
        {
            char to_line[24] = {};
            std::snprintf(to_line, sizeof(to_line), "TO:%s", target[0] != '\0' ? target : "-");
            drawTextClipped(0, 10, display_.width(), to_line, false);
        }

        char body_text[kComposeMax + 1] = {};
        if (compose_len_ > 0)
        {
            copyText(body_text, compose_buffer_);
        }

        const int line_h = std::max(1, text_renderer_.lineHeight());
        const int body_y = edit_target_ == EditTarget::Message ? 22 : 10;
        const bool show_cn_candidates = compose_mode_ == ComposeMode::Cn && hasPhysicalChinesePreedit();
        const bool selecting_hanzi = compose_t9_selected_pinyin_[0] != '\0';
        const size_t cn_candidate_count = selecting_hanzi ? compose_candidate_count_ : compose_t9_pinyin_candidate_count_;
        const size_t cn_selected_index = selecting_hanzi ? compose_candidate_index_ : compose_t9_pinyin_candidate_index_;
        const size_t cn_candidate_page_size = composeCandidatePageSize(host_);
        const size_t cn_candidate_page_start =
            composeCandidatePageStart(cn_selected_index, cn_candidate_count, cn_candidate_page_size);
        const size_t cn_visible_candidate_count =
            cn_candidate_count > cn_candidate_page_start
                ? std::min(cn_candidate_page_size, cn_candidate_count - cn_candidate_page_start)
                : 0U;
        const int cn_candidate_rows = (show_cn_candidates && cn_visible_candidate_count > 3U && display_.height() >= 96) ? 2 : 1;
        const int ime_rows = show_cn_candidates ? (1 + cn_candidate_rows) : 1;
        const int ime_y = std::max(body_y + line_h, display_.height() - (ime_rows * line_h));
        const size_t body_line_count = std::max<size_t>(1U, static_cast<size_t>(std::max(line_h, ime_y - body_y) / line_h));
        const size_t body_visible_per_line = std::max<size_t>(
            16U,
            std::min<size_t>(42U, static_cast<size_t>(std::max(80, display_.width())) / 5U));
        const size_t body_text_len = std::strlen(body_text);
        const size_t body_window = body_visible_per_line * body_line_count;
        const size_t body_start = body_text_len > body_window ? (body_text_len - body_window) : 0U;
        for (size_t line_index = 0; line_index < body_line_count; ++line_index)
        {
            char line[48] = {};
            const size_t offset = body_start + line_index * body_visible_per_line;
            if (offset < body_text_len)
            {
                const size_t len = std::min(body_visible_per_line, body_text_len - offset);
                std::memcpy(line, body_text + offset, len);
                line[len] = '\0';
            }
            else if (line_index == 0)
            {
                std::snprintf(line, sizeof(line), "_");
            }
            drawTextClipped(0,
                            body_y + static_cast<int>(line_index * static_cast<size_t>(line_h)),
                            display_.width(),
                            line,
                            false);
        }

        char ime_lines[3][96] = {};
        if (show_cn_candidates)
        {
            if (compose_t9_selected_pinyin_[0] != '\0')
            {
                std::snprintf(ime_lines[0],
                              sizeof(ime_lines[0]),
                              "%s %s_",
                              compose_t9_digits_,
                              compose_t9_selected_pinyin_);
            }
            else
            {
                std::snprintf(ime_lines[0], sizeof(ime_lines[0]), "%s_", compose_t9_digits_);
            }

            size_t row = 1;
            size_t pos = 0;
            if (cn_visible_candidate_count > 0 && pos + 1 < sizeof(ime_lines[row]))
            {
                ime_lines[row][0] = '\0';
            }
            else
            {
                std::snprintf(ime_lines[row], sizeof(ime_lines[row]), "-");
            }
            for (size_t slot = 0; slot < cn_visible_candidate_count && row < static_cast<size_t>(ime_rows); ++slot)
            {
                const size_t i = cn_candidate_page_start + slot;
                const char* candidate = selecting_hanzi ? compose_candidates_[i] : compose_t9_pinyin_candidates_[i];
                const bool selected = selecting_hanzi ? (i == compose_candidate_index_) : (i == compose_t9_pinyin_candidate_index_);
                char token[32] = {};
                std::snprintf(token, sizeof(token),
                              selected ? "[%s]" : "%s",
                              candidate);
                const int current_w = text_renderer_.measureTextWidth(ime_lines[row]);
                const int token_w = text_renderer_.measureTextWidth(token);
                if (row + 1U < static_cast<size_t>(ime_rows) &&
                    pos > 0 && current_w + token_w + text_renderer_.measureTextWidth(" ") > display_.width())
                {
                    ++row;
                    pos = 0;
                    ime_lines[row][0] = '\0';
                }
                appendTextBounded(ime_lines[row], sizeof(ime_lines[row]), pos, token);
                if (slot + 1 < cn_visible_candidate_count && pos + 1 < sizeof(ime_lines[row]))
                {
                    ime_lines[row][pos++] = ' ';
                    ime_lines[row][pos] = '\0';
                }
            }
        }
        else
        {
            char hint[32] = {};
            formatPhoneCycleHint(hint, sizeof(hint), compose_mode_, compose_physical_last_key_, compose_abc_tap_index_);
            if (hint[0] != '\0' && compose_mode_ != ComposeMode::Num)
            {
                std::snprintf(ime_lines[0], sizeof(ime_lines[0]), "* %s #SP %s",
                              physicalComposeModeLabel(compose_mode_), hint);
            }
            else
            {
                std::snprintf(ime_lines[0], sizeof(ime_lines[0]), "* %s  # SPACE", physicalComposeModeLabel(compose_mode_));
            }
        }
        for (int row = 0; row < ime_rows; ++row)
        {
            drawTextClipped(0, ime_y + row * line_h, display_.width(), ime_lines[row], false);
        }
        return;
    }

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

    if (edit_target_ == EditTarget::Message)
    {
        char target[16] = {};
        formatComposeTarget(target, sizeof(target));
        char to_line[24] = {};
        std::snprintf(to_line, sizeof(to_line), "TO:%s", target[0] != '\0' ? target : "-");
        drawTextClipped(0, 0, display_.width(), to_line, false);
    }
    else
    {
        const char* title = "EDIT";
        if (edit_target_ == EditTarget::MeshtasticPrimaryKey)
        {
            title = "MT PSK";
        }
        else if (edit_target_ == EditTarget::MeshCoreChannelKey)
        {
            title = "MC KEY";
        }
        else if (edit_target_ == EditTarget::MeshCoreChannelName)
        {
            title = "MC NAME";
        }
        drawTitleBar(title, composeModeLabel(compose_mode_));
    }

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
        const size_t candidate_page_size = composeCandidatePageSize(host_);
        const size_t candidate_page_start =
            composeCandidatePageStart(compose_candidate_index_, compose_candidate_count_, candidate_page_size);
        const size_t candidate_page_end =
            std::min(compose_candidate_count_, candidate_page_start + candidate_page_size);
        for (size_t i = candidate_page_start; i < candidate_page_end && pos + 1 < sizeof(candidate_line); ++i)
        {
            const bool selected = (i == compose_candidate_index_);
            char token[32] = {};
            std::snprintf(token, sizeof(token),
                          selected ? "[%s]" : "%s",
                          compose_candidates_[i]);
            appendTextBounded(candidate_line, sizeof(candidate_line), pos, token);
            if (i + 1 < candidate_page_end && pos + 1 < sizeof(candidate_line))
            {
                candidate_line[pos++] = ' ';
                candidate_line[pos] = '\0';
            }
        }
    }
    drawTextClipped(0, line_h * 3, display_.width(), candidate_line, false);

    const char* keyset = composeKeysetForMode(compose_mode_);
    const size_t key_count = std::strlen(keyset);
    if (usesFullVirtualKeyboard())
    {
        const VirtualKeyboardRows grid = fullKeyboardRowsForMode(compose_mode_);
        size_t key_index_base = 0;
        for (size_t row = 0; row < grid.row_count; ++row)
        {
            const char* row_keys = grid.rows[row];
            const size_t row_len = std::strlen(row_keys);
            if (row_len == 0)
            {
                continue;
            }
            const int cell_w = std::max(1, display_.width() / static_cast<int>(row_len));
            const int row_w = cell_w * static_cast<int>(row_len);
            const int x0 = std::max(0, (display_.width() - row_w) / 2);
            for (size_t col = 0; col < row_len; ++col)
            {
                const size_t key_index = key_index_base + col;
                const bool selected = compose_focus_ == ComposeFocus::Body && key_index == compose_charset_index_;
                const char key_char = row_keys[col];
                char cell[8] = {};
                std::snprintf(cell, sizeof(cell), " %c ", displayComposeKeyChar(compose_mode_, key_char));
                drawTextClipped(x0 + static_cast<int>(col) * cell_w,
                                line_h * (4 + static_cast<int>(row)),
                                cell_w,
                                cell,
                                selected);
            }
            key_index_base += row_len;
        }
    }
    else
    {
        const size_t page_start = key_count == 0 ? 0U : ((compose_charset_index_ / kVirtualKeyboardPageSize) * kVirtualKeyboardPageSize);
        const int cell_w = std::max(1, display_.width() / static_cast<int>(kVirtualKeyboardCols));
        for (size_t row = 0; row < kVirtualKeyboardRows; ++row)
        {
            for (size_t col = 0; col < kVirtualKeyboardCols; ++col)
            {
                const size_t key_index = page_start + row * kVirtualKeyboardCols + col;
                if (key_index >= key_count)
                {
                    continue;
                }

                const bool selected = compose_focus_ == ComposeFocus::Body && key_index == compose_charset_index_;
                const char key_char = keyset[key_index];
                char cell[8] = {};
                std::snprintf(cell, sizeof(cell), " %c ", displayComposeKeyChar(compose_mode_, key_char));
                drawTextClipped(static_cast<int>(col) * cell_w,
                                line_h * (4 + static_cast<int>(row)),
                                cell_w,
                                cell,
                                selected);
            }
        }
    }

    const int action_y = line_h * 7;
    const int action_x[5] = {0, 24, 44, 70, 104};
    const char* action_labels[5] = {composeModeLabel(compose_mode_),
                                    "  ",
                                    "BACK",
                                    "DELETE",
                                    edit_target_ == EditTarget::Message ? "SEND" : "OK"};
    for (size_t i = 0; i < 5; ++i)
    {
        char action_cell[10] = {};
        std::snprintf(action_cell, sizeof(action_cell), "[%s]", action_labels[i]);
        const bool selected = compose_focus_ == ComposeFocus::Action && compose_action_index_ == i;
        text_renderer_.drawText(display_, action_x[i], action_y, action_cell, selected);
    }
}

void Runtime::renderDiscoverPage()
{
    if (!mainMenuShowsDiscover())
    {
        drawTitleBar("DISCOVER", nullptr);
        text_renderer_.drawText(display_, 0, 18, "MESHCORE ONLY");
        return;
    }

    if (discover_index_ >= arrayCount(kDiscoverItems))
    {
        discover_index_ = arrayCount(kDiscoverItems) - 1U;
    }
    drawMenuList("DISCOVER", kDiscoverItems, arrayCount(kDiscoverItems), discover_index_);
}

void Runtime::renderSettingsMenu()
{
    drawMenuList("SETTINGS", kSettingsMenuItems, arrayCount(kSettingsMenuItems), settings_menu_index_);
}

void Runtime::renderRadioSettings()
{
    const auto protocol = app()->getConfig().mesh_protocol;
    const size_t item_count = radioItemCount();
    if (item_count == 0)
    {
        return;
    }
    if (radio_index_ >= item_count)
    {
        radio_index_ = item_count - 1;
    }

    drawTitleBar("PROTO", protocolShortLabel(protocol));
    char value[40] = {};
    auto& cfg = app()->getConfig();
    const int row_y = 12;
    const int row_h = std::max(1, text_renderer_.lineHeight());
    const size_t visible = std::min(item_count, visibleRowsFrom(row_y, 4));
    size_t start = 0;
    if (item_count > visible)
    {
        start = (radio_index_ + 1 > visible) ? (radio_index_ + 1 - visible) : 0U;
        if (start + visible > item_count)
        {
            start = item_count - visible;
        }
    }

    for (size_t row = 0; row < visible && start + row < item_count; ++row)
    {
        const size_t i = start + row;
        value[0] = '\0';
        formatRadioSettingValue(cfg, radioSettingItem(i), value, sizeof(value));

        char line[64] = {};
        std::snprintf(line, sizeof(line), "%s: %s", radioSettingLabel(i), value);
        drawTextClipped(0, row_y + static_cast<int>(row * static_cast<size_t>(row_h)),
                        display_.width(), line, i == radio_index_);
    }
}

void Runtime::renderDeviceSettings()
{
    drawTitleBar("DEVICE", nullptr);
#if !TRAILMATE_NRF52_BLE_DISABLED
    ble::BlePairingStatus ble_status{};
    const bool has_ble_status = loadBlePairingStatus(app(), &ble_status);
#endif
    char line[48] = {};
    constexpr int kRowY = 12;
    const int row_h = rowStrideFor(arrayCount(kDeviceItems), kRowY, 4);
    for (size_t i = 0; i < arrayCount(kDeviceItems); ++i)
    {
        line[0] = '\0';
        switch (deviceSettingItem(i))
        {
#if !TRAILMATE_NRF52_BLE_DISABLED
        case DeviceSettingItem::Ble:
            if (has_ble_status && ble_status.available && ble_status.requires_passkey && ble_status.passkey != 0)
            {
                std::snprintf(line, sizeof(line), "BLE: ON %s %06lu",
                              ble_status.is_fixed_pin ? "FIX" : "PIN",
                              static_cast<unsigned long>(ble_status.passkey));
            }
            else if (has_ble_status && ble_status.available && ble_status.requires_passkey)
            {
                std::snprintf(line, sizeof(line), "BLE: ON %s",
                              ble_status.is_fixed_pin ? "FIXED" : "RANDOM");
            }
            else
            {
                std::snprintf(line, sizeof(line), "BLE: %s", bleDisplayStateLabel(resolveBleDisplayState(app())));
            }
            break;
#endif
#if TRAILMATE_NRF_MONO_TARGET_MESSAGE_SOUND_SETTING
        case DeviceSettingItem::MessageTone:
        {
            const uint8_t volume = host_.message_tone_volume_fn ? host_.message_tone_volume_fn() : 0;
            std::snprintf(line, sizeof(line), "MSG SOUND: %s", volume > 0 ? "ON" : "OFF");
            break;
        }
#endif
#if TRAILMATE_NRF_MONO_TARGET_DEVICE_IO_SETTINGS
        case DeviceSettingItem::MessageLight:
        {
            const bool enabled = host_.message_light_enabled_fn ? host_.message_light_enabled_fn() : false;
            std::snprintf(line, sizeof(line), "MSG LIGHT: %s", enabled ? "ON" : "OFF");
            break;
        }
        case DeviceSettingItem::LedColor:
        {
            const uint8_t color = host_.status_led_color_index_fn ? host_.status_led_color_index_fn() : 0;
            const char* label = host_.status_led_color_label_fn ? host_.status_led_color_label_fn(color) : nullptr;
            std::snprintf(line, sizeof(line), "LED COLOR: %s", (label && label[0] != '\0') ? label : "-");
            break;
        }
        case DeviceSettingItem::KeyboardLight:
        {
            const bool enabled = host_.keyboard_light_enabled_fn ? host_.keyboard_light_enabled_fn() : false;
            std::snprintf(line, sizeof(line), "KEY LIGHT: %s", enabled ? "ON" : "OFF");
            break;
        }
#endif
        case DeviceSettingItem::Gps:
            std::snprintf(line, sizeof(line), "GPS: %s", app()->getConfig().gps_enabled ? "ON" : "OFF");
            break;
        case DeviceSettingItem::Sats:
            std::snprintf(line, sizeof(line), "SATS: %s", gpsSatMaskLabel(app()->getConfig().gps_sat_mask));
            break;
        case DeviceSettingItem::GpsInterval:
            std::snprintf(line, sizeof(line), "GPS INT: %lus",
                          static_cast<unsigned long>(app()->getConfig().gps_interval_ms / 1000UL));
            break;
        case DeviceSettingItem::ScreenOff:
        {
            char timeout_label[16] = {};
            formatScreenTimeoutLabel(timeout_label, sizeof(timeout_label), platform::ui::screen::timeout_ms());
            std::snprintf(line, sizeof(line), "SCREEN OFF: %s", timeout_label);
            break;
        }
        case DeviceSettingItem::TimeZone:
        {
            const int tz = host_.timezone_offset_min_fn ? host_.timezone_offset_min_fn() : 0;
            char tz_label[16] = {};
            formatTimezoneLabel(tz, tz_label, sizeof(tz_label));
            std::snprintf(line, sizeof(line), "TIME ZONE: %s", tz_label);
            break;
        }
        }
        drawTextClipped(0, kRowY + static_cast<int>(i * static_cast<size_t>(row_h)),
                        display_.width(), line, i == device_index_);
    }
}

void Runtime::renderSettingPopup()
{
    char title[24] = {};
    char value[32] = {};
    const bool is_radio = setting_popup_owner_ == Page::RadioSettings;
    const bool is_device = setting_popup_owner_ == Page::DeviceSettings;
    const bool is_action = setting_popup_owner_ == Page::ActionPage;
    if (!is_radio && !is_device && !is_action)
    {
        return;
    }

    if (is_radio)
    {
        if (setting_popup_index_ >= radioItemCount())
        {
            return;
        }
        std::snprintf(title, sizeof(title), "%s", radioSettingLabel(setting_popup_index_));
    }
    else if (is_device)
    {
        if (setting_popup_index_ >= arrayCount(kDeviceItems))
        {
            return;
        }
        std::snprintf(title, sizeof(title), "%s", kDeviceItems[setting_popup_index_]);
    }
    else
    {
        if (setting_popup_index_ >= arrayCount(kActionItems))
        {
            return;
        }
        std::snprintf(title, sizeof(title), "%s", kActionItems[setting_popup_index_]);
        std::snprintf(value, sizeof(value), "SELECT TO CONFIRM");
    }

    if (!is_action)
    {
        formatSettingPopupValue(value, sizeof(value));
    }

    constexpr int kBoxX = 8;
    constexpr int kBoxY = 14;
    constexpr int kBoxW = 112;
    constexpr int kBoxH = 36;
    display_.fillRect(kBoxX, kBoxY, kBoxW, kBoxH, false);
    drawFrame(display_, kBoxX, kBoxY, kBoxW, kBoxH);
    const int title_w = text_renderer_.measureTextWidth(title);
    text_renderer_.drawText(display_, kBoxX + std::max(2, (kBoxW - title_w) / 2), kBoxY + 3, title);
    const int value_w = text_renderer_.measureTextWidth(value);
    text_renderer_.drawText(display_, kBoxX + std::max(2, (kBoxW - value_w) / 2), kBoxY + 14, value);
    if (is_action)
    {
        drawTextClipped(kBoxX + 2, kBoxY + 25, kBoxW - 4, "SELECT CONFIRM  BACK CANCEL");
    }
    else
    {
        drawTextClipped(kBoxX + 2, kBoxY + 25, kBoxW - 4, "ADJ ARROWS SEL OK BACK ESC");
    }
}

void Runtime::renderTransientPopup()
{
    const bool large_layout = display_.width() >= 160 && display_.height() >= 120;
    const int kBoxW = large_layout ? std::min(display_.width() - 24, 148) : 100;
    const int kBoxH = large_layout ? 44 : 28;
    const int kBoxX = large_layout ? std::max(0, (display_.width() - kBoxW) / 2) : 14;
    const int kBoxY = large_layout ? std::max(12, (display_.height() - kBoxH) / 2) : 18;
    display_.fillRect(kBoxX, kBoxY, kBoxW, kBoxH, false);
    drawFrame(display_, kBoxX, kBoxY, kBoxW, kBoxH);
    if (transient_popup_title_[0] != '\0')
    {
        drawTextClipped(kBoxX + 6, kBoxY + (large_layout ? 8 : 4),
                        kBoxW - 12, transient_popup_title_);
    }
    if (transient_popup_message_[0] != '\0')
    {
        drawTextClipped(kBoxX + 6, kBoxY + (large_layout ? 24 : 15),
                        kBoxW - 12, transient_popup_message_);
    }
}

void Runtime::renderInfoPage()
{
    char lines[32][40] = {};
    size_t line_count = 0;
    auto push_line = [&](const char* text)
    {
        if (!text || text[0] == '\0' || line_count >= arrayCount(lines))
        {
            return;
        }
        copyText(lines[line_count++], text);
    };
    auto push_kv = [&](const char* key, const char* value)
    {
        if (!key || !value || line_count >= arrayCount(lines))
        {
            return;
        }
        appendInfoLine(lines[line_count++], key, value);
    };

    char long_name[24] = {};
    char short_name[12] = {};
    app()->getEffectiveUserInfo(long_name, sizeof(long_name), short_name, sizeof(short_name));
    const auto& cfg = app()->getConfig();
    const auto battery = host_.battery_info_fn ? host_.battery_info_fn() : platform::ui::device::BatteryInfo{};
    const auto gps = host_.gps_data_fn ? host_.gps_data_fn() : platform::ui::gps::GpsState{};
    const auto ram = host_.ram_usage_fn ? host_.ram_usage_fn() : HostCallbacks::ResourceUsage{};
    const auto flash = host_.flash_usage_fn ? host_.flash_usage_fn() : HostCallbacks::ResourceUsage{};
#if !TRAILMATE_NRF52_BLE_DISABLED
    ble::BlePairingStatus ble_status{};
    const bool has_ble_status = loadBlePairingStatus(app(), &ble_status);
#endif

    push_line("[DEVICE]");
    push_kv("NAME", long_name[0] ? long_name : "-");
    push_kv("SHORT", short_name[0] ? short_name : "-");
    char self_id[16] = {};
    formatNodeLabel(self_id, sizeof(self_id));
    push_kv("NODE", self_id[0] ? self_id : "-");

    char value[40] = {};
    std::snprintf(value, sizeof(value), "%s", protocolLabel(cfg.mesh_protocol));
    push_kv("PROTO", value);
#if !TRAILMATE_NRF52_BLE_DISABLED
    push_kv("BLE", bleDisplayStateLabel(resolveBleDisplayState(app())));
    if (has_ble_status && ble_status.available && ble_status.requires_passkey)
    {
        push_kv("BLE MODE", blePairingModeLabel(ble_status));
        if (ble_status.passkey != 0)
        {
            std::snprintf(value, sizeof(value), "%06lu", static_cast<unsigned long>(ble_status.passkey));
            push_kv("BLE PIN", value);
        }
    }
#endif

    if (host_.format_frequency_fn)
    {
        char freq[24] = {};
        host_.format_frequency_fn(host_.active_lora_frequency_hz_fn ? host_.active_lora_frequency_hz_fn() : 0U,
                                  freq,
                                  sizeof(freq));
        push_kv("FREQ", freq[0] ? freq : "-");
    }

    push_line("[RADIO]");
    std::snprintf(value, sizeof(value), "%s", protocolLabel(cfg.mesh_protocol));
    push_kv("PROTO", value);
    push_kv("REGION", radioRegionLabel(cfg));
    std::snprintf(value, sizeof(value), "%ddBm", static_cast<int>(cfg.activeMeshConfig().tx_power));
    push_kv("TX", value);
    if (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
    {
        push_kv("MODEM", chat::meshtastic::presetDisplayName(
                             static_cast<meshtastic_Config_LoRaConfig_ModemPreset>(cfg.meshtastic_config.modem_preset)));
        formatMeshtasticChannelSlot(value, sizeof(value), cfg.meshtastic_config.channel_num);
        push_kv("CH SLOT", value);
        push_kv("ENCRYPT", encryptEnabled(cfg) ? "ON" : "OFF");
    }
    else
    {
        push_kv("PRESET", radioRegionLabel(cfg));
        push_kv("NAME", cfg.meshcore_config.meshcore_channel_name);
        std::snprintf(value, sizeof(value), "%u",
                      static_cast<unsigned>(cfg.meshcore_config.meshcore_channel_slot));
        push_kv("SLOT", value);
        push_kv("ENCRYPT", encryptEnabled(cfg) ? "ON" : "OFF");
    }

    push_line("[SYSTEM]");
#if !TRAILMATE_NRF52_BLE_DISABLED
    push_kv("BLE", bleDisplayStateLabel(resolveBleDisplayState(app())));
    if (has_ble_status && ble_status.available && ble_status.requires_passkey)
    {
        push_kv("BLE MODE", blePairingModeLabel(ble_status));
        if (ble_status.passkey != 0)
        {
            std::snprintf(value, sizeof(value), "%06lu", static_cast<unsigned long>(ble_status.passkey));
            push_kv("BLE PIN", value);
        }
    }
#endif
    if (battery.available && battery.level >= 0)
    {
        std::snprintf(value, sizeof(value), "%d%%%s", battery.level, battery.charging ? "+" : "");
        push_kv("BAT", value);
    }
    else
    {
        push_kv("BAT", "-");
    }
    char tz_label[16] = {};
    formatTimezoneLabel(host_.timezone_offset_min_fn ? host_.timezone_offset_min_fn() : 0, tz_label, sizeof(tz_label));
    push_kv("TZ", tz_label[0] ? tz_label : "UTC+0");

    char time_buf[16] = {};
    char date_buf[24] = {};
    formatTime(time_buf, sizeof(time_buf), date_buf, sizeof(date_buf));
    push_kv("TIME", time_buf[0] ? time_buf : "-");
    push_kv("DATE", date_buf[0] ? date_buf : "-");
    formatUptimeLabel(host_.millis_fn ? (host_.millis_fn() / 1000U) : 0U, value, sizeof(value));
    push_kv("UPTIME", value);
    if (ram.available && ram.total_bytes > 0)
    {
        std::snprintf(value, sizeof(value), "%lu/%luK",
                      static_cast<unsigned long>(ram.used_bytes / 1024U),
                      static_cast<unsigned long>(ram.total_bytes / 1024U));
        push_kv("SRAM", value);
    }
    if (flash.available && flash.total_bytes > 0)
    {
        const uint32_t flash_free_bytes =
            flash.total_bytes > flash.used_bytes ? (flash.total_bytes - flash.used_bytes) : 0U;
        std::snprintf(value, sizeof(value), "%lu/%luK free",
                      static_cast<unsigned long>(flash_free_bytes / 1024U),
                      static_cast<unsigned long>(flash.total_bytes / 1024U));
        push_kv("FLASH", value);
    }

    push_line("[GPS]");
    push_kv("GPS", cfg.gps_enabled ? "ON" : "OFF");
    push_kv("SATS", gpsSatMaskLabel(cfg.gps_sat_mask));
    std::snprintf(value, sizeof(value), "%lus", static_cast<unsigned long>(cfg.gps_interval_ms / 1000UL));
    push_kv("INT", value);
    push_kv("PWR", (host_.gps_powered_fn && host_.gps_powered_fn()) ? "ON" : "OFF");
    if (gps.valid)
    {
        std::snprintf(value, sizeof(value), "%.5f", gps.lat);
        push_kv("LAT", value);
        std::snprintf(value, sizeof(value), "%.5f", gps.lng);
        push_kv("LON", value);
        std::snprintf(value, sizeof(value), "%u", static_cast<unsigned>(gps.satellites));
        push_kv("SATS", value);
        if (gps.has_alt)
        {
            std::snprintf(value, sizeof(value), "%.0fm", gps.alt_m);
            push_kv("ALT", value);
        }
        if (gps.has_speed)
        {
            std::snprintf(value, sizeof(value), "%.1fkmh", gps.speed_mps * 3.6);
            push_kv("SPD", value);
        }
        if (gps.has_course)
        {
            std::snprintf(value, sizeof(value), "%.0fdeg", gps.course_deg);
            push_kv("CRS", value);
        }
        std::snprintf(value, sizeof(value), "%lus", static_cast<unsigned long>(gps.age / 1000UL));
        push_kv("AGE", value);
    }
    else
    {
        push_kv("FIX", "SEARCH");
    }

    const size_t page_size = visibleRowsFrom(10);
    const size_t max_scroll = line_count > page_size ? (line_count - page_size) : 0U;
    if (info_scroll_ > max_scroll)
    {
        info_scroll_ = max_scroll;
    }

    const size_t total_pages = std::max<size_t>(1U, (line_count + page_size - 1U) / page_size);
    const size_t current_page = (line_count > 0) ? ((info_scroll_ / page_size) + 1U) : 1U;
    char pos[16] = {};
    std::snprintf(pos, sizeof(pos), "%u/%u",
                  static_cast<unsigned>(current_page),
                  static_cast<unsigned>(total_pages));
    drawTitleBar("INFO", pos);

    const int line_h = text_renderer_.lineHeight();
    const int start_y = 10;
    const size_t visible = std::min(line_count - info_scroll_, page_size);
    for (size_t i = 0; i < visible; ++i)
    {
        drawTextClipped(0,
                        start_y + static_cast<int>(i * line_h),
                        display_.width(),
                        lines[info_scroll_ + i]);
    }
}

void Runtime::renderGnssPage()
{
    refreshGnssSnapshot();
    const auto& state = gnss_snapshot_state_;
    const auto& status = gnss_snapshot_status_;
    const std::size_t sat_count = gnss_snapshot_count_;
    const auto* sats = gnss_snapshot_sats_.data();
    char summary_lines[14][40] = {};
    size_t summary_count = 0;

    pushFormattedLine(summary_lines, summary_count, "USE:%u/%u",
                      static_cast<unsigned>(status.sats_in_use),
                      static_cast<unsigned>(status.sats_in_view > 0 ? status.sats_in_view : sat_count));
    pushFormattedLine(summary_lines, summary_count, "FIX:%s", gnssFixLabel(status.fix));
    pushFormattedLine(summary_lines, summary_count, "HDOP:%.1f", static_cast<double>(status.hdop));
    if ((host_.gps_enabled_fn && host_.gps_enabled_fn()) && (host_.gps_powered_fn && host_.gps_powered_fn()))
    {
        if (sat_count == 0)
        {
            pushFormattedLine(summary_lines, summary_count, "STATE:TIME ONLY");
        }
        else if (!state.valid)
        {
            pushFormattedLine(summary_lines, summary_count, "STATE:SEARCH FIX");
        }
        else
        {
            pushFormattedLine(summary_lines, summary_count, "STATE:LOCKED");
        }
    }
    else
    {
        pushFormattedLine(summary_lines, summary_count, "STATE:GPS OFF");
    }
    pushFormattedLine(summary_lines, summary_count, "EN:%s", (host_.gps_enabled_fn && host_.gps_enabled_fn()) ? "YES" : "NO");
    pushFormattedLine(summary_lines, summary_count, "PWR:%s", (host_.gps_powered_fn && host_.gps_powered_fn()) ? "YES" : "NO");
    pushFormattedLine(summary_lines, summary_count, "AGE:%lums", static_cast<unsigned long>(state.age));

    if (state.valid)
    {
        pushFormattedLine(summary_lines, summary_count, "LAT:%.5f", state.lat);
        pushFormattedLine(summary_lines, summary_count, "LON:%.5f", state.lng);
    }
    else
    {
        pushFormattedLine(summary_lines, summary_count, "LAT:-");
        pushFormattedLine(summary_lines, summary_count, "LON:-");
    }

    if (state.has_alt)
    {
        pushFormattedLine(summary_lines, summary_count, "ALT:%.0fm", state.alt_m);
    }
    else
    {
        pushFormattedLine(summary_lines, summary_count, "ALT:-");
    }

    if (state.has_speed)
    {
        pushFormattedLine(summary_lines, summary_count, "SPD:%.1fkmh", state.speed_mps * 3.6);
    }
    else
    {
        pushFormattedLine(summary_lines, summary_count, "SPD:-");
    }

    if (state.has_course)
    {
        pushFormattedLine(summary_lines, summary_count, "CRS:%.0f %s", state.course_deg, bearingCardinal(state.course_deg));
    }
    else
    {
        pushFormattedLine(summary_lines, summary_count, "CRS:-");
    }

    const uint32_t last_motion_ms = platform::ui::gps::last_motion_ms();
    if (last_motion_ms > 0)
    {
        const uint32_t age_s = nowMs() >= last_motion_ms ? (nowMs() - last_motion_ms) / 1000U : 0U;
        pushFormattedLine(summary_lines, summary_count, "MOVE:%lus", static_cast<unsigned long>(age_s));
    }
    else
    {
        pushFormattedLine(summary_lines, summary_count, "MOVE:-");
    }

    const size_t summary_page_size = visibleRowsFrom(10);
    const size_t sat_page_size = visibleRowsFrom(20);
    const size_t summary_pages = std::max<size_t>(1U, (summary_count + summary_page_size - 1U) / summary_page_size);
    const size_t sat_pages = std::max<size_t>(1U, (sat_count + sat_page_size - 1U) / sat_page_size);
    const size_t total_pages = summary_pages + sat_pages;
    if (gnss_page_index_ >= total_pages)
    {
        gnss_page_index_ = total_pages - 1U;
    }

    char pos[16] = {};
    std::snprintf(pos, sizeof(pos), "%u/%u",
                  static_cast<unsigned>(gnss_page_index_ + 1U),
                  static_cast<unsigned>(total_pages));
    drawTitleBar("GPS", pos);

    const int line_h = text_renderer_.lineHeight();

    if (gnss_page_index_ < summary_pages)
    {
        const size_t start = gnss_page_index_ * summary_page_size;
        const size_t visible = std::min(summary_page_size, summary_count - std::min(start, summary_count));
        for (size_t i = 0; i < visible; ++i)
        {
            text_renderer_.drawText(display_, 0, 10 + static_cast<int>(i * line_h), summary_lines[start + i]);
        }
        return;
    }

    constexpr int kSatHeaderY = 10;
    constexpr int kSatRowY = 20;
    const bool wide = display_.width() >= 160;
    const int sys_x = 0;
    const int sys_w = wide ? 28 : 16;
    const int id_x = wide ? 30 : 18;
    const int id_w = wide ? 18 : 14;
    const int use_x = wide ? 50 : 34;
    const int use_w = wide ? 22 : 18;
    const int snr_x = wide ? 74 : 54;
    const int snr_w = wide ? 28 : 22;
    const int elv_x = wide ? 104 : 78;
    const int elv_w = wide ? 28 : 20;
    const int azi_x = wide ? 134 : 100;
    const int azi_w = wide ? 32 : 28;
    const int sig_x = 168;

    display_.fillRect(0, kSatHeaderY, display_.width(), line_h, true);
    drawTextClipped(sys_x, kSatHeaderY, sys_w, "SAT", true);
    drawTextClipped(id_x, kSatHeaderY, id_w, "ID", true);
    drawTextClipped(use_x, kSatHeaderY, use_w, "USE", true);
    drawTextClipped(snr_x, kSatHeaderY, snr_w, "SNR", true);
    drawTextClipped(elv_x, kSatHeaderY, elv_w, "ELV", true);
    drawTextClipped(azi_x, kSatHeaderY, azi_w, "AZI", true);
    if (wide)
    {
        drawTextClipped(sig_x, kSatHeaderY, display_.width() - sig_x, "SIG", true);
    }
    const size_t sat_page = gnss_page_index_ - summary_pages;
    const size_t sat_start = sat_page * sat_page_size;
    const size_t sat_visible = std::min(sat_page_size, sat_count - std::min(sat_start, sat_count));
    if (sat_visible == 0)
    {
        text_renderer_.drawText(display_, 0, 22, "NO SATELLITE DATA");
        return;
    }

    for (size_t i = 0; i < sat_visible; ++i)
    {
        const auto& sat = sats[sat_start + i];
        const int row_y = kSatRowY + static_cast<int>(i * line_h);
        char cell[12] = {};

        std::snprintf(cell, sizeof(cell), "%s", gnssSystemLabel(sat.sys));
        drawTextClipped(sys_x, row_y, sys_w, cell);

        std::snprintf(cell, sizeof(cell), "%02u", static_cast<unsigned>(sat.id));
        drawTextClipped(id_x, row_y, id_w, cell);

        std::snprintf(cell, sizeof(cell), "%c", sat.used ? 'Y' : '-');
        drawTextClipped(use_x, row_y, use_w, cell);

        const int snr = static_cast<int>(sat.snr >= 0 ? sat.snr : 0);
        std::snprintf(cell, sizeof(cell), "%03d", snr);
        drawTextClipped(snr_x, row_y, snr_w, cell);

        std::snprintf(cell, sizeof(cell), "%02u", static_cast<unsigned>(sat.elevation));
        drawTextClipped(elv_x, row_y, elv_w, cell);

        std::snprintf(cell, sizeof(cell), "%03u", static_cast<unsigned>(sat.azimuth));
        drawTextClipped(azi_x, row_y, azi_w, cell);

        if (wide)
        {
            const int bars = snr >= 40 ? 5 : snr >= 30 ? 4
                                         : snr >= 20   ? 3
                                         : snr >= 10   ? 2
                                         : snr > 0     ? 1
                                                       : 0;
            for (int bar = 0; bar < bars; ++bar)
            {
                const int bar_h = 2 + bar * 2;
                const int bar_x = sig_x + 2 + bar * 4;
                const int bar_y = row_y + line_h - 1 - bar_h;
                display_.fillRect(bar_x, bar_y, 3, bar_h, true);
            }
        }
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
    if (page == Page::Screensaver)
    {
        last_screensaver_render_ms_ = 0;
    }
    if (page == Page::Screensaver || page == Page::GnssPage)
    {
        refreshGnssSnapshot(true);
    }
    if (page == Page::ChatList)
    {
        rebuildConversationList();
        chat_list_index_ = std::min(chat_list_index_, conversation_count_);
    }
    else if (page == Page::NewChatPage)
    {
        new_chat_index_ = std::min(new_chat_index_, arrayCount(kNewChatItems) - 1U);
    }
    else if (page == Page::NodeList)
    {
        rebuildNodeList();
        node_list_index_ = std::min(node_list_index_, node_count_ == 0 ? 0U : node_count_ - 1U);
    }
    else if (page == Page::NodeInfo)
    {
        node_info_scroll_ = 0;
        buildNodeInfo();
    }
    else if (page == Page::NodeCompass)
    {
        node_action_index_ = std::min(node_action_index_, nodeActionCount() - 1U);
    }
    else if (page == Page::Conversation)
    {
        if (app())
        {
            app()->getChatService().markConversationRead(active_conversation_);
        }
        rebuildMessages();
        message_focus_started_ms_ = nowMs();
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
    else if (page == Page::DiscoverPage)
    {
        discover_index_ = 0;
    }
    else if (page == Page::InfoPage)
    {
        info_scroll_ = 0;
    }
    else if (page == Page::GnssPage)
    {
        gnss_page_index_ = 0;
    }
}

void Runtime::openCompose(EditTarget target, const char* seed_text)
{
    edit_target_ = target;
    page_before_compose_ = page_;
    compose_buffer_[0] = '\0';
    compose_len_ = 0;
    compose_charset_index_ = 0;
    if (target == EditTarget::Message && usesPhysicalTextInput())
    {
        compose_mode_ = ComposeMode::Cn;
    }
    else if (editUsesKeyCharset())
    {
        compose_mode_ = ComposeMode::AbcUpper;
    }
    else
    {
        compose_mode_ = ComposeMode::AbcLower;
    }
    compose_focus_ = ComposeFocus::Body;
    compose_action_index_ = 0;
    compose_abc_group_index_ = 0;
    compose_abc_tap_index_ = 0;
    compose_abc_last_tap_ms_ = 0;
    compose_abc_last_group_index_ = -1;
    compose_physical_last_key_ = '\0';
    compose_preedit_[0] = '\0';
    compose_preedit_len_ = 0;
    clearComposeT9State();
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
    if (accept && !saveEditedTextToConfig())
    {
        return;
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

void Runtime::rebuildNodeList()
{
    node_count_ = 0;
    if (!app())
    {
        return;
    }

    auto contacts = app()->getContactService().getContacts();
    auto nearby = app()->getContactService().getNearby();
    auto ignored = app()->getContactService().getIgnoredNodes();
    contacts.insert(contacts.end(), nearby.begin(), nearby.end());
    contacts.insert(contacts.end(), ignored.begin(), ignored.end());
    std::sort(contacts.begin(), contacts.end(),
              [](const chat::contacts::NodeInfo& a, const chat::contacts::NodeInfo& b)
              {
                  if (a.last_seen != b.last_seen)
                  {
                      return a.last_seen > b.last_seen;
                  }
                  return a.node_id < b.node_id;
              });

    node_count_ = std::min(contacts.size(), static_cast<size_t>(kMaxNodeItems));
    for (size_t i = 0; i < node_count_; ++i)
    {
        nodes_[i] = contacts[i];
    }

    if (node_count_ == 0)
    {
        node_list_index_ = 0;
    }
    else if (node_list_index_ >= node_count_)
    {
        node_list_index_ = node_count_ - 1U;
    }
}

void Runtime::buildNodeInfo()
{
    node_info_count_ = 0;
    if (node_count_ == 0)
    {
        return;
    }

    const size_t index = std::min(node_list_index_, node_count_ - 1U);
    const auto& node = nodes_[index];

    auto push_line = [this](const char* text)
    {
        if (!text || text[0] == '\0' || node_info_count_ >= kNodeInfoLines)
        {
            return;
        }
        copyText(node_info_lines_[node_info_count_], text);
        ++node_info_count_;
    };

    auto push_kv = [this](const char* key, const char* value)
    {
        if (!key || !value || node_info_count_ >= kNodeInfoLines)
        {
            return;
        }
        appendInfoLine(node_info_lines_[node_info_count_], key, value);
        ++node_info_count_;
    };

    auto push_section = [this](const char* title)
    {
        if (!title || node_info_count_ >= kNodeInfoLines)
        {
            return;
        }
        setInfoSection(node_info_lines_[node_info_count_], title);
        ++node_info_count_;
    };

    char value[40] = {};
    push_section("NODE");
    std::snprintf(value, sizeof(value), "%08lX", static_cast<unsigned long>(node.node_id));
    push_kv("ID", value);
    push_kv("NM", node.display_name.empty() ? "-" : node.display_name.c_str());
    push_kv("SH", node.short_name[0] != '\0' ? node.short_name : "-");
    push_kv("LN", node.long_name[0] != '\0' ? node.long_name : "-");

    push_section("LINK");
    push_kv("P", node.protocol == chat::contacts::NodeProtocolType::MeshCore ? "MC" : node.protocol == chat::contacts::NodeProtocolType::Meshtastic ? "MT"
                                                                                                                                                    : "?");
    std::snprintf(value, sizeof(value), "%u", static_cast<unsigned>(node.hops_away));
    push_kv("HP", node.hops_away == 0xFF ? "-" : value);
    std::snprintf(value, sizeof(value), "%u", static_cast<unsigned>(node.channel));
    push_kv("CH", node.channel == 0xFF ? "-" : value);
    std::snprintf(value, sizeof(value), "%.1f", static_cast<double>(node.snr));
    push_kv("SN", value);
    std::snprintf(value, sizeof(value), "%.1f", static_cast<double>(node.rssi));
    push_kv("RS", value);
    formatTimestamp(value, sizeof(value), node.last_seen);
    push_kv("SEEN", value[0] != '\0' ? value : "-");
    formatElapsedShort(host_.utc_now_fn ? host_.utc_now_fn() : 0, node.last_seen, value, sizeof(value));
    push_kv("AGO", value);
    push_kv("SIG", signalRatingLabel(node.snr, node.rssi));
    std::snprintf(value, sizeof(value), "%u", static_cast<unsigned>(node.hw_model));
    push_kv("HW", node.hw_model == 0 ? "-" : value);
    std::snprintf(value, sizeof(value), "%02X", static_cast<unsigned>(node.next_hop));
    push_kv("NH", node.next_hop == 0 ? "-" : value);
    push_kv("MQTT", node.via_mqtt ? "Y" : "N");
    push_kv("IGN", node.is_ignored ? "Y" : "N");
    if (node.has_macaddr)
    {
        char mac_buf[24];
        std::snprintf(mac_buf,
                      sizeof(mac_buf),
                      "%02X:%02X:%02X:%02X:%02X:%02X",
                      static_cast<unsigned>(node.macaddr[0]),
                      static_cast<unsigned>(node.macaddr[1]),
                      static_cast<unsigned>(node.macaddr[2]),
                      static_cast<unsigned>(node.macaddr[3]),
                      static_cast<unsigned>(node.macaddr[4]),
                      static_cast<unsigned>(node.macaddr[5]));
        push_kv("MAC", mac_buf);
    }
    push_kv("PKI", node.key_manually_verified ? "VERIFIED" : (node.has_public_key ? "KNOWN" : "-"));
    if (node.has_device_metrics)
    {
        if (node.device_metrics.has_battery_level)
        {
            std::snprintf(value, sizeof(value), "%lu%%",
                          static_cast<unsigned long>(node.device_metrics.battery_level));
            push_kv("BAT", value);
        }
        if (node.device_metrics.has_voltage)
        {
            std::snprintf(value, sizeof(value), "%.2fV", static_cast<double>(node.device_metrics.voltage));
            push_kv("V", value);
        }
        if (node.device_metrics.has_channel_utilization)
        {
            std::snprintf(value, sizeof(value), "%.1f%%",
                          static_cast<double>(node.device_metrics.channel_utilization));
            push_kv("UTIL", value);
        }
        if (node.device_metrics.has_air_util_tx)
        {
            std::snprintf(value, sizeof(value), "%.1f%%",
                          static_cast<double>(node.device_metrics.air_util_tx));
            push_kv("AIR", value);
        }
        if (node.device_metrics.has_uptime_seconds)
        {
            std::snprintf(value, sizeof(value), "%lu",
                          static_cast<unsigned long>(node.device_metrics.uptime_seconds));
            push_kv("UP", value);
        }
    }

    push_section("POS");
    if (node.position.valid)
    {
        std::snprintf(value, sizeof(value), "%.5f", static_cast<double>(node.position.latitude_i) / 1e7);
        push_kv("LAT", value);
        std::snprintf(value, sizeof(value), "%.5f", static_cast<double>(node.position.longitude_i) / 1e7);
        push_kv("LON", value);
        if (node.position.has_altitude)
        {
            std::snprintf(value, sizeof(value), "%ldm", static_cast<long>(node.position.altitude));
            push_kv("ALT", value);
        }
        formatTimestamp(value, sizeof(value), node.position.timestamp);
        push_kv("TIME", value[0] != '\0' ? value : "-");
    }
    else
    {
        push_line("NO POSITION");
    }

    const auto gps = host_.gps_data_fn ? host_.gps_data_fn() : platform::ui::gps::GpsState{};
    if (gps.valid && node.position.valid)
    {
        const double node_lat = static_cast<double>(node.position.latitude_i) / 1e7;
        const double node_lon = static_cast<double>(node.position.longitude_i) / 1e7;
        const double dist_m = haversineMeters(gps.lat, gps.lng, node_lat, node_lon);
        const double brg_deg = bearingDegrees(gps.lat, gps.lng, node_lat, node_lon);

        push_section("NAV");
        if (dist_m < 1000.0)
        {
            std::snprintf(value, sizeof(value), "%.0fm", dist_m);
        }
        else
        {
            std::snprintf(value, sizeof(value), "%.2fkm", dist_m / 1000.0);
        }
        push_kv("DST", value);

        std::snprintf(value, sizeof(value), "%s %.0f", bearingCardinal(brg_deg), brg_deg);
        push_kv("BRG", value);

        if (gps.has_course)
        {
            const double rel = normalizeBearingDeg(brg_deg - gps.course_deg);
            std::snprintf(value, sizeof(value), "%.0f", rel);
            push_kv("REL", value);
        }
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
        messages_[i] = list[message_count_ - 1U - i];
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

    auto fitUtf8Bytes = [](const char* text, size_t max_bytes)
    {
        if (!text || max_bytes == 0)
        {
            return static_cast<size_t>(0);
        }

        size_t used = 0;
        while (text[used] != '\0')
        {
            const size_t char_len = utf8CharLength(static_cast<unsigned char>(text[used]));
            if (used + char_len > max_bytes)
            {
                break;
            }
            used += char_len;
        }
        return used;
    };

    auto push_text_block = [this, &push_line, &fitUtf8Bytes](const char* value)
    {
        if (!value || message_info_count_ >= kMessageInfoLines)
        {
            return;
        }

        const int available_width = std::max(1, display_.width());
        const size_t storage_limit = std::max<size_t>(1U, kMessageInfoWidth - 1U);

        const char* cursor = value;
        while (message_info_count_ < kMessageInfoLines)
        {
            const char* logical_end = cursor;
            while (*logical_end != '\0' && *logical_end != '\n' && *logical_end != '\r')
            {
                ++logical_end;
            }

            const size_t logical_len = static_cast<size_t>(logical_end - cursor);
            if (logical_len == 0)
            {
                push_line(" ");
            }
            else
            {
                std::string logical_line(cursor, logical_len);
                const char* wrapped = logical_line.c_str();
                while (*wrapped != '\0' && message_info_count_ < kMessageInfoLines)
                {
                    size_t keep_bytes = text_renderer_.clipTextToWidth(wrapped, available_width);
                    if (keep_bytes == 0)
                    {
                        keep_bytes = utf8CharLength(static_cast<unsigned char>(*wrapped));
                    }
                    keep_bytes = std::min(keep_bytes, fitUtf8Bytes(wrapped, storage_limit));
                    if (keep_bytes == 0)
                    {
                        keep_bytes = std::min(storage_limit, static_cast<size_t>(1));
                    }

                    char chunk[kMessageInfoWidth] = {};
                    std::memcpy(chunk, wrapped, std::min(keep_bytes, sizeof(chunk) - 1));
                    chunk[std::min(keep_bytes, sizeof(chunk) - 1)] = '\0';
                    push_line(chunk);
                    wrapped += keep_bytes;
                }
            }

            if (*logical_end == '\0')
            {
                break;
            }

            if (*logical_end == '\r' && logical_end[1] == '\n')
            {
                cursor = logical_end + 2;
            }
            else
            {
                cursor = logical_end + 1;
            }
        }
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

        const time_t adjusted = applyLocalTimezone(host_, static_cast<time_t>(timestamp_s));
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
        push_section("TXT");
        push_text_block(msg->text.c_str());
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

    if (!app()->getChatService().canSendToConversation(active_conversation_))
    {
        showTransientPopup("MESSAGE", "VIEW ONLY", 2000U);
        return;
    }

    const chat::MessageId msg_id =
        app()->getChatService().sendTextToConversation(active_conversation_,
                                                       compose_buffer_);
    if (msg_id == 0)
    {
        showTransientPopup("MESSAGE", "SEND FAILED", 2000U);
    }
    finishTextEdit(false);
    enterPage(Page::Conversation);
}

void Runtime::retrySelectedMessage()
{
    const chat::ChatMessage* msg = selectedMessage();
    if (!app() || !msg)
    {
        showTransientPopup("MESSAGE", "NO MESSAGE", 1600U);
        return;
    }
    if (msg->from != 0 || msg->status != chat::MessageStatus::Failed || msg->msg_id == 0)
    {
        showTransientPopup("MESSAGE", "NOT RETRYABLE", 1800U);
        return;
    }
    if (!app()->getChatService().canSendToConversation(
            chat::ConversationId(msg->channel, msg->peer, msg->protocol)))
    {
        showTransientPopup("MESSAGE", "VIEW ONLY", 2000U);
        return;
    }

    if (!app()->getChatService().resendFailed(msg->msg_id))
    {
        showTransientPopup("MESSAGE", "RETRY FAILED", 2000U);
        return;
    }

    showTransientPopup("MESSAGE", "RETRY QUEUED", 1600U);
    rebuildMessages();
    enterPage(Page::Conversation);
}

void Runtime::executeNewChatPageItem(size_t index)
{
    if (index >= arrayCount(kNewChatItems) - 1U)
    {
        enterPage(Page::ChatList);
        return;
    }
    if (!app())
    {
        showTransientPopup("NEW CHAT", "APP NOT READY");
        return;
    }
    active_conversation_ = chat::ConversationId(
        index == 1U ? chat::ChannelId::SECONDARY : chat::ChannelId::PRIMARY,
        0,
        app()->getConfig().mesh_protocol);
    if (!app()->getChatService().canSendToConversation(active_conversation_))
    {
        showTransientPopup("NEW CHAT", "VIEW ONLY");
        return;
    }
    openCompose(EditTarget::Message);
}

void Runtime::executeDiscoverPageItem(size_t index)
{
    if (index >= arrayCount(kDiscoverItems) - 1U)
    {
        enterPage(Page::MainMenu);
        return;
    }
    if (!app())
    {
        showTransientPopup("DISCOVER", "APP NOT READY");
        return;
    }
    if (app()->getConfig().mesh_protocol != chat::MeshProtocol::MeshCore)
    {
        showTransientPopup("DISCOVER", "MESHCORE ONLY");
        return;
    }

    chat::MeshDiscoveryAction action = chat::MeshDiscoveryAction::ScanLocal;
    const char* success = "SENT";
    const char* boot_log = "discover sent";
    switch (index)
    {
    case 0:
        action = chat::MeshDiscoveryAction::ScanLocal;
        success = "SCANNING 5S";
        boot_log = "discover scan";
        break;
    case 1:
        action = chat::MeshDiscoveryAction::SendIdLocal;
        success = "ID SENT LOCAL";
        boot_log = "discover id local";
        break;
    case 2:
        action = chat::MeshDiscoveryAction::SendIdBroadcast;
        success = "ID SENT BROADCAST";
        boot_log = "discover id bcast";
        break;
    default:
        break;
    }

    const chat::MeshActionResult result =
        app()->getChatService().triggerDiscoveryActionDetailed(action);
    if (result.ok)
    {
        appendBootLog(boot_log);
        showTransientPopup("DISCOVER", success, index == 0 ? 2200U : 1800U);
        return;
    }

    appendBootLog("discover failed");
    showTransientPopup("DISCOVER", meshOperationFailureLabel(result.failure));
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

void Runtime::ensureSleepTimeout(InputAction action)
{
    if (page_ == Page::BootLog || page_ == Page::Sleep)
    {
        return;
    }

    const uint32_t now = nowMs();
    if (action != InputAction::None)
    {
        return;
    }

    if (platform::ui::screen::is_sleep_disabled())
    {
        return;
    }

    const uint32_t timeout_ms = platform::ui::screen::timeout_ms();
    if (timeout_ms >= kScreenTimeoutAlwaysMs)
    {
        return;
    }

    if ((now - last_interaction_ms_) < timeout_ms)
    {
        return;
    }

    page_before_sleep_ = page_;
    enterPage(Page::Sleep);
}

void Runtime::beginSettingPopup(Page owner, size_t index)
{
    if (!app())
    {
        return;
    }
    setting_popup_active_ = true;
    setting_popup_owner_ = owner;
    setting_popup_index_ = index;
    setting_popup_config_ = app()->getConfig();
#if !TRAILMATE_NRF52_BLE_DISABLED
    setting_popup_ble_enabled_ = app()->isBleEnabled();
#endif
    setting_popup_screen_timeout_ms_ = platform::ui::screen::timeout_ms();
    setting_popup_timezone_min_ = host_.timezone_offset_min_fn ? host_.timezone_offset_min_fn() : 0;
    setting_popup_message_tone_volume_ = host_.message_tone_volume_fn ? host_.message_tone_volume_fn() : 0;
    setting_popup_message_light_enabled_ = host_.message_light_enabled_fn ? host_.message_light_enabled_fn() : true;
    setting_popup_status_led_color_index_ = host_.status_led_color_index_fn ? host_.status_led_color_index_fn() : 0;
    setting_popup_keyboard_light_enabled_ = host_.keyboard_light_enabled_fn ? host_.keyboard_light_enabled_fn() : false;
}

void Runtime::cancelSettingPopup()
{
    setting_popup_active_ = false;
}

void Runtime::confirmSettingPopup()
{
    if (!setting_popup_active_ || !app())
    {
        return;
    }

    if (setting_popup_owner_ == Page::ActionPage)
    {
        const size_t action = setting_popup_index_;
        setting_popup_active_ = false;
        executeActionPageItem(action);
        return;
    }

    if (setting_popup_owner_ == Page::RadioSettings && setting_popup_index_ == 0)
    {
        sanitizeMeshtasticChannelNum(setting_popup_config_);
        const chat::MeshProtocol target_protocol = setting_popup_config_.mesh_protocol;
        if (target_protocol != app()->getConfig().mesh_protocol)
        {
            if (!app()->switchMeshProtocol(target_protocol, true))
            {
                appendStatus(this, "proto fail");
                return;
            }

            setting_popup_config_ = app()->getConfig();
            radio_index_ = 0;
            char value[32] = {};
            formatSettingPopupValue(value, sizeof(value));
            appendStatus(this, "%s", value);
            setting_popup_active_ = false;
            return;
        }
    }

    auto& cfg = app()->getConfig();
    sanitizeMeshtasticChannelNum(setting_popup_config_);
    cfg = setting_popup_config_;
#if !TRAILMATE_NRF52_BLE_DISABLED
    cfg.ble_enabled = setting_popup_ble_enabled_;
#endif
    if (host_.set_timezone_offset_min_fn)
    {
        host_.set_timezone_offset_min_fn(setting_popup_timezone_min_);
    }
    if (host_.set_message_tone_volume_fn)
    {
        host_.set_message_tone_volume_fn(setting_popup_message_tone_volume_);
    }
    if (host_.set_message_light_enabled_fn)
    {
        host_.set_message_light_enabled_fn(setting_popup_message_light_enabled_);
    }
    if (host_.set_status_led_color_index_fn)
    {
        host_.set_status_led_color_index_fn(setting_popup_status_led_color_index_);
    }
    if (host_.set_keyboard_light_enabled_fn)
    {
        host_.set_keyboard_light_enabled_fn(setting_popup_keyboard_light_enabled_);
    }
    platform::ui::screen::set_timeout_ms(setting_popup_screen_timeout_ms_);
#if !TRAILMATE_NRF52_BLE_DISABLED
    app()->setBleEnabled(setting_popup_ble_enabled_);
#endif
    app()->saveConfig();
    if (setting_popup_owner_ == Page::RadioSettings)
    {
        app()->applyMeshConfig();
    }
    if (setting_popup_owner_ == Page::DeviceSettings)
    {
        app()->applyPositionConfig();
    }

    char value[32] = {};
    formatSettingPopupValue(value, sizeof(value));
    appendStatus(this, "%s", value);
    setting_popup_active_ = false;
}

void Runtime::adjustSettingPopup(int delta)
{
    if (!setting_popup_active_ || !app() || delta == 0)
    {
        return;
    }

    if (setting_popup_owner_ == Page::ActionPage)
    {
        return;
    }

    if (setting_popup_owner_ == Page::RadioSettings)
    {
        auto& cfg = setting_popup_config_;
        switch (radioSettingItem(setting_popup_index_))
        {
        case RadioSettingItem::Protocol:
            cfg.mesh_protocol = (cfg.mesh_protocol == chat::MeshProtocol::Meshtastic)
                                    ? chat::MeshProtocol::MeshCore
                                    : chat::MeshProtocol::Meshtastic;
            break;
        case RadioSettingItem::MtRegion:
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
                index = static_cast<size_t>(
                    clampValue<int>(static_cast<int>(index) + delta, 0, static_cast<int>(count) - 1));
                cfg.meshtastic_config.region = static_cast<uint8_t>(table[index].code);
            }
            break;
        }
        case RadioSettingItem::MtMode:
            cfg.meshtastic_config.use_preset = !cfg.meshtastic_config.use_preset;
            break;
        case RadioSettingItem::MtPreset:
        {
            constexpr int kPresetMin = static_cast<int>(meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST);
            constexpr int kPresetMax = static_cast<int>(meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO);
            cfg.meshtastic_config.modem_preset = static_cast<uint8_t>(clampValue(
                static_cast<int>(cfg.meshtastic_config.modem_preset) + delta, kPresetMin, kPresetMax));
            cfg.meshtastic_config.use_preset = true;
            break;
        }
        case RadioSettingItem::MtBandwidth:
            cfg.meshtastic_config.bandwidth_khz =
                stepOption(cfg.meshtastic_config.bandwidth_khz, kLoRaBandwidthOptionsKhz, delta);
            cfg.meshtastic_config.use_preset = false;
            break;
        case RadioSettingItem::MtSpreadFactor:
            cfg.meshtastic_config.spread_factor = static_cast<uint8_t>(
                clampValue<int>(static_cast<int>(cfg.meshtastic_config.spread_factor) + delta, 5, 12));
            cfg.meshtastic_config.use_preset = false;
            break;
        case RadioSettingItem::MtCodingRate:
            cfg.meshtastic_config.coding_rate = static_cast<uint8_t>(
                clampValue<int>(static_cast<int>(cfg.meshtastic_config.coding_rate) + delta, 5, 8));
            cfg.meshtastic_config.use_preset = false;
            break;
        case RadioSettingItem::MtTxPower:
            cfg.meshtastic_config.tx_power = static_cast<int8_t>(clampValue<int>(
                static_cast<int>(cfg.meshtastic_config.tx_power) + delta,
                static_cast<int>(app::AppConfig::kTxPowerMinDbm),
                static_cast<int>(app::AppConfig::kTxPowerMaxDbm)));
            break;
        case RadioSettingItem::MtChannelSlot:
        {
            const int current = static_cast<int>(normalizedMeshtasticChannelNum(cfg.meshtastic_config.channel_num));
            cfg.meshtastic_config.channel_num = static_cast<uint16_t>(clampValue<int>(
                current + delta, 0, static_cast<int>(kMeshtasticChannelNumMax)));
            break;
        }
        case RadioSettingItem::MtPrimaryKey:
            break;
        case RadioSettingItem::MtOverrideFrequency:
            if (cfg.meshtastic_config.override_frequency_mhz <= 0.0f)
            {
                if (delta > 0)
                {
                    cfg.meshtastic_config.override_frequency_mhz =
                        clampValue(chat::meshtastic::estimateFrequencyMhz(cfg.meshtastic_config.region,
                                                                          cfg.meshtastic_config.modem_preset),
                                   kRadioFrequencyMinMhz,
                                   kRadioFrequencyMaxMhz);
                }
            }
            else
            {
                const float next = cfg.meshtastic_config.override_frequency_mhz +
                                   (static_cast<float>(delta) * kRadioFrequencyStepMhz);
                if (next < kRadioFrequencyMinMhz)
                {
                    cfg.meshtastic_config.override_frequency_mhz = 0.0f;
                }
                else
                {
                    cfg.meshtastic_config.override_frequency_mhz =
                        clampValue(next, kRadioFrequencyMinMhz, kRadioFrequencyMaxMhz);
                }
            }
            break;
        case RadioSettingItem::MtFrequencyOffset:
            cfg.meshtastic_config.frequency_offset_mhz =
                stepClampedFloat(cfg.meshtastic_config.frequency_offset_mhz,
                                 delta,
                                 kMeshtasticFrequencyOffsetStepMhz,
                                 kMeshtasticFrequencyOffsetMinMhz,
                                 kMeshtasticFrequencyOffsetMaxMhz);
            break;
        case RadioSettingItem::Encrypt:
            setEncryptEnabled(cfg, !encryptEnabled(cfg));
            break;
        case RadioSettingItem::McRegion:
            adjustMeshCoreRegionPreset(cfg.meshcore_config, delta);
            break;
        case RadioSettingItem::McFrequency:
            cfg.meshcore_config.meshcore_freq_mhz =
                stepClampedFloat(cfg.meshcore_config.meshcore_freq_mhz <= 0.0f ? 869.525f
                                                                               : cfg.meshcore_config.meshcore_freq_mhz,
                                 delta,
                                 kRadioFrequencyStepMhz,
                                 kRadioFrequencyMinMhz,
                                 kRadioFrequencyMaxMhz);
            break;
        case RadioSettingItem::McBandwidth:
            cfg.meshcore_config.meshcore_bw_khz =
                stepOption(cfg.meshcore_config.meshcore_bw_khz, kLoRaBandwidthOptionsKhz, delta);
            break;
        case RadioSettingItem::McSpreadFactor:
            cfg.meshcore_config.meshcore_sf = static_cast<uint8_t>(
                clampValue<int>(static_cast<int>(cfg.meshcore_config.meshcore_sf) + delta, 5, 12));
            break;
        case RadioSettingItem::McCodingRate:
            cfg.meshcore_config.meshcore_cr = static_cast<uint8_t>(
                clampValue<int>(static_cast<int>(cfg.meshcore_config.meshcore_cr) + delta, 5, 8));
            break;
        case RadioSettingItem::McTxPower:
            cfg.meshcore_config.tx_power = static_cast<int8_t>(clampValue<int>(
                static_cast<int>(cfg.meshcore_config.tx_power) + delta,
                static_cast<int>(app::AppConfig::kTxPowerMinDbm),
                static_cast<int>(app::AppConfig::kTxPowerMaxDbm)));
            break;
        case RadioSettingItem::McChannelSlot:
            cfg.meshcore_config.meshcore_channel_slot = static_cast<uint8_t>(clampValue<int>(
                static_cast<int>(cfg.meshcore_config.meshcore_channel_slot) + delta, 0, 15));
            break;
        case RadioSettingItem::McChannelName:
        case RadioSettingItem::McChannelKey:
        default:
            break;
        }
        return;
    }

    if (setting_popup_owner_ == Page::DeviceSettings)
    {
        switch (deviceSettingItem(setting_popup_index_))
        {
#if !TRAILMATE_NRF52_BLE_DISABLED
        case DeviceSettingItem::Ble:
            setting_popup_ble_enabled_ = !setting_popup_ble_enabled_;
            break;
#endif
#if TRAILMATE_NRF_MONO_TARGET_MESSAGE_SOUND_SETTING
        case DeviceSettingItem::MessageTone:
            setting_popup_message_tone_volume_ = setting_popup_message_tone_volume_ > 0 ? 0U : 45U;
            break;
#endif
#if TRAILMATE_NRF_MONO_TARGET_DEVICE_IO_SETTINGS
        case DeviceSettingItem::MessageLight:
            setting_popup_message_light_enabled_ = !setting_popup_message_light_enabled_;
            break;
        case DeviceSettingItem::LedColor:
        {
            const uint8_t count = host_.status_led_color_count_fn ? host_.status_led_color_count_fn() : 0;
            if (count > 0)
            {
                int next = static_cast<int>(setting_popup_status_led_color_index_) + delta;
                while (next < 0)
                {
                    next += count;
                }
                setting_popup_status_led_color_index_ = static_cast<uint8_t>(next % count);
            }
            break;
        }
        case DeviceSettingItem::KeyboardLight:
            setting_popup_keyboard_light_enabled_ = !setting_popup_keyboard_light_enabled_;
            break;
#endif
        case DeviceSettingItem::Gps:
            setting_popup_config_.gps_enabled = !setting_popup_config_.gps_enabled;
            break;
        case DeviceSettingItem::Sats:
            setting_popup_config_.gps_sat_mask = nextGpsSatMask(setting_popup_config_.gps_sat_mask, delta);
            break;
        case DeviceSettingItem::GpsInterval:
        {
            static constexpr uint32_t kGpsIntervals[] = {15000UL, 30000UL, 60000UL, 300000UL, 600000UL};
            size_t index = 0;
            while (index + 1 < arrayCount(kGpsIntervals) &&
                   kGpsIntervals[index] < setting_popup_config_.gps_interval_ms)
            {
                ++index;
            }
            const int next = clampValue<int>(static_cast<int>(index) + delta, 0, static_cast<int>(arrayCount(kGpsIntervals)) - 1);
            setting_popup_config_.gps_interval_ms = kGpsIntervals[next];
            break;
        }
        case DeviceSettingItem::ScreenOff:
            setting_popup_screen_timeout_ms_ = stepScreenTimeoutMs(setting_popup_screen_timeout_ms_, delta);
            break;
        case DeviceSettingItem::TimeZone:
            setting_popup_timezone_min_ = clampValue(setting_popup_timezone_min_ + delta * kTimezoneStep,
                                                     kTimezoneMin,
                                                     kTimezoneMax);
            break;
        default:
            break;
        }
    }
}

void Runtime::formatSettingPopupValue(char* out, size_t out_len) const
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';

    if (setting_popup_owner_ == Page::RadioSettings)
    {
        const auto& cfg = setting_popup_config_;
        formatRadioSettingValue(cfg, radioSettingItem(setting_popup_index_), out, out_len);
        return;
    }

    if (setting_popup_owner_ == Page::DeviceSettings)
    {
        switch (deviceSettingItem(setting_popup_index_))
        {
#if !TRAILMATE_NRF52_BLE_DISABLED
        case DeviceSettingItem::Ble:
            std::snprintf(out, out_len, "%s", setting_popup_ble_enabled_ ? "ON" : "OFF");
            return;
#endif
#if TRAILMATE_NRF_MONO_TARGET_MESSAGE_SOUND_SETTING
        case DeviceSettingItem::MessageTone:
            std::snprintf(out, out_len, "%s", setting_popup_message_tone_volume_ > 0 ? "ON" : "OFF");
            return;
#endif
#if TRAILMATE_NRF_MONO_TARGET_DEVICE_IO_SETTINGS
        case DeviceSettingItem::MessageLight:
            std::snprintf(out, out_len, "%s", setting_popup_message_light_enabled_ ? "ON" : "OFF");
            return;
        case DeviceSettingItem::LedColor:
        {
            const char* label = host_.status_led_color_label_fn
                                    ? host_.status_led_color_label_fn(setting_popup_status_led_color_index_)
                                    : nullptr;
            std::snprintf(out, out_len, "%s", (label && label[0] != '\0') ? label : "-");
            return;
        }
        case DeviceSettingItem::KeyboardLight:
            std::snprintf(out, out_len, "%s", setting_popup_keyboard_light_enabled_ ? "ON" : "OFF");
            return;
#endif
        case DeviceSettingItem::Gps:
            std::snprintf(out, out_len, "%s", setting_popup_config_.gps_enabled ? "ON" : "OFF");
            return;
        case DeviceSettingItem::Sats:
            std::snprintf(out, out_len, "%s", gpsSatMaskLabel(setting_popup_config_.gps_sat_mask));
            return;
        case DeviceSettingItem::GpsInterval:
            std::snprintf(out, out_len, "%lus", static_cast<unsigned long>(setting_popup_config_.gps_interval_ms / 1000UL));
            return;
        case DeviceSettingItem::ScreenOff:
            formatScreenTimeoutLabel(out, out_len, setting_popup_screen_timeout_ms_);
            return;
        case DeviceSettingItem::TimeZone:
        {
            char tz_label[16] = {};
            formatTimezoneLabel(setting_popup_timezone_min_, tz_label, sizeof(tz_label));
            std::snprintf(out, out_len, "%s", tz_label);
            return;
        }
        default:
            break;
        }
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

void Runtime::adjustComposeT9PinyinCandidate(int delta)
{
    if (compose_t9_pinyin_candidate_count_ == 0)
    {
        compose_t9_pinyin_candidate_index_ = 0;
        return;
    }
    const int next = static_cast<int>(compose_t9_pinyin_candidate_index_) + delta;
    if (next < 0)
    {
        compose_t9_pinyin_candidate_index_ = compose_t9_pinyin_candidate_count_ - 1;
    }
    else
    {
        compose_t9_pinyin_candidate_index_ =
            static_cast<size_t>(next) % compose_t9_pinyin_candidate_count_;
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
    if (usesPhysicalTextInput())
    {
        compose_physical_last_key_ = '\0';
        compose_abc_last_tap_ms_ = 0;
        compose_abc_tap_index_ = 0;
        if (compose_mode_ == ComposeMode::Cn && hasPhysicalChinesePreedit())
        {
            if (compose_t9_selected_pinyin_[0] != '\0')
            {
                compose_t9_selected_pinyin_[0] = '\0';
                rebuildComposeCandidates();
            }
            else if (compose_t9_digit_len_ > 0)
            {
                popChar(compose_t9_digits_, compose_t9_digit_len_);
                rebuildComposeCandidates();
            }
        }
        else if (compose_preedit_len_ > 0)
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
    if (usesPhysicalTextInput())
    {
        (void)commitPhysicalComposeToText(true);
    }

    if (compose_mode_ == ComposeMode::Num)
    {
        compose_mode_ = ComposeMode::AbcLower;
    }
    else if (compose_mode_ == ComposeMode::AbcLower)
    {
        compose_mode_ = ComposeMode::AbcUpper;
    }
    else if (compose_mode_ == ComposeMode::AbcUpper)
    {
        compose_mode_ = editAllowsChineseInput() ? ComposeMode::Cn : ComposeMode::Sym;
    }
    else if (compose_mode_ == ComposeMode::Cn)
    {
        compose_mode_ = ComposeMode::Sym;
    }
    else
    {
        compose_mode_ = ComposeMode::Num;
    }
    compose_charset_index_ = 0;
    compose_abc_tap_index_ = 0;
    compose_abc_last_tap_ms_ = 0;
    compose_abc_last_group_index_ = -1;
    compose_physical_last_key_ = '\0';
    rebuildComposeCandidates();
}

bool Runtime::handlePhysicalComposeText(char ch)
{
    if (!usesPhysicalTextInput() || page_ != Page::Compose)
    {
        return false;
    }

    if (ch == '*')
    {
        cycleComposeMode();
        return true;
    }

    if (ch == '#')
    {
        (void)commitPhysicalComposeToText(true);
        appendComposeChar(' ');
        return true;
    }

    if (ch < '0' || ch > '9')
    {
        return false;
    }

    compose_focus_ = ComposeFocus::Body;
    if (compose_mode_ == ComposeMode::Sym)
    {
        (void)commitPhysicalComposePreedit(true);
        const char* chars = phoneSymbolCycleCharsForKey(ch);
        const size_t char_count = std::strlen(chars);
        if (char_count == 0)
        {
            return true;
        }

        const uint32_t now = nowMs();
        const bool can_cycle = compose_physical_last_key_ == ch &&
                               compose_len_ > 0 &&
                               (now - compose_abc_last_tap_ms_) <= kComposeMultiTapWindowMs;
        if (can_cycle)
        {
            compose_abc_tap_index_ = (compose_abc_tap_index_ + 1U) % char_count;
            compose_buffer_[compose_len_ - 1U] = chars[compose_abc_tap_index_];
        }
        else
        {
            compose_abc_tap_index_ = 0;
            appendChar(compose_buffer_, sizeof(compose_buffer_), compose_len_, chars[0]);
        }

        compose_physical_last_key_ = ch;
        compose_abc_last_tap_ms_ = now;
        compose_abc_last_group_index_ = -1;
        return true;
    }

    if (compose_mode_ == ComposeMode::Cn)
    {
        if (ch == '0')
        {
            (void)commitPhysicalComposeToText(true);
            appendComposeChar(' ');
            compose_physical_last_key_ = '\0';
            compose_abc_last_tap_ms_ = 0;
            compose_abc_tap_index_ = 0;
            return true;
        }
        if (ch == '1')
        {
            (void)commitPhysicalComposeToText(true);
            appendComposeRawText(u8"，");
            compose_physical_last_key_ = '\0';
            compose_abc_last_tap_ms_ = 0;
            compose_abc_tap_index_ = 0;
            return true;
        }

        if (ch < '2' || ch > '9')
        {
            return true;
        }

        if (compose_t9_selected_pinyin_[0] != '\0')
        {
            (void)commitPhysicalComposeToText(true);
        }
        appendChar(compose_t9_digits_, sizeof(compose_t9_digits_), compose_t9_digit_len_, ch);
        compose_t9_selected_pinyin_[0] = '\0';
        compose_physical_last_key_ = ch;
        compose_abc_last_tap_ms_ = 0;
        compose_abc_tap_index_ = 0;
        compose_abc_last_group_index_ = -1;
        rebuildComposeCandidates();
        return true;
    }

    if (compose_mode_ == ComposeMode::Num)
    {
        (void)commitPhysicalComposePreedit(true);
        appendComposeChar(ch);
        compose_physical_last_key_ = '\0';
        compose_abc_last_tap_ms_ = 0;
        compose_abc_tap_index_ = 0;
        return true;
    }

    if (ch == '0')
    {
        (void)commitPhysicalComposePreedit(true);
        appendComposeChar(' ');
        compose_physical_last_key_ = '\0';
        return true;
    }

    const char* chars = phoneCycleCharsForKey(ch);
    const size_t char_count = std::strlen(chars);
    if (char_count == 0)
    {
        return true;
    }

    const uint32_t now = nowMs();
    const bool can_cycle = compose_physical_last_key_ == ch &&
                           compose_len_ > 0 &&
                           (now - compose_abc_last_tap_ms_) <= kComposeMultiTapWindowMs;
    if (can_cycle)
    {
        compose_abc_tap_index_ = (compose_abc_tap_index_ + 1U) % char_count;
        compose_buffer_[compose_len_ - 1U] = applyPhoneCycleCase(compose_mode_, chars[compose_abc_tap_index_]);
    }
    else
    {
        compose_abc_tap_index_ = 0;
        appendChar(compose_buffer_, sizeof(compose_buffer_), compose_len_,
                   applyPhoneCycleCase(compose_mode_, chars[0]));
    }

    compose_physical_last_key_ = ch;
    compose_abc_last_tap_ms_ = now;
    compose_abc_last_group_index_ = -1;
    return true;
}

bool Runtime::hasPhysicalChinesePreedit() const
{
    return usesPhysicalTextInput() && compose_mode_ == ComposeMode::Cn &&
           (compose_t9_digit_len_ > 0 || compose_t9_selected_pinyin_[0] != '\0');
}

void Runtime::clearComposeT9State()
{
    compose_t9_digits_[0] = '\0';
    compose_t9_digit_len_ = 0;
    compose_t9_selected_pinyin_[0] = '\0';
    compose_t9_pinyin_candidate_count_ = 0;
    compose_t9_pinyin_candidate_index_ = 0;
    for (size_t i = 0; i < kComposeCandidateMax; ++i)
    {
        compose_t9_pinyin_candidates_[i][0] = '\0';
    }
}

bool Runtime::selectComposeT9PinyinCandidate()
{
    if (!hasPhysicalChinesePreedit() ||
        compose_t9_selected_pinyin_[0] != '\0' ||
        compose_t9_pinyin_candidate_count_ == 0 ||
        compose_t9_pinyin_candidate_index_ >= compose_t9_pinyin_candidate_count_)
    {
        return false;
    }

    copyText(compose_t9_selected_pinyin_,
             compose_t9_pinyin_candidates_[compose_t9_pinyin_candidate_index_]);
    compose_preedit_[0] = '\0';
    compose_preedit_len_ = 0;
    compose_candidate_index_ = 0;
    rebuildComposeCandidates();
    return true;
}

bool Runtime::commitPhysicalComposePreedit(bool prefer_candidate)
{
    if (!usesPhysicalTextInput())
    {
        return false;
    }
    if (compose_mode_ == ComposeMode::Cn && hasPhysicalChinesePreedit())
    {
        if (compose_t9_selected_pinyin_[0] == '\0')
        {
            if (prefer_candidate)
            {
                (void)selectComposeT9PinyinCandidate();
            }
            return true;
        }
        if (prefer_candidate && commitComposeCandidate())
        {
            return true;
        }
        appendComposeRawText(compose_t9_selected_pinyin_);
        clearComposeT9State();
        rebuildComposeCandidates();
        return true;
    }
    if (compose_preedit_len_ == 0)
    {
        return false;
    }
    return commitComposePreedit(prefer_candidate);
}

bool Runtime::commitPhysicalComposeToText(bool prefer_candidate)
{
    if (!usesPhysicalTextInput())
    {
        return false;
    }
    if (compose_mode_ == ComposeMode::Cn && hasPhysicalChinesePreedit())
    {
        if (compose_t9_selected_pinyin_[0] == '\0')
        {
            if (!selectComposeT9PinyinCandidate())
            {
                appendComposeRawText(compose_t9_digits_);
                clearComposeT9State();
                rebuildComposeCandidates();
                return true;
            }
        }
        if (prefer_candidate && commitComposeCandidate())
        {
            return true;
        }
        appendComposeRawText(compose_t9_selected_pinyin_);
        clearComposeT9State();
        rebuildComposeCandidates();
        return true;
    }
    return commitPhysicalComposePreedit(prefer_candidate);
}

void Runtime::rebuildComposeCandidates()
{
    compose_candidate_count_ = 0;
    compose_candidate_index_ = 0;
    for (size_t i = 0; i < kComposeCandidateMax; ++i)
    {
        compose_candidates_[i][0] = '\0';
    }

    if (usesPhysicalTextInput() && compose_mode_ == ComposeMode::Cn)
    {
        if (compose_t9_selected_pinyin_[0] != '\0')
        {
#if TRAIL_MATE_MONO_PINYIN_LOOKUP_ENABLED
            auto add_hanzi = [this](const char* word) -> bool
            {
                if (!word || word[0] == '\0')
                {
                    return false;
                }
                for (size_t i = 0; i < compose_candidate_count_; ++i)
                {
                    if (std::strcmp(compose_candidates_[i], word) == 0)
                    {
                        return compose_candidate_count_ >= kComposeCandidateMax;
                    }
                }
                copyText(compose_candidates_[compose_candidate_count_], word);
                ++compose_candidate_count_;
                return compose_candidate_count_ >= kComposeCandidateMax;
            };

            ::ui::widgets::ime::collectPinyinCandidates(compose_t9_selected_pinyin_, add_hanzi);
#endif
            return;
        }

        compose_t9_pinyin_candidate_count_ = 0;
        compose_t9_pinyin_candidate_index_ = 0;
        for (size_t i = 0; i < kComposeCandidateMax; ++i)
        {
            compose_t9_pinyin_candidates_[i][0] = '\0';
        }
        if (compose_t9_digit_len_ == 0)
        {
            return;
        }

#if TRAIL_MATE_MONO_PINYIN_LOOKUP_ENABLED
        auto add_pinyin = [this](const char* pinyin) -> bool
        {
            if (!pinyin || pinyin[0] == '\0')
            {
                return false;
            }
            for (size_t i = 0; i < compose_t9_pinyin_candidate_count_; ++i)
            {
                if (std::strcmp(compose_t9_pinyin_candidates_[i], pinyin) == 0)
                {
                    return compose_t9_pinyin_candidate_count_ >= kComposeCandidateMax;
                }
            }
            copyText(compose_t9_pinyin_candidates_[compose_t9_pinyin_candidate_count_], pinyin);
            ++compose_t9_pinyin_candidate_count_;
            return compose_t9_pinyin_candidate_count_ >= kComposeCandidateMax;
        };

        ::ui::widgets::ime::collectPinyinSpellingsForDigits(compose_t9_digits_, add_pinyin);
#endif
        return;
    }

    if (compose_preedit_len_ == 0)
    {
        return;
    }

    if ((usesPhysicalTextInput() || usesSmartCompose()) && compose_mode_ == ComposeMode::Cn)
    {
#if TRAIL_MATE_MONO_PINYIN_LOOKUP_ENABLED
        auto add_candidate = [this](const char* word) -> bool
        {
            if (!word || word[0] == '\0')
            {
                return false;
            }
            for (size_t i = 0; i < compose_candidate_count_; ++i)
            {
                if (std::strcmp(compose_candidates_[i], word) == 0)
                {
                    return compose_candidate_count_ >= kComposeCandidateMax;
                }
            }
            copyText(compose_candidates_[compose_candidate_count_], word);
            ++compose_candidate_count_;
            return compose_candidate_count_ >= kComposeCandidateMax;
        };

        ::ui::widgets::ime::collectPinyinCandidates(compose_preedit_, add_candidate);
#endif
        return;
    }

    if (!usesSmartCompose() || !isAlphaComposeMode(compose_mode_))
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
    if (compose_mode_ == ComposeMode::Cn)
    {
        appendComposeRawText(compose_candidates_[compose_candidate_index_]);
    }
    else
    {
        appendComposeWord(compose_candidates_[compose_candidate_index_]);
    }
    compose_preedit_[0] = '\0';
    compose_preedit_len_ = 0;
    compose_abc_last_group_index_ = -1;
    compose_abc_last_tap_ms_ = 0;
    compose_abc_tap_index_ = 0;
    compose_physical_last_key_ = '\0';
    if (usesPhysicalTextInput() && compose_mode_ == ComposeMode::Cn)
    {
        clearComposeT9State();
    }
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
    if (compose_mode_ == ComposeMode::Cn)
    {
        appendComposeRawText(compose_preedit_);
    }
    else
    {
        appendComposeWord(compose_preedit_);
    }
    compose_preedit_[0] = '\0';
    compose_preedit_len_ = 0;
    compose_abc_last_group_index_ = -1;
    compose_abc_last_tap_ms_ = 0;
    compose_abc_tap_index_ = 0;
    compose_physical_last_key_ = '\0';
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

    if (editUsesDirectTextEntry())
    {
        appendChar(compose_buffer_,
                   sizeof(compose_buffer_),
                   compose_len_,
                   isAlphaAscii(ch) ? applyComposeAlphaCase(compose_mode_, ch) : ch);
        return;
    }

    if ((isAlphaComposeMode(compose_mode_) || compose_mode_ == ComposeMode::Cn) && isAlphaAscii(ch))
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

void Runtime::appendComposeRawText(const char* text)
{
    if (!text || text[0] == '\0')
    {
        return;
    }

    appendText(compose_buffer_, sizeof(compose_buffer_), compose_len_, text);
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
        if (edit_target_ == EditTarget::Message)
        {
            sendComposeMessage();
        }
        else
        {
            finishTextEdit(true);
        }
        break;
    }
}

bool Runtime::saveEditedTextToConfig()
{
    if (!app())
    {
        return false;
    }

    auto& cfg = app()->getConfig();
    bool mesh_config_changed = false;
    switch (edit_target_)
    {
    case EditTarget::MeshCoreChannelName:
        copyText(cfg.meshcore_config.meshcore_channel_name, compose_buffer_);
        mesh_config_changed = true;
        break;
    case EditTarget::MeshtasticPrimaryKey:
        if (!applyMeshtasticPrimaryKeyText(cfg.meshtastic_config, compose_buffer_))
        {
            showTransientPopup("INVALID KEY", "MT PSK");
            return false;
        }
        mesh_config_changed = true;
        break;
    case EditTarget::MeshCoreChannelKey:
        if (!applyMeshCoreChannelKeyText(cfg.meshcore_config, compose_buffer_))
        {
            showTransientPopup("INVALID KEY", "MC KEY");
            return false;
        }
        mesh_config_changed = true;
        break;
    default:
        break;
    }
    commitConfig();
    if (mesh_config_changed)
    {
        app()->applyMeshConfig();
    }
    return true;
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

    if (has_valid_wall_clock)
    {
        now = applyLocalTimezone(host_, now);
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

void Runtime::formatTimestamp(char* out, size_t out_len, uint32_t timestamp_s) const
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

    const time_t adjusted = applyLocalTimezone(host_, static_cast<time_t>(timestamp_s));
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
}

void Runtime::formatConversationTime(char* out, size_t out_len, uint32_t timestamp_s, bool expanded) const
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (timestamp_s == 0)
    {
        std::snprintf(out, out_len, "%s", "--:--");
        return;
    }

    if (timestamp_s >= 1700000000U)
    {
        const time_t adjusted = applyLocalTimezone(host_, static_cast<time_t>(timestamp_s));
        const std::tm* tm = std::gmtime(&adjusted);
        if (tm)
        {
            if (expanded)
            {
                std::snprintf(out, out_len, "%02d/%02d %02d:%02d",
                              tm->tm_mon + 1,
                              tm->tm_mday,
                              tm->tm_hour,
                              tm->tm_min);
            }
            else
            {
                std::snprintf(out, out_len, "%02d/%02d", tm->tm_mon + 1, tm->tm_mday);
            }
            return;
        }
    }

    const unsigned long hours = static_cast<unsigned long>((timestamp_s / 3600U) % 24U);
    const unsigned long minutes = static_cast<unsigned long>((timestamp_s / 60U) % 60U);
    if (expanded)
    {
        std::snprintf(out, out_len, "--/-- %02lu:%02lu", hours, minutes);
    }
    else
    {
        std::snprintf(out, out_len, "%02lu:%02lu", hours, minutes);
    }
}

void Runtime::formatConversationSender(char* out, size_t out_len, const chat::ChatMessage& msg, bool expanded) const
{
    if (!out || out_len == 0)
    {
        return;
    }

    if (msg.from == 0)
    {
        const chat::NodeId self_id = app() ? app()->getSelfNodeId() : 0;
        if (expanded && self_id != 0)
        {
            std::snprintf(out, out_len, "%08lX", static_cast<unsigned long>(self_id));
        }
        else
        {
            std::snprintf(out, out_len, "%s", "ME");
        }
        return;
    }

    if (expanded)
    {
        std::snprintf(out, out_len, "%08lX", static_cast<unsigned long>(msg.from));
    }
    else
    {
        std::snprintf(out, out_len, "%04lX", static_cast<unsigned long>(msg.from & 0xFFFFUL));
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
    auto draw_with_renderer = [&](const TextRenderer& renderer, int x, int y, int w,
                                  const char* text, bool inverse = false)
    {
        if (!text || w <= 0)
        {
            return;
        }

        char clipped[64] = {};
        if (renderer.measureTextWidth(text) <= w)
        {
            copyText(clipped, text);
        }
        else if (w > renderer.ellipsisWidth())
        {
            const size_t keep_bytes = renderer.clipTextToWidth(text, w - renderer.ellipsisWidth());
            const size_t copy_len = std::min(keep_bytes, sizeof(clipped) - 4);
            std::memcpy(clipped, text, copy_len);
            clipped[copy_len] = '\0';
            std::strcat(clipped, "...");
        }
        else
        {
            const size_t keep_bytes = renderer.clipTextToWidth(text, w);
            const size_t copy_len = std::min(keep_bytes, sizeof(clipped) - 1);
            std::memcpy(clipped, text, copy_len);
            clipped[copy_len] = '\0';
        }
        renderer.drawText(display_, x, y, clipped, inverse);
    };

    const bool large_layout = display_.width() >= 160 && display_.height() >= 120;
    if (large_layout)
    {
        constexpr int kMargin = 12;
        constexpr int kTitleY = 10;
        const int title_w = accent_text_renderer_.measureTextWidth(title);
        const int title_x = std::max(kMargin, (display_.width() - title_w) / 2);
        accent_text_renderer_.drawText(display_, title_x, kTitleY, title ? title : "");
        display_.drawHLine(kMargin, kTitleY + accent_text_renderer_.lineHeight() + 7,
                           display_.width() - (kMargin * 2));

        const int row_h = accent_text_renderer_.lineHeight() + 9;
        const int list_y = kTitleY + accent_text_renderer_.lineHeight() + 19;
        const int available_h = std::max(0, display_.height() - list_y - 10);
        const size_t visible_capacity = std::max<size_t>(1U, static_cast<size_t>(available_h / row_h));
        const size_t visible = std::min(count, visible_capacity);
        size_t start = 0;
        if (count > visible)
        {
            start = (selected + 1 > visible) ? (selected + 1 - visible) : 0;
            if (start + visible > count)
            {
                start = count - visible;
            }
        }

        for (size_t i = 0; i < visible && (start + i) < count; ++i)
        {
            const size_t item_index = start + i;
            const bool selected_row = (item_index == selected);
            const int y = list_y + static_cast<int>(i * row_h);
            if (selected_row)
            {
                display_.fillRect(kMargin - 3, y - 4, display_.width() - ((kMargin - 3) * 2),
                                  row_h - 1, true);
            }
            draw_with_renderer(accent_text_renderer_, kMargin, y,
                               display_.width() - (kMargin * 2), items[item_index], selected_row);
        }
        return;
    }

    drawTitleBar(title, nullptr);
    const int line_h = text_renderer_.lineHeight();
    const size_t visible = std::min(count, static_cast<size_t>(6));
    size_t start = 0;
    if (count > visible)
    {
        start = (selected + 1 > visible) ? (selected + 1 - visible) : 0;
        if (start + visible > count)
        {
            start = count - visible;
        }
    }

    for (size_t i = 0; i < visible && (start + i) < count; ++i)
    {
        const size_t item_index = start + i;
        drawTextClipped(0, 10 + static_cast<int>(i * line_h), display_.width(), items[item_index], item_index == selected);
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

size_t Runtime::visibleRowsFrom(int start_y, int bottom_margin) const
{
    const int line_h = std::max(1, text_renderer_.lineHeight());
    const int available_h = display_.height() - start_y - std::max(0, bottom_margin);
    if (available_h <= 0)
    {
        return 1U;
    }
    return std::max<size_t>(1U, static_cast<size_t>(available_h / line_h));
}

int Runtime::rowStrideFor(size_t count, int start_y, int bottom_margin) const
{
    const int line_h = std::max(1, text_renderer_.lineHeight());
    if (count <= 1U)
    {
        return line_h;
    }

    const int available_h = display_.height() - start_y - std::max(0, bottom_margin) - line_h;
    const int distributed = available_h > 0 ? (available_h / static_cast<int>(count - 1U)) : line_h;
    return std::max(line_h, std::min(line_h + 12, distributed));
}

void Runtime::drawConversationText(int x, int y, int w, const char* text, bool selected, bool align_right)
{
    if (!text || w <= 0)
    {
        return;
    }

    const int full_width = text_renderer_.measureTextWidth(text);

    if (full_width <= w)
    {
        const int draw_x = align_right ? (x + std::max(0, w - full_width)) : x;
        text_renderer_.drawText(display_, draw_x, y, text, false);
        return;
    }

    char clipped[96] = {};
    if (!selected)
    {
        if (w > text_renderer_.ellipsisWidth())
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
        const int clipped_width = text_renderer_.measureTextWidth(clipped);
        const int draw_x = align_right ? (x + std::max(0, w - clipped_width)) : x;
        text_renderer_.drawText(display_, draw_x, y, clipped, false);
        return;
    }

    const size_t text_len = std::strlen(text);
    std::vector<size_t> offsets;
    offsets.reserve(text_len + 1);
    for (size_t off = 0; off < text_len;)
    {
        offsets.push_back(off);
        off += utf8CharLength(static_cast<unsigned char>(text[off]));
    }
    offsets.push_back(text_len);

    size_t max_scroll_index = 0;
    for (size_t i = 0; i + 1 < offsets.size(); ++i)
    {
        if (text_renderer_.measureTextWidth(text + offsets[i]) > w)
        {
            max_scroll_index = i + 1;
        }
    }

    size_t scroll_index = 0;
    const uint32_t elapsed_ms = nowMs() >= message_focus_started_ms_ ? (nowMs() - message_focus_started_ms_) : 0U;
    if (max_scroll_index > 0 && elapsed_ms > kConversationScrollStartPauseMs)
    {
        const uint32_t rolling_ms = static_cast<uint32_t>(max_scroll_index) * kConversationScrollStepMs;
        const uint32_t cycle_ms = rolling_ms + kConversationScrollEndPauseMs;
        const uint32_t within_cycle = (elapsed_ms - kConversationScrollStartPauseMs) % std::max<uint32_t>(1U, cycle_ms);
        if (within_cycle < rolling_ms)
        {
            scroll_index = std::min<size_t>(max_scroll_index, within_cycle / kConversationScrollStepMs);
        }
        else
        {
            scroll_index = max_scroll_index;
        }
    }

    const char* window_text = text + offsets[scroll_index];
    const size_t keep_bytes = text_renderer_.clipTextToWidth(window_text, w);
    std::memcpy(clipped, window_text, std::min(keep_bytes, sizeof(clipped) - 1));
    clipped[std::min(keep_bytes, sizeof(clipped) - 1)] = '\0';
    const int clipped_width = text_renderer_.measureTextWidth(clipped);
    const int draw_x = align_right ? (x + std::max(0, w - clipped_width)) : x;
    text_renderer_.drawText(display_, draw_x, y, clipped, false);
}

void Runtime::drawConversationBubble(int x, int y, int w, const char* sender, const char* time_text,
                                     const char* text, bool selected, bool align_right)
{
    if (!text || w <= 4)
    {
        return;
    }

    const int line_h = text_renderer_.lineHeight();
    constexpr int kBubbleBodyBottomPadding = 2;
    const int bubble_h = (line_h * 2) + 1 + kBubbleBodyBottomPadding;
    drawFrame(display_, x, y, w, bubble_h);

    const int inner_x = x + 1;
    const int inner_w = w - 2;
    const int divider_y = y + line_h;
    const int band_h = std::max(1, line_h - 1);
    display_.fillRect(inner_x, divider_y, inner_w, 1, true);

    display_.fillRect(inner_x, y + 1, inner_w, band_h, true);

    const bool header_inverse = true;
    const int header_pad = 1;
    const int header_x = inner_x + header_pad;
    const int header_w = std::max(0, inner_w - (header_pad * 2));
    const int time_w = (time_text && time_text[0] != '\0') ? text_renderer_.measureTextWidth(time_text) : 0;
    const int time_x = x + w - 2 - header_pad - time_w;
    const int sender_w = std::max(0, time_x - header_x - 2);

    if (sender && sender[0] != '\0' && sender_w > 0)
    {
        drawTextClipped(header_x, y, sender_w, sender, header_inverse);
    }
    if (time_text && time_text[0] != '\0' && header_w > 0)
    {
        text_renderer_.drawText(display_, std::max(header_x, time_x), y, time_text, header_inverse);
    }

    const int body_x = inner_x + 1;
    const int body_w = std::max(0, inner_w - 2);
    drawConversationText(body_x, y + line_h + 1, body_w, text, selected, align_right);
}

void Runtime::executeActionPageItem(size_t index)
{
    if (!app())
    {
        return;
    }

    switch (index)
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
        if (host_.clear_volatile_storage_fn && host_.clear_volatile_storage_fn())
        {
            appendBootLog("cache cleared");
            showTransientPopup("STORAGE", "CACHE CLEARED", 2000U);
        }
        else
        {
            appendBootLog("cache clear fail");
            showTransientPopup("STORAGE", "CLEAR FAILED", 2500U);
        }
        break;
    default:
        break;
    }
}

void Runtime::showTransientPopup(const char* title, const char* message, uint32_t duration_ms)
{
    copyText(transient_popup_title_, title ? title : "");
    copyText(transient_popup_message_, message ? message : "");
    transient_popup_active_ = true;
    transient_popup_expires_ms_ = nowMs() + duration_ms;
}

void Runtime::expireTransientPopup()
{
    if (!transient_popup_active_)
    {
        return;
    }
    const uint32_t now = nowMs();
    if (now >= transient_popup_expires_ms_)
    {
        transient_popup_active_ = false;
        transient_popup_title_[0] = '\0';
        transient_popup_message_[0] = '\0';
        transient_popup_expires_ms_ = 0;
    }
}

bool Runtime::editUsesHexCharset() const
{
    return false;
}

bool Runtime::editUsesKeyCharset() const
{
    return edit_target_ == EditTarget::MeshtasticPrimaryKey ||
           edit_target_ == EditTarget::MeshCoreChannelKey;
}

bool Runtime::editUsesDirectTextEntry() const
{
    return edit_target_ == EditTarget::MeshCoreChannelName ||
           editUsesKeyCharset();
}

bool Runtime::editAllowsChineseInput() const
{
#if !TRAIL_MATE_MONO_PINYIN_LOOKUP_ENABLED
    return false;
#else
    return edit_target_ == EditTarget::Message;
#endif
}

bool Runtime::editAcceptsTextInput() const
{
    return edit_target_ == EditTarget::Message ||
           edit_target_ == EditTarget::MeshCoreChannelName ||
           editUsesKeyCharset();
}

bool Runtime::usesSmartCompose() const
{
    return editAcceptsTextInput() && !usesPhysicalTextInput();
}

bool Runtime::usesPhysicalTextInput() const
{
    return host_.physical_text_input && editAcceptsTextInput();
}

bool Runtime::usesFullVirtualKeyboard() const
{
    return usesSmartCompose() &&
           host_.virtual_keyboard_layout == HostCallbacks::VirtualKeyboardLayout::FullKeyboard;
}

bool Runtime::usesLargeScreensaverLayout() const
{
    return ::ui::mono::usesLargeScreensaverLayout(display_);
}

bool Runtime::shouldRenderForTick(InputAction action)
{
    (void)action;
    if (page_ != Page::Screensaver || !usesLargeScreensaverLayout())
    {
        return true;
    }

    const uint32_t now = nowMs();
    if (last_screensaver_render_ms_ != 0 &&
        now >= last_screensaver_render_ms_ &&
        (now - last_screensaver_render_ms_) < kLargeScreensaverRefreshMs)
    {
        return false;
    }

    last_screensaver_render_ms_ = now;
    return true;
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

const chat::contacts::NodeInfo* Runtime::selectedNode() const
{
    if (node_count_ == 0)
    {
        return nullptr;
    }
    const size_t index = std::min(node_list_index_, node_count_ - 1U);
    return &nodes_[index];
}

size_t Runtime::nodeActionCount() const
{
    const bool meshtastic_mode = app() && app()->getConfig().mesh_protocol != chat::MeshProtocol::MeshCore;
    return meshtastic_mode ? kNodeActionItemCount : (kNodeActionItemCount - 2U);
}

chat::MessageId Runtime::nextMeshtasticActionRequestId(chat::NodeId peer)
{
    uint32_t value = next_meshtastic_action_request_id_;
    if (value == 0)
    {
        value = nowMs() ^ (peer * 2654435761UL) ^ 0xA57EACE1UL;
    }
    value = value * 1664525UL + 1013904223UL;
    if (value == 0)
    {
        value = 1;
    }
    next_meshtastic_action_request_id_ = value;
    return value;
}

chat::runtime::RuntimeContext Runtime::buildMeshtasticProtocolContext() const
{
    chat::runtime::RuntimeContext context{};
    context.protocol = chat::MeshProtocol::Meshtastic;
    if (auto* mesh = app() ? app()->getMeshAdapter() : nullptr)
    {
        context.self_node = mesh->getNodeId();
    }
    context.now_ms = nowMs();
    return context;
}

void Runtime::handleMeshtasticFacadeResult(
    const chat::runtime::MeshProtocolFacadeResult& result)
{
    if (!result.hasActionResult())
    {
        return;
    }

    chat::runtime::EmitActionResultEffect effect{};
    effect.protocol = result.action_result.protocol;
    effect.action = result.action_result.action;
    effect.state = result.action_result.state;
    effect.peer = result.action_result.peer;
    effect.request_id = result.action_result.request_id;
    effect.message_id = result.action_result.message_id;
    effect.detail = result.action_result.detail;
    handleMeshtasticActionResult(effect);
}

void Runtime::handleMeshtasticActionResult(
    const chat::runtime::EmitActionResultEffect& result)
{
    if (result.protocol != chat::MeshProtocol::Meshtastic)
    {
        return;
    }

    const char* title = nullptr;
    if (result.action == chat::runtime::ProtocolActionKind::TraceRoute)
    {
        title = "TRACE ROUTE";
    }
    else if (result.action == chat::runtime::ProtocolActionKind::ExchangePosition)
    {
        title = "EXCHANGE POSITION";
    }
    else if (result.action == chat::runtime::ProtocolActionKind::SharePosition)
    {
        title = "SHARE POSITION";
    }
    else if (result.action == chat::runtime::ProtocolActionKind::ShareWaypoint)
    {
        title = "SHARE POI";
    }
    else
    {
        title = "MESH ACTION";
    }

    const bool trace = result.action == chat::runtime::ProtocolActionKind::TraceRoute;
    const bool exchange_position =
        result.action == chat::runtime::ProtocolActionKind::ExchangePosition;

    switch (result.state)
    {
    case chat::runtime::ProtocolActionState::Delivered:
        appendBootLog(trace ? "trace delivered" : "mesh delivered");
        showTransientPopup(title, "DELIVERED", 1500U);
        break;
    case chat::runtime::ProtocolActionState::Completed:
        appendBootLog(trace ? "trace reply ok" : "pos reply ok");
        showTransientPopup(title, "REPLY RECEIVED");
        break;
    case chat::runtime::ProtocolActionState::Failed:
    {
        char message[32] = {};
        const char* reason = "FAILED";
        if (result.detail == chat::runtime::kMeshtasticActionDetailLocalSendFailed)
        {
            reason = "SEND FAILED";
        }
        else if (result.detail >= 0)
        {
            reason = chat::meshtastic::routingErrorName(
                static_cast<meshtastic_Routing_Error>(result.detail));
        }
        std::snprintf(message, sizeof(message), "%s", reason);
        appendBootLog(trace ? "trace failed" : (exchange_position ? "pos failed" : "mesh failed"));
        showTransientPopup(title, message);
        break;
    }
    case chat::runtime::ProtocolActionState::TimedOut:
        appendBootLog(trace ? "trace timeout" : "pos timeout");
        showTransientPopup(title, "TIMEOUT");
        break;
    default:
        break;
    }
}

const char* Runtime::nodeActionLabel(size_t index) const
{
    const chat::contacts::NodeInfo* node = selectedNode();
    const bool meshtastic_mode = app() && app()->getConfig().mesh_protocol != chat::MeshProtocol::MeshCore;
    const bool can_reply = node && app() &&
                           chat::infra::meshProtocolFromRaw(
                               static_cast<uint8_t>(node->protocol),
                               app()->getConfig().mesh_protocol) ==
                               app()->getConfig().mesh_protocol;
    switch (index)
    {
    case 0:
        return "DETAIL";
    case 1:
        return can_reply ? "SEND" : "VIEW ONLY";
    case 2:
        return "ADD CONTACT";
    case 3:
        return (node && node->is_ignored) ? "UNIGNORE NODE" : "IGNORE NODE";
    case 4:
        return meshtastic_mode ? "TRACE ROUTE" : "OPEN COMPASS";
    case 5:
        return meshtastic_mode ? "EXCHANGE POSITION" : "";
    case 6:
        return meshtastic_mode ? "OPEN COMPASS" : "";
    default:
        return "";
    }
}

void Runtime::executeNodeAction()
{
    auto* mesh = app() ? app()->getMeshAdapter() : nullptr;
    auto* contacts = app() ? &app()->getContactService() : nullptr;
    const chat::contacts::NodeInfo* node = selectedNode();
    const bool meshtastic_mode = app() && app()->getConfig().mesh_protocol != chat::MeshProtocol::MeshCore;
    if (!node)
    {
        appendBootLog("node action na");
        showTransientPopup("NODE", "ACTION UNAVAILABLE");
        return;
    }

    switch (node_action_index_)
    {
    case 0:
        node_info_scroll_ = 0;
        enterPage(Page::NodeInfo);
        return;
    case 1:
    {
        if (!app())
        {
            showTransientPopup("NODE", "APP NOT READY");
            return;
        }
        const chat::MeshProtocol node_protocol =
            chat::infra::meshProtocolFromRaw(static_cast<uint8_t>(node->protocol),
                                             app()->getConfig().mesh_protocol);
        if (node_protocol != app()->getConfig().mesh_protocol)
        {
            showTransientPopup("NODE", "VIEW ONLY");
            return;
        }
        active_conversation_ = chat::ConversationId(chat::ChannelId::PRIMARY,
                                                    node->node_id,
                                                    node_protocol);
        openCompose(EditTarget::Message);
        return;
    }
    case 2:
    {
        if (!contacts)
        {
            appendBootLog("contact svc na");
            showTransientPopup("ADD CONTACT", "UNAVAILABLE");
            return;
        }
        if (node->is_contact)
        {
            appendBootLog("contact exists");
            showTransientPopup("ADD CONTACT", "ALREADY EXISTS");
            return;
        }

        char nickname[13] = {};
        char fallback_nickname[13] = {};
        if (node->short_name[0] != '\0')
        {
            copyText(nickname, node->short_name);
        }
        else
        {
            std::snprintf(nickname, sizeof(nickname), "%04lX",
                          static_cast<unsigned long>(node->node_id & 0xFFFFUL));
        }
        std::snprintf(fallback_nickname, sizeof(fallback_nickname), "%04lX",
                      static_cast<unsigned long>(node->node_id & 0xFFFFUL));

        bool added = contacts->addContact(node->node_id, nickname);
        if (!added && std::strcmp(nickname, fallback_nickname) != 0)
        {
            added = contacts->addContact(node->node_id, fallback_nickname);
        }

        if (added)
        {
            appendBootLog("contact added");
            showTransientPopup("ADD CONTACT", "SUCCESS");
            rebuildNodeList();
            enterPage(Page::NodeList);
        }
        else
        {
            appendBootLog("contact failed");
            showTransientPopup("ADD CONTACT", "FAILED");
        }
        return;
    }
    case 3:
    {
        if (!contacts)
        {
            appendBootLog("ignore na");
            showTransientPopup("IGNORE NODE", "UNAVAILABLE");
            return;
        }
        const bool ignored = !node->is_ignored;
        const bool ok = contacts->setNodeIgnored(node->node_id, ignored);
        appendBootLog(ok ? (ignored ? "node ignored" : "node unignored") : "ignore failed");
        showTransientPopup(ignored ? "IGNORE NODE" : "UNIGNORE NODE", ok ? "SUCCESS" : "FAILED");
        if (ok)
        {
            rebuildNodeList();
            enterPage(Page::NodeList);
        }
        return;
    }
    case 4:
    {
        if (!meshtastic_mode)
        {
            showTransientPopup("OPEN COMPASS", "OPENED");
            enterPage(Page::NodeCompass);
            return;
        }
        if (!mesh)
        {
            appendBootLog("trace na");
            showTransientPopup("TRACE ROUTE", "UNAVAILABLE");
            return;
        }
        const chat::runtime::RuntimeContext context = buildMeshtasticProtocolContext();
        chat::runtime::FixedProtocolRuntimeContextProvider context_provider(context);
        chat::runtime::MeshAdapterProtocolEffectExecutor executor(*mesh);
        chat::runtime::ProtocolRuntimeSelection runtime_selection{};
        runtime_selection.meshtastic = &meshtastic_protocol_runtime_;
        const auto bundle = chat::runtime::protocolRuntimeFor(chat::MeshProtocol::Meshtastic,
                                                              runtime_selection,
                                                              executor,
                                                              context_provider);
        if (!bundle.valid())
        {
            appendBootLog("trace runtime na");
            showTransientPopup("TRACE ROUTE", "UNAVAILABLE");
            return;
        }
        auto facade = bundle.createFacade();
        chat::runtime::TraceRouteIntent intent{};
        intent.channel = chat::ChannelId::PRIMARY;
        intent.peer = node->node_id;
        intent.request_id = nextMeshtasticActionRequestId(node->node_id);
        intent.timeout_ms = chat::runtime::kMeshtasticAppActionTimeoutMs;

        const auto result = facade.executeIntent(intent);
        if (result.hasActionResult())
        {
            handleMeshtasticFacadeResult(result);
            return;
        }
        if (result.ok() && result.executed_effect_count > 0)
        {
            appendBootLog("trace wait reply");
            showTransientPopup("TRACE ROUTE", "WAIT REPLY");
            return;
        }

        appendBootLog("trace send fail");
        showTransientPopup("TRACE ROUTE", "SEND FAILED");
        return;
    }
    case 5:
        if (meshtastic_mode)
        {
            requestNodePositionExchange();
            return;
        }
        showTransientPopup("OPEN COMPASS", "OPENED");
        enterPage(Page::NodeCompass);
        return;
    case 6:
        showTransientPopup("OPEN COMPASS", "OPENED");
        enterPage(Page::NodeCompass);
        return;
    default:
        return;
    }
}

void Runtime::requestNodePositionExchange()
{
    auto* mesh = app() ? app()->getMeshAdapter() : nullptr;
    const chat::contacts::NodeInfo* node = selectedNode();
    if (!node)
    {
        appendBootLog("pos req node na");
        showTransientPopup("EXCHANGE POSITION", "NO NODE");
        return;
    }
    if (!mesh)
    {
        appendBootLog("pos req na");
        showTransientPopup("EXCHANGE POSITION", "UNAVAILABLE");
        return;
    }

    const chat::runtime::RuntimeContext context = buildMeshtasticProtocolContext();
    chat::runtime::FixedProtocolRuntimeContextProvider context_provider(context);
    chat::runtime::MeshAdapterProtocolEffectExecutor executor(*mesh);
    chat::runtime::ProtocolRuntimeSelection runtime_selection{};
    runtime_selection.meshtastic = &meshtastic_protocol_runtime_;
    const auto bundle = chat::runtime::protocolRuntimeFor(chat::MeshProtocol::Meshtastic,
                                                          runtime_selection,
                                                          executor,
                                                          context_provider);
    if (!bundle.valid())
    {
        appendBootLog("pos runtime na");
        showTransientPopup("EXCHANGE POSITION", "UNAVAILABLE");
        return;
    }
    auto facade = bundle.createFacade();
    chat::runtime::ExchangePositionIntent intent{};
    intent.channel = chat::ChannelId::PRIMARY;
    intent.peer = node->node_id;
    intent.request_id = nextMeshtasticActionRequestId(node->node_id);

    const auto result = facade.executeIntent(intent);
    if (result.hasActionResult())
    {
        handleMeshtasticFacadeResult(result);
        return;
    }
    if (result.ok() && result.executed_effect_count > 0)
    {
        appendBootLog("pos wait reply");
        showTransientPopup("EXCHANGE POSITION", "WAIT REPLY");
        return;
    }

    appendBootLog("pos req failed");
    showTransientPopup("EXCHANGE POSITION", "SEND FAILED");
}

} // namespace ui::mono
