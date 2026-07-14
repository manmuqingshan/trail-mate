#include "ui/screens/network/micron_markup_contract.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace ui::screens::network::micron
{
namespace
{

constexpr std::size_t kReticulumHashTextLen = 32;

bool ascii_space(char ch)
{
    return ch == ' ' || ch == '\t';
}

void copy_range(char* out, std::size_t out_len, const char* start, std::size_t len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    if (!start)
    {
        out[0] = '\0';
        return;
    }
    const std::size_t copy_len = len < out_len - 1U ? len : out_len - 1U;
    std::memcpy(out, start, copy_len);
    out[copy_len] = '\0';
}

bool is_hex_text(const char* text, std::size_t len)
{
    if (!text)
    {
        return false;
    }
    for (std::size_t i = 0; i < len; ++i)
    {
        if (!std::isxdigit(static_cast<unsigned char>(text[i])))
        {
            return false;
        }
    }
    return true;
}

uint8_t hex_nibble(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return static_cast<uint8_t>(ch - '0');
    }
    if (ch >= 'A' && ch <= 'F')
    {
        return static_cast<uint8_t>(ch - 'A' + 10);
    }
    if (ch >= 'a' && ch <= 'f')
    {
        return static_cast<uint8_t>(ch - 'a' + 10);
    }
    return 0xFF;
}

std::size_t utf8_step_len(unsigned char lead)
{
    if ((lead & 0x80U) == 0)
    {
        return 1;
    }
    if ((lead & 0xE0U) == 0xC0U)
    {
        return 2;
    }
    if ((lead & 0xF0U) == 0xE0U)
    {
        return 3;
    }
    if ((lead & 0xF8U) == 0xF0U)
    {
        return 4;
    }
    return 1;
}

const char* strip_known_scheme(const char* target)
{
    if (!target)
    {
        return "";
    }
    static constexpr const char* kNomadScheme = "nomadnetwork://";
    static constexpr const char* kLxmfScheme = "lxmf://";
    if (std::strncmp(target, kNomadScheme, std::strlen(kNomadScheme)) == 0)
    {
        return target + std::strlen(kNomadScheme);
    }
    if (std::strncmp(target, kLxmfScheme, std::strlen(kLxmfScheme)) == 0)
    {
        return target + std::strlen(kLxmfScheme);
    }
    return target;
}

bool starts_with_page_path(const char* path)
{
    if (!path || path[0] == '\0')
    {
        return true;
    }
    if (std::strncmp(path, "/page", 5) != 0)
    {
        return false;
    }
    return path[5] == '\0' || path[5] == '/' || path[5] == '.';
}

LinkTargetKind classify_path(const char* path)
{
    if (!path || path[0] == '\0')
    {
        return LinkTargetKind::Page;
    }
    if (starts_with_page_path(path))
    {
        return LinkTargetKind::Page;
    }
    if (path[0] == '/')
    {
        return LinkTargetKind::Resource;
    }
    return LinkTargetKind::Unknown;
}

void trim_span(Span& span)
{
    while (span.len > 0 && ascii_space(*span.start))
    {
        ++span.start;
        --span.len;
    }
    while (span.len > 0 && ascii_space(span.start[span.len - 1U]))
    {
        --span.len;
    }
}

void append_slug_char(char* out,
                      std::size_t out_len,
                      std::size_t& out_pos,
                      bool& pending_dash,
                      char ch)
{
    if (!out || out_len == 0)
    {
        return;
    }
    const unsigned char value = static_cast<unsigned char>(ch);
    if (value >= 0x80U)
    {
        if (pending_dash && out_pos > 0 && out_pos + 1U < out_len)
        {
            out[out_pos++] = '-';
        }
        pending_dash = false;
        if (out_pos + 1U < out_len)
        {
            out[out_pos++] = ch;
        }
        return;
    }
    if (std::isalnum(value))
    {
        if (pending_dash && out_pos > 0 && out_pos + 1U < out_len)
        {
            out[out_pos++] = '-';
        }
        pending_dash = false;
        if (out_pos + 1U < out_len)
        {
            out[out_pos++] =
                static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        return;
    }
    if (out_pos > 0)
    {
        pending_dash = true;
    }
}

void append_slug_text(char* out,
                      std::size_t out_len,
                      std::size_t& out_pos,
                      bool& pending_dash,
                      const char* text,
                      std::size_t len)
{
    if (!text)
    {
        return;
    }
    for (std::size_t i = 0; i < len; ++i)
    {
        append_slug_char(out, out_len, out_pos, pending_dash, text[i]);
    }
}

void append_slug_utf8(char* out,
                      std::size_t out_len,
                      std::size_t& out_pos,
                      bool& pending_dash,
                      const char* text,
                      std::size_t len,
                      std::size_t& index)
{
    const unsigned char lead = static_cast<unsigned char>(text[index]);
    if (lead < 0x80U)
    {
        append_slug_char(out, out_len, out_pos, pending_dash, text[index]);
        ++index;
        return;
    }
    const std::size_t step = utf8_step_len(lead);
    if (index + step > len)
    {
        append_slug_char(out, out_len, out_pos, pending_dash, text[index]);
        ++index;
        return;
    }
    if (pending_dash && out_pos > 0 && out_pos + 1U < out_len)
    {
        out[out_pos++] = '-';
    }
    pending_dash = false;
    for (std::size_t i = 0; i < step && out_pos + 1U < out_len; ++i)
    {
        out[out_pos++] = text[index + i];
    }
    index += step;
}

void copy_anchor_value(const char* value, char* out, std::size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!value)
    {
        return;
    }
    std::size_t len = 0;
    while (value[len] != '\0' && anchor_name_char(value[len]))
    {
        ++len;
    }
    copy_range(out, out_len, value, len);
}

} // namespace

