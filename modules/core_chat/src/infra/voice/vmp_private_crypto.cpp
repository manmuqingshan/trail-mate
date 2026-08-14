/**
 * @file vmp_private_crypto.cpp
 * @brief Private-session cryptography for Trail Mate VMP v1.
 */

#include "chat/infra/voice/vmp_private_crypto.h"

#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include "chat/infra/voice/reticulum_crypto_compat.h"
#endif

#if defined(ESP_PLATFORM) && !defined(ARDUINO)
#include "mbedtls/chachapoly.h"
#include "mbedtls/md.h"
#elif defined(ARDUINO)
#include <ChaChaPoly.h>
#include <HKDF.h>
#include <SHA256.h>
#elif defined(TRAIL_MATE_HAS_OPENSSL)
#include <openssl/evp.h>
#include <openssl/hmac.h>
#endif

#include <cstring>

namespace chat::voice::vmp
{
namespace
{

constexpr std::size_t kPrivateControlAuthenticatedBytes =
    kControlFrameSize - kControlIntegrityTagSize;
constexpr uint8_t kControlNonceDomain = 0xC1U;
constexpr uint8_t kReadyNonceDomain = 0x52U;
constexpr uint8_t kContactDerivationSalt[kSessionNonceSize] = {
    'T', 'M', 'V', 'M', 'P', '-', 'V', '1', '-', 'C', 'T', 'K'};

bool isAllZero(const uint8_t* data, std::size_t len)
{
    if (!data)
    {
        return true;
    }
    uint8_t accumulated = 0;
    for (std::size_t index = 0; index < len; ++index)
    {
        accumulated |= data[index];
    }
    return accumulated == 0U;
}

void secureZero(void* data, std::size_t len)
{
    volatile uint8_t* cursor = static_cast<volatile uint8_t*>(data);
    while (cursor && len != 0U)
    {
        *cursor++ = 0;
        --len;
    }
}

bool constantTimeEqual(const uint8_t* left, const uint8_t* right, std::size_t len)
{
    if (!left || !right)
    {
        return false;
    }
    uint8_t difference = 0;
    for (std::size_t index = 0; index < len; ++index)
    {
        difference |= static_cast<uint8_t>(left[index] ^ right[index]);
    }
    return difference == 0U;
}

void writeSessionId(uint64_t session_id, uint8_t out[sizeof(session_id)])
{
    for (std::size_t index = 0; index < sizeof(session_id); ++index)
    {
        const std::size_t shift = (sizeof(session_id) - 1U - index) * 8U;
        out[index] = static_cast<uint8_t>(session_id >> shift);
    }
}

bool buildKdfInfo(const char* label,
                  uint64_t session_id,
                  uint8_t out_info[64],
                  std::size_t* out_len)
{
    if (!label || !out_info || !out_len)
    {
        return false;
    }
    const std::size_t label_len = std::strlen(label);
    if (label_len + sizeof(session_id) + 1U > 64U)
    {
        return false;
    }
    std::memcpy(out_info, label, label_len);
    writeSessionId(session_id, out_info + label_len);
    out_info[label_len + sizeof(session_id)] = 1U;
    *out_len = label_len + sizeof(session_id) + 1U;
    return true;
}

bool deriveKey(const uint8_t input_key[kPrivateKeySize],
               const uint8_t session_nonce[kSessionNonceSize],
               const char* label,
               uint64_t session_id,
               uint8_t out_key[kPrivateKeySize])
{
    if (!input_key || !session_nonce || !label || !out_key ||
        isAllZero(input_key, kPrivateKeySize))
    {
        return false;
    }

    uint8_t info[64] = {};
    std::size_t info_len = 0;
    if (!buildKdfInfo(label, session_id, info, &info_len))
    {
        return false;
    }

#if defined(ESP_PLATFORM) && !defined(ARDUINO)
    uint8_t prk[kPrivateKeySize] = {};
    const mbedtls_md_info_t* const md =
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    const bool ok = md != nullptr &&
                    mbedtls_md_hmac(md,
                                    session_nonce,
                                    kSessionNonceSize,
                                    input_key,
                                    kPrivateKeySize,
                                    prk) == 0 &&
                    mbedtls_md_hmac(md,
                                    prk,
                                    sizeof(prk),
                                    info,
                                    info_len,
                                    out_key) == 0;
    secureZero(prk, sizeof(prk));
    secureZero(info, sizeof(info));
    return ok;
#elif defined(ARDUINO)
    hkdf<SHA256>(out_key,
                 kPrivateKeySize,
                 input_key,
                 kPrivateKeySize,
                 session_nonce,
                 kSessionNonceSize,
                 info,
                 info_len - 1U);
    secureZero(info, sizeof(info));
    return !isAllZero(out_key, kPrivateKeySize);
#elif defined(TRAIL_MATE_HAS_OPENSSL)
    uint8_t prk[kPrivateKeySize] = {};
    unsigned int output_len = 0;
    const bool ok =
        HMAC(EVP_sha256(),
             session_nonce,
             static_cast<int>(kSessionNonceSize),
             input_key,
             kPrivateKeySize,
             prk,
             &output_len) != nullptr &&
        output_len == kPrivateKeySize &&
        HMAC(EVP_sha256(),
             prk,
             static_cast<int>(sizeof(prk)),
             info,
             info_len,
             out_key,
             &output_len) != nullptr &&
        output_len == kPrivateKeySize;
    secureZero(prk, sizeof(prk));
    secureZero(info, sizeof(info));
    return ok;
#else
    secureZero(info, sizeof(info));
    (void)session_id;
    return false;
#endif
}

const char* contactFamilyLabel(ContactSecretIdentityFamily family)
{
    switch (family)
    {
    case ContactSecretIdentityFamily::Meshtastic:
        return "TrailMate/VMP/v1/contact/mt";
    case ContactSecretIdentityFamily::MeshCore:
        return "TrailMate/VMP/v1/contact/mc";
    case ContactSecretIdentityFamily::Reticulum:
        return "TrailMate/VMP/v1/contact/rt";
    default:
        return nullptr;
    }
}

void makeControlNonce(const uint8_t session_nonce[kSessionNonceSize],
                      ControlType type,
                      PrivateFrameDirection direction,
                      uint8_t out_nonce[kPrivateFrameNonceSize])
{
    std::memcpy(out_nonce, session_nonce, 8U);
    out_nonce[8] = static_cast<uint8_t>(type);
    out_nonce[9] = static_cast<uint8_t>(direction);
    out_nonce[10] = kControlNonceDomain;
    out_nonce[11] = 0U;
}

bool makeDataNonce(const uint8_t session_nonce[kSessionNonceSize],
                   const DataHeader& header,
                   PrivateFrameDirection direction,
                   uint8_t domain,
                   uint8_t out_nonce[kPrivateFrameNonceSize])
{
    if (!session_nonce || !out_nonce || !isValidDataHeader(header))
    {
        return false;
    }
    std::memcpy(out_nonce, session_nonce, 8U);
    out_nonce[8] = static_cast<uint8_t>(header.type);
    out_nonce[9] = header.block_index;
    out_nonce[10] = header.shard_index;
    out_nonce[11] = static_cast<uint8_t>(
        static_cast<uint8_t>(direction) ^ domain);
    return true;
}

bool isKnownDirection(PrivateFrameDirection direction)
{
    return direction == PrivateFrameDirection::SenderToReceiver ||
           direction == PrivateFrameDirection::ReceiverToSender;
}

bool sealAead(const uint8_t key[kPrivateKeySize],
              const uint8_t nonce[kPrivateFrameNonceSize],
              const uint8_t* aad,
              std::size_t aad_len,
              const uint8_t* plaintext,
              std::size_t plaintext_len,
              uint8_t* out_ciphertext,
              uint8_t out_tag[kPrivateDataAuthTagSize])
{
    if (!key || !nonce || !aad || aad_len == 0U ||
        (plaintext_len != 0U && (!plaintext || !out_ciphertext)) || !out_tag)
    {
        return false;
    }

#if defined(ESP_PLATFORM) && !defined(ARDUINO)
    uint8_t empty = 0;
    mbedtls_chachapoly_context context;
    mbedtls_chachapoly_init(&context);
    const bool ok =
        mbedtls_chachapoly_setkey(&context, key) == 0 &&
        mbedtls_chachapoly_encrypt_and_tag(
            &context,
            plaintext_len,
            nonce,
            aad,
            aad_len,
            plaintext_len == 0U ? &empty : plaintext,
            plaintext_len == 0U ? &empty : out_ciphertext,
            out_tag) == 0;
    mbedtls_chachapoly_free(&context);
    return ok;
#elif defined(ARDUINO)
    uint8_t empty = 0;
    ChaChaPoly cipher;
    const bool ok = cipher.setKey(key, kPrivateKeySize) &&
                    cipher.setIV(nonce, kPrivateFrameNonceSize);
    if (ok)
    {
        cipher.addAuthData(aad, aad_len);
        cipher.encrypt(plaintext_len == 0U ? &empty : out_ciphertext,
                       plaintext_len == 0U ? &empty : plaintext,
                       plaintext_len);
        cipher.computeTag(out_tag, kPrivateDataAuthTagSize);
    }
    cipher.clear();
    return ok;
#elif defined(TRAIL_MATE_HAS_OPENSSL)
    EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
    int output_len = 0;
    int final_len = 0;
    const bool ok =
        context != nullptr &&
        EVP_EncryptInit_ex(context, EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) ==
            1 &&
        EVP_CIPHER_CTX_ctrl(context,
                            EVP_CTRL_AEAD_SET_IVLEN,
                            static_cast<int>(kPrivateFrameNonceSize),
                            nullptr) == 1 &&
        EVP_EncryptInit_ex(context, nullptr, nullptr, key, nonce) == 1 &&
        EVP_EncryptUpdate(context,
                          nullptr,
                          &output_len,
                          aad,
                          static_cast<int>(aad_len)) == 1 &&
        (plaintext_len == 0U ||
         EVP_EncryptUpdate(context,
                           out_ciphertext,
                           &output_len,
                           plaintext,
                           static_cast<int>(plaintext_len)) == 1) &&
        EVP_EncryptFinal_ex(context, nullptr, &final_len) == 1 &&
        EVP_CIPHER_CTX_ctrl(context,
                            EVP_CTRL_AEAD_GET_TAG,
                            static_cast<int>(kPrivateDataAuthTagSize),
                            out_tag) == 1;
    if (context)
    {
        EVP_CIPHER_CTX_free(context);
    }
    return ok;
#else
    (void)plaintext;
    (void)plaintext_len;
    (void)out_ciphertext;
    (void)out_tag;
    return false;
#endif
}

bool openAead(const uint8_t key[kPrivateKeySize],
              const uint8_t nonce[kPrivateFrameNonceSize],
              const uint8_t* aad,
              std::size_t aad_len,
              const uint8_t* ciphertext,
              std::size_t ciphertext_len,
              const uint8_t tag[kPrivateDataAuthTagSize],
              uint8_t* out_plaintext)
{
    if (!key || !nonce || !aad || aad_len == 0U ||
        (ciphertext_len != 0U && (!ciphertext || !out_plaintext)) || !tag)
    {
        return false;
    }

#if defined(ESP_PLATFORM) && !defined(ARDUINO)
    uint8_t empty = 0;
    mbedtls_chachapoly_context context;
    mbedtls_chachapoly_init(&context);
    const bool ok =
        mbedtls_chachapoly_setkey(&context, key) == 0 &&
        mbedtls_chachapoly_auth_decrypt(
            &context,
            ciphertext_len,
            nonce,
            aad,
            aad_len,
            tag,
            ciphertext_len == 0U ? &empty : ciphertext,
            ciphertext_len == 0U ? &empty : out_plaintext) == 0;
    mbedtls_chachapoly_free(&context);
    return ok;
#elif defined(ARDUINO)
    uint8_t empty = 0;
    ChaChaPoly cipher;
    const bool initialized = cipher.setKey(key, kPrivateKeySize) &&
                             cipher.setIV(nonce, kPrivateFrameNonceSize);
    bool ok = false;
    if (initialized)
    {
        cipher.addAuthData(aad, aad_len);
        cipher.decrypt(ciphertext_len == 0U ? &empty : out_plaintext,
                       ciphertext_len == 0U ? &empty : ciphertext,
                       ciphertext_len);
        ok = cipher.checkTag(tag, kPrivateDataAuthTagSize);
    }
    cipher.clear();
    return ok;
#elif defined(TRAIL_MATE_HAS_OPENSSL)
    EVP_CIPHER_CTX* context = EVP_CIPHER_CTX_new();
    int output_len = 0;
    int final_len = 0;
    const bool ok =
        context != nullptr &&
        EVP_DecryptInit_ex(context, EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) ==
            1 &&
        EVP_CIPHER_CTX_ctrl(context,
                            EVP_CTRL_AEAD_SET_IVLEN,
                            static_cast<int>(kPrivateFrameNonceSize),
                            nullptr) == 1 &&
        EVP_DecryptInit_ex(context, nullptr, nullptr, key, nonce) == 1 &&
        EVP_DecryptUpdate(context,
                          nullptr,
                          &output_len,
                          aad,
                          static_cast<int>(aad_len)) == 1 &&
        (ciphertext_len == 0U ||
         EVP_DecryptUpdate(context,
                           out_plaintext,
                           &output_len,
                           ciphertext,
                           static_cast<int>(ciphertext_len)) == 1) &&
        EVP_CIPHER_CTX_ctrl(context,
                            EVP_CTRL_AEAD_SET_TAG,
                            static_cast<int>(kPrivateDataAuthTagSize),
                            const_cast<uint8_t*>(tag)) == 1 &&
        EVP_DecryptFinal_ex(context, nullptr, &final_len) == 1;
    if (context)
    {
        EVP_CIPHER_CTX_free(context);
    }
    return ok;
#else
    (void)ciphertext;
    (void)ciphertext_len;
    (void)tag;
    (void)out_plaintext;
    return false;
#endif
}

bool deriveEphemeralSecret(uint8_t local_ephemeral_private[kPrivateKeySize],
                           const uint8_t peer_ephemeral_public[kEphemeralPublicKeySize],
                           uint8_t out_secret[kPrivateKeySize])
{
    if (!local_ephemeral_private || !peer_ephemeral_public || !out_secret ||
        isAllZero(local_ephemeral_private, kPrivateKeySize) ||
        isAllZero(peer_ephemeral_public, kEphemeralPublicKeySize))
    {
        return false;
    }

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    std::memcpy(out_secret, peer_ephemeral_public, kPrivateKeySize);
    return Curve25519::dh2(out_secret, local_ephemeral_private) &&
           !isAllZero(out_secret, kPrivateKeySize);
#elif defined(TRAIL_MATE_HAS_OPENSSL)
    EVP_PKEY* const local = EVP_PKEY_new_raw_private_key(
        EVP_PKEY_X25519, nullptr, local_ephemeral_private, kPrivateKeySize);
    EVP_PKEY* const peer = EVP_PKEY_new_raw_public_key(
        EVP_PKEY_X25519, nullptr, peer_ephemeral_public, kEphemeralPublicKeySize);
    EVP_PKEY_CTX* const context = local ? EVP_PKEY_CTX_new(local, nullptr) : nullptr;
    std::size_t secret_len = kPrivateKeySize;
    const bool ok = local != nullptr && peer != nullptr && context != nullptr &&
                    EVP_PKEY_derive_init(context) == 1 &&
                    EVP_PKEY_derive_set_peer(context, peer) == 1 &&
                    EVP_PKEY_derive(context, out_secret, &secret_len) == 1 &&
                    secret_len == kPrivateKeySize &&
                    !isAllZero(out_secret, kPrivateKeySize);
    if (context)
    {
        EVP_PKEY_CTX_free(context);
    }
    if (peer)
    {
        EVP_PKEY_free(peer);
    }
    if (local)
    {
        EVP_PKEY_free(local);
    }
    secureZero(local_ephemeral_private, kPrivateKeySize);
    return ok;
#else
    (void)out_secret;
    return false;
#endif
}

} // namespace

bool generateEphemeralKeyPair(EphemeralKeyPair* out_pair)
{
    if (!out_pair)
    {
        return false;
    }
    *out_pair = {};

#if defined(ESP_PLATFORM) || defined(ARDUINO)
    RNG.begin("trail-mate-vmp");
    Curve25519::dh1(out_pair->public_key, out_pair->private_key);
    const bool ok = !isAllZero(out_pair->public_key, sizeof(out_pair->public_key)) &&
                    !isAllZero(out_pair->private_key, sizeof(out_pair->private_key));
    if (!ok)
    {
        *out_pair = {};
    }
    return ok;
#elif defined(TRAIL_MATE_HAS_OPENSSL)
    EVP_PKEY_CTX* context = EVP_PKEY_CTX_new_id(EVP_PKEY_X25519, nullptr);
    EVP_PKEY* key = nullptr;
    std::size_t public_len = sizeof(out_pair->public_key);
    std::size_t private_len = sizeof(out_pair->private_key);
    const bool ok = context != nullptr && EVP_PKEY_keygen_init(context) == 1 &&
                    EVP_PKEY_keygen(context, &key) == 1 && key != nullptr &&
                    EVP_PKEY_get_raw_public_key(
                        key, out_pair->public_key, &public_len) == 1 &&
                    EVP_PKEY_get_raw_private_key(
                        key, out_pair->private_key, &private_len) == 1 &&
                    public_len == sizeof(out_pair->public_key) &&
                    private_len == sizeof(out_pair->private_key);
    if (key)
    {
        EVP_PKEY_free(key);
    }
    if (context)
    {
        EVP_PKEY_CTX_free(context);
    }
    if (!ok)
    {
        *out_pair = {};
    }
    return ok;
#else
    return false;
#endif
}

bool deriveVmpContactSecret(
    const uint8_t verified_identity_shared_secret[kPrivateKeySize],
    ContactSecretIdentityFamily family,
    uint32_t local_node_id,
    uint32_t peer_node_id,
    uint8_t out_contact_secret[kPrivateKeySize])
{
    if (!verified_identity_shared_secret || !out_contact_secret ||
        isAllZero(verified_identity_shared_secret, kPrivateKeySize) ||
        local_node_id == 0U || peer_node_id == 0U || local_node_id == peer_node_id)
    {
        if (out_contact_secret)
        {
            secureZero(out_contact_secret, kPrivateKeySize);
        }
        return false;
    }
    const char* const label = contactFamilyLabel(family);
    if (!label)
    {
        secureZero(out_contact_secret, kPrivateKeySize);
        return false;
    }
    const uint32_t lower_node_id = local_node_id < peer_node_id ? local_node_id : peer_node_id;
    const uint32_t higher_node_id = local_node_id < peer_node_id ? peer_node_id : local_node_id;
    const uint64_t pair_binding =
        (static_cast<uint64_t>(lower_node_id) << 32U) | higher_node_id;
    const bool ok = deriveKey(verified_identity_shared_secret,
                              kContactDerivationSalt,
                              label,
                              pair_binding,
                              out_contact_secret);
    if (!ok)
    {
        secureZero(out_contact_secret, kPrivateKeySize);
    }
    return ok;
}

bool derivePrivateControlKey(
    const uint8_t verified_contact_secret[kPrivateKeySize],
    const uint8_t session_nonce[kSessionNonceSize],
    uint64_t session_id,
    uint8_t out_control_key[kPrivateKeySize])
{
    if (!verified_contact_secret || !session_nonce || !out_control_key ||
        session_id == 0U || isAllZero(verified_contact_secret, kPrivateKeySize) ||
        isAllZero(session_nonce, kSessionNonceSize))
    {
        return false;
    }
    return deriveKey(verified_contact_secret,
                     session_nonce,
                     "TrailMate/VMP/v1/private-control",
                     session_id,
                     out_control_key);
}

bool derivePrivateSessionKeys(
    const uint8_t verified_contact_secret[kPrivateKeySize],
    uint8_t local_ephemeral_private[kPrivateKeySize],
    const uint8_t peer_ephemeral_public[kEphemeralPublicKeySize],
    const uint8_t session_nonce[kSessionNonceSize],
    uint64_t session_id,
    PrivateSessionKeys* out_keys)
{
    if (!verified_contact_secret || !local_ephemeral_private ||
        !peer_ephemeral_public || !session_nonce || !out_keys || session_id == 0U ||
        isAllZero(verified_contact_secret, kPrivateKeySize) ||
        isAllZero(session_nonce, kSessionNonceSize))
    {
        if (local_ephemeral_private)
        {
            secureZero(local_ephemeral_private, kPrivateKeySize);
        }
        clearPrivateSessionKeys(out_keys);
        return false;
    }

    clearPrivateSessionKeys(out_keys);
    uint8_t ephemeral_secret[kPrivateKeySize] = {};
    const bool shared_secret_ok = deriveEphemeralSecret(
        local_ephemeral_private, peer_ephemeral_public, ephemeral_secret);
    const bool keys_ok =
        shared_secret_ok &&
        derivePrivateControlKey(verified_contact_secret,
                                session_nonce,
                                session_id,
                                out_keys->control_key) &&
        deriveKey(ephemeral_secret,
                  session_nonce,
                  "TrailMate/VMP/v1/ready",
                  session_id,
                  out_keys->ready_key) &&
        deriveKey(ephemeral_secret,
                  session_nonce,
                  "TrailMate/VMP/v1/data",
                  session_id,
                  out_keys->data_key) &&
        deriveKey(ephemeral_secret,
                  session_nonce,
                  "TrailMate/VMP/v1/mqtt",
                  session_id,
                  out_keys->mqtt_key);
    secureZero(ephemeral_secret, sizeof(ephemeral_secret));
    secureZero(local_ephemeral_private, kPrivateKeySize);
    if (!keys_ok)
    {
        clearPrivateSessionKeys(out_keys);
    }
    return keys_ok;
}

bool derivePrivateMqttSessionKeys(
    const uint8_t verified_contact_secret[kPrivateKeySize],
    const uint8_t session_nonce[kSessionNonceSize],
    uint64_t session_id,
    PrivateSessionKeys* out_keys)
{
    if (!verified_contact_secret || !session_nonce || !out_keys || session_id == 0U ||
        isAllZero(verified_contact_secret, kPrivateKeySize) ||
        isAllZero(session_nonce, kSessionNonceSize))
    {
        clearPrivateSessionKeys(out_keys);
        return false;
    }

    clearPrivateSessionKeys(out_keys);
    const bool keys_ok =
        derivePrivateControlKey(verified_contact_secret,
                                session_nonce,
                                session_id,
                                out_keys->control_key) &&
        deriveKey(out_keys->control_key,
                  session_nonce,
                  "TrailMate/VMP/v1/mqtt-ready",
                  session_id,
                  out_keys->ready_key) &&
        deriveKey(out_keys->control_key,
                  session_nonce,
                  "TrailMate/VMP/v1/mqtt-data",
                  session_id,
                  out_keys->data_key) &&
        deriveKey(out_keys->control_key,
                  session_nonce,
                  "TrailMate/VMP/v1/mqtt-manifest",
                  session_id,
                  out_keys->mqtt_key);
    if (!keys_ok)
    {
        clearPrivateSessionKeys(out_keys);
    }
    return keys_ok;
}

void clearPrivateSessionKeys(PrivateSessionKeys* keys)
{
    if (keys)
    {
        secureZero(keys, sizeof(*keys));
    }
}

bool tagPrivateControl(
    const PrivateSessionKeys& keys,
    const uint8_t session_nonce[kSessionNonceSize],
    ControlType type,
    PrivateFrameDirection direction,
    const uint8_t* authenticated_control,
    std::size_t authenticated_control_len,
    uint8_t out_tag[kControlIntegrityTagSize])
{
    if (!session_nonce || !authenticated_control || !out_tag ||
        authenticated_control_len != kPrivateControlAuthenticatedBytes ||
        !isKnownDirection(direction))
    {
        return false;
    }
    uint8_t nonce[kPrivateFrameNonceSize] = {};
    makeControlNonce(session_nonce, type, direction, nonce);
    const bool ok = sealAead(keys.control_key,
                             nonce,
                             authenticated_control,
                             authenticated_control_len,
                             nullptr,
                             0,
                             nullptr,
                             out_tag);
    secureZero(nonce, sizeof(nonce));
    return ok;
}

bool verifyPrivateControlTag(
    const PrivateSessionKeys& keys,
    const uint8_t session_nonce[kSessionNonceSize],
    ControlType type,
    PrivateFrameDirection direction,
    const uint8_t* authenticated_control,
    std::size_t authenticated_control_len,
    const uint8_t tag[kControlIntegrityTagSize])
{
    if (!tag)
    {
        return false;
    }
    uint8_t expected_tag[kControlIntegrityTagSize] = {};
    const bool ok = tagPrivateControl(keys,
                                      session_nonce,
                                      type,
                                      direction,
                                      authenticated_control,
                                      authenticated_control_len,
                                      expected_tag) &&
                    constantTimeEqual(expected_tag, tag, sizeof(expected_tag));
    secureZero(expected_tag, sizeof(expected_tag));
    return ok;
}

bool tagPrivateReady(const PrivateSessionKeys& keys,
                     const uint8_t session_nonce[kSessionNonceSize],
                     PrivateFrameDirection direction,
                     const DataHeader& header,
                     uint8_t out_tag[kPrivateDataAuthTagSize])
{
    if (!session_nonce || !out_tag || !isKnownDirection(direction) ||
        (header.type != DataType::ReadyProbe && header.type != DataType::Ready) ||
        !isValidDataHeader(header))
    {
        return false;
    }
    uint8_t encoded_header[kDataHeaderSize] = {};
    std::size_t encoded_len = sizeof(encoded_header);
    uint8_t nonce[kPrivateFrameNonceSize] = {};
    const bool ok = encodeDataHeader(header, encoded_header, &encoded_len) &&
                    makeDataNonce(session_nonce,
                                  header,
                                  direction,
                                  kReadyNonceDomain,
                                  nonce) &&
                    sealAead(keys.ready_key,
                             nonce,
                             encoded_header,
                             encoded_len,
                             nullptr,
                             0,
                             nullptr,
                             out_tag);
    secureZero(encoded_header, sizeof(encoded_header));
    secureZero(nonce, sizeof(nonce));
    return ok;
}

bool verifyPrivateReadyTag(const PrivateSessionKeys& keys,
                           const uint8_t session_nonce[kSessionNonceSize],
                           PrivateFrameDirection direction,
                           const DataHeader& header,
                           const uint8_t tag[kPrivateDataAuthTagSize])
{
    if (!tag)
    {
        return false;
    }
    uint8_t expected_tag[kPrivateDataAuthTagSize] = {};
    const bool ok =
        tagPrivateReady(keys, session_nonce, direction, header, expected_tag) &&
        constantTimeEqual(expected_tag, tag, sizeof(expected_tag));
    secureZero(expected_tag, sizeof(expected_tag));
    return ok;
}

bool sealPrivateShard(const PrivateSessionKeys& keys,
                      const uint8_t session_nonce[kSessionNonceSize],
                      PrivateFrameDirection direction,
                      const DataHeader& header,
                      const uint8_t* plaintext,
                      std::size_t plaintext_len,
                      uint8_t* out_ciphertext,
                      uint8_t out_tag[kPrivateDataAuthTagSize])
{
    if (!session_nonce || !plaintext || !out_ciphertext || !out_tag ||
        !isKnownDirection(direction) || header.type != DataType::Shard ||
        plaintext_len == 0U || plaintext_len != header.payload_len ||
        !isValidDataHeader(header))
    {
        return false;
    }
    uint8_t encoded_header[kDataHeaderSize] = {};
    std::size_t encoded_len = sizeof(encoded_header);
    uint8_t nonce[kPrivateFrameNonceSize] = {};
    const bool ok = encodeDataHeader(header, encoded_header, &encoded_len) &&
                    makeDataNonce(
                        session_nonce, header, direction, 0U, nonce) &&
                    sealAead(keys.data_key,
                             nonce,
                             encoded_header,
                             encoded_len,
                             plaintext,
                             plaintext_len,
                             out_ciphertext,
                             out_tag);
    secureZero(encoded_header, sizeof(encoded_header));
    secureZero(nonce, sizeof(nonce));
    return ok;
}

bool openPrivateShard(const PrivateSessionKeys& keys,
                      const uint8_t session_nonce[kSessionNonceSize],
                      PrivateFrameDirection direction,
                      const DataHeader& header,
                      const uint8_t* ciphertext,
                      std::size_t ciphertext_len,
                      const uint8_t tag[kPrivateDataAuthTagSize],
                      uint8_t* out_plaintext)
{
    if (!session_nonce || !ciphertext || !tag || !out_plaintext ||
        !isKnownDirection(direction) || header.type != DataType::Shard ||
        ciphertext_len == 0U || ciphertext_len != header.payload_len ||
        !isValidDataHeader(header))
    {
        return false;
    }
    uint8_t encoded_header[kDataHeaderSize] = {};
    std::size_t encoded_len = sizeof(encoded_header);
    uint8_t nonce[kPrivateFrameNonceSize] = {};
    const bool ok = encodeDataHeader(header, encoded_header, &encoded_len) &&
                    makeDataNonce(
                        session_nonce, header, direction, 0U, nonce) &&
                    openAead(keys.data_key,
                             nonce,
                             encoded_header,
                             encoded_len,
                             ciphertext,
                             ciphertext_len,
                             tag,
                             out_plaintext);
    secureZero(encoded_header, sizeof(encoded_header));
    secureZero(nonce, sizeof(nonce));
    if (!ok)
    {
        secureZero(out_plaintext, ciphertext_len);
    }
    return ok;
}

} // namespace chat::voice::vmp
