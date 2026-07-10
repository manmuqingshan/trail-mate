#include "platform/ui/reticulum_directory_runtime.h"

#include "platform/linux/runtime_paths.h"
#include "platform/ui/device_runtime.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace platform::ui::reticulum_directory
{
namespace
{

constexpr const char* kRelativeDir = "trailmate/reticulum";
constexpr const char* kAnnouncesRelativePath = "trailmate/reticulum/announces.tsv";
constexpr const char* kAnnouncesRelativeTempPath = "trailmate/reticulum/announces.tmp";
constexpr const char* kLxmfAddressesRelativePath = "trailmate/reticulum/lxmf_addresses.tsv";
constexpr const char* kLxmfAddressesRelativeTempPath = "trailmate/reticulum/lxmf_addresses.tmp";
constexpr const char* kAnnouncesLogicalPath = "/trailmate/reticulum/announces.tsv";
constexpr const char* kLxmfAddressesLogicalPath = "/trailmate/reticulum/lxmf_addresses.tsv";
constexpr std::size_t kMaxDirectoryEntries = 1024;
constexpr std::size_t kMaxLineBytes = 4096;
constexpr std::size_t kMaxTsvFields = 14;

struct TsvFields
{
    std::string_view values[kMaxTsvFields];
    std::size_t count = 0;
};

void copy_text(char* out, std::size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%s", text ? text : "");
}

void copy_view(char* out, std::size_t out_len, std::string_view text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    const std::size_t len = text.size() < out_len - 1U ? text.size() : out_len - 1U;
    if (len != 0)
    {
        std::memcpy(out, text.data(), len);
    }
    out[len] = '\0';
}

void set_status(Status& out, const char* message, const char* detail = nullptr)
{
    copy_text(out.message, sizeof(out.message), message);
    copy_text(out.detail, sizeof(out.detail), detail);
}

std::filesystem::path sd_root()
{
    return ::platform::linux_runtime::resolve_paths().sd_root;
}

std::filesystem::path path_under_sd(const char* relative)
{
    return sd_root() / (relative ? relative : "");
}

bool ensure_directory()
{
    return ::platform::linux_runtime::ensure_directory(sd_root() / kRelativeDir);
}

bool zero_hash(const uint8_t* hash, std::size_t len)
{
    if (!hash)
    {
        return true;
    }
    for (std::size_t index = 0; index < len; ++index)
    {
        if (hash[index] != 0)
        {
            return false;
        }
    }
    return true;
}

void append_hex(std::string& out, const uint8_t* data, std::size_t len)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    if (!data || len == 0)
    {
        return;
    }
    out.reserve(out.size() + (len * 2U));
    for (std::size_t index = 0; index < len; ++index)
    {
        out.push_back(kHex[(data[index] >> 4U) & 0x0FU]);
        out.push_back(kHex[data[index] & 0x0FU]);
    }
}

std::string hex_text(const uint8_t* data, std::size_t len)
{
    std::string out;
    append_hex(out, data, len);
    return out;
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

bool parse_hex(std::string_view text, uint8_t* out, std::size_t out_len)
{
    if (!out || text.size() != out_len * 2U)
    {
        return false;
    }
    for (std::size_t index = 0; index < out_len; ++index)
    {
        const uint8_t hi = hex_nibble(text[index * 2U]);
        const uint8_t lo = hex_nibble(text[(index * 2U) + 1U]);
        if (hi == 0xFF || lo == 0xFF)
        {
            return false;
        }
        out[index] = static_cast<uint8_t>((hi << 4U) | lo);
    }
    return true;
}

bool copy_text_app_data_display_name_from_hex(std::string_view text,
                                              char* out,
                                              std::size_t out_len)
{
    if (!out || out_len == 0 || text.empty() || (text.size() % 2U) != 0)
    {
        return false;
    }
    const std::size_t byte_count = text.size() / 2U;
    if (byte_count > 96)
    {
        return false;
    }

    std::size_t used = 0;
    bool has_visible = false;
    for (std::size_t index = 0; index < byte_count; ++index)
    {
        const uint8_t hi = hex_nibble(text[index * 2U]);
        const uint8_t lo = hex_nibble(text[(index * 2U) + 1U]);
        if (hi == 0xFF || lo == 0xFF)
        {
            out[0] = '\0';
            return false;
        }
        uint8_t byte = static_cast<uint8_t>((hi << 4U) | lo);
        if (byte == '\t' || byte == '\r' || byte == '\n')
        {
            byte = ' ';
        }
        else if (byte == 0 || byte < 0x20 || byte == 0x7F)
        {
            out[0] = '\0';
            return false;
        }
        if (used + 1U < out_len)
        {
            out[used++] = static_cast<char>(byte);
        }
        if (byte != ' ')
        {
            has_visible = true;
        }
    }
    while (used != 0 && out[used - 1U] == ' ')
    {
        --used;
    }
    out[used] = '\0';
    return has_visible && used != 0;
}

std::string sanitize_field(const char* text)
{
    std::string out = text ? text : "";
    for (char& ch : out)
    {
        if (ch == '\t' || ch == '\r' || ch == '\n')
        {
            ch = ' ';
        }
    }
    return out;
}

std::string_view trim_view(std::string_view line)
{
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n' ||
                             line.back() == ' ' || line.back() == '\t'))
    {
        line.remove_suffix(1);
    }
    std::size_t start = 0;
    while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
    {
        ++start;
    }
    line.remove_prefix(start);
    return line;
}

