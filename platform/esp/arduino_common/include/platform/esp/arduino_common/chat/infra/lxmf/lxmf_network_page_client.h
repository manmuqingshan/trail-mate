/**
 * @file lxmf_network_page_client.h
 * @brief Pending Nomad/Network page request owner.
 */

#pragma once

#include "chat/infra/lxmf/lxmf_wire.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace chat::lxmf::runtime
{

struct PendingNomadPageRequest
{
    uint8_t destination_hash[reticulum::kTruncatedHashSize] = {};
    uint8_t request_id[reticulum::kTruncatedHashSize] = {};
    char path[64] = {};
    uint32_t created_ms = 0;
    uint32_t last_attempt_ms = 0;
    uint32_t last_path_request_ms = 0;
    bool path_requested = false;
    bool link_started = false;
    bool request_sent = false;
};

enum class NetworkPageQueueResult
{
    Queued,
    Duplicate,
    Full,
    Invalid,
};

class NetworkPageClient
{
  public:
    NetworkPageClient() = default;
    NetworkPageClient(const NetworkPageClient&) = delete;
    NetworkPageClient& operator=(const NetworkPageClient&) = delete;
    NetworkPageClient(NetworkPageClient&&) = delete;
    NetworkPageClient& operator=(NetworkPageClient&&) = delete;

    std::size_t size() const;
    bool empty() const;
    void clear();

    NetworkPageQueueResult queue(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize],
        const char* path,
        uint32_t now_ms,
        std::size_t max_pending,
        std::size_t max_path_len,
        PendingNomadPageRequest** out_request);

    PendingNomadPageRequest* at(std::size_t index);
    const PendingNomadPageRequest* at(std::size_t index) const;
    void eraseAt(std::size_t index);

    PendingNomadPageRequest* findByRequestId(
        const uint8_t destination_hash[reticulum::kTruncatedHashSize],
        const uint8_t* request_id,
        std::size_t request_id_len);

    template <typename Fn>
    void forEach(Fn&& fn)
    {
        for (auto& request : pending_)
        {
            fn(request);
        }
    }

    template <typename Fn>
    void forEach(Fn&& fn) const
    {
        for (const auto& request : pending_)
        {
            fn(request);
        }
    }

  private:
    std::vector<PendingNomadPageRequest> pending_;
};

} // namespace chat::lxmf::runtime
