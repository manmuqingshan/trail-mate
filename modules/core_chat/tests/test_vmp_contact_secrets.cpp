#include "chat/infra/voice/vmp_contact_secrets.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace
{

using chat::voice::vmp::FixedVerifiedContactSecretDirectory;
using chat::voice::vmp::kPrivateKeySize;

std::array<uint8_t, kPrivateKeySize> secret(uint8_t seed)
{
    std::array<uint8_t, kPrivateKeySize> result{};
    for (std::size_t index = 0U; index < result.size(); ++index)
    {
        result[index] = static_cast<uint8_t>(seed + index + 1U);
    }
    return result;
}

void test_directory_rejects_unverified_or_broadcast_entries()
{
    FixedVerifiedContactSecretDirectory directory;
    const std::array<uint8_t, kPrivateKeySize> zero{};
    assert(!directory.upsertVerifiedContactSecret(0U, zero.data()));
    assert(!directory.upsertVerifiedContactSecret(0xFFFFFFFFU, zero.data()));
    assert(!directory.upsertVerifiedContactSecret(42U, zero.data()));
    assert(directory.size() == 0U);
}

void test_directory_replaces_and_clears_contact_secrets()
{
    FixedVerifiedContactSecretDirectory directory;
    const auto first = secret(0x10U);
    const auto replacement = secret(0x40U);
    std::array<uint8_t, kPrivateKeySize> copied{};

    assert(directory.upsertVerifiedContactSecret(0x01020304U, first.data()));
    assert(directory.size() == 1U);
    assert(directory.hasVerifiedContactSecret(0x01020304U));
    assert(directory.lookupVerifiedContactSecret(0x01020304U, copied.data()));
    assert(copied == first);

    assert(directory.upsertVerifiedContactSecret(0x01020304U, replacement.data()));
    assert(directory.size() == 1U);
    assert(directory.lookupVerifiedContactSecret(0x01020304U, copied.data()));
    assert(copied == replacement);

    copied.fill(0xA5U);
    assert(!directory.lookupVerifiedContactSecret(0x55667788U, copied.data()));
    for (uint8_t byte : copied)
    {
        assert(byte == 0U);
    }

    assert(directory.removeVerifiedContactSecret(0x01020304U));
    assert(directory.size() == 0U);
    assert(!directory.hasVerifiedContactSecret(0x01020304U));
}

} // namespace

int main()
{
    test_directory_rejects_unverified_or_broadcast_entries();
    test_directory_replaces_and_clears_contact_secrets();
    return 0;
}
