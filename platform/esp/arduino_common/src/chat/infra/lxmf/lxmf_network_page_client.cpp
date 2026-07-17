/**
 * @file lxmf_network_page_client.cpp
 * @brief Pending Nomad/Network page request owner.
 */

#include "platform/esp/arduino_common/chat/infra/lxmf/lxmf_network_page_client.h"

#include <cstdio>
#include <cstring>

namespace chat::lxmf::runtime
{
namespace
{

bool hashesEqual(const uint8_t* a, const uint8_t* b, std::size_t len)
{
    if ((!a || !b) && len != 0)
    {
        return false;
    }
    for (std::size_t i = 0; i < len; ++i)
    {
        if (a[i] != b[i])
        {
            return false;
        }
    }
    return true;
}

bool isZeroBytes(const uint8_t* data, std::size_t len)
{
    if (!data)
    {
        return true;
    }
    for (std::size_t i = 0; i < len; ++i)
    {
        if (data[i] != 0)
        {
            return false;
        }
    }
    return true;
}

void copyHash(uint8_t* out, const uint8_t* in, std::size_t len)
{
    if (!out || !in || len == 0)
    {
        return;
    }
    std::memcpy(out, in, len);
}

} // namespace

std::size_t NetworkPageClient::size() const
{
    return pending_.size();
}

bool NetworkPageClient::empty() const
{
    return pending_.empty();
}

void NetworkPageClient::clear()
{
    pending_.clear();
}

NetworkPageQueueResult NetworkPageClient::queue(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    const char* path,
    uint32_t now_ms,
    std::size_t max_pending,
    std::size_t max_path_len,
    PendingNomadPageRequest** out_request)
{
    if (out_request)
    {
        *out_request = nullptr;
    }
    if (!destination_hash ||
        isZeroBytes(destination_hash, reticulum::kTruncatedHashSize) ||
        !path || path[0] == '\0' ||
        std::strlen(path) >= max_path_len ||
        max_path_len > sizeof(PendingNomadPageRequest::path))
    {
        return NetworkPageQueueResult::Invalid;
    }

    for (PendingNomadPageRequest& pending : pending_)
    {
        if (hashesEqual(pending.destination_hash,
                        destination_hash,
                        reticulum::kTruncatedHashSize) &&
            std::strcmp(pending.path, path) == 0)
        {
            if (out_request)
            {
                *out_request = &pending;
            }
            return NetworkPageQueueResult::Duplicate;
        }
    }

    if (pending_.size() >= max_pending)
    {
        return NetworkPageQueueResult::Full;
    }

    PendingNomadPageRequest request{};
    copyHash(request.destination_hash,
             destination_hash,
             sizeof(request.destination_hash));
    std::snprintf(request.path, sizeof(request.path), "%s", path);
    request.created_ms = now_ms;
    pending_.push_back(request);
    if (out_request)
    {
        *out_request = &pending_.back();
    }
    return NetworkPageQueueResult::Queued;
}

PendingNomadPageRequest* NetworkPageClient::at(std::size_t index)
{
    return index < pending_.size() ? &pending_[index] : nullptr;
}

const PendingNomadPageRequest* NetworkPageClient::at(std::size_t index) const
{
    return index < pending_.size() ? &pending_[index] : nullptr;
}

void NetworkPageClient::eraseAt(std::size_t index)
{
    if (index < pending_.size())
    {
        pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

PendingNomadPageRequest* NetworkPageClient::findByRequestId(
    const uint8_t destination_hash[reticulum::kTruncatedHashSize],
    const uint8_t* request_id,
    std::size_t request_id_len)
{
    if (!destination_hash || !request_id ||
        request_id_len != reticulum::kTruncatedHashSize)
    {
        return nullptr;
    }
    for (PendingNomadPageRequest& request : pending_)
    {
        if (hashesEqual(request.destination_hash,
                        destination_hash,
                        reticulum::kTruncatedHashSize) &&
            std::memcmp(request.request_id,
                        request_id,
                        reticulum::kTruncatedHashSize) == 0)
        {
            return &request;
        }
    }
    return nullptr;
}

} // namespace chat::lxmf::runtime
