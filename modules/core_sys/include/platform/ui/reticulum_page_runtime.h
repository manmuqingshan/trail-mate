#pragma once

#include <cstddef>
#include <cstdint>

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

enum class RequestStartCode : std::uint8_t
{
    Started = 0,
    AlreadyPending,
    InvalidInput,
    Unsupported,
    NotReady,
    Busy,
    EncodeFailed,
    RadioTxFailed,
    StorageUnavailable,
    Unknown,
};

struct RequestStartResult
{
    RequestStartCode code = RequestStartCode::Unsupported;
    int detail = 0;
};

using RequestStartHandler = RequestStartResult (*)(
    const std::uint8_t destination_hash[kReticulumPageDestinationTextSize / 2U],
    const char* path,
    void* context);

const char* cache_root_path();

bool normalize_path(const char* path, char* out_path, std::size_t out_len);

void bind_request_start_handler(RequestStartHandler handler, void* context);

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
