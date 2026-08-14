/**
 * @file vmp_contact_secrets.cpp
 * @brief Fixed, separately-provisioned private-contact secret directory.
 */

#include "chat/infra/voice/vmp_contact_secrets.h"

#include <cstring>

namespace chat::voice::vmp
{
namespace
{

bool validPeerId(uint32_t peer_id)
{
    return peer_id != 0U && peer_id != kBroadcastTargetId;
}

bool allZero(const uint8_t* bytes, std::size_t size)
{
    if (!bytes)
    {
        return true;
    }
    uint8_t value = 0U;
    for (std::size_t index = 0U; index < size; ++index)
    {
        value |= bytes[index];
    }
    return value == 0U;
}

void secureClear(uint8_t* bytes, std::size_t size)
{
    volatile uint8_t* cursor = bytes;
    while (cursor && size-- != 0U)
    {
        *cursor++ = 0U;
    }
}

} // namespace

bool FixedVerifiedContactSecretDirectory::upsertVerifiedContactSecret(
    uint32_t peer_id,
    const uint8_t secret[kPrivateKeySize])
{
    if (!validPeerId(peer_id) || !secret || allZero(secret, kPrivateKeySize))
    {
        return false;
    }

    Entry* destination = nullptr;
    for (Entry& entry : entries_)
    {
        if (entry.occupied && entry.peer_id == peer_id)
        {
            destination = &entry;
            break;
        }
        if (!destination && !entry.occupied)
        {
            destination = &entry;
        }
    }
    if (!destination)
    {
        return false;
    }

    const bool was_occupied = destination->occupied;
    destination->peer_id = peer_id;
    std::memcpy(destination->secret, secret, sizeof(destination->secret));
    destination->occupied = true;
    if (!was_occupied)
    {
        ++size_;
    }
    return true;
}

bool FixedVerifiedContactSecretDirectory::removeVerifiedContactSecret(
    uint32_t peer_id)
{
    if (!validPeerId(peer_id))
    {
        return false;
    }
    for (Entry& entry : entries_)
    {
        if (entry.occupied && entry.peer_id == peer_id)
        {
            secureClear(entry.secret, sizeof(entry.secret));
            entry.peer_id = 0U;
            entry.occupied = false;
            --size_;
            return true;
        }
    }
    return false;
}

void FixedVerifiedContactSecretDirectory::clear()
{
    for (Entry& entry : entries_)
    {
        secureClear(entry.secret, sizeof(entry.secret));
        entry.peer_id = 0U;
        entry.occupied = false;
    }
    size_ = 0U;
}

bool FixedVerifiedContactSecretDirectory::lookupVerifiedContactSecret(
    uint32_t peer_id,
    uint8_t out_secret[kPrivateKeySize]) const
{
    if (!validPeerId(peer_id) || !out_secret)
    {
        return false;
    }
    for (const Entry& entry : entries_)
    {
        if (entry.occupied && entry.peer_id == peer_id)
        {
            std::memcpy(out_secret, entry.secret, sizeof(entry.secret));
            return true;
        }
    }
    secureClear(out_secret, kPrivateKeySize);
    return false;
}

bool FixedVerifiedContactSecretDirectory::hasVerifiedContactSecret(
    uint32_t peer_id) const
{
    if (!validPeerId(peer_id))
    {
        return false;
    }
    for (const Entry& entry : entries_)
    {
        if (entry.occupied && entry.peer_id == peer_id)
        {
            return true;
        }
    }
    return false;
}

} // namespace chat::voice::vmp
