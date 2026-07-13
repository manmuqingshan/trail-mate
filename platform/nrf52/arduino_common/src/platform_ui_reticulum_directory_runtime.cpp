#include "platform/ui/reticulum_directory_runtime.h"
#include "platform/ui/reticulum_page_runtime.h"

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

void bind_mesh_peer_directory(chat::IMeshPeerDirectory*)
{
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

namespace platform::ui::reticulum_page
{
namespace
{

constexpr const char* kPagesPath = "/trailmate/reticulum/pages";

void copy_text(char* out, std::size_t out_len, const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    std::snprintf(out, out_len, "%s", text ? text : "");
}

Status unsupported(const char* message, const char* detail)
{
    Status out{};
    copy_text(out.message, sizeof(out.message), message);
    copy_text(out.detail, sizeof(out.detail), detail);
    return out;
}

} // namespace

const char* cache_root_path()
{
    return kPagesPath;
}

void bind_request_start_handler(RequestStartHandler, void*)
{
}

bool normalize_path(const char* path, char* out_path, std::size_t out_len)
{
    if (!out_path || out_len == 0)
    {
        return false;
    }
    std::snprintf(out_path, out_len, "%s", (path && path[0]) ? path : "/page/index.mu");
    return true;
}

Status load_cached_page(const char*, const char*, char*, std::size_t, std::size_t* out_body_len)
{
    if (out_body_len)
    {
        *out_body_len = 0;
    }
    return unsupported("Nomad page cache unsupported", kPagesPath);
}

Status request_cached_page_load(const char*, const char*, bool)
{
    return unsupported("Nomad page cache unsupported", kPagesPath);
}

Status poll_cached_page_load(const char*, const char*, char*, std::size_t, std::size_t* out_body_len)
{
    if (out_body_len)
    {
        *out_body_len = 0;
    }
    return unsupported("Nomad page cache unsupported", kPagesPath);
}

Status store_cached_page_now(const char*, const char*, const char*, std::size_t)
{
    return unsupported("Nomad page cache unsupported", kPagesPath);
}

Status clear_cached_page(const char*, const char*)
{
    return unsupported("Nomad page cache unsupported", kPagesPath);
}

Status request_page(const char*, const char*)
{
    return unsupported("Nomad page fetch unsupported", kPagesPath);
}

RequestProgress get_request_progress(const char*, const char*)
{
    return {};
}

void update_request_progress(const char*,
                             const char*,
                             int,
                             const char*,
                             const char*,
                             bool,
                             bool,
                             RequestProgress::FailureKind)
{
}

void clear_request_progress(const char*, const char*)
{
}

} // namespace platform::ui::reticulum_page
