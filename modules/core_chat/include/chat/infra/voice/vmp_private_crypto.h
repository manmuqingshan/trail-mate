/**
 * @file vmp_private_crypto.h
 * @brief Private-session cryptography for Trail Mate VMP v1.
 *
 * The contact secret supplied here is derived from a separately verified and
 * pinned X25519 contact identity.  VMP never falls back to a channel key or
 * an unauthenticated peer ID.  Every voice transfer creates a new ephemeral
 * X25519 key pair and destroys its private half after deriving these keys.
 */

#pragma once

#include "chat/infra/voice/vmp_wire.h"

#include <cstddef>
#include <cstdint>

namespace chat::voice::vmp
{

inline constexpr std::size_t kPrivateKeySize = 32;
inline constexpr std::size_t kPrivateFrameNonceSize = 12;

/**
 * Static identity family used only to domain-separate a derived VMP contact
 * secret.  This is not an on-air VMP field and does not reuse any MT/MC
 * channel, forwarding, or application key.
 */
enum class ContactSecretIdentityFamily : uint8_t
{
    Meshtastic = 1U,
    MeshCore = 2U,
    Reticulum = 3U,
};

enum class PrivateFrameDirection : uint8_t
{
    SenderToReceiver = 1,
    ReceiverToSender = 2,
};

struct EphemeralKeyPair
{
    uint8_t public_key[kEphemeralPublicKeySize] = {};
    uint8_t private_key[kPrivateKeySize] = {};
};

struct PrivateSessionKeys
{
    uint8_t control_key[kPrivateKeySize] = {};
    uint8_t ready_key[kPrivateKeySize] = {};
    uint8_t data_key[kPrivateKeySize] = {};
    uint8_t mqtt_key[kPrivateKeySize] = {};
};

/**
 * @brief Creates a fresh X25519 ephemeral pair using the platform CSPRNG.
 */
bool generateEphemeralKeyPair(EphemeralKeyPair* out_pair);

/**
 * Turns a shared secret from an already verified static contact identity into
 * VMP-only `K_contact`.  Both node IDs are sorted before binding, so each peer
 * obtains identical bytes while different protocol families remain separated.
 * The input is immediately suitable only after the caller has verified the
 * peer identity through its own contact-verification policy.
 */
bool deriveVmpContactSecret(
    const uint8_t verified_identity_shared_secret[kPrivateKeySize],
    ContactSecretIdentityFamily family,
    uint32_t local_node_id,
    uint32_t peer_node_id,
    uint8_t out_contact_secret[kPrivateKeySize]);

/**
 * @brief Derives the static-contact control key before an OFFER is accepted.
 *
 * The receiver invokes this with a verified contact secret to authenticate an
 * incoming OFFER before it allocates an ephemeral key or reserves 2.4 GHz.
 */
bool derivePrivateControlKey(
    const uint8_t verified_contact_secret[kPrivateKeySize],
    const uint8_t session_nonce[kSessionNonceSize],
    uint64_t session_id,
    uint8_t out_control_key[kPrivateKeySize]);

/**
 * @brief Derives VMP keys and securely clears `local_ephemeral_private`.
 *
 * `verified_contact_secret` must be the 32-byte static-contact X25519 shared
 * secret created only after contact verification.  A caller must abort a
 * private session if this function returns false; plaintext must never be
 * transmitted as a fallback.
 */
bool derivePrivateSessionKeys(
    const uint8_t verified_contact_secret[kPrivateKeySize],
    uint8_t local_ephemeral_private[kPrivateKeySize],
    const uint8_t peer_ephemeral_public[kEphemeralPublicKeySize],
    const uint8_t session_nonce[kSessionNonceSize],
    uint64_t session_id,
    PrivateSessionKeys* out_keys);

/**
 * @brief Derives the private VMP MQTT key schedule without a radio handshake.
 *
 * MQTT is an asynchronous store-and-forward carrier and therefore cannot use
 * the receiver's per-transfer `ACCEPT` ephemeral key. This schedule remains
 * end-to-end encrypted using only the already verified static contact secret,
 * with independent control, data, and MQTT keys. It MUST be used only by the
 * VMP MQTT carrier; radio sessions always use derivePrivateSessionKeys().
 */
bool derivePrivateMqttSessionKeys(
    const uint8_t verified_contact_secret[kPrivateKeySize],
    const uint8_t session_nonce[kSessionNonceSize],
    uint64_t session_id,
    PrivateSessionKeys* out_keys);

/** @brief Explicitly erases session keys when a VMP reservation ends. */
void clearPrivateSessionKeys(PrivateSessionKeys* keys);

/**
 * @brief Computes the 16-byte AEAD tag for serialized private OFFER/ACCEPT.
 *
 * `authenticated_control` is the first 79 bytes of a control frame (all
 * fields before its integrity trailer).  The returned tag authenticates all
 * of those bytes; it does not encrypt control metadata.
 */
bool tagPrivateControl(
    const PrivateSessionKeys& keys,
    const uint8_t session_nonce[kSessionNonceSize],
    ControlType type,
    PrivateFrameDirection direction,
    const uint8_t* authenticated_control,
    std::size_t authenticated_control_len,
    uint8_t out_tag[kControlIntegrityTagSize]);

/** @brief Constant-time verification counterpart to tagPrivateControl(). */
bool verifyPrivateControlTag(
    const PrivateSessionKeys& keys,
    const uint8_t session_nonce[kSessionNonceSize],
    ControlType type,
    PrivateFrameDirection direction,
    const uint8_t* authenticated_control,
    std::size_t authenticated_control_len,
    const uint8_t tag[kControlIntegrityTagSize]);

/**
 * @brief Authenticates a private READY_PROBE or READY frame on 2.4 GHz.
 */
bool tagPrivateReady(const PrivateSessionKeys& keys,
                     const uint8_t session_nonce[kSessionNonceSize],
                     PrivateFrameDirection direction,
                     const DataHeader& header,
                     uint8_t out_tag[kPrivateDataAuthTagSize]);

bool verifyPrivateReadyTag(const PrivateSessionKeys& keys,
                           const uint8_t session_nonce[kSessionNonceSize],
                           PrivateFrameDirection direction,
                           const DataHeader& header,
                           const uint8_t tag[kPrivateDataAuthTagSize]);

/**
 * @brief Encrypts one private VMP source/parity shard using ChaCha20-Poly1305.
 *
 * The data header is serialized as AEAD associated data.  Ciphertext has the
 * same length as plaintext and the full 16-byte authentication tag is emitted
 * separately, so every private 2.4 GHz voice frame is 192 bytes at most.
 */
bool sealPrivateShard(const PrivateSessionKeys& keys,
                      const uint8_t session_nonce[kSessionNonceSize],
                      PrivateFrameDirection direction,
                      const DataHeader& header,
                      const uint8_t* plaintext,
                      std::size_t plaintext_len,
                      uint8_t* out_ciphertext,
                      uint8_t out_tag[kPrivateDataAuthTagSize]);

/**
 * @brief Authenticates and decrypts a private VMP shard.
 *
 * On a tag failure `out_plaintext` is cleared.  Callers MUST reject the frame
 * and MUST NOT place the shard into the FEC slot if this returns false.
 */
bool openPrivateShard(const PrivateSessionKeys& keys,
                      const uint8_t session_nonce[kSessionNonceSize],
                      PrivateFrameDirection direction,
                      const DataHeader& header,
                      const uint8_t* ciphertext,
                      std::size_t ciphertext_len,
                      const uint8_t tag[kPrivateDataAuthTagSize],
                      uint8_t* out_plaintext);

} // namespace chat::voice::vmp