bool read_line(std::istream& stream, std::string& out)
{
    out.clear();
    if (!std::getline(stream, out))
    {
        return false;
    }
    if (out.size() > kMaxLineBytes)
    {
        out.clear();
    }
    return true;
}

TsvFields split_tsv(std::string_view line)
{
    TsvFields out{};
    std::size_t start = 0;
    while (start <= line.size() && out.count < kMaxTsvFields)
    {
        const std::size_t end = line.find('\t', start);
        out.values[out.count++] =
            line.substr(start,
                        end == std::string::npos ? std::string::npos : end - start);
        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1U;
    }
    return out;
}

bool data_line(std::string_view line)
{
    return !line.empty() && line[0] != '#' && line.rfind("version\t", 0) != 0;
}

bool first_field_matches(std::string_view line, std::string_view key)
{
    const std::size_t tab = line.find('\t');
    const std::string_view first =
        tab == std::string::npos ? line : line.substr(0, tab);
    return first == key;
}

uint32_t parse_u32(std::string_view text, uint32_t fallback = 0)
{
    if (text.empty())
    {
        return fallback;
    }
    uint32_t value = 0;
    for (char ch : text)
    {
        if (ch < '0' || ch > '9')
        {
            return fallback;
        }
        value = (value * 10U) + static_cast<uint32_t>(ch - '0');
    }
    return value;
}

bool truthy(std::string_view text)
{
    return text == "1" || text == "true" || text == "yes" || text == "enabled";
}

char lower_ascii(char ch)
{
    if (ch >= 'A' && ch <= 'Z')
    {
        return static_cast<char>(ch - 'A' + 'a');
    }
    return ch;
}

bool contains_ci(std::string_view text, std::string_view query)
{
    if (query.empty())
    {
        return true;
    }
    if (query.size() > text.size())
    {
        return false;
    }
    for (std::size_t start = 0; start + query.size() <= text.size(); ++start)
    {
        bool match = true;
        for (std::size_t offset = 0; offset < query.size(); ++offset)
        {
            if (lower_ascii(text[start + offset]) != lower_ascii(query[offset]))
            {
                match = false;
                break;
            }
        }
        if (match)
        {
            return true;
        }
    }
    return false;
}

bool address_line_matches_query(std::string_view line, std::string_view query)
{
    const std::string_view trimmed_query = trim_view(query);
    if (trimmed_query.empty())
    {
        return true;
    }
    const TsvFields fields = split_tsv(line);
    return (fields.count > 0 && contains_ci(fields.values[0], trimmed_query)) ||
           (fields.count > 1 && contains_ci(fields.values[1], trimmed_query)) ||
           (fields.count > 4 && contains_ci(fields.values[4], trimmed_query));
}

uint32_t node_id_from_destination_hash(
    const uint8_t destination_hash[kReticulumHashSize])
{
    if (!destination_hash)
    {
        return 0;
    }
    return (static_cast<uint32_t>(destination_hash[12]) << 24) |
           (static_cast<uint32_t>(destination_hash[13]) << 16) |
           (static_cast<uint32_t>(destination_hash[14]) << 8) |
           static_cast<uint32_t>(destination_hash[15]);
}

