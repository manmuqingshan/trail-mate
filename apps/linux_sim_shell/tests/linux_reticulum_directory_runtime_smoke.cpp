#include "platform/ui/reticulum_directory_runtime.h"
#include "platform/ui/reticulum_page_runtime.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

namespace rtdir = platform::ui::reticulum_directory;
namespace rtpage = platform::ui::reticulum_page;

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

    page_status = rtpage::request_page(alpha_destination, "/page/index.mu");
    assert(!page_status.request_started);
    assert(std::strcmp(page_status.message, "Nomad page fetch unavailable") == 0);

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
