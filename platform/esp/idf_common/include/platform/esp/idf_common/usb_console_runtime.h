#pragma once

namespace platform::esp::idf_common::usb_console
{

bool ensure_started();
void stop();
bool is_started();

} // namespace platform::esp::idf_common::usb_console
