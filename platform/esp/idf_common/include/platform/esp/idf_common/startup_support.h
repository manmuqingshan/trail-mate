#pragma once

namespace platform::esp::idf_common::startup_support
{

void initializeClockProviders();
void logStartupBanner(const char* tag);

} // namespace platform::esp::idf_common::startup_support