std::string bool_text(bool value)
{
    return value ? "1" : "0";
}

std::string source_text(EntrySource source)
{
    switch (source)
    {
    case EntrySource::RuntimeRx:
        return "runtime_rx";
    case EntrySource::PathResponse:
        return "path_response";
    case EntrySource::Manual:
        return "manual";
    case EntrySource::Import:
        return "import";
    case EntrySource::Unknown:
    default:
        return "unknown";
    }
}

EntrySource parse_source(std::string_view source)
{
    if (source == "runtime_rx")
    {
        return EntrySource::RuntimeRx;
    }
    if (source == "path_response")
    {
        return EntrySource::PathResponse;
    }
    if (source == "manual")
    {
        return EntrySource::Manual;
    }
    if (source == "import")
    {
        return EntrySource::Import;
    }
    return EntrySource::Unknown;
}

std::string aspect_text(AnnounceAspect aspect)
{
    switch (aspect)
    {
    case AnnounceAspect::LxmfDelivery:
        return "lxmf.delivery";
    case AnnounceAspect::LxmfPropagation:
        return "lxmf.propagation";
    case AnnounceAspect::CallAudio:
        return "call.audio";
    case AnnounceAspect::NomadNetworkNode:
        return "nomadnetwork.node";
    case AnnounceAspect::Unknown:
    default:
        return "unknown";
    }
}

AnnounceAspect parse_aspect(std::string_view aspect)
{
    if (aspect == "lxmf.delivery")
    {
        return AnnounceAspect::LxmfDelivery;
    }
    if (aspect == "lxmf.propagation")
    {
        return AnnounceAspect::LxmfPropagation;
    }
    if (aspect == "call.audio")
    {
        return AnnounceAspect::CallAudio;
    }
    if (aspect == "nomadnetwork.node")
    {
        return AnnounceAspect::NomadNetworkNode;
    }
    return AnnounceAspect::Unknown;
}

template <typename Record>
void append_latest_record(Record* records,
                          std::size_t max_records,
                          std::size_t& count,
                          const Record& record)
{
    if (!records || max_records == 0)
    {
        return;
    }
    if (count < max_records)
    {
        records[count++] = record;
        return;
    }
    for (std::size_t index = 1; index < max_records; ++index)
    {
        records[index - 1U] = records[index];
    }
    records[max_records - 1U] = record;
}

template <typename Record>
void reverse_records(Record* records, std::size_t count)
{
    if (!records || count < 2)
    {
        return;
    }
    for (std::size_t left = 0, right = count - 1U; left < right; ++left, --right)
    {
        Record temp = records[left];
        records[left] = records[right];
        records[right] = temp;
    }
}

uint32_t find_existing_first_seen(const char* relative_path,
                                  const std::string& destination,
                                  std::size_t first_seen_field,
                                  uint32_t fallback)
{
    std::ifstream in(path_under_sd(relative_path), std::ios::binary);
    if (!in.is_open())
    {
        return fallback;
    }
    std::string line;
    while (read_line(in, line))
    {
        const std::string_view view = trim_view(line);
        if (data_line(view) && first_field_matches(view, destination))
        {
            const TsvFields fields = split_tsv(view);
            return fields.count > first_seen_field
                       ? parse_u32(fields.values[first_seen_field], fallback)
                       : fallback;
        }
    }
    return fallback;
}

bool parse_address_line(std::string_view line, LxmfAddressRecord& out)
{
    const TsvFields fields = split_tsv(line);
    if (fields.count < 11)
    {
        return false;
    }
    LxmfAddressRecord parsed{};
    if (!parse_hex(fields.values[0], parsed.destination_hash, kReticulumHashSize) ||
        !parse_hex(fields.values[1], parsed.identity_hash, kReticulumHashSize) ||
        !parse_hex(fields.values[2], parsed.enc_pub, kReticulumPublicKeySize) ||
        !parse_hex(fields.values[3], parsed.sig_pub, kReticulumPublicKeySize))
    {
        return false;
    }
    copy_view(parsed.display_name, sizeof(parsed.display_name), fields.values[4]);
    parsed.favorite = truthy(fields.values[5]);
    parsed.ignored = truthy(fields.values[6]);
    parsed.trusted = truthy(fields.values[7]);
    parsed.source = parse_source(fields.values[8]);
    parsed.first_seen_s = parse_u32(fields.values[9]);
    parsed.last_seen_s = parse_u32(fields.values[10]);
    parsed.valid = true;
    out = parsed;
    return true;
}

