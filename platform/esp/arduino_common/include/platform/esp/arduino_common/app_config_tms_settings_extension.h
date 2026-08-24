/**
 * @file app_config_tms_settings_extension.h
 * @brief Platform-owned TMS records for Settings values outside AppConfig.
 *
 * AppConfig owns mesh, location, and protocol configuration.  This extension
 * owns the remaining user-editable runtime settings that have independent
 * NVS owners, so the SD working document remains a single configuration
 * authority without moving platform runtime state into AppConfig.
 */

#pragma once

#include "app/tms_config_codec.h"

namespace app::sd_tms::settings_extension
{

// Begin a fresh two-pass read before feeding records to the core Decoder.
// The bounded staging state is allocated only for the active decode and is
// released by endRead(), so it never becomes a permanent internal-RAM cost.
void beginRead(bool applying);
void endRead();

tms::RecordConsumeResult consumeRecord(void* context, const tms::RecordReader& reader);
bool finishDocument(void* context, bool applying, uint16_t schema_version);
bool writeRecords(void* context, tms::RecordWriter& writer);

} // namespace app::sd_tms::settings_extension
