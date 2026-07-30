#pragma once

namespace platform::esp::idf_common::flash_storage_runtime
{

bool ensure_ready(bool format_if_mount_failed = false);
bool ready();
const char* mount_point();

} // namespace platform::esp::idf_common::flash_storage_runtime
