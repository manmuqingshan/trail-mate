/**
 * @file reticulum_contact_projection_policy.h
 * @brief Product projection policy for Reticulum contact destinations.
 */

#pragma once

#include "platform/ui/reticulum_directory_runtime.h"

#include <cstddef>

namespace platform::ui::reticulum_contacts
{

enum class ProjectionBucket : uint8_t
{
    Hidden = 0,
    Contact = 1,
    Announced = 2,
    Ignored = 3,
};

inline bool has_nonzero_bytes(const uint8_t* bytes, std::size_t len)
{
    if (!bytes || len == 0)
    {
        return false;
    }
    for (std::size_t index = 0; index < len; ++index)
    {
        if (bytes[index] != 0)
        {
            return true;
        }
    }
    return false;
}

inline ProjectionBucket classify(
    const reticulum_directory::LxmfAddressRecord& record)
{
    if (!record.valid ||
        !has_nonzero_bytes(record.destination_hash,
                           sizeof(record.destination_hash)) ||
        !has_nonzero_bytes(record.identity_hash,
                           sizeof(record.identity_hash)) ||
        !has_nonzero_bytes(record.enc_pub, sizeof(record.enc_pub)) ||
        !has_nonzero_bytes(record.sig_pub, sizeof(record.sig_pub)))
    {
        return ProjectionBucket::Hidden;
    }
    if (record.ignored)
    {
        return ProjectionBucket::Ignored;
    }
    if (record.favorite ||
        record.source == reticulum_directory::EntrySource::Manual ||
        record.source == reticulum_directory::EntrySource::Import)
    {
        return ProjectionBucket::Contact;
    }
    return ProjectionBucket::Announced;
}

} // namespace platform::ui::reticulum_contacts
