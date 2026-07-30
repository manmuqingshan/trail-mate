#include "ui/screens/network/micron_markup_contract.h"

#include <cassert>
#include <cstdint>
#include <cstring>

namespace micron = ui::screens::network::micron;

int main()
{
    char slug[micron::kMaxAnchorNameBytes] = {};
    micron::slug_from_text(">Introduction & `!Setup`! `:local-anchor",
                           slug,
                           sizeof(slug));
    assert(std::strcmp(slug, "introduction-setup") == 0);
    micron::slug_from_text(">网络 页面", slug, sizeof(slug));
    assert(std::strcmp(slug, "网络-页面") == 0);
    micron::slug_from_text(">`[Visible label`/page/index.mu] `<12|name`value> `q",
                           slug,
                           sizeof(slug));
    assert(std::strcmp(slug, "visible-label") == 0);

    std::uint32_t color = 0;
    assert(micron::parse_color_text("6cf", 3, &color));
    assert(color == 0x66CCFF);
    assert(micron::parse_color_text("g50", 3, &color));
    assert(color == 0x808080);
    assert(micron::parse_color_text("123456", 6, &color));
    assert(color == 0x123456);
    micron::ColorToken short_color = micron::parse_color_token("F6cf", 4, 0);
    assert(short_color.valid);
    assert(short_color.rgb == 0x66CCFF);
    assert(short_color.consumed == 4);
    micron::ColorToken true_color = micron::parse_color_token("FT123456", 8, 0);
    assert(true_color.valid);
    assert(true_color.rgb == 0x123456);
    assert(true_color.consumed == 8);
    micron::ColorToken split_color = micron::parse_color_token("F123`F456", 9, 0);
    assert(split_color.valid);
    assert(split_color.rgb == 0x415263);
    assert(split_color.consumed == 9);
    assert(!micron::parse_color_token("Fzzzz", 5, 0).valid);

    micron::Span cells[micron::kMaxTableCells] = {};
    const std::size_t count =
        micron::parse_table_cells("| Name | :---: | --: |", cells, micron::kMaxTableCells);
    assert(count == 3);
    assert(cells[0].len == 4 && std::strncmp(cells[0].start, "Name", 4) == 0);
    assert(micron::table_separator_cell(cells[1]));
    assert(micron::table_separator_align(cells[1]) == micron::Align::Center);
    assert(micron::table_separator_align(cells[2]) == micron::Align::Right);

    const micron::TableOpenOptions centered = micron::parse_table_opening_tag("`tc30");
    assert(centered.valid);
    assert(centered.has_align);
    assert(centered.align == micron::Align::Center);
    assert(centered.has_max_width);
    assert(centered.max_width == 30);
    const micron::TableOpenOptions right_clamped =
        micron::parse_table_opening_tag("`tr12345");
    assert(right_clamped.valid);
    assert(right_clamped.has_align);
    assert(right_clamped.align == micron::Align::Right);
    assert(right_clamped.has_max_width);
    assert(right_clamped.max_width == 999);

    const micron::LinkFields anchor_only =
        micron::parse_link_fields("anchor=conclusion");
    assert(anchor_only.has_fields);
    assert(anchor_only.has_anchor);
    assert(!anchor_only.submits_fields);
    assert(std::strcmp(anchor_only.anchor, "conclusion") == 0);

    const micron::LinkFields submit_fields =
        micron::parse_link_fields("username|auth_token|action=view");
    assert(submit_fields.has_fields);
    assert(submit_fields.submits_fields);
    const micron::LinkFields anchor_and_submit =
        micron::parse_link_fields("anchor=matrix|*");
    assert(anchor_and_submit.has_anchor);
    assert(anchor_and_submit.submits_fields);

    char target[96] = {};
    const bool appended =
        micron::append_anchor_to_target("abcd:/page/doc.mu", "conclusion", target, sizeof(target));
    assert(appended);
    assert(std::strcmp(target, "abcd:/page/doc.mu#conclusion") == 0);

    assert(micron::classify_link_target("#local") == micron::LinkTargetKind::Anchor);
    assert(micron::classify_link_target("/page/index.mu") == micron::LinkTargetKind::Page);
    assert(micron::classify_link_target(":/file/manual.pdf") ==
           micron::LinkTargetKind::Resource);
    assert(micron::classify_link_target(
               "0123456789abcdef0123456789abcdef:/file/manual.pdf") ==
           micron::LinkTargetKind::Resource);
    assert(micron::classify_link_target("https://example.invalid/manual") ==
           micron::LinkTargetKind::Resource);
    assert(micron::classify_link_target("not-a-page") == micron::LinkTargetKind::Unknown);
    assert(std::strcmp(micron::link_target_kind_label(micron::LinkTargetKind::Page),
                       "page") == 0);

    char partial[96] = {};
    assert(micron::extract_partial_url("`{0123456789abcdef:/page/partial.mu`10}",
                                       partial,
                                       sizeof(partial)));
    assert(std::strcmp(partial, "0123456789abcdef:/page/partial.mu") == 0);
    const micron::PartialReference page_partial =
        micron::parse_partial_reference("`{:/page/live-status.mu`30}");
    assert(page_partial.valid);
    assert(page_partial.target_kind == micron::LinkTargetKind::Page);
    assert(page_partial.has_display_hint);
    assert(page_partial.display_hint == 30);
    assert(std::strcmp(page_partial.target, ":/page/live-status.mu") == 0);
    const micron::PartialReference resource_partial =
        micron::parse_partial_reference("`{:/file/photo.jpg`16}");
    assert(resource_partial.valid);
    assert(resource_partial.target_kind == micron::LinkTargetKind::Resource);

    return 0;
}
