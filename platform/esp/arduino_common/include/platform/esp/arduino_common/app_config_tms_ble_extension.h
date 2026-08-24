#pragma once

#include "app/tms_config_codec.h"

#include <cstdint>

namespace app::sd_tms::ble_extension
{

// A bounded PSRAM staging extension for BLE configuration that still has
// runtime owners outside AppConfig. Values are mirrored into their historical
// Preferences namespaces only after TMS validation succeeds.
bool beginRead();
void endRead();
tms::RecordConsumeResult consumeRecord(const tms::RecordReader& reader);
bool finishDocument(bool applying, uint16_t schema_version);
bool writeRecords(tms::RecordWriter& writer);

} // namespace app::sd_tms::ble_extension
