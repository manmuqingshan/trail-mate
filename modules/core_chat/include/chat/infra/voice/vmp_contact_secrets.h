/**
 * @file vmp_contact_secrets.h
 * @brief Fixed, separately-provisioned private-contact secret directory.
 *
 * This is intentionally separate from MT channel keys, MC routing secrets,
 * and Reticulum transport/link state. A private VMP session may proceed only
 * when its peer has an explicitly verified secret in this directory.
 */

#pragma once

#include "chat/infra/voice/vmp_private_crypto.h"

#include <cstddef>
#include <cstdint>

namespace chat::voice::vmp
{

inline constexpr std::size_t kMaxVerifiedVoiceContacts = 16U;

class IVerifiedContactSecretProvider
{
  public:
    virtual ~IVerifiedContactSecretProvider() = default;

    /**
     * @brief Obtains the pinned static-contact secret for exactly one peer.
     *
     * The returned bytes are `K_contact`, not an MT/MC key and not an
     * ephemeral VMP session key. The caller owns and must clear `out_secret`
     * after deriving VMP session keys.
     */
    virtual bool lookupVerifiedContactSecret(
        uint32_t peer_id,
        uint8_t out_secret[kPrivateKeySize]) const = 0;
};

/**
 * @brief Bounded in-memory implementation used behind a protected key store.
 *
 * Firmware must keep an instance in owned static/runtime storage. The
 * provisioning adapter supplies secrets only after user-visible contact-key
 * verification; this type intentionally has no unauthenticated "learn" API.
 */
class FixedVerifiedContactSecretDirectory final
    : public IVerifiedContactSecretProvider
{
  public:
    bool upsertVerifiedContactSecret(
        uint32_t peer_id,
        const uint8_t secret[kPrivateKeySize]);
    bool removeVerifiedContactSecret(uint32_t peer_id);
    void clear();

    bool lookupVerifiedContactSecret(
        uint32_t peer_id,
        uint8_t out_secret[kPrivateKeySize]) const override;

    [[nodiscard]] bool hasVerifiedContactSecret(uint32_t peer_id) const;

    [[nodiscard]] std::size_t size() const { return size_; }

  private:
    struct Entry
    {
        uint32_t peer_id = 0U;
        uint8_t secret[kPrivateKeySize] = {};
        bool occupied = false;
    };

    Entry entries_[kMaxVerifiedVoiceContacts] = {};
    std::size_t size_ = 0U;
};

} // namespace chat::voice::vmp
