#include "ui/screens/network/micron_markup_contract.h"

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace micron = ui::screens::network::micron;

namespace
{

struct FixtureSummary
{
    unsigned header_colors = 0;
    unsigned headings = 0;
    unsigned anchors = 0;
    unsigned links = 0;
    unsigned page_links = 0;
    unsigned anchor_links = 0;
    unsigned resource_links = 0;
    unsigned unknown_links = 0;
    unsigned fields = 0;
    unsigned submit_links = 0;
    unsigned partials = 0;
    unsigned page_partials = 0;
    unsigned resource_partials = 0;
    unsigned tables = 0;
    unsigned table_rows = 0;
    unsigned literal_lines = 0;
    unsigned dividers = 0;
    unsigned color_tokens = 0;
    unsigned unknown_tags = 0;
    bool saw_unicode_text = false;
};

std::string repo_root()
{
    const std::string file = __FILE__;
    const std::string marker =
        "modules/ui_shared/tests/test_network_micron_renderer_fixture.cpp";
    const std::size_t pos = file.find(marker);
    assert(pos != std::string::npos);
    return file.substr(0, pos);
}

std::string read_text_file(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    assert(input.good());
    std::ostringstream out;
    out << input.rdbuf();
    return out.str();
}

void trim_cr(std::string& line)
{
    if (!line.empty() && line.back() == '\r')
    {
        line.pop_back();
    }
}

void count_link_target(FixtureSummary& summary, micron::LinkTargetKind kind)
{
    ++summary.links;
    switch (kind)
    {
    case micron::LinkTargetKind::Page:
        ++summary.page_links;
        break;
    case micron::LinkTargetKind::Anchor:
        ++summary.anchor_links;
        break;
    case micron::LinkTargetKind::Resource:
        ++summary.resource_links;
        break;
    case micron::LinkTargetKind::Unknown:
    default:
        ++summary.unknown_links;
        break;
    }
}

void scan_micron_link(const char* data, FixtureSummary& summary)
{
    char target[micron::kMaxLinkTargetBytes] = {};
    char fields[96] = {};
    const char* link_data = data && data[0] == '`' ? data + 1 : data;
    const char* first_tick = std::strchr(link_data, '`');
    if (!first_tick)
    {
        std::snprintf(target, sizeof(target), "%s", link_data ? link_data : "");
    }
    else
    {
        const char* second_tick = std::strchr(first_tick + 1, '`');
        if (second_tick)
        {
            const std::size_t target_len =
                static_cast<std::size_t>(second_tick - first_tick - 1);
            std::snprintf(target,
                          sizeof(target),
                          "%.*s",
                          static_cast<int>(target_len),
                          first_tick + 1);
            std::snprintf(fields, sizeof(fields), "%s", second_tick + 1);
        }
        else
        {
            std::snprintf(target, sizeof(target), "%s", first_tick + 1);
        }
    }

    const micron::LinkFields link_fields = micron::parse_link_fields(fields);
    if (link_fields.has_anchor)
    {
        char anchored[micron::kMaxLinkTargetBytes] = {};
        (void)micron::append_anchor_to_target(
            target, link_fields.anchor, anchored, sizeof(anchored));
        if (anchored[0] != '\0')
        {
            std::snprintf(target, sizeof(target), "%s", anchored);
        }
    }
    if (link_fields.submits_fields)
    {
        ++summary.submit_links;
    }
    count_link_target(summary, micron::classify_link_target(target));
}

void scan_inline(const std::string& text, FixtureSummary& summary)
{
    for (const unsigned char ch : text)
    {
        if (ch >= 0x80U)
        {
            summary.saw_unicode_text = true;
            break;
        }
    }
    bool escape = false;
    for (std::size_t i = 0; i < text.size();)
    {
        const char ch = text[i];
        if (escape)
        {
            escape = false;
            ++i;
            continue;
        }
        if (ch == '\\')
        {
            escape = true;
            ++i;
            continue;
        }
        if (ch != '`')
        {
            ++i;
            continue;
        }
        if (i + 1U >= text.size())
        {
            break;
        }
        if (text[i + 1U] == '`')
        {
            i += 2;
            continue;
        }

        const std::size_t tag_index = i + 1U;
        const char tag = text[tag_index];
        switch (tag)
        {
        case '!':
        case '_':
        case '*':
        case 'f':
        case 'b':
        case 'c':
        case 'l':
        case 'r':
        case 'a':
        case '`':
            i += 2;
            break;
        case 'F':
        case 'B':
        {
            const micron::ColorToken color =
                micron::parse_color_token(text.c_str(), text.size(), tag_index);
            if (color.valid)
            {
                ++summary.color_tokens;
                i = tag_index + color.consumed;
            }
            else
            {
                ++summary.unknown_tags;
                i += 2;
            }
            break;
        }
        case ':':
        {
            std::size_t len = 0;
            while (tag_index + 1U + len < text.size() &&
                   micron::anchor_name_char(text[tag_index + 1U + len]))
            {
                ++len;
            }
            if (len > 0)
            {
                ++summary.anchors;
            }
            i = tag_index + 1U + len;
            break;
        }
        case '<':
        {
            const char* spec_end = std::strchr(text.c_str() + tag_index + 1U, '`');
            const char* close = spec_end ? std::strchr(spec_end + 1U, '>') : nullptr;
            if (close)
            {
                ++summary.fields;
                i = static_cast<std::size_t>(close - text.c_str()) + 1U;
            }
            else
            {
                ++summary.unknown_tags;
                i += 2;
            }
            break;
        }
        case '[':
        {
            const char* close = std::strchr(text.c_str() + tag_index + 1U, ']');
            if (close)
            {
                char data[224] = {};
                const std::size_t len =
                    static_cast<std::size_t>(close - (text.c_str() + tag_index + 1U));
                std::snprintf(data, sizeof(data), "%.*s", static_cast<int>(len),
                              text.c_str() + tag_index + 1U);
                scan_micron_link(data, summary);
                i = static_cast<std::size_t>(close - text.c_str()) + 1U;
            }
            else
            {
                ++summary.unknown_tags;
                i += 2;
            }
            break;
        }
        case '{':
            ++summary.unknown_tags;
            while (i < text.size() && text[i] != '}')
            {
                ++i;
            }
            if (i < text.size())
            {
                ++i;
            }
            break;
        default:
            ++summary.unknown_tags;
            i += 2;
            break;
        }
    }
}

void scan_table_row(const std::string& line, FixtureSummary& summary)
{
    micron::Span cells[micron::kMaxTableCells] = {};
    const std::size_t count =
        micron::parse_table_cells(line.c_str(), cells, micron::kMaxTableCells);
    if (micron::table_separator_row(cells, count))
    {
        return;
    }
    if (count == 0)
    {
        scan_inline(line, summary);
        return;
    }
    ++summary.table_rows;
    for (std::size_t i = 0; i < count; ++i)
    {
        scan_inline(std::string(cells[i].start, cells[i].len), summary);
    }
}

FixtureSummary summarize_fixture(const std::string& body)
{
    FixtureSummary summary{};
    bool literal = false;
    bool table = false;
    std::istringstream lines(body);
    std::string line;
    while (std::getline(lines, line))
    {
        trim_cr(line);
        if (line.rfind("#!fg=", 0) == 0 || line.rfind("#!bg=", 0) == 0)
        {
            ++summary.header_colors;
            continue;
        }
        if (!literal && line == "`=")
        {
            literal = true;
            continue;
        }
        if (literal)
        {
            if (line == "`=")
            {
                literal = false;
            }
            else
            {
                ++summary.literal_lines;
            }
            continue;
        }

        const micron::TableOpenOptions table_options =
            micron::parse_table_opening_tag(line.c_str());
        if (table_options.valid)
        {
            table = !table;
            if (table)
            {
                ++summary.tables;
            }
            continue;
        }
        if (table)
        {
            scan_table_row(line, summary);
            continue;
        }
        if (line.empty() || line[0] == '#')
        {
            continue;
        }
        if (line[0] == '>')
        {
            ++summary.headings;
            const char* title = line.c_str();
            while (*title == '>')
            {
                ++title;
            }
            char slug[micron::kMaxAnchorNameBytes] = {};
            micron::slug_from_text(title, slug, sizeof(slug));
            if (slug[0] != '\0')
            {
                ++summary.anchors;
            }
            scan_inline(title, summary);
            continue;
        }
        if (line.size() > 0 && line[0] == '-' && (line.size() == 1 || line.size() == 2))
        {
            ++summary.dividers;
            continue;
        }
        if (line.rfind("`{", 0) == 0)
        {
            const micron::PartialReference partial =
                micron::parse_partial_reference(line.c_str());
            assert(partial.valid);
            ++summary.partials;
            if (partial.target_kind == micron::LinkTargetKind::Page)
            {
                ++summary.page_partials;
            }
            else if (partial.target_kind == micron::LinkTargetKind::Resource)
            {
                ++summary.resource_partials;
            }
            continue;
        }
        if (line[0] == '<')
        {
            scan_inline(line.substr(1), summary);
            continue;
        }
        scan_inline(line, summary);
    }
    return summary;
}

} // namespace

