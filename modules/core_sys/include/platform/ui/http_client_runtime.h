#pragma once

#include "platform/ui/wifi_access_runtime.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace platform::ui::http_client
{

using WriteCallback = bool (*)(const std::uint8_t* data,
                               std::size_t len,
                               void* context);
using ProgressCallback = void (*)(std::uint32_t bytes,
                                  std::uint32_t total_bytes,
                                  void* context);

struct Request
{
    const char* url = nullptr;
    wifi_access::Client client = wifi_access::Client::Unknown;
    wifi_access::AccessKind access_kind = wifi_access::AccessKind::HttpDownload;
    wifi_access::Priority priority = wifi_access::Priority::Background;
    const char* reason = nullptr;
    int timeout_ms = 30000;
    int buffer_size = 1024;
    int tx_buffer_size = 512;
    std::uint32_t max_bytes = 0;
    ProgressCallback progress = nullptr;
    void* progress_context = nullptr;
};

struct TransferStats
{
    int http_status = 0;
    std::uint32_t bytes = 0;
};

bool get_text(const Request& request,
              std::string& out,
              std::string& out_error,
              TransferStats* out_stats = nullptr);

bool download(const Request& request,
              WriteCallback write,
              void* write_context,
              std::string& out_error,
              TransferStats* out_stats = nullptr);

} // namespace platform::ui::http_client