bool parse_announce_line(std::string_view line, AnnounceRecord& out)
{
    const TsvFields fields = split_tsv(line);
    if (fields.count < 12)
    {
        return false;
    }
    AnnounceRecord parsed{};
    if (!parse_hex(fields.values[0], parsed.destination_hash, kReticulumHashSize) ||
        !parse_hex(fields.values[1], parsed.identity_hash, kReticulumHashSize))
    {
        return false;
    }
    parsed.aspect = parse_aspect(fields.values[2]);
    parsed.source = parse_source(fields.values[3]);
    parsed.first_seen_s = parse_u32(fields.values[4]);
    parsed.last_seen_s = parse_u32(fields.values[5]);
    parsed.hops = static_cast<uint8_t>(parse_u32(fields.values[6], 0xFF) & 0xFFU);
    parsed.path_response = truthy(fields.values[7]);
    parsed.local_destination = truthy(fields.values[8]);
    parsed.delivery = truthy(fields.values[9]);
    parsed.propagation = truthy(fields.values[10]);
    copy_view(parsed.display_name, sizeof(parsed.display_name), fields.values[11]);
    if (parsed.display_name[0] == '\0' && fields.count >= 14)
    {
        (void)copy_text_app_data_display_name_from_hex(fields.values[13],
                                                       parsed.display_name,
                                                       sizeof(parsed.display_name));
    }
    parsed.valid = true;
    out = parsed;
    return true;
}

void preserve_address_flags(const char* relative_path,
                            const std::string& destination,
                            bool* favorite,
                            bool* ignored,
                            bool* trusted,
                            uint32_t* first_seen_s)
{
    std::ifstream in(path_under_sd(relative_path), std::ios::binary);
    if (!in.is_open())
    {
        return;
    }
    std::string line;
    while (read_line(in, line))
    {
        const std::string_view view = trim_view(line);
        if (data_line(view) && first_field_matches(view, destination))
        {
            LxmfAddressRecord parsed{};
            if (parse_address_line(view, parsed))
            {
                if (favorite)
                {
                    *favorite = parsed.favorite;
                }
                if (ignored)
                {
                    *ignored = parsed.ignored;
                }
                if (trusted)
                {
                    *trusted = parsed.trusted;
                }
                if (first_seen_s && parsed.first_seen_s != 0)
                {
                    *first_seen_s = parsed.first_seen_s;
                }
            }
            return;
        }
    }
}

bool write_line(std::ofstream& out, std::string_view line)
{
    out.write(line.data(), static_cast<std::streamsize>(line.size()));
    out.put('\n');
    return out.good();
}