bool anchor_name_char(char ch)
{
    const unsigned char value = static_cast<unsigned char>(ch);
    return std::isalnum(value) || ch == '_' || ch == '-';
}

void slug_from_text(const char* text, char* out, std::size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!text)
    {
        return;
    }

    const std::size_t len = std::strlen(text);
    std::size_t out_pos = 0;
    bool pending_dash = false;
    bool escape = false;
    for (std::size_t i = 0; i < len;)
    {
        const char ch = text[i];
        if (escape)
        {
            append_slug_char(out, out_len, out_pos, pending_dash, ch);
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
            append_slug_utf8(out, out_len, out_pos, pending_dash, text, len, i);
            continue;
        }

        if (i + 1U >= len)
        {
            break;
        }
        const char tag = text[i + 1U];
        if (tag == '[')
        {
            const char* close = std::strchr(text + i + 2U, ']');
            if (!close)
            {
                i += 2U;
                continue;
            }
            const char* first_tick = std::strchr(text + i + 2U, '`');
            const char* visible_end =
                first_tick && first_tick < close ? first_tick : close;
            append_slug_text(out,
                             out_len,
                             out_pos,
                             pending_dash,
                             text + i + 2U,
                             static_cast<std::size_t>(visible_end -
                                                      (text + i + 2U)));
            i = static_cast<std::size_t>(close - text) + 1U;
            continue;
        }
        if (tag == '<' || tag == '{')
        {
            const char close_ch = tag == '<' ? '>' : '}';
            const char* close = std::strchr(text + i + 2U, close_ch);
            i = close ? static_cast<std::size_t>(close - text) + 1U : len;
            continue;
        }
        if (tag == ':')
        {
            i += 2U;
            while (i < len && anchor_name_char(text[i]))
            {
                ++i;
            }
            continue;
        }
        if ((tag == 'F' || tag == 'B') && i + 2U < len)
        {
            i += (text[i + 2U] == 'T' || text[i + 2U] == 't') ? 9U : 5U;
            if (i > len)
            {
                i = len;
            }
            continue;
        }
        i += 2U;
    }
    out[out_pos] = '\0';
}

bool parse_color_text(const char* text, std::size_t len, std::uint32_t* out)
{
    if (!text || !out)
    {
        return false;
    }
    if (len == 3 && (text[0] == 'g' || text[0] == 'G') &&
        std::isdigit(static_cast<unsigned char>(text[1])) &&
        std::isdigit(static_cast<unsigned char>(text[2])))
    {
        const std::uint32_t value =
            static_cast<std::uint32_t>((text[1] - '0') * 10 + (text[2] - '0'));
        const std::uint32_t byte = value > 99U ? 255U : (value * 255U) / 99U;
        *out = (byte << 16U) | (byte << 8U) | byte;
        return true;
    }
    if (len == 3 && is_hex_text(text, len))
    {
        const std::uint32_t r = static_cast<std::uint32_t>(hex_nibble(text[0]) * 17U);
        const std::uint32_t g = static_cast<std::uint32_t>(hex_nibble(text[1]) * 17U);
        const std::uint32_t b = static_cast<std::uint32_t>(hex_nibble(text[2]) * 17U);
        *out = (r << 16U) | (g << 8U) | b;
        return true;
    }
    if (len == 6 && is_hex_text(text, len))
    {
        std::uint32_t value = 0;
        for (std::size_t i = 0; i < 6; ++i)
        {
            value = (value << 4U) | hex_nibble(text[i]);
        }
        *out = value;
        return true;
    }
    return false;
}