int main()
{
    const std::string path = repo_root() + "docs/reticulum/pages/trail-mate/index.mu";
    const std::string body = read_text_file(path);
    assert(body.size() > 3000U);
    assert(body.size() < 4096U);

    const FixtureSummary summary = summarize_fixture(body);
    assert(summary.header_colors == 2);
    assert(summary.headings >= 8);
    assert(summary.anchors >= 12);
    assert(summary.links >= 10);
    assert(summary.resource_links >= 1);
    assert(summary.fields >= 5);
    assert(summary.submit_links >= 1);
    assert(summary.partials >= 1);
    assert(summary.page_partials >= 1);
    assert(summary.tables >= 3);
    assert(summary.table_rows >= 17);
    assert(summary.literal_lines >= 2);
    assert(summary.dividers >= 2);
    assert(summary.color_tokens >= 7);
    assert(summary.unknown_tags >= 1);
    const FixtureSummary unicode = summarize_fixture("UTF-8 sample: \xE2\x9C\x93\n");
    assert(unicode.saw_unicode_text);

    const FixtureSummary message_board = summarize_fixture(read_text_file(
        repo_root() + "docs/reticulum/pages/corpus/nomadnet-messageboard.mu"));
    assert(message_board.header_colors == 2);
    assert(message_board.headings >= 1);
    assert(message_board.tables == 1);
    assert(message_board.table_rows >= 2);

    const FixtureSummary forms = summarize_fixture(read_text_file(
        repo_root() + "docs/reticulum/pages/corpus/nomadnet-forms.mu"));
    assert(forms.header_colors == 2);
    assert(forms.fields >= 6);
    assert(forms.submit_links == 2);
    assert(forms.page_links == 2);

    const FixtureSummary resilience = summarize_fixture(read_text_file(
        repo_root() + "docs/reticulum/pages/corpus/nomadnet-resilience.mu"));
    assert(resilience.anchor_links >= 1);
    assert(resilience.page_links >= 1);
    assert(resilience.resource_links >= 1);
    assert(resilience.page_partials == 1);
    assert(resilience.literal_lines >= 1);
    assert(resilience.unknown_tags >= 1);
    return 0;
}
