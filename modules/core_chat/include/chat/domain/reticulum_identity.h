/**
 * @file reticulum_identity.h
 * @brief Reticulum destination identity facts shared by chat and contact domains.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace chat
{

constexpr std::size_t kReticulumPeerHashSize = 16;
constexpr std::size_t kReticulumLxmfHashSize = 32;

struct ReticulumPeerIdentity
{
    bool valid = false;
    uint8_t destination_hash[kReticulumPeerHashSize] = {};
    uint8_t identity_hash[kReticulumPeerHashSize] = {};
};

inline ReticulumPeerIdentity makeReticulumPeerIdentity(
    const uint8_t* destination_hash,
    const uint8_t* identity_hash)
{
    ReticulumPeerIdentity identity{};
    if (!destination_hash || !identity_hash)
    {
        return identity;
    }
    identity.valid = true;
    std::memcpy(identity.destination_hash,
                destination_hash,
                kReticulumPeerHashSize);
    std::memcpy(identity.identity_hash, identity_hash, kReticulumPeerHashSize);
    return identity;
}

inline bool copyReticulumIdentityHashes(
    uint8_t* destination_hash,
    uint8_t* identity_hash,
    const ReticulumPeerIdentity& identity)
{
    if (!destination_hash || !identity_hash || !identity.valid)
    {
        return false;
    }
    std::memcpy(destination_hash,
                identity.destination_hash,
                kReticulumPeerHashSize);
    std::memcpy(identity_hash, identity.identity_hash, kReticulumPeerHashSize);
    return true;
}

inline ReticulumPeerIdentity makeReticulumDestinationIdentity(
    const uint8_t* destination_hash)
{
    ReticulumPeerIdentity identity{};
    if (!destination_hash)
    {
        return identity;
    }
    identity.valid = true;
    std::memcpy(identity.destination_hash,
                destination_hash,
                kReticulumPeerHashSize);
    return identity;
}

inline bool copyReticulumDestinationHash(
    uint8_t* out,
    const ReticulumPeerIdentity& identity)
{
    if (!out || !identity.valid)
    {
        return false;
    }
    std::memcpy(out, identity.destination_hash, kReticulumPeerHashSize);
    return true;
}

inline bool sameReticulumDestinationHash(const ReticulumPeerIdentity& identity,
                                         const uint8_t* destination_hash)
{
    if (!identity.valid || !destination_hash)
    {
        return false;
    }
    return std::memcmp(identity.destination_hash,
                       destination_hash,
                       kReticulumPeerHashSize) == 0;
}

inline bool hasReticulumDestinationIdentity(const ReticulumPeerIdentity& identity)
{
    return identity.valid;
}

inline int compareReticulumDestinationHash(const ReticulumPeerIdentity& lhs,
                                           const ReticulumPeerIdentity& rhs)
{
    return std::memcmp(lhs.destination_hash,
                       rhs.destination_hash,
                       kReticulumPeerHashSize);
}

inline bool sameReticulumDestinationHash(const ReticulumPeerIdentity& lhs,
                                         const ReticulumPeerIdentity& rhs)
{
    return compareReticulumDestinationHash(lhs, rhs) == 0;
}

inline bool sameReticulumPeerIdentity(const ReticulumPeerIdentity& lhs,
                                      const ReticulumPeerIdentity& rhs)
{
    if (lhs.valid != rhs.valid)
    {
        return false;
    }
    if (!lhs.valid)
    {
        return true;
    }
    return std::memcmp(lhs.destination_hash,
                       rhs.destination_hash,
                       kReticulumPeerHashSize) == 0 &&
           std::memcmp(lhs.identity_hash,
                       rhs.identity_hash,
                       kReticulumPeerHashSize) == 0;
}

inline void copyReticulumIdentityText(char* out,
                                      std::size_t out_len,
                                      const char* text)
{
    if (!out || out_len == 0)
    {
        return;
    }
    const char* source = text ? text : "";
    std::size_t index = 0;
    for (; index + 1 < out_len && source[index] != '\0'; ++index)
    {
        out[index] = source[index];
    }
    out[index] = '\0';
}

inline int reticulumHexNibble(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f')
    {
        return 10 + ch - 'a';
    }
    if (ch >= 'A' && ch <= 'F')
    {
        return 10 + ch - 'A';
    }
    return -1;
}

inline char reticulumHexDigit(uint8_t value)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    return kHex[value & 0x0F];
}

inline bool parseReticulumDestinationHashBytes(const char* text,
                                               uint8_t* out,
                                               char* error,
                                               std::size_t error_len)
{
    if (!text || !out)
    {
        copyReticulumIdentityText(error, error_len, "Destination required");
        return false;
    }

    char compact[kReticulumPeerHashSize * 2 + 1] = {};
    std::size_t compact_len = 0;
    for (const char* cursor = text; *cursor != '\0'; ++cursor)
    {
        const char ch = *cursor;
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' ||
            ch == ':' || ch == '-' || ch == '_')
        {
            continue;
        }
        if (compact_len >= sizeof(compact) - 1)
        {
            copyReticulumIdentityText(error,
                                      error_len,
                                      "Destination must be 32 hex chars");
            return false;
        }
        if (reticulumHexNibble(ch) < 0)
        {
            copyReticulumIdentityText(error,
                                      error_len,
                                      "Destination contains non-hex chars");
            return false;
        }
        compact[compact_len++] = ch;
    }

    if (compact_len != kReticulumPeerHashSize * 2)
    {
        copyReticulumIdentityText(error,
                                  error_len,
                                  "Destination must be 32 hex chars");
        return false;
    }

    for (std::size_t index = 0; index < kReticulumPeerHashSize; ++index)
    {
        const int high = reticulumHexNibble(compact[index * 2]);
        const int low = reticulumHexNibble(compact[index * 2 + 1]);
        if (high < 0 || low < 0)
        {
            copyReticulumIdentityText(error,
                                      error_len,
                                      "Destination contains non-hex chars");
            return false;
        }
        out[index] = static_cast<uint8_t>((high << 4) | low);
    }
    return true;
}

inline bool parseReticulumDestinationHashText(const char* text,
                                              ReticulumPeerIdentity* out_identity,
                                              char* error,
                                              std::size_t error_len)
{
    if (!out_identity)
    {
        copyReticulumIdentityText(error, error_len, "Destination required");
        return false;
    }
    uint8_t hash[kReticulumPeerHashSize] = {};
    if (!parseReticulumDestinationHashBytes(text, hash, error, error_len))
    {
        *out_identity = ReticulumPeerIdentity{};
        return false;
    }
    *out_identity = makeReticulumDestinationIdentity(hash);
    return true;
}

inline void formatReticulumDestinationHashText(
    const ReticulumPeerIdentity& identity,
    char* out,
    std::size_t out_len)
{
    if (!out || out_len == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!hasReticulumDestinationIdentity(identity) ||
        out_len < (kReticulumPeerHashSize * 2 + 1))
    {
        return;
    }

    for (std::size_t index = 0; index < kReticulumPeerHashSize; ++index)
    {
        out[index * 2] =
            reticulumHexDigit(static_cast<uint8_t>(identity.destination_hash[index] >> 4));
        out[index * 2 + 1] = reticulumHexDigit(identity.destination_hash[index]);
    }
    out[kReticulumPeerHashSize * 2] = '\0';
}

} // namespace chat