Status stream_upsert_line(const char* relative_path,
                          const char* temp_relative_path,
                          const char* logical_path,
                          const char* title,
                          const char* success_message,
                          const std::string& destination,
                          const std::string& new_line)
{
    Status out{};
    out.supported = true;
    out.sd_present = ::platform::ui::device::card_ready();
    if (!out.sd_present)
    {
        set_status(out, "SD card required", logical_path);
        return out;
    }
    if (!ensure_directory())
    {
        set_status(out, "Cannot create Reticulum directory", "/trailmate/reticulum");
        return out;
    }

    const auto path = path_under_sd(relative_path);
    const auto temp_path = path_under_sd(temp_relative_path);
    std::error_code ec;
    out.file_present = std::filesystem::exists(path, ec) && !ec;
    std::filesystem::remove(temp_path, ec);

    std::ofstream out_file(temp_path, std::ios::binary | std::ios::trunc);
    if (!out_file.is_open())
    {
        set_status(out, "Cannot open Reticulum directory temp", logical_path);
        return out;
    }

    out_file << "# Trail Mate Reticulum " << title << "\nversion\t1\n";
    bool ok = out_file.good();
    std::size_t existing_data_lines = 0;
    if (ok && out.file_present)
    {
        std::ifstream count_file(path, std::ios::binary);
        if (!count_file.is_open())
        {
            out_file.close();
            std::filesystem::remove(temp_path, ec);
            set_status(out, "Cannot read Reticulum directory", logical_path);
            return out;
        }
        std::string old_line;
        while (read_line(count_file, old_line))
        {
            const std::string_view view = trim_view(old_line);
            if (data_line(view) && !first_field_matches(view, destination))
            {
                ++existing_data_lines;
            }
        }
    }

    const std::size_t skip_oldest =
        existing_data_lines + 1U > kMaxDirectoryEntries
            ? (existing_data_lines + 1U - kMaxDirectoryEntries)
            : 0;
    std::size_t kept = 0;
    std::size_t skipped = 0;
    if (ok && out.file_present)
    {
        std::ifstream in_file(path, std::ios::binary);
        if (!in_file.is_open())
        {
            out_file.close();
            std::filesystem::remove(temp_path, ec);
            set_status(out, "Cannot read Reticulum directory", logical_path);
            return out;
        }
        std::string old_line;
        while (read_line(in_file, old_line))
        {
            const std::string_view view = trim_view(old_line);
            if (data_line(view) && !first_field_matches(view, destination))
            {
                if (skipped < skip_oldest)
                {
                    ++skipped;
                    continue;
                }
                ok = write_line(out_file, view);
                if (!ok)
                {
                    break;
                }
                ++kept;
            }
        }
    }

    ok = ok && write_line(out_file, new_line);
    out_file.close();
    if (!ok)
    {
        std::filesystem::remove(temp_path, ec);
        set_status(out, "Cannot write Reticulum directory", logical_path);
        return out;
    }

    std::filesystem::remove(path, ec);
    std::filesystem::rename(temp_path, path, ec);
    if (ec)
    {
        std::filesystem::remove(temp_path, ec);
        set_status(out, "Cannot replace Reticulum directory", logical_path);
        return out;
    }

    out.file_present = true;
    out.saved = true;
    set_status(out, success_message, logical_path);
    return out;
}

std::string announce_line(const AnnounceRecord& record, uint32_t first_seen_s)
{
    std::string line;
    line.reserve(256U + (record.raw_packet_len * 2U) + (record.app_data_len * 2U));
    line += hex_text(record.destination_hash, kReticulumHashSize);
    line += "\t";
    line += hex_text(record.identity_hash, kReticulumHashSize);
    line += "\t";
    line += aspect_text(record.aspect);
    line += "\t";
    line += source_text(record.source);
    line += "\t";
    line += std::to_string(first_seen_s);
    line += "\t";
    line += std::to_string(record.last_seen_s);
    line += "\t";
    line += std::to_string(static_cast<unsigned>(record.hops));
    line += "\t";
    line += bool_text(record.path_response);
    line += "\t";
    line += bool_text(record.local_destination);
    line += "\t";
    line += bool_text(record.delivery);
    line += "\t";
    line += bool_text(record.propagation);
    line += "\t";
    line += sanitize_field(record.display_name);
    line += "\t";
    append_hex(line, record.raw_packet, record.raw_packet_len);
    line += "\t";
    append_hex(line, record.app_data, record.app_data_len);
    return line;
}

std::string address_line(const LxmfAddressRecord& record,
                         bool favorite,
                         bool ignored,
                         bool trusted,
                         uint32_t first_seen_s)
{
    std::string line;
    line.reserve(256);
    line += hex_text(record.destination_hash, kReticulumHashSize);
    line += "\t";
    line += hex_text(record.identity_hash, kReticulumHashSize);
    line += "\t";
    line += hex_text(record.enc_pub, kReticulumPublicKeySize);
    line += "\t";
    line += hex_text(record.sig_pub, kReticulumPublicKeySize);
    line += "\t";
    line += sanitize_field(record.display_name);
    line += "\t";
    line += bool_text(favorite);
    line += "\t";
    line += bool_text(ignored);
    line += "\t";
    line += bool_text(trusted);
    line += "\t";
    line += source_text(record.source);
    line += "\t";
    line += std::to_string(first_seen_s);
    line += "\t";
    line += std::to_string(record.last_seen_s);
    return line;
}

} // namespace

const char* announces_path()
{
    return kAnnouncesLogicalPath;
}

