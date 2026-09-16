// Deterministic directory/page input for the unchanged native Nomad renderer.
#include "platform/ui/reticulum_directory_runtime.h"
#include "platform/ui/reticulum_page_runtime.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace platform::ui::reticulum_directory
{
static Status ready()
{
    Status s;
    s.supported = true;
    s.sd_present = true;
    s.file_present = true;
    s.loaded = true;
    return s;
}
Status load_announces(AnnounceRecord* out, std::size_t max, std::size_t* count)
{
    *count = max ? 1 : 0;
    if (*count)
    {
        out[0] = {};
        out[0].valid = true;
        std::memset(out[0].destination_hash, 0x11, 16);
        std::memset(out[0].identity_hash, 0x22, 16);
        out[0].aspect = AnnounceAspect::NomadNetworkNode;
        out[0].source = EntrySource::RuntimeRx;
        out[0].hops = 1;
        std::snprintf(out[0].display_name, sizeof(out[0].display_name), "Ridge shelter");
    }
    return ready();
}
Status load_lxmf_addresses(LxmfAddressRecord*, std::size_t, std::size_t* count)
{
    *count = 0;
    return ready();
}
Status load_lxmf_addresses_matching(const char*, LxmfAddressRecord* out, std::size_t n, std::size_t* count) { return load_lxmf_addresses(out, n, count); }
Status find_lxmf_address_by_destination(const uint8_t*, LxmfAddressRecord* out)
{
    if (out) *out = {};
    return ready();
}
Status set_lxmf_address_favorite_now(const uint8_t*, bool) { return ready(); }
} // namespace platform::ui::reticulum_directory
namespace platform::ui::reticulum_page
{
static bool cached = true;
static bool completed = false;
static Status ready()
{
    Status s;
    s.supported = true;
    s.sd_present = true;
    return s;
}
bool normalize_path(const char* path, char* out, std::size_t n)
{
    if (!out || !n) return false;
    const char* value = path && *path ? path : "/page/index.mu";
    return std::snprintf(out, n, "%s", value) < static_cast<int>(n);
}
Status request_cached_page_load(const char*, const char*, bool)
{
    auto s = ready();
    s.cache_checked = true;
    s.file_present = cached;
    return s;
}
Status poll_cached_page_load(const char*, const char*, char* body, std::size_t cap, std::size_t* count)
{
    auto s = ready();
    s.cache_checked = true;
    s.file_present = cached;
    s.loaded = cached;
    const char* text = ">Ridge shelter\nWelcome to the field bulletin.\n\nWater point: east side.\nTrail: open to Pine Ridge.\n\nThis bulletin is sample data supplied to the firmware Micron renderer.\n";
    *count = 0;
    if (cached && body && cap)
    {
        *count = std::min(cap - 1, std::strlen(text));
        std::memcpy(body, text, *count);
        body[*count] = 0;
    }
    return s;
}
Status clear_cached_page(const char*, const char*)
{
    cached = false;
    return ready();
}
Status request_page(const char*, const char*)
{
    cached = true;
    completed = true;
    auto s = ready();
    s.request_started = true;
    return s;
}
Status request_page_with_data(const char* dest, const char* path, const uint8_t*, std::size_t) { return request_page(dest, path); }
Status cancel_request(const char*, const char*)
{
    auto s = ready();
    s.cancelled = true;
    return s;
}
void clear_request_progress(const char*, const char*) { completed = false; }
RequestProgress get_request_progress(const char*, const char*)
{
    RequestProgress p;
    p.complete = completed;
    p.progress_percent = completed ? 100 : -1;
    return p;
}
} // namespace platform::ui::reticulum_page
