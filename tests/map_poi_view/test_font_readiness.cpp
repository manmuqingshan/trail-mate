#include "ui/widgets/map/poi_overlay.h"
#include <cassert>
#include <cstring>

int main()
{
    lv_init();
    ui::widgets::map::PoiOverlay view;
    view.prepare_text("Road");
    int16_t width = 0, height = 0;
    assert(view.measure_text(&view, "Road", 4, false, width, height));
    assert(width > 0 && height > 0);
    const char* missing = "\xF4\x8F\xBF\xBF"; // valid Unicode with no glyph in the host font
    assert(!view.measure_text(&view, missing, std::strlen(missing), false, width, height));
    assert(!view.measure_text(&view, "\xE4\xBA", 2, false, width, height));
    lv_deinit();
}
