#pragma once

#include "app/app_config.h"

#include <cstdint>

namespace boards::t_echo_lite::settings_store
{

enum class StoreStatus : uint8_t
{
    Ok = 0,
    NotFound,
    FsInitFailed,
    OpenFailed,
    ReadFailed,
    WriteFailed,
    FlushFailed,
    HeaderInvalid,
    VersionMismatch,
    PayloadSizeMismatch,
    CrcMismatch,
    RenameFailed,
    BackupFailed,
};

void normalizeConfig(app::AppConfig& config);
bool loadAppConfig(app::AppConfig& config);
void cacheAppConfig(const app::AppConfig& config);
bool saveAppConfig(const app::AppConfig& config);
void queueSaveAppConfig(const app::AppConfig& config);
uint8_t loadMessageToneVolume();
bool saveMessageToneVolume(uint8_t volume);
void queueSaveMessageToneVolume(uint8_t volume);
uint8_t loadStatusLedColor();
void queueSaveStatusLedColor(uint8_t color_index);
bool loadKeyboardLightEnabled();
void queueSaveKeyboardLightEnabled(bool enabled);
bool loadMessageKeyboardLightEnabled();
void queueSaveMessageKeyboardLightEnabled(bool enabled);
bool tickDeferredSave();
bool hasDeferredSavePending();
StoreStatus lastLoadStatus();
StoreStatus lastSaveStatus();
const char* statusLabel(StoreStatus status);

} // namespace boards::t_echo_lite::settings_store
