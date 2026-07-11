#pragma once

#include "platform/ui/route_storage.h"

namespace ui::widgets::route_image_operation
{

void sync(const platform::ui::route_storage::RouteImageDownloadStatus& status);
void refresh();
void clear();

} // namespace ui::widgets::route_image_operation
