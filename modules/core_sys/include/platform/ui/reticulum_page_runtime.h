#pragma once

#include <cstddef>

namespace platform::ui::reticulum_page
{

constexpr std::size_t kReticulumPageDestinationTextSize = 33;
constexpr std::size_t kReticulumPagePathSize = 64;
constexpr std::size_t kReticulumPageBodyMaxBytes = 4096;

struct Status
{
    bool supported = false;
    bool sd_present = false;
    bool file_present = false;
    bool loaded = false;
    bool saved = false;
    bool request_started = false;
    bool truncated = false;
    char message[96] = {};
    char detail[128] = {};
};

const char* cache_root_path();

bool normalize_path(const char* path, char* out_path, std::size_t out_len);

Status load_cached_page(const char* destination_hash,
                        const char* path,
                        char* out_body,
                        std::size_t body_capacity,
                        std::size_t* out_body_len);

Status store_cached_page_now(const char* destination_hash,
                             const char* path,
                             const char* body,
                             std::size_t body_len);

Status request_page(const char* destination_hash, const char* path);

} // namespace platform::ui::reticulum_page
