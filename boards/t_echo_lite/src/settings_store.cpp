#include "boards/t_echo_lite/settings_store.h"

#include "boards/t_echo_lite/t_echo_lite_board.h"
#include "chat/infra/mesh_protocol_utils.h"
#include "chat/infra/meshcore/mc_region_presets.h"
#include "chat/infra/meshtastic/mt_region.h"
#include "platform/nrf52/arduino_common/internal_fs_utils.h"
#include "platform/nrf52/arduino_common/settings_file_store.h"

#include <Arduino.h>
#include <InternalFileSystem.h>

#include <algorithm>
#include <cstring>

namespace boards::t_echo_lite::settings_store
{
namespace
{
using Adafruit_LittleFS_Namespace::FILE_O_READ;
namespace settings_file = ::platform::nrf52::arduino_common::settings_file;
using FileHeader = settings_file::SettingsFileHeader;
using settings_file::crc32;

constexpr const char* kSettingsPath = "/t_echo_lite_settings.bin";
constexpr const char* kSettingsTempPath = "/t_echo_lite_settings.bin.tmp";
constexpr const char* kLogTag = "[t-echo-lite][settings]";
constexpr uint32_t kSettingsMagic = 0x54454C54UL; // TLET
constexpr uint16_t kSettingsVersion = 6;
constexpr uint8_t kDefaultToneVolume = 45;
constexpr uint8_t kDefaultStatusLedColor = 0;
constexpr uint8_t kDefaultKeyboardLightEnabled = 0;
constexpr uint8_t kDefaultMessageKeyboardLightEnabled = 1;
constexpr uint32_t kDeferredSaveDebounceMs = 1500UL;
constexpr uint32_t kImmediateSaveRetryDelayMs = 20UL;

struct PersistedPayload
{
    app::AppConfig config;
    uint8_t tone_volume = kDefaultToneVolume;
    uint8_t status_led_color = kDefaultStatusLedColor;
    uint8_t keyboard_light_enabled = kDefaultKeyboardLightEnabled;
    uint8_t message_keyboard_light_enabled = kDefaultMessageKeyboardLightEnabled;
    uint8_t reserved[5] = {};
};

struct CachedSettings
{
    app::AppConfig config;
    uint8_t tone_volume = kDefaultToneVolume;
    uint8_t status_led_color = kDefaultStatusLedColor;
    bool keyboard_light_enabled = false;
    bool message_keyboard_light_enabled = true;
};

bool s_cache_loaded = false;
CachedSettings s_cache{};
StoreStatus s_last_load_status = StoreStatus::NotFound;
StoreStatus s_last_save_status = StoreStatus::NotFound;
bool s_deferred_save_pending = false;
uint32_t s_last_dirty_ms = 0;
bool s_save_in_progress = false;
uint32_t s_last_save_attempt_ms = 0;
// Keep persistence payload scratch off the nRF52 task stack. Protocol switching
// can nest save + verify while UI/GPS code continues to run, and these payloads
// are large enough to make the stack path fragile.
FileHeader s_file_header_scratch{};
PersistedPayload s_payload_scratch{};

class ScopedGpsSuspend
{
  public:
    ScopedGpsSuspend()
        : board_(&::boards::t_echo_lite::TEchoLiteBoard::instance()),
          resume_(board_->gpsEnabled())
    {
        if (resume_)
        {
            board_->suspendGps();
        }
    }

    ~ScopedGpsSuspend()
    {
        if (resume_)
        {
            board_->resumeGps();
        }
    }

    ScopedGpsSuspend(const ScopedGpsSuspend&) = delete;
    ScopedGpsSuspend& operator=(const ScopedGpsSuspend&) = delete;

