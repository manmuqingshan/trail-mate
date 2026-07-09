#include "platform/ui/reticulum_directory_runtime.h"

#include <cstdio>
#include <cstring>

namespace platform::ui::reticulum_directory
{
namespace
{

constexpr const char* kAnnouncesPath = "/trailmate/reticulum/announces.tsv";
constexpr const char* kLxmfAddressesPath = "/trailmate/reticulum/lxmf_addresses.tsv";

void copy_text(char* out, std::size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%s", text ? text : "");
}

void set_status(Status& out, const char* message, const char* detail)
{
    copy_text(out.message, sizeof(out.message), message);
    copy_text(out.detail, sizeof(out.detail), detail);
}

Status unsupported(const char* message, const char* detail)
{
    Status out{};
    out.supported = false;
    set_status(out, message, detail);
    return out;
}

} // namespace

const char* announces_path()
{
    return kAnnouncesPath;
}

const char* lxmf_addresses_path()
{
    return kLxmfAddressesPath;
}

Status record_announce(const AnnounceRecord&)
{
    return unsupported("Reticulum announce storage unsupported", kAnnouncesPath);
}

Status record_lxmf_address(const LxmfAddressRecord&)
{
    return unsupported("LXMF address storage unsupported", kLxmfAddressesPath);
}

Status record_lxmf_address_now(const LxmfAddressRecord&)
{
    return unsupported("LXMF address storage unsupported", kLxmfAddressesPath);
}

Status set_lxmf_address_favorite_now(const uint8_t*, bool)
{
    return unsupported("LXMF address storage unsupported", kLxmfAddressesPath);
}

Status load_announces(AnnounceRecord*, std::size_t, std::size_t* out_count)
{
    if (out_count)
    {
        *out_count = 0;
    }
    return unsupported("Reticulum announce storage unsupported", kAnnouncesPath);
}

Status load_lxmf_addresses(LxmfAddressRecord*, std::size_t, std::size_t* out_count)
{
    if (out_count)
    {
        *out_count = 0;
    }
    return unsupported("LXMF address storage unsupported", kLxmfAddressesPath);
}

Status load_lxmf_addresses_matching(const char*,
                                    LxmfAddressRecord*,
                                    std::size_t,
                                    std::size_t* out_count)
{
    if (out_count)
    {
        *out_count = 0;
    }
    return unsupported("LXMF address storage unsupported", kLxmfAddressesPath);
}

Status find_lxmf_address_by_destination(const uint8_t*, LxmfAddressRecord* out_record)
{
    if (out_record)
    {
        *out_record = LxmfAddressRecord{};
    }
    return unsupported("LXMF address storage unsupported", kLxmfAddressesPath);
}

Status find_lxmf_address_by_node_id(uint32_t, LxmfAddressRecord* out_record)
{
    if (out_record)
    {
        *out_record = LxmfAddressRecord{};
    }
    return unsupported("LXMF address storage unsupported", kLxmfAddressesPath);
}

} // namespace platform::ui::reticulum_directory
