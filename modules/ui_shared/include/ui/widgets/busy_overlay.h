#pragma once

namespace ui::widgets::busy_overlay
{

void show(const char* title, const char* detail = nullptr);
void update(const char* title, const char* detail = nullptr);
void set_progress(int progress_percent);
void hide();
bool visible();

} // namespace ui::widgets::busy_overlay
