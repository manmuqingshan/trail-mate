#pragma once

#include "nrf52_node_target_board.h"

namespace trailmate::apps::nrf52_node::ui_runtime
{
bool initialize();
void bindChatObservers();
void appendBootLog(const char* line);
void tick(const target_board::BoardInputEvent* event);
void showDisplayProbe();

} // namespace trailmate::apps::nrf52_node::ui_runtime
