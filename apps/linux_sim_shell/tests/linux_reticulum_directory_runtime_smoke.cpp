#include "platform/ui/reticulum_directory_runtime.h"
#include "platform/ui/reticulum_page_runtime.h"
#include "platform/ui/reticulum_receive_runtime.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

namespace rtdir = platform::ui::reticulum_directory;
namespace rtpage = platform::ui::reticulum_page;
namespace rtreceive = platform::ui::reticulum_receive;

namespace
{

void set_env_var(const char* name, const std::string& value)
{
#if defined(_WIN32)
    const std::string assignment = std::string(name) + "=" + value;
    _putenv(assignment.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

std::filesystem::path make_temp_root()
{
    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        "trailmate_linux_reticulum_directory_runtime_smoke";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

rtdir::LxmfAddressRecord make_address(const char* display_name,
                                      bool favorite,
                                      bool trusted)
{
    rtdir::LxmfAddressRecord record{};
    record.valid = true;
    for (std::size_t index = 0; index < rtdir::kReticulumHashSize; ++index)
    {
        record.destination_hash[index] = static_cast<uint8_t>(0x20U + index);
        record.identity_hash[index] = static_cast<uint8_t>(0x40U + index);
    }
    record.destination_hash[12] = 0x12;
    record.destination_hash[13] = 0x34;
    record.destination_hash[14] = 0x56;
    record.destination_hash[15] = 0x78;

    for (std::size_t index = 0; index < rtdir::kReticulumPublicKeySize; ++index)
    {
        record.enc_pub[index] = static_cast<uint8_t>(0x60U + index);
        record.sig_pub[index] = static_cast<uint8_t>(0x90U + index);
    }
    std::snprintf(record.display_name,
                  sizeof(record.display_name),
                  "%s",
                  display_name);
    record.favorite = favorite;
    record.trusted = trusted;
    record.source = rtdir::EntrySource::Import;
    record.first_seen_s = 10;
    record.last_seen_s = 20;
    return record;
}

void format_hash_hex(const uint8_t* hash, char* out, std::size_t out_len)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    assert(hash != nullptr);
    assert(out != nullptr);
    assert(out_len >= rtpage::kReticulumPageDestinationTextSize);
    for (std::size_t i = 0; i < rtdir::kReticulumHashSize; ++i)
    {
        out[i * 2U] = kHex[(hash[i] >> 4U) & 0x0FU];
        out[(i * 2U) + 1U] = kHex[hash[i] & 0x0FU];
    }
    out[rtdir::kReticulumHashSize * 2U] = '\0';
}

struct RequestHarness
{
    int starts = 0;
    int cancels = 0;
    uint8_t data[256] = {};
    std::size_t data_len = 0;
    char path[rtpage::kReticulumPagePathSize] = {};
};

rtpage::RequestStartResult start_page_request(
    const uint8_t*,
    const char* path,
    const uint8_t* request_data,
    std::size_t request_data_len,
    void* context)
{
    auto* harness = static_cast<RequestHarness*>(context);
    assert(harness != nullptr);
    assert(request_data_len <= sizeof(harness->data));
    ++harness->starts;
    std::snprintf(harness->path, sizeof(harness->path), "%s", path);
    if (request_data_len != 0)
    {
        assert(request_data != nullptr);
        std::memcpy(harness->data, request_data, request_data_len);
        harness->data_len = request_data_len;
    }
    return {rtpage::RequestStartCode::Started, 0};
}

bool cancel_page_request(const uint8_t*, const char* path, void* context)
{
    auto* harness = static_cast<RequestHarness*>(context);
    assert(harness != nullptr);
    assert(std::strcmp(path, harness->path) == 0);
    ++harness->cancels;
    return true;
}

bool cancel_receive(const uint8_t link_id[rtreceive::kHashSize],
                    const uint8_t resource_hash[32],
                    void* context)
{
    auto* calls = static_cast<int*>(context);
    assert(link_id[0] == 0x10);
    assert(resource_hash[0] == 0x80);
    ++(*calls);
    return true;
}

} // namespace

namespace platform::ui::device
{

bool card_ready()
{
    return true;
}

} // namespace platform::ui::device

int main()
{
    const std::filesystem::path root = make_temp_root();
    const std::filesystem::path settings = root / "settings";
    const std::filesystem::path sd = root / "sdcard";
    const std::filesystem::path cache = root / "cache";
    std::filesystem::create_directories(settings);
    std::filesystem::create_directories(sd);
    std::filesystem::create_directories(cache);

    set_env_var("TRAIL_MATE_SETTINGS_ROOT", settings.string());
    set_env_var("TRAIL_MATE_SD_ROOT", sd.string());
    set_env_var("TRAIL_MATE_CACHE_ROOT", cache.string());

    rtdir::bind_mesh_peer_directory(nullptr);
    assert(std::strcmp(rtdir::lxmf_addresses_path(), "/mesh/peers.bin") == 0);

    rtdir::AnnounceRecord announce{};
    announce.valid = true;
    for (std::size_t index = 0; index < rtdir::kReticulumHashSize; ++index)
    {
        announce.destination_hash[index] = static_cast<uint8_t>(0x10U + index);
        announce.identity_hash[index] = static_cast<uint8_t>(0x30U + index);
    }
    announce.aspect = rtdir::AnnounceAspect::LxmfDelivery;
    announce.source = rtdir::EntrySource::RuntimeRx;
    announce.first_seen_s = 10;
    announce.last_seen_s = 20;
    announce.hops = 2;
    announce.delivery = true;
    std::snprintf(announce.display_name,
                  sizeof(announce.display_name),
                  "%s",
                  "first announce");
    assert(rtdir::record_announce(announce).saved);

    announce.first_seen_s = 30;
    announce.last_seen_s = 40;
    announce.hops = 1;
    std::snprintf(announce.display_name,
                  sizeof(announce.display_name),
                  "%s",
                  "updated announce");
    assert(rtdir::record_announce(announce).saved);

    rtdir::AnnounceRecord loaded_announces[2] = {};
    std::size_t loaded_announce_count = 0;
    const rtdir::Status announce_load_status =
        rtdir::load_announces(loaded_announces,
                              2,
                              &loaded_announce_count);
    assert(announce_load_status.loaded);
    assert(loaded_announce_count == 1);
    assert(loaded_announces[0].first_seen_s == 10);
    assert(loaded_announces[0].last_seen_s == 40);
    assert(loaded_announces[0].hops == 1);
    assert(std::strcmp(loaded_announces[0].display_name,
                       "updated announce") == 0);

    rtdir::LxmfAddressRecord alpha = make_address("alpha trail", true, true);
    char alpha_destination[rtpage::kReticulumPageDestinationTextSize] = {};
    format_hash_hex(alpha.destination_hash,
                    alpha_destination,
                    sizeof(alpha_destination));

    char normalized_path[rtpage::kReticulumPagePathSize] = {};
    assert(rtpage::normalize_path("page/index.mu",
                                  normalized_path,
                                  sizeof(normalized_path)));
    assert(std::strcmp(normalized_path, "/page/index.mu") == 0);
    assert(!rtpage::normalize_path("/page/../secret.mu",
                                   normalized_path,
                                   sizeof(normalized_path)));

    const char page_body[] = "# Alpha Trail\n=> /page/about.mu About\nhello";
    rtpage::Status page_status =
        rtpage::store_cached_page_now(alpha_destination,
                                      "page/index.mu",
                                      page_body,
                                      std::strlen(page_body));
    assert(page_status.saved);
    assert(std::filesystem::exists(sd / "trailmate" / "reticulum" / "pages" /
                                   alpha_destination / "page" / "index.mu"));

    char loaded_page[128] = {};
    std::size_t loaded_len = 0;
    page_status = rtpage::load_cached_page(alpha_destination,
                                           "/page/index.mu",
                                           loaded_page,
                                           sizeof(loaded_page),
                                           &loaded_len);
    assert(page_status.loaded);
    assert(!page_status.truncated);
    assert(loaded_len == std::strlen(page_body));
    assert(std::strcmp(loaded_page, page_body) == 0);

    char tiny_page[8] = {};
    loaded_len = 0;
    page_status = rtpage::load_cached_page(alpha_destination,
                                           "/page/index.mu",
                                           tiny_page,
                                           sizeof(tiny_page),
                                           &loaded_len);
    assert(page_status.loaded);
    assert(page_status.truncated);
    assert(loaded_len == sizeof(tiny_page) - 1U);

    page_status =
        rtpage::request_cached_page_load(alpha_destination, "/page/index.mu");
    assert(page_status.loaded);
    loaded_len = 0;
    std::memset(loaded_page, 0, sizeof(loaded_page));
    page_status = rtpage::poll_cached_page_load(alpha_destination,
                                                "/page/index.mu",
                                                loaded_page,
                                                sizeof(loaded_page),
                                                &loaded_len);
    assert(page_status.loaded);
    assert(loaded_len == std::strlen(page_body));
    assert(std::strcmp(loaded_page, page_body) == 0);

    page_status = rtpage::clear_cached_page(alpha_destination, "/page/index.mu");
    assert(page_status.supported);
    assert(page_status.cache_checked);
    assert(!page_status.file_present);
    assert(std::strcmp(page_status.message, "Nomad page cache cleared") == 0);
    assert(!std::filesystem::exists(sd / "trailmate" / "reticulum" / "pages" /
                                    alpha_destination / "page" / "index.mu"));

    loaded_len = 0;
    std::memset(loaded_page, 0, sizeof(loaded_page));
    page_status = rtpage::poll_cached_page_load(alpha_destination,
                                                "/page/index.mu",
                                                loaded_page,
                                                sizeof(loaded_page),
                                                &loaded_len);
    assert(!page_status.loaded);
    assert(loaded_len == 0);

    page_status = rtpage::load_cached_page(alpha_destination,
                                           "/page/index.mu",
                                           loaded_page,
                                           sizeof(loaded_page),
                                           &loaded_len);
    assert(page_status.cache_checked);
    assert(!page_status.file_present);
    assert(!page_status.loaded);

    page_status = rtpage::request_page(alpha_destination, "/page/index.mu");
    assert(!page_status.request_started);
    assert(std::strcmp(page_status.message, "Nomad page fetch unavailable") == 0);

    RequestHarness request_harness{};
    rtpage::bind_request_start_handler(start_page_request, &request_harness);
    rtpage::bind_request_cancel_handler(cancel_page_request, &request_harness);
    const uint8_t form_data[] = {0x81, 0xA9, 'f', 'i', 'e', 'l', 'd', '_', 'i', 'd', 0xA2, '4', '2'};
    page_status = rtpage::request_page_with_data(alpha_destination,
                                                 "/page/form.mu",
                                                 form_data,
                                                 sizeof(form_data));
    assert(page_status.request_started);
    assert(request_harness.starts == 1);
    assert(std::strcmp(request_harness.path, "/page/form.mu") == 0);
    assert(request_harness.data_len == sizeof(form_data));
    assert(std::memcmp(request_harness.data, form_data, sizeof(form_data)) == 0);
    page_status = rtpage::cancel_request(alpha_destination, "/page/form.mu");
    assert(page_status.cancelled);
    assert(request_harness.cancels == 1);
    rtpage::bind_request_start_handler(nullptr, nullptr);
    rtpage::bind_request_cancel_handler(nullptr, nullptr);

    uint8_t receive_link[rtreceive::kHashSize] = {};
    uint8_t receive_resource[32] = {};
    receive_link[0] = 0x10;
    receive_resource[0] = 0x80;
    int receive_cancels = 0;
    rtreceive::bind_cancel_handler(cancel_receive, &receive_cancels);
    rtreceive::begin(receive_link, receive_resource, 4, 1024);
    auto receive_status = rtreceive::snapshot();
    assert(receive_status.active);
    assert(receive_status.progress_percent == 0);
    rtreceive::update(receive_resource, 2, 512);
    receive_status = rtreceive::snapshot();
    assert(receive_status.progress_percent == 50);
    assert(receive_status.received_bytes == 512);
    assert(rtreceive::cancel());
    assert(receive_cancels == 1);
    assert(!rtreceive::snapshot().active);
    rtreceive::bind_cancel_handler(nullptr, nullptr);

    rtdir::Status status = rtdir::record_lxmf_address(alpha);
    assert(status.saved);
    assert(std::filesystem::exists(sd / "mesh" / "peers.bin"));
    assert(!std::filesystem::exists(sd / "trailmate" / "reticulum" /
                                    "lxmf_addresses.tsv"));

    rtdir::LxmfAddressRecord found{};
    status = rtdir::find_lxmf_address_by_destination(alpha.destination_hash,
                                                     &found);
    assert(status.loaded);
    assert(found.valid);
    assert(found.favorite);
    assert(found.trusted);
    assert(std::strcmp(found.display_name, "alpha trail") == 0);

    status = rtdir::find_lxmf_address_by_node_id(0x12345678, &found);
    assert(status.loaded);
    assert(found.valid);

    status = rtdir::set_lxmf_address_favorite_now(alpha.destination_hash, false);
    assert(status.saved);
    status = rtdir::find_lxmf_address_by_destination(alpha.destination_hash,
                                                     &found);
    assert(status.loaded);
    assert(!found.favorite);
    assert(found.trusted);

    rtdir::LxmfAddressRecord beta = make_address("beta trail", false, false);
    status = rtdir::record_lxmf_address(beta);
    assert(status.saved);
    status = rtdir::find_lxmf_address_by_destination(beta.destination_hash,
                                                     &found);
    assert(status.loaded);
    assert(!found.favorite);
    assert(found.trusted);
    assert(std::strcmp(found.display_name, "beta trail") == 0);

    rtdir::LxmfAddressRecord records[2]{};
    std::size_t count = 0;
    status = rtdir::load_lxmf_addresses(records, 2, &count);
    assert(status.loaded);
    assert(count == 1);
    assert(std::strcmp(records[0].display_name, "beta trail") == 0);

    count = 0;
    status = rtdir::load_lxmf_addresses_matching("beta", records, 2, &count);
    assert(status.loaded);
    assert(count == 1);
    assert(std::strcmp(records[0].display_name, "beta trail") == 0);

    std::filesystem::remove_all(root);
    return 0;
}
