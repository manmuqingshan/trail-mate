/**
 * @file top_bar_power_presenter.h
 * @brief Unified presenter for TopBar right-side power status.
 */

#pragma once

#include "ui/widgets/top_bar.h"

namespace ui
{
namespace widgets
{
namespace top_bar_power
{

void bind(TopBar& bar);
void unbind(TopBar& bar);
void tick();
void refresh_now();

} // namespace top_bar_power
} // namespace widgets
} // namespace ui
