#pragma once

#include "lvgl.h"
#include "ui/widgets/text_candidate_data.h"

namespace ui::widgets
{

void open_text_candidate_picker(lv_obj_t* textarea,
                                text_candidates::CandidateSet set);

lv_obj_t* add_text_candidate_button(lv_obj_t* toolbar,
                                    lv_obj_t* textarea,
                                    text_candidates::CandidateSet set,
                                    lv_group_t* group = nullptr,
                                    lv_obj_t* reference_button = nullptr);

} // namespace ui::widgets