ColorToken parse_color_token(const char* line, std::size_t len, std::size_t index)
{
    ColorToken token{};
    if (!line || index >= len)
    {
        return token;
    }
    std::uint32_t rgb = 0;
    if (index + 8U <= len && (line[index + 1U] == 'T' || line[index + 1U] == 't') &&
        parse_color_text(line + index + 2U, 6, &rgb))
    {
        token.valid = true;
        token.rgb = rgb;
        token.consumed = 8;
        return token;
    }
    if (index + 9U <= len && line[index + 4U] == '`' &&
        line[index + 5U] == line[index] && is_hex_text(line + index + 1U, 3) &&
        is_hex_text(line + index + 6U, 3))
    {
        char expanded[6] = {
            line[index + 6U],
            line[index + 1U],
            line[index + 7U],
            line[index + 2U],
            line[index + 8U],
            line[index + 3U],
        };
        if (parse_color_text(expanded, sizeof(expanded), &rgb))
        {
            token.valid = true;
            token.rgb = rgb;
            token.consumed = 9;
            return token;
        }
    }
    if (index + 4U <= len && parse_color_text(line + index + 1U, 3, &rgb))
    {
        token.valid = true;
        token.rgb = rgb;
        token.consumed = 4;
    }
    return token;
}

TableOpenOptions parse_table_opening_tag(const char* line)
{
    TableOpenOptions options{};
    if (!line || line[0] != '`' || line[1] != 't')
    {
        return options;
    }
    options.valid = true;
    const char* cursor = line + 2;
    if (*cursor == 'l' || *cursor == 'c' || *cursor == 'r')
    {
        options.has_align = true;
        options.align = *cursor == 'c'   ? Align::Center
                        : *cursor == 'r' ? Align::Right
                                         : Align::Left;
        ++cursor;
    }
    while (*cursor != '\0')
    {
        if (std::isdigit(static_cast<unsigned char>(*cursor)))
        {
            options.has_max_width = true;
            const unsigned value =
                static_cast<unsigned>(options.max_width) * 10U +
                static_cast<unsigned>(*cursor - '0');
            options.max_width =
                static_cast<std::uint16_t>(value > 999U ? 999U : value);
        }
        ++cursor;
    }
    return options;
}

std::size_t parse_table_cells(const char* row_text,
                              Span* cells,
                              std::size_t cell_capacity)
{
    if (!cells || cell_capacity == 0)
    {
        return 0;
    }
    for (std::size_t i = 0; i < cell_capacity; ++i)
    {
        cells[i] = Span{};
    }
    if (!row_text || !std::strchr(row_text, '|'))
    {
        return 0;
    }

    const char* cursor = row_text;
    const char* end = row_text + std::strlen(row_text);
    if (cursor < end && *cursor == '|')
    {
        ++cursor;
    }

    std::size_t count = 0;
    while (cursor <= end && count < cell_capacity)
    {
        const char* cell_start = cursor;
        while (cursor < end && *cursor != '|')
        {
            ++cursor;
        }

        Span span{cell_start, static_cast<std::size_t>(cursor - cell_start)};
        trim_span(span);
        if (!(span.len == 0 && cursor >= end && end > row_text && end[-1] == '|'))
        {
            cells[count++] = span;
        }

        if (cursor >= end)
        {
            break;
        }
        ++cursor;
        if (cursor >= end && end > row_text && end[-1] == '|')
        {
            break;
        }
    }
    return count;
}

bool table_separator_cell(const Span& cell)
{
    if (!cell.start || cell.len == 0)
    {
        return false;
    }
    bool dash = false;
    for (std::size_t i = 0; i < cell.len; ++i)
    {
        const char ch = cell.start[i];
        if (ch == '-')
        {
            dash = true;
            continue;
        }
        if (ch == ':' || ascii_space(ch))
        {
            continue;
        }
        return false;
    }
    return dash;
}

bool table_separator_row(const Span* cells, std::size_t count)
{
    if (!cells || count == 0)
    {
        return false;
    }
    for (std::size_t i = 0; i < count; ++i)
    {
        if (!table_separator_cell(cells[i]))
        {
            return false;
        }
    }
    return true;
}

Align table_separator_align(const Span& cell)
{
    if (!cell.start || cell.len == 0)
    {
        return Align::Left;
    }
    const bool left = cell.start[0] == ':';
    const bool right = cell.start[cell.len - 1U] == ':';
    if (left && right)
    {
        return Align::Center;
    }
    if (right)
    {
        return Align::Right;
    }
    return Align::Left;
}