  private:
    ::boards::t_echo_lite::TEchoLiteBoard* board_ = nullptr;
    bool resume_ = false;
};

int8_t clampTxPower(int8_t value)
{
    if (value < app::AppConfig::kTxPowerMinDbm)
    {
        return app::AppConfig::kTxPowerMinDbm;
    }
    if (value > app::AppConfig::kTxPowerMaxDbm)
    {
        return app::AppConfig::kTxPowerMaxDbm;
    }
    return value;
}

uint8_t clampToneVolume(uint8_t volume)
{
    return static_cast<uint8_t>(std::min<unsigned>(volume, 100U));
}

const char* statusToText(StoreStatus status)
{
    return settings_file::statusText(status);
}

bool removeInvalidSettingsFile(StoreStatus status)
{
    if (!InternalFS.exists(kSettingsPath))
    {
        return true;
    }

    if (InternalFS.remove(kSettingsPath))
    {
        Serial.printf("[T-Echo Lite][settings] removed corrupt store status=%s\n",
                      statusToText(status));
        return true;
    }

    Serial.printf("[T-Echo Lite][settings] failed to remove corrupt store status=%s\n",
                  statusToText(status));
    return false;
}

uint32_t currentSettingsFileSize()
{
    return static_cast<uint32_t>(sizeof(FileHeader) + sizeof(PersistedPayload));
}

void resetCacheToDefaults()
{
    s_cache = CachedSettings{};
    s_cache.tone_volume = kDefaultToneVolume;
    s_cache.status_led_color = kDefaultStatusLedColor;
    s_cache.keyboard_light_enabled = (kDefaultKeyboardLightEnabled != 0);
    s_cache.message_keyboard_light_enabled = (kDefaultMessageKeyboardLightEnabled != 0);
    normalizeConfig(s_cache.config);
}

bool loadFromFs()
{
    if (!::platform::nrf52::arduino_common::internal_fs::ensureMounted(true, kLogTag))
    {
        s_last_load_status = StoreStatus::FsInitFailed;
        return false;
    }

    if (!InternalFS.exists(kSettingsPath))
    {
        s_last_load_status = StoreStatus::NotFound;
        return false;
    }

    auto file = InternalFS.open(kSettingsPath, FILE_O_READ);
    if (!file)
    {
        s_last_load_status = StoreStatus::OpenFailed;
        Serial.printf("[T-Echo Lite][settings] open failed path=%s\n", kSettingsPath);
        return false;
    }

    const uint32_t actual_size = file.size();
    const uint32_t expected_current_size = currentSettingsFileSize();
    if (actual_size != expected_current_size)
    {
        file.close();
        s_last_load_status = StoreStatus::PayloadSizeMismatch;
        (void)removeInvalidSettingsFile(s_last_load_status);
        Serial.printf("[T-Echo Lite][settings] removed non-current store actual=%lu expected=%lu version=%u\n",
                      static_cast<unsigned long>(actual_size),
                      static_cast<unsigned long>(expected_current_size),
                      static_cast<unsigned>(kSettingsVersion));
        return false;
    }

    if (actual_size < sizeof(FileHeader))
    {
        file.close();
        s_last_load_status = StoreStatus::PayloadSizeMismatch;
        (void)removeInvalidSettingsFile(s_last_load_status);
        Serial.printf("[T-Echo Lite][settings] size mismatch actual=%lu expected_at_least=%lu\n",
                      static_cast<unsigned long>(actual_size),
                      static_cast<unsigned long>(sizeof(FileHeader)));
        return false;
    }

    FileHeader header{};
    if (file.read(&header, sizeof(header)) != sizeof(header))
    {
        file.close();
        s_last_load_status = StoreStatus::ReadFailed;
        Serial.printf("[T-Echo Lite][settings] header read failed\n");
        return false;
    }

    if (header.magic != kSettingsMagic)
    {
        file.close();
        s_last_load_status = StoreStatus::HeaderInvalid;
        (void)removeInvalidSettingsFile(s_last_load_status);
        Serial.printf("[T-Echo Lite][settings] magic mismatch got=0x%08lX expected=0x%08lX\n",
                      static_cast<unsigned long>(header.magic),
                      static_cast<unsigned long>(kSettingsMagic));
        return false;
    }

    if (header.version != kSettingsVersion)
    {
        file.close();
        s_last_load_status = StoreStatus::VersionMismatch;
        (void)removeInvalidSettingsFile(s_last_load_status);
        Serial.printf("[T-Echo Lite][settings] removed old-version store got=%u expected=%u\n",
                      static_cast<unsigned>(header.version),
                      static_cast<unsigned>(kSettingsVersion));
        return false;
    }

    if (header.payload_size != sizeof(PersistedPayload) ||
        actual_size != static_cast<uint32_t>(sizeof(FileHeader) + sizeof(PersistedPayload)))
    {
        file.close();
        s_last_load_status = StoreStatus::PayloadSizeMismatch;
        (void)removeInvalidSettingsFile(s_last_load_status);
        Serial.printf("[T-Echo Lite][settings] payload size mismatch got=%lu expected=%lu actual=%lu\n",
                      static_cast<unsigned long>(header.payload_size),
                      static_cast<unsigned long>(sizeof(PersistedPayload)),
                      static_cast<unsigned long>(actual_size));
        return false;
    }

    auto& payload = s_payload_scratch;
    std::memset(&payload, 0, sizeof(payload));
    if (file.read(&payload, sizeof(payload)) != sizeof(payload))
    {
        file.close();
        s_last_load_status = StoreStatus::ReadFailed;
        Serial.printf("[T-Echo Lite][settings] payload read failed\n");
        return false;
    }
    file.close();

    const uint32_t actual_crc = crc32(reinterpret_cast<const uint8_t*>(&payload), sizeof(payload));
    if (actual_crc != header.crc32)
    {
        s_last_load_status = StoreStatus::CrcMismatch;
        (void)removeInvalidSettingsFile(s_last_load_status);
        Serial.printf("[T-Echo Lite][settings] crc mismatch got=0x%08lX expected=0x%08lX\n",
                      static_cast<unsigned long>(actual_crc),
                      static_cast<unsigned long>(header.crc32));
        return false;
    }

    s_cache.config = payload.config;
    normalizeConfig(s_cache.config);
    s_cache.tone_volume = clampToneVolume(payload.tone_volume);
    s_cache.status_led_color =
        static_cast<uint8_t>(payload.status_led_color % ::boards::t_echo_lite::TEchoLiteBoard::statusLedColorCount());
    s_cache.keyboard_light_enabled = payload.keyboard_light_enabled != 0;
    s_cache.message_keyboard_light_enabled = payload.message_keyboard_light_enabled != 0;
    s_last_load_status = StoreStatus::Ok;
    Serial.printf("[T-Echo Lite][settings] load ok tone=%u ble=%u proto=%u version=%u\n",
                  static_cast<unsigned>(s_cache.tone_volume),
                  static_cast<unsigned>(s_cache.config.ble_enabled ? 1 : 0),
                  static_cast<unsigned>(s_cache.config.mesh_protocol),
                  static_cast<unsigned>(header.version));
    return true;
}

bool saveToFsOnce()
{
    Serial.printf("[T-Echo Lite][settings] save begin path=%s\n", kSettingsPath);

    if (!::platform::nrf52::arduino_common::internal_fs::ensureMounted(true, kLogTag))
    {
        s_last_save_status = StoreStatus::FsInitFailed;
        Serial.printf("%s ensureMounted failed\n", kLogTag);
        return false;
    }

    auto& payload = s_payload_scratch;
    std::memset(&payload, 0, sizeof(payload));
    payload.config = s_cache.config;
    payload.tone_volume = clampToneVolume(s_cache.tone_volume);
    payload.status_led_color =
        static_cast<uint8_t>(s_cache.status_led_color % ::boards::t_echo_lite::TEchoLiteBoard::statusLedColorCount());
    payload.keyboard_light_enabled = s_cache.keyboard_light_enabled ? 1U : 0U;
    payload.message_keyboard_light_enabled = s_cache.message_keyboard_light_enabled ? 1U : 0U;

    settings_file::ReplaceRequest request{};
    request.path = kSettingsPath;
    request.temp_path = kSettingsTempPath;
    request.fs_log_tag = kLogTag;
    request.log_prefix = "[T-Echo Lite][settings]";
    request.magic = kSettingsMagic;
    request.version = kSettingsVersion;
    request.payload = &payload;
    request.payload_size = sizeof(PersistedPayload);
    request.header_scratch = &s_file_header_scratch;
    request.verify_payload_scratch = &s_payload_scratch;
    request.allow_format_recovery = true;

    settings_file::ReplaceResult result{};
    s_last_save_status = settings_file::replaceSettingsFile(request, &result);
    if (s_last_save_status != StoreStatus::Ok)
    {
        return false;
    }

    s_last_save_status = StoreStatus::Ok;
    Serial.printf("[T-Echo Lite][settings] save ok size=%lu crc=0x%08lx tone=%u\n",
                  static_cast<unsigned long>(sizeof(PersistedPayload)),
                  static_cast<unsigned long>(result.crc32),
                  static_cast<unsigned>(payload.tone_volume));
    return true;
}

bool saveToFs()
{
    if (s_save_in_progress)
    {
        Serial.printf("[T-Echo Lite][settings] save skipped: already in progress\n");
        return false;
    }

    s_save_in_progress = true;
    s_last_save_attempt_ms = millis();
    ScopedGpsSuspend suspend_gps;

    bool ok = saveToFsOnce();
    if (!ok)
    {
        Serial.printf("[T-Echo Lite][settings] save first attempt failed status=%s retry_delay_ms=%lu\n",
                      statusToText(s_last_save_status),
                      static_cast<unsigned long>(kImmediateSaveRetryDelayMs));

        delay(kImmediateSaveRetryDelayMs);

        ok = saveToFsOnce();
        if (!ok)
        {
            Serial.printf("[T-Echo Lite][settings] save retry failed status=%s\n",
                          statusToText(s_last_save_status));
        }
    }

    s_save_in_progress = false;
    return ok;
}

void markDeferredSaveDirty()
{
    s_deferred_save_pending = true;
    s_last_dirty_ms = millis();
}

void ensureCacheLoaded()
{
    if (s_cache_loaded)
    {
        return;
    }

    resetCacheToDefaults();
    (void)loadFromFs();
    s_cache_loaded = true;
}

} // namespace

void normalizeConfig(app::AppConfig& config)
{
    Serial.printf("[T-Echo Lite][settings] normalize start proto=%u mt_region=%u ok_to_mqtt=%u ignore_mqtt=%u gps_ms=%lu\n",
                  static_cast<unsigned>(config.mesh_protocol),
                  static_cast<unsigned>(config.meshtastic_config.region),
                  config.meshtastic_config.config_ok_to_mqtt ? 1U : 0U,
                  config.meshtastic_config.ignore_mqtt ? 1U : 0U,
                  static_cast<unsigned long>(config.gps_interval_ms));
    Serial2.printf("[T-Echo Lite][settings] normalize start proto=%u mt_region=%u ok_to_mqtt=%u ignore_mqtt=%u gps_ms=%lu\n",
                   static_cast<unsigned>(config.mesh_protocol),
                   static_cast<unsigned>(config.meshtastic_config.region),
                   config.meshtastic_config.config_ok_to_mqtt ? 1U : 0U,
                   config.meshtastic_config.ignore_mqtt ? 1U : 0U,
                   static_cast<unsigned long>(config.gps_interval_ms));
    if (!chat::infra::isValidMeshProtocol(config.mesh_protocol))
    {
        config.mesh_protocol = chat::MeshProtocol::Meshtastic;
    }
    Serial.printf("[T-Echo Lite][settings] normalize post-proto proto=%u\n",
                  static_cast<unsigned>(config.mesh_protocol));
    Serial2.printf("[T-Echo Lite][settings] normalize post-proto proto=%u\n",
                   static_cast<unsigned>(config.mesh_protocol));

    if (chat::meshtastic::findRegion(
            static_cast<meshtastic_Config_LoRaConfig_RegionCode>(config.meshtastic_config.region)) == nullptr)
    {
        config.meshtastic_config.region = app::AppConfig::kDefaultRegionCode;
    }
    Serial.printf("[T-Echo Lite][settings] normalize post-region mt_region=%u\n",
                  static_cast<unsigned>(config.meshtastic_config.region));
    Serial2.printf("[T-Echo Lite][settings] normalize post-region mt_region=%u\n",
                   static_cast<unsigned>(config.meshtastic_config.region));

    config.meshtastic_config.tx_power = clampTxPower(config.meshtastic_config.tx_power);
    config.meshcore_config.tx_power = clampTxPower(config.meshcore_config.tx_power);
    Serial.printf("[T-Echo Lite][settings] normalize post-tx mt_tx=%d mc_tx=%d\n",
                  static_cast<int>(config.meshtastic_config.tx_power),
                  static_cast<int>(config.meshcore_config.tx_power));
    Serial2.printf("[T-Echo Lite][settings] normalize post-tx mt_tx=%d mc_tx=%d\n",
                   static_cast<int>(config.meshtastic_config.tx_power),
                   static_cast<int>(config.meshcore_config.tx_power));

    if (!chat::meshcore::isValidRegionPresetId(config.meshcore_config.meshcore_region_preset))
    {
        config.meshcore_config.meshcore_region_preset = 0;
    }
    Serial.printf("[T-Echo Lite][settings] normalize post-mc-preset preset=%u\n",
                  static_cast<unsigned>(config.meshcore_config.meshcore_region_preset));
    Serial2.printf("[T-Echo Lite][settings] normalize post-mc-preset preset=%u\n",
                   static_cast<unsigned>(config.meshcore_config.meshcore_region_preset));

    if (config.gps_interval_ms == 0)
    {
        config.gps_interval_ms = 60000UL;
    }
    Serial.printf("[T-Echo Lite][settings] normalize done gps_ms=%lu\n",
                  static_cast<unsigned long>(config.gps_interval_ms));
    Serial2.printf("[T-Echo Lite][settings] normalize done gps_ms=%lu\n",
                   static_cast<unsigned long>(config.gps_interval_ms));
}

bool loadAppConfig(app::AppConfig& config)
{
    ensureCacheLoaded();
    config = s_cache.config;
    normalizeConfig(config);
    s_cache.config = config;
    return s_last_load_status == StoreStatus::Ok;
}

void cacheAppConfig(const app::AppConfig& config)
{
    ensureCacheLoaded();
    s_cache.config = config;
    normalizeConfig(s_cache.config);
}

bool saveAppConfig(const app::AppConfig& config)
{
    ensureCacheLoaded();
    s_cache.config = config;
    normalizeConfig(s_cache.config);
    s_deferred_save_pending = false;
    return saveToFs();
}

void queueSaveAppConfig(const app::AppConfig& config)
{
    ensureCacheLoaded();
    s_cache.config = config;
    normalizeConfig(s_cache.config);
    markDeferredSaveDirty();
}

uint8_t loadMessageToneVolume()
{
    ensureCacheLoaded();
    return s_cache.tone_volume;
}

bool saveMessageToneVolume(uint8_t volume)
{
    ensureCacheLoaded();
    s_cache.tone_volume = clampToneVolume(volume);
    s_deferred_save_pending = false;
    return saveToFs();
}

void queueSaveMessageToneVolume(uint8_t volume)
{
    ensureCacheLoaded();
    s_cache.tone_volume = clampToneVolume(volume);
    markDeferredSaveDirty();
}

uint8_t loadStatusLedColor()
{
    ensureCacheLoaded();
    return static_cast<uint8_t>(s_cache.status_led_color %
                                ::boards::t_echo_lite::TEchoLiteBoard::statusLedColorCount());
}

void queueSaveStatusLedColor(uint8_t color_index)
{
    ensureCacheLoaded();
    s_cache.status_led_color =
        static_cast<uint8_t>(color_index % ::boards::t_echo_lite::TEchoLiteBoard::statusLedColorCount());
    markDeferredSaveDirty();
}

bool loadKeyboardLightEnabled()
{
    ensureCacheLoaded();
    return s_cache.keyboard_light_enabled;
}

void queueSaveKeyboardLightEnabled(bool enabled)
{
    ensureCacheLoaded();
    s_cache.keyboard_light_enabled = enabled;
    markDeferredSaveDirty();
}

bool loadMessageKeyboardLightEnabled()
{
    ensureCacheLoaded();
    return s_cache.message_keyboard_light_enabled;
}

void queueSaveMessageKeyboardLightEnabled(bool enabled)
{
    ensureCacheLoaded();
    s_cache.message_keyboard_light_enabled = enabled;
    markDeferredSaveDirty();
}

bool tickDeferredSave()
{
    ensureCacheLoaded();
    if (!s_deferred_save_pending)
    {
        return false;
    }

    if (s_save_in_progress)
    {
        return false;
    }

    const uint32_t now_ms = millis();
    if ((now_ms - s_last_dirty_ms) < kDeferredSaveDebounceMs)
    {
        return false;
    }

    s_deferred_save_pending = false;
    if (saveToFs())
    {
        return true;
    }

    s_deferred_save_pending = true;
    s_last_dirty_ms = millis();
    return false;
}

bool hasDeferredSavePending()
{
    ensureCacheLoaded();
    return s_deferred_save_pending;
}

StoreStatus lastLoadStatus()
{
    return s_last_load_status;
}

StoreStatus lastSaveStatus()
{
    return s_last_save_status;
}

const char* statusLabel(StoreStatus status)
{
    return statusToText(status);
}

} // namespace boards::t_echo_lite::settings_store
