#include "screen_app_internal.h"

#include <cstdio>

namespace ui::mono::screens::screen_240x320::detail
{
namespace
{

constexpr lv_coord_t kConversationInset = 2;
constexpr lv_coord_t kBubbleWidth = 174;
constexpr lv_coord_t kBubbleContentWidth = kBubbleWidth - 16;

lv_obj_t* s_conversation_body = nullptr;

void style_message_row(lv_obj_t* row, bool outgoing)
{
    lv_obj_set_width(row, kContentWidth - (kConversationInset * 2));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(row,
                          LV_FLEX_ALIGN_START,
                          outgoing ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                          outgoing ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
}

void style_message_bubble(lv_obj_t* bubble, bool outgoing, bool source_unverified)
{
    lv_obj_set_width(bubble, kBubbleWidth);
    lv_obj_set_height(bubble, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bubble, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(bubble, outgoing ? lv_color_black() : lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bubble, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(bubble, source_unverified ? 2 : 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(bubble, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_radius(bubble, 9, LV_PART_MAIN);
    lv_obj_set_style_pad_left(bubble, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_right(bubble, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_top(bubble, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(bubble, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_row(bubble, 2, LV_PART_MAIN);
    lv_obj_clear_flag(bubble, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* create_bubble_label(lv_obj_t* bubble, bool outgoing)
{
    lv_obj_t* label = lv_label_create(bubble);
    lv_obj_set_width(label, kBubbleContentWidth);
    lv_obj_set_style_text_font(label, LV_FONT_DEFAULT, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, outgoing ? lv_color_white() : lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(label, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(label, 0, LV_PART_MAIN);
    return label;
}

void add_message_bubble(lv_obj_t* parent, const ::ui::chat::MessageRow& message)
{
    const bool outgoing = message.outgoing;
    lv_obj_t* row = lv_obj_create(parent);
    style_message_row(row, outgoing);

    lv_obj_t* bubble = lv_obj_create(row);
    style_message_bubble(bubble, outgoing, message.source_unverified);

    lv_obj_t* meta = create_bubble_label(bubble, outgoing);
    lv_label_set_long_mode(meta, LV_LABEL_LONG_CLIP);
    const char* sender = outgoing ? "YOU" : message.sender_label.c_str();
    if (sender == nullptr || sender[0] == '\0')
    {
        sender = outgoing ? "YOU" : "UNKNOWN";
    }
    char metadata[64]{};
    if (message.time_label.empty())
    {
        std::snprintf(metadata, sizeof(metadata), "%s", sender);
    }
    else
    {
        std::snprintf(metadata, sizeof(metadata), "%s  %s", sender, message.time_label.c_str());
    }
    lv_label_set_text(meta, metadata);
    lv_obj_set_style_text_opa(meta, LV_OPA_70, LV_PART_MAIN);

    lv_obj_t* text = create_bubble_label(bubble, outgoing);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    lv_label_set_text(text, message.text.empty() ? "(NO TEXT)" : message.text.c_str());
}

void create_conversation_body()
{
    s_conversation_body = lv_obj_create(s_state.root);
    lv_obj_set_pos(s_conversation_body, kMargin, kHeaderRuleY + kConversationInset);
    lv_obj_set_size(s_conversation_body,
                    kContentWidth,
                    kActionTop - kHeaderRuleY - (kConversationInset * 2));
    style_paper(s_conversation_body);
    lv_obj_add_flag(s_conversation_body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_conversation_body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_conversation_body, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_flex_flow(s_conversation_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_conversation_body,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_left(s_conversation_body, kConversationInset, LV_PART_MAIN);
    lv_obj_set_style_pad_right(s_conversation_body, kConversationInset, LV_PART_MAIN);
    lv_obj_set_style_pad_top(s_conversation_body, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(s_conversation_body, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_conversation_body, 5, LV_PART_MAIN);
}

} // namespace

void reset_chat_conversation_page()
{
    if (valid(s_conversation_body))
    {
        lv_obj_del(s_conversation_body);
    }
    s_conversation_body = nullptr;
}

void render_chat_conversation_page(ChatFlowState& flow, const char* title_prefix)
{
    const ::ui::chat::ChatWorkspaceSnapshot& snapshot = flow.snapshot;
    reset_chat_conversation_page();
    set_text(s_state.title, title_prefix ? title_prefix : "CHAT");
    set_text(s_state.subtitle, "THREAD");
    clear_lines_from(0);
    create_conversation_body();

    lv_obj_t* caption = create_text(s_conversation_body, kContentWidth - (kConversationInset * 2), LV_TEXT_ALIGN_CENTER);
    lv_label_set_long_mode(caption, LV_LABEL_LONG_CLIP);
    set_text(caption, snapshot.workspace_title.empty() ? "CONVERSATION" : snapshot.workspace_title.c_str());
    lv_obj_set_style_text_opa(caption, LV_OPA_70, LV_PART_MAIN);

    for (size_t index = 0; index < snapshot.message_count; ++index)
    {
        add_message_bubble(s_conversation_body, snapshot.messages[index]);
    }
    if (snapshot.message_count == 0)
    {
        lv_obj_t* empty = create_text(s_conversation_body,
                                      kContentWidth - (kConversationInset * 2),
                                      LV_TEXT_ALIGN_CENTER);
        set_text(empty, "NO MESSAGES IN THIS CONVERSATION");
    }
}

} // namespace ui::mono::screens::screen_240x320::detail