LinkFields parse_link_fields(const char* fields)
{
    LinkFields result{};
    if (!fields || fields[0] == '\0')
    {
        return result;
    }
    result.has_fields = true;

    const char* token = fields;
    while (*token != '\0')
    {
        const char* end = token;
        while (*end != '\0' && *end != '|')
        {
            ++end;
        }
        const std::size_t token_len = static_cast<std::size_t>(end - token);
        if (token_len == 1 && token[0] == '*')
        {
            result.submits_fields = true;
        }
        else if (token_len > 0)
        {
            const char* equals = static_cast<const char*>(
                std::memchr(token, '=', token_len));
            if (equals)
            {
                const std::size_t key_len =
                    static_cast<std::size_t>(equals - token);
                if (key_len == 6 && std::strncmp(token, "anchor", 6) == 0)
                {
                    copy_anchor_value(equals + 1,
                                      result.anchor,
                                      sizeof(result.anchor));
                    result.has_anchor = result.anchor[0] != '\0';
                }
                else
                {
                    result.submits_fields = true;
                }
            }
            else
            {
                result.submits_fields = true;
            }
        }

        token = *end == '|' ? end + 1 : end;
    }
    return result;
}

bool append_anchor_to_target(const char* target,
                             const char* anchor,
                             char* out,
                             std::size_t out_len)
{
    if (!out || out_len == 0)
    {
        return false;
    }
    out[0] = '\0';
    if (!target)
    {
        return false;
    }
    if (!anchor || anchor[0] == '\0' || std::strchr(target, '#'))
    {
        std::snprintf(out, out_len, "%s", target);
        return false;
    }
    std::snprintf(out, out_len, "%s#%s", target, anchor);
    return true;
}

LinkTargetKind classify_link_target(const char* target)
{
    const char* value = strip_known_scheme(target);
    if (!value || value[0] == '\0')
    {
        return LinkTargetKind::Unknown;
    }
    if (value[0] == '#')
    {
        return LinkTargetKind::Anchor;
    }
    if (std::strncmp(value, "home:/", 6) == 0)
    {
        return LinkTargetKind::Page;
    }
    if (value[0] == ':')
    {
        return classify_path(value[1] != '\0' ? value + 1 : "");
    }
    if (value[0] == '/')
    {
        return classify_path(value);
    }
    if (is_hex_text(value, kReticulumHashTextLen))
    {
        const char* cursor = value + kReticulumHashTextLen;
        if (*cursor == ':')
        {
            ++cursor;
        }
        return classify_path(cursor);
    }
    if (std::strstr(value, "://") || std::strchr(value, ':'))
    {
        return LinkTargetKind::Resource;
    }
    return LinkTargetKind::Unknown;
}

const char* link_target_kind_label(LinkTargetKind kind)
{
    switch (kind)
    {
    case LinkTargetKind::Page:
        return "page";
    case LinkTargetKind::Anchor:
        return "anchor";
    case LinkTargetKind::Resource:
        return "resource";
    case LinkTargetKind::Unknown:
    default:
        return "unknown";
    }
}

bool extract_partial_url(const char* line, char* out, std::size_t out_len)
{
    if (!out || out_len == 0)
    {
        return false;
    }
    out[0] = '\0';
    const PartialReference partial = parse_partial_reference(line);
    if (!partial.valid)
    {
        return false;
    }
    copy_range(out, out_len, partial.target, std::strlen(partial.target));
    return out[0] != '\0';
}

PartialReference parse_partial_reference(const char* line)
{
    PartialReference partial{};
    if (!line || line[0] != '`' || line[1] != '{')
    {
        return partial;
    }

    const char* start = line + 2;
    const char* end = start;
    while (*end != '\0' && *end != '`' && *end != '}')
    {
        ++end;
    }
    while (end > start && ascii_space(end[-1]))
    {
        --end;
    }
    while (*start != '\0' && start < end && ascii_space(*start))
    {
        ++start;
    }
    copy_range(partial.target,
               sizeof(partial.target),
               start,
               static_cast<std::size_t>(end - start));
    if (partial.target[0] == '\0')
    {
        return partial;
    }
    partial.valid = true;
    partial.target_kind = classify_link_target(partial.target);

    if (*end == '`')
    {
        const char* hint = end + 1;
        while (ascii_space(*hint))
        {
            ++hint;
        }
        while (std::isdigit(static_cast<unsigned char>(*hint)))
        {
            partial.has_display_hint = true;
            const unsigned value =
                static_cast<unsigned>(partial.display_hint) * 10U +
                static_cast<unsigned>(*hint - '0');
            partial.display_hint =
                static_cast<std::uint16_t>(value > 999U ? 999U : value);
            ++hint;
        }
    }
    return partial;
}

} // namespace ui::screens::network::micron
