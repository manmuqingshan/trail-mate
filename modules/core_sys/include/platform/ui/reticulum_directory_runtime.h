#pragma once

#include <cstddef>
#include <cstdint>

namespace platform::ui::reticulum_directory
{

constexpr std::size_t kReticulumHashSize = 16;
constexpr std::size_t kReticulumPublicKeySize = 32;
constexpr std::size_t kReticulumDisplayNameSize = 32;

enum class AnnounceAspect : uint8_t
{
    Unknown = 0,
    LxmfDelivery = 1,
    LxmfPropagation = 2,
    CallAudio = 3,
};

enum class EntrySource : uint8_t
{
    Unknown = 0,
    RuntimeRx = 1,
    PathResponse = 2,
    Manual = 3,
    Import = 4,
};

struct Status
{
    bool supported = false;
    bool sd_present = false;
    bool file_present = false;
    bool loaded = false;
    bool saved = false;
    char message[96] = {};
    char detail[128] = {};
};

struct AnnounceRecord
{
    bool valid = false;
    uint8_t destination_hash[kReticulumHashSize] = {};
    uint8_t identity_hash[kReticulumHashSize] = {};
    AnnounceAspect aspect = AnnounceAspect::Unknown;
    EntrySource source = EntrySource::Unknown;
    uint32_t first_seen_s = 0;
    uint32_t last_seen_s = 0;
    uint8_t hops = 0xFF;
    bool path_response = false;
    bool local_destination = false;
    bool delivery = false;
    bool propagation = false;
    char display_name[kReticulumDisplayNameSize] = {};
    const uint8_t* raw_packet = nullptr;
    std::size_t raw_packet_len = 0;
    const uint8_t* app_data = nullptr;
    std::size_t app_data_len = 0;
};

struct LxmfAddressRecord
{
    bool valid = false;
    uint8_t destination_hash[kReticulumHashSize] = {};
    uint8_t identity_hash[kReticulumHashSize] = {};
    uint8_t enc_pub[kReticulumPublicKeySize] = {};
    uint8_t sig_pub[kReticulumPublicKeySize] = {};
    char display_name[kReticulumDisplayNameSize] = {};
    bool favorite = false;
    bool ignored = false;
    bool trusted = false;
    EntrySource source = EntrySource::Unknown;
    uint32_t first_seen_s = 0;
    uint32_t last_seen_s = 0;
};

const char* announces_path();
const char* lxmf_addresses_path();

Status record_announce(const AnnounceRecord& record);
Status record_lxmf_address(const LxmfAddressRecord& record);
Status load_announces(AnnounceRecord* out_records,
                      std::size_t max_records,
                      std::size_t* out_count);
Status load_lxmf_addresses(LxmfAddressRecord* out_records,
                           std::size_t max_records,
                           std::size_t* out_count);

} // namespace platform::ui::reticulum_directory
