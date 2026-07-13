#pragma once

#include <cstddef>
#include <cstdint>

namespace ui::screens::network::micron
{

constexpr std::size_t kMaxAnchorNameBytes = 48;
constexpr std::size_t kMaxTableCells = 6;

enum class Align : std::uint8_t
{
    Left,
    Center,
    Right,
};

enum class LinkTargetKind : std::uint8_t
{
    Page,
    Anchor,
    Resource,
    Unknown,
};

struct Span
{
    const char* start = nullptr;
    std::size_t len = 0;
};

struct TableOpenOptions
{
    bool valid = false;
    bool has_align = false;
    Align align = Align::Left;
    bool has_max_width = false;
    std::uint16_t max_width = 0;
};

struct LinkFields
{
    bool has_fields = false;
    bool submits_fields = false;
    bool has_anchor = false;
    char anchor[kMaxAnchorNameBytes] = {};
};

bool anchor_name_char(char ch);
void slug_from_text(const char* text, char* out, std::size_t out_len);

TableOpenOptions parse_table_opening_tag(const char* line);
std::size_t parse_table_cells(const char* row_text,
                              Span* cells,
                              std::size_t cell_capacity);
bool table_separator_cell(const Span& cell);
bool table_separator_row(const Span* cells, std::size_t count);
Align table_separator_align(const Span& cell);

LinkFields parse_link_fields(const char* fields);
bool append_anchor_to_target(const char* target,
                             const char* anchor,
                             char* out,
                             std::size_t out_len);
LinkTargetKind classify_link_target(const char* target);

bool extract_partial_url(const char* line, char* out, std::size_t out_len);

} // namespace ui::screens::network::micron