const char* lxmf_addresses_path()
{
    return kLxmfAddressesLogicalPath;
}

Status record_announce(const AnnounceRecord& record)
{
    Status out{};
    out.supported = true;
    out.sd_present = ::platform::ui::device::card_ready();
    if (!record.valid ||
        zero_hash(record.destination_hash, kReticulumHashSize) ||
        zero_hash(record.identity_hash, kReticulumHashSize))
    {
        set_status(out, "Invalid Reticulum announce", kAnnouncesLogicalPath);
        return out;
    }

    const std::string destination = hex_text(record.destination_hash, kReticulumHashSize);
    const uint32_t fallback_first_seen =
        record.first_seen_s != 0 ? record.first_seen_s : record.last_seen_s;
    const uint32_t first_seen_s =
        out.sd_present
            ? find_existing_first_seen(kAnnouncesRelativePath,
                                       destination,
                                       4,
                                       fallback_first_seen)
            : fallback_first_seen;
    return stream_upsert_line(kAnnouncesRelativePath,
                              kAnnouncesRelativeTempPath,
                              kAnnouncesLogicalPath,
                              "announces",
                              "Reticulum announce saved",
                              destination,
                              announce_line(record, first_seen_s));
}

Status record_lxmf_address(const LxmfAddressRecord& record)
{
    Status out{};
    out.supported = true;
    out.sd_present = ::platform::ui::device::card_ready();
    if (!record.valid ||
        zero_hash(record.destination_hash, kReticulumHashSize) ||
        zero_hash(record.identity_hash, kReticulumHashSize) ||
        zero_hash(record.enc_pub, kReticulumPublicKeySize) ||
        zero_hash(record.sig_pub, kReticulumPublicKeySize))
    {
        set_status(out, "Invalid LXMF address", kLxmfAddressesLogicalPath);
        return out;
    }

    const std::string destination = hex_text(record.destination_hash, kReticulumHashSize);
    const bool requested_favorite = record.favorite;
    const bool requested_ignored = record.ignored;
    const bool requested_trusted = record.trusted;
    bool favorite = record.favorite;
    bool ignored = record.ignored;
    bool trusted = record.trusted;
    uint32_t first_seen_s = record.first_seen_s != 0 ? record.first_seen_s : record.last_seen_s;
    if (out.sd_present)
    {
        preserve_address_flags(kLxmfAddressesRelativePath,
                               destination,
                               &favorite,
                               &ignored,
                               &trusted,
                               &first_seen_s);
    }
    favorite = favorite || requested_favorite;
    ignored = ignored || requested_ignored;
    trusted = trusted || requested_trusted;

    return stream_upsert_line(kLxmfAddressesRelativePath,
                              kLxmfAddressesRelativeTempPath,
                              kLxmfAddressesLogicalPath,
                              "LXMF addresses",
                              "LXMF address saved",
                              destination,
                              address_line(record, favorite, ignored, trusted, first_seen_s));
}

Status record_lxmf_address_now(const LxmfAddressRecord& record)
{
    return record_lxmf_address(record);
}

Status set_lxmf_address_favorite_now(
    const uint8_t destination_hash[kReticulumHashSize],
    bool favorite)
{
    Status out{};
    out.supported = true;
    out.sd_present = ::platform::ui::device::card_ready();
    if (!destination_hash || zero_hash(destination_hash, kReticulumHashSize))
    {
        set_status(out, "Invalid LXMF address", kLxmfAddressesLogicalPath);
        return out;
    }
    if (!out.sd_present)
    {
        set_status(out, "SD card required", kLxmfAddressesLogicalPath);
        return out;
    }

    LxmfAddressRecord record{};
    Status find_status = find_lxmf_address_by_destination(destination_hash, &record);
    out.file_present = find_status.file_present;
    if (!find_status.loaded || !record.valid)
    {
        set_status(out, "LXMF address not found", kLxmfAddressesLogicalPath);
        return out;
    }

    record.favorite = favorite;
    const std::string destination = hex_text(record.destination_hash, kReticulumHashSize);
    return stream_upsert_line(kLxmfAddressesRelativePath,
                              kLxmfAddressesRelativeTempPath,
                              kLxmfAddressesLogicalPath,
                              "LXMF addresses",
                              "LXMF address saved",
                              destination,
                              address_line(record,
                                           record.favorite,
                                           record.ignored,
                                           record.trusted,
                                           record.first_seen_s));
}

