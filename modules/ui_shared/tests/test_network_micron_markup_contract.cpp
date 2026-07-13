#include "ui/screens/network/micron_markup_contract.h"

#include <cassert>
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

    char partial[96] = {};
    assert(micron::extract_partial_url("`{0123456789abcdef:/page/partial.mu`10}",
                                       partial,
                                       sizeof(partial)));
    assert(std::strcmp(partial, "0123456789abcdef:/page/partial.mu") == 0);

    return 0;
}
