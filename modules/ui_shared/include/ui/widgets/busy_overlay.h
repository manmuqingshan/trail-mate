#pragma once

namespace ui::widgets::busy_overlay
{

void show(const char* title, const char* detail = nullptr);
void hide();
bool visible();

} // namespace ui::widgets::busy_overlay