Status load_announces(AnnounceRecord* out_records,
                      std::size_t max_records,
                      std::size_t* out_count)
{
    Status out{};
    out.supported = true;
    out.sd_present = ::platform::ui::device::card_ready();
    if (out_count)
    {
        *out_count = 0;
    }
    if (!out_records || max_records == 0)
    {
        set_status(out, "Reticulum announce storage unavailable", kAnnouncesLogicalPath);
        return out;
    }
    if (!out.sd_present)
    {
        set_status(out, "SD card required", kAnnouncesLogicalPath);
        return out;
    }

    const auto path = path_under_sd(kAnnouncesRelativePath);
    std::error_code ec;
    out.file_present = std::filesystem::exists(path, ec) && !ec;
    if (!out.file_present)
    {
        out.loaded = true;
        set_status(out, "No Reticulum announces", kAnnouncesLogicalPath);
        return out;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
    {
        set_status(out, "Cannot read Reticulum announces", kAnnouncesLogicalPath);
        return out;
    }

    std::size_t count = 0;
    std::string line;
    while (read_line(in, line))
    {
        const std::string_view view = trim_view(line);
        if (data_line(view))
        {
            AnnounceRecord parsed{};
            if (parse_announce_line(view, parsed))
            {
                append_latest_record(out_records, max_records, count, parsed);
            }
        }
    }

    reverse_records(out_records, count);
    if (out_count)
    {
        *out_count = count;
    }
    out.loaded = true;
    set_status(out,
               count == 0 ? "No Reticulum announces" : "Reticulum announces loaded",
               kAnnouncesLogicalPath);
    return out;
}

Status load_lxmf_addresses(LxmfAddressRecord* out_records,
                           std::size_t max_records,
                           std::size_t* out_count)
{
    Status out{};
    out.supported = true;
    out.sd_present = ::platform::ui::device::card_ready();
    if (out_count)
    {
        *out_count = 0;
    }
    if (!out_records || max_records == 0)
    {
        set_status(out, "LXMF address storage unavailable", kLxmfAddressesLogicalPath);
        return out;
    }
    if (!out.sd_present)
    {
        set_status(out, "SD card required", kLxmfAddressesLogicalPath);
        return out;
    }

    const auto path = path_under_sd(kLxmfAddressesRelativePath);
    std::error_code ec;
    out.file_present = std::filesystem::exists(path, ec) && !ec;
    if (!out.file_present)
    {
        out.loaded = true;
        set_status(out, "No LXMF addresses", kLxmfAddressesLogicalPath);
        return out;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
    {
        set_status(out, "Cannot read LXMF addresses", kLxmfAddressesLogicalPath);
        return out;
    }

    std::size_t count = 0;
    std::string line;
    while (read_line(in, line))
    {
        const std::string_view view = trim_view(line);
        if (data_line(view))
        {
            LxmfAddressRecord parsed{};
            if (parse_address_line(view, parsed))
            {
                append_latest_record(out_records, max_records, count, parsed);
            }
        }
    }
    reverse_records(out_records, count);
    if (out_count)
    {
        *out_count = count;
    }
    out.loaded = true;
    set_status(out,
               count == 0 ? "No LXMF addresses" : "LXMF addresses loaded",
               kLxmfAddressesLogicalPath);
    return out;
}

Status load_lxmf_addresses_matching(const char* query,
                                    LxmfAddressRecord* out_records,
                                    std::size_t max_records,
                                    std::size_t* out_count)
{
    Status out{};
    out.supported = true;
    out.sd_present = ::platform::ui::device::card_ready();
    if (out_count)
    {
        *out_count = 0;
    }
    if (!query || trim_view(query).empty())
    {
        return load_lxmf_addresses(out_records, max_records, out_count);
    }
    if (!out_records || max_records == 0)
    {
        set_status(out, "LXMF address storage unavailable", kLxmfAddressesLogicalPath);
        return out;
    }
    if (!out.sd_present)
    {
        set_status(out, "SD card required", kLxmfAddressesLogicalPath);
        return out;
    }

    const auto path = path_under_sd(kLxmfAddressesRelativePath);
    std::error_code ec;
    out.file_present = std::filesystem::exists(path, ec) && !ec;
    if (!out.file_present)
    {
        out.loaded = true;
        set_status(out, "No LXMF addresses", kLxmfAddressesLogicalPath);
        return out;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
    {
        set_status(out, "Cannot read LXMF addresses", kLxmfAddressesLogicalPath);
        return out;
    }

    std::size_t count = 0;
    std::string line;
    while (read_line(in, line))
    {
        const std::string_view view = trim_view(line);
        if (data_line(view) && address_line_matches_query(view, query))
        {
            LxmfAddressRecord parsed{};
            if (parse_address_line(view, parsed))
            {
                append_latest_record(out_records, max_records, count, parsed);
            }
        }
    }
    reverse_records(out_records, count);
    if (out_count)
    {
        *out_count = count;
    }
    out.loaded = true;
    set_status(out,
               count == 0 ? "No LXMF address matches" : "LXMF address matches loaded",
               kLxmfAddressesLogicalPath);
    return out;
}

Status find_lxmf_address_by_destination(
    const uint8_t destination_hash[kReticulumHashSize],
    LxmfAddressRecord* out_record)
{
    Status out{};
    out.supported = true;
    out.sd_present = ::platform::ui::device::card_ready();
    if (out_record)
    {
        *out_record = LxmfAddressRecord{};
    }
    if (!destination_hash || !out_record ||
        zero_hash(destination_hash, kReticulumHashSize))
    {
        set_status(out, "Invalid LXMF address", kLxmfAddressesLogicalPath);
        return out;
    }
    if (!out.sd_present)
    {
        set_status(out, "SD card required", kLxmfAddressesLogicalPath);
        return out;
    }

    const auto path = path_under_sd(kLxmfAddressesRelativePath);
    std::error_code ec;
    out.file_present = std::filesystem::exists(path, ec) && !ec;
    if (!out.file_present)
    {
        out.loaded = true;
        set_status(out, "No LXMF addresses", kLxmfAddressesLogicalPath);
        return out;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
    {
        set_status(out, "Cannot read LXMF addresses", kLxmfAddressesLogicalPath);
        return out;
    }

    const std::string destination = hex_text(destination_hash, kReticulumHashSize);
    std::string line;
    while (read_line(in, line))
    {
        const std::string_view view = trim_view(line);
        if (data_line(view) && first_field_matches(view, destination))
        {
            out.loaded = parse_address_line(view, *out_record);
            break;
        }
    }
    set_status(out,
               out.loaded ? "LXMF address loaded" : "LXMF address not found",
               kLxmfAddressesLogicalPath);
    return out;
}

Status find_lxmf_address_by_node_id(uint32_t node_id,
                                    LxmfAddressRecord* out_record)
{
    Status out{};
    out.supported = true;
    out.sd_present = ::platform::ui::device::card_ready();
    if (out_record)
    {
        *out_record = LxmfAddressRecord{};
    }
    if (node_id == 0 || !out_record)
    {
        set_status(out, "Invalid LXMF node", kLxmfAddressesLogicalPath);
        return out;
    }
    if (!out.sd_present)
    {
        set_status(out, "SD card required", kLxmfAddressesLogicalPath);
        return out;
    }

    const auto path = path_under_sd(kLxmfAddressesRelativePath);
    std::error_code ec;
    out.file_present = std::filesystem::exists(path, ec) && !ec;
    if (!out.file_present)
    {
        out.loaded = true;
        set_status(out, "No LXMF addresses", kLxmfAddressesLogicalPath);
        return out;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
    {
        set_status(out, "Cannot read LXMF addresses", kLxmfAddressesLogicalPath);
        return out;
    }

    std::string line;
    bool found = false;
    while (read_line(in, line))
    {
        const std::string_view view = trim_view(line);
        if (!data_line(view))
        {
            continue;
        }
        LxmfAddressRecord parsed{};
        if (parse_address_line(view, parsed) &&
            node_id_from_destination_hash(parsed.destination_hash) == node_id)
        {
            *out_record = parsed;
            found = true;
        }
    }
    out.loaded = found;
    set_status(out,
               found ? "LXMF address loaded" : "LXMF address not found",
               kLxmfAddressesLogicalPath);
    return out;
}

} // namespace platform::ui::reticulum_directory
