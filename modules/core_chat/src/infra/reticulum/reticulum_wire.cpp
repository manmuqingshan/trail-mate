/**
 * @file reticulum_wire.cpp
 * @brief Shared Reticulum packet and token helpers for device-side subsets
 */

#include "chat/infra/reticulum/reticulum_wire.h"

#if defined(TRAIL_MATE_RETICULUM_PARSE_ONLY)
#elif defined(TRAIL_MATE_RETICULUM_HASH_ONLY)
#elif defined(ESP_PLATFORM)
#include "mbedtls/aes.h"
#include "mbedtls/md.h"
#include "mbedtls/sha256.h"
#elif defined(TRAIL_MATE_HAS_OPENSSL)
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#else
#include <AES.h>
#include <Crypto.h>
#include <SHA256.h>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace chat::reticulum
{
namespace
{
constexpr size_t kAesBlockSize = 16;
constexpr size_t kHeader1Size = 2 + kTruncatedHashSize + 1;
constexpr size_t kHeader2Size = 2 + kTruncatedHashSize + kTruncatedHashSize + 1;
constexpr uint8_t kHeaderType1 = 0x00;
constexpr uint8_t kHeaderType2 = 0x01;

#if defined(TRAIL_MATE_RETICULUM_HASH_ONLY)
constexpr std::array<uint32_t, 64> kSha256RoundConstants = {
    0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL,
    0x3956c25bUL, 0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL,
    0xd807aa98UL, 0x12835b01UL, 0x243185beUL, 0x550c7dc3UL,
    0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL, 0xc19bf174UL,
    0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL,
    0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL,
    0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL,
    0xc6e00bf3UL, 0xd5a79147UL, 0x06ca6351UL, 0x14292967UL,
    0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL,
    0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
    0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL,
    0xd192e819UL, 0xd6990624UL, 0xf40e3585UL, 0x106aa070UL,
    0x19a4c116UL, 0x1e376c08UL, 0x2748774cUL, 0x34b0bcb5UL,
    0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL, 0x682e6ff3UL,
    0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL,
    0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL};

uint32_t rotateRight(uint32_t value, uint8_t bits)
{
    return (value >> bits) | (value << (32U - bits));
}

uint32_t readBe32(const uint8_t* data)
{
    return (static_cast<uint32_t>(data[0]) << 24U) |
           (static_cast<uint32_t>(data[1]) << 16U) |
           (static_cast<uint32_t>(data[2]) << 8U) |
           static_cast<uint32_t>(data[3]);
}

void writeBe32(uint8_t* out, uint32_t value)
{
    out[0] = static_cast<uint8_t>(value >> 24U);
    out[1] = static_cast<uint8_t>(value >> 16U);
    out[2] = static_cast<uint8_t>(value >> 8U);
    out[3] = static_cast<uint8_t>(value);
}

void sha256Hash(const uint8_t* data, size_t len, uint8_t out_hash[kFullHashSize])
{
    std::vector<uint8_t> padded;
    if (data && len != 0U)
    {
        padded.assign(data, data + len);
    }
    padded.push_back(0x80U);
    while ((padded.size() % 64U) != 56U)
    {
        padded.push_back(0x00U);
    }

    const uint64_t bit_len = static_cast<uint64_t>(len) * 8ULL;
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        padded.push_back(static_cast<uint8_t>(bit_len >> shift));
    }

    std::array<uint32_t, 8> state = {0x6a09e667UL,
                                     0xbb67ae85UL,
                                     0x3c6ef372UL,
                                     0xa54ff53aUL,
                                     0x510e527fUL,
                                     0x9b05688cUL,
                                     0x1f83d9abUL,
                                     0x5be0cd19UL};

    for (size_t chunk = 0; chunk < padded.size(); chunk += 64U)
    {
        std::array<uint32_t, 64> schedule = {};
        for (size_t index = 0; index < 16U; ++index)
        {
            schedule[index] = readBe32(padded.data() + chunk + (index * 4U));
        }
        for (size_t index = 16U; index < 64U; ++index)
        {
            const uint32_t s0 = rotateRight(schedule[index - 15U], 7U) ^
                                rotateRight(schedule[index - 15U], 18U) ^
                                (schedule[index - 15U] >> 3U);
            const uint32_t s1 = rotateRight(schedule[index - 2U], 17U) ^
                                rotateRight(schedule[index - 2U], 19U) ^
                                (schedule[index - 2U] >> 10U);
            schedule[index] = schedule[index - 16U] + s0 +
                              schedule[index - 7U] + s1;
        }

        uint32_t a = state[0];
        uint32_t b = state[1];
        uint32_t c = state[2];
        uint32_t d = state[3];
        uint32_t e = state[4];
        uint32_t f = state[5];
        uint32_t g = state[6];
        uint32_t h = state[7];

        for (size_t index = 0; index < 64U; ++index)
        {
            const uint32_t s1 = rotateRight(e, 6U) ^ rotateRight(e, 11U) ^
                                rotateRight(e, 25U);
            const uint32_t ch = (e & f) ^ ((~e) & g);
            const uint32_t temp1 =
                h + s1 + ch + kSha256RoundConstants[index] + schedule[index];
            const uint32_t s0 = rotateRight(a, 2U) ^ rotateRight(a, 13U) ^
                                rotateRight(a, 22U);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    for (size_t index = 0; index < state.size(); ++index)
    {
        writeBe32(out_hash + (index * 4U), state[index]);
    }
}
#endif

class Aes256CbcCipher
{
  public:
    Aes256CbcCipher()
    {
#if defined(ESP_PLATFORM)
        mbedtls_aes_init(&encrypt_);
        mbedtls_aes_init(&decrypt_);
#endif
    }

    ~Aes256CbcCipher()
    {
#if defined(ESP_PLATFORM)
        mbedtls_aes_free(&encrypt_);
        mbedtls_aes_free(&decrypt_);
#endif
    }

    void setKey(const uint8_t* key, size_t len)
    {
        valid_ = (key != nullptr && len == 32);
        if (valid_)
        {
#if defined(TRAIL_MATE_RETICULUM_PARSE_ONLY)
            valid_ = false;
#elif defined(TRAIL_MATE_RETICULUM_HASH_ONLY)
            valid_ = false;
#elif defined(ESP_PLATFORM)
            valid_ =
                mbedtls_aes_setkey_enc(&encrypt_, key, static_cast<unsigned>(len * 8U)) == 0 &&
                mbedtls_aes_setkey_dec(&decrypt_, key, static_cast<unsigned>(len * 8U)) == 0;
#elif defined(TRAIL_MATE_HAS_OPENSSL)
            valid_ =
                AES_set_encrypt_key(key, static_cast<int>(len * 8U), &encrypt_) == 0 &&
                AES_set_decrypt_key(key, static_cast<int>(len * 8U), &decrypt_) == 0;
#else
            aes_.setKey(key, len);
#endif
        }
    }

    bool valid() const
    {
        return valid_;
    }

    void encryptBlock(uint8_t* out, const uint8_t* in)
    {
        if (!out || !in)
        {
            return;
        }
#if defined(TRAIL_MATE_RETICULUM_PARSE_ONLY)
        memset(out, 0, kAesBlockSize);
#elif defined(TRAIL_MATE_RETICULUM_HASH_ONLY)
        memset(out, 0, kAesBlockSize);
#elif defined(ESP_PLATFORM)
        if (valid_)
        {
            (void)mbedtls_aes_crypt_ecb(&encrypt_, MBEDTLS_AES_ENCRYPT, in, out);
        }
#elif defined(TRAIL_MATE_HAS_OPENSSL)
        AES_encrypt(in, out, &encrypt_);
#else
        aes_.encryptBlock(out, in);
#endif
    }

    void decryptBlock(uint8_t* out, const uint8_t* in)
    {
        if (!out || !in)
        {
            return;
        }
#if defined(TRAIL_MATE_RETICULUM_PARSE_ONLY)
        memset(out, 0, kAesBlockSize);
#elif defined(TRAIL_MATE_RETICULUM_HASH_ONLY)
        memset(out, 0, kAesBlockSize);
#elif defined(ESP_PLATFORM)
        if (valid_)
        {
            (void)mbedtls_aes_crypt_ecb(&decrypt_, MBEDTLS_AES_DECRYPT, in, out);
        }
#elif defined(TRAIL_MATE_HAS_OPENSSL)
        AES_decrypt(in, out, &decrypt_);
#else
        aes_.decryptBlock(out, in);
#endif
    }

  private:
#if defined(TRAIL_MATE_RETICULUM_PARSE_ONLY)
#elif defined(TRAIL_MATE_RETICULUM_HASH_ONLY)
#elif defined(ESP_PLATFORM)
    mbedtls_aes_context encrypt_{};
    mbedtls_aes_context decrypt_{};
#elif defined(TRAIL_MATE_HAS_OPENSSL)
    AES_KEY encrypt_{};
    AES_KEY decrypt_{};
#else
    AESSmall256 aes_;
#endif
    bool valid_ = false;
};

void hmacSha256(const uint8_t* key, size_t key_len,
                const uint8_t* data, size_t data_len,
                uint8_t out_hash[kFullHashSize])
{
    if (!out_hash)
    {
        return;
    }

#if defined(TRAIL_MATE_RETICULUM_PARSE_ONLY)
    memset(out_hash, 0, kFullHashSize);
#elif defined(TRAIL_MATE_RETICULUM_HASH_ONLY)
    std::array<uint8_t, 64> key_block = {};
    if (key && key_len > key_block.size())
    {
        sha256Hash(key, key_len, key_block.data());
        key_len = kFullHashSize;
    }
    else if (key && key_len != 0)
    {
        memcpy(key_block.data(), key, key_len);
    }

    std::array<uint8_t, 64> ipad = {};
    std::array<uint8_t, 64> opad = {};
    for (size_t index = 0; index < key_block.size(); ++index)
    {
        ipad[index] = static_cast<uint8_t>(key_block[index] ^ 0x36U);
        opad[index] = static_cast<uint8_t>(key_block[index] ^ 0x5CU);
    }

    std::vector<uint8_t> inner;
    inner.insert(inner.end(), ipad.begin(), ipad.end());
    if (data && data_len != 0)
    {
        inner.insert(inner.end(), data, data + data_len);
    }
    uint8_t inner_hash[kFullHashSize] = {};
    sha256Hash(inner.data(), inner.size(), inner_hash);

    std::array<uint8_t, 64 + kFullHashSize> outer = {};
    memcpy(outer.data(), opad.data(), opad.size());
    memcpy(outer.data() + opad.size(), inner_hash, sizeof(inner_hash));
    sha256Hash(outer.data(), outer.size(), out_hash);
#elif defined(ESP_PLATFORM)
    static constexpr uint8_t kEmpty = 0;
    const uint8_t* input = (data && data_len != 0) ? data : &kEmpty;
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!info ||
        mbedtls_md_hmac(info, key, key_len, input, data_len, out_hash) != 0)
    {
        memset(out_hash, 0, kFullHashSize);
    }
#elif defined(TRAIL_MATE_HAS_OPENSSL)
    static constexpr uint8_t kEmpty = 0;
    const uint8_t* input = (data && data_len != 0) ? data : &kEmpty;
    unsigned int actual_len = 0;
    if (!HMAC(EVP_sha256(),
              key,
              static_cast<int>(key_len),
              input,
              data_len,
              out_hash,
              &actual_len) ||
        actual_len != kFullHashSize)
    {
        memset(out_hash, 0, kFullHashSize);
    }
#else
    SHA256 sha;
    sha.resetHMAC(key, key_len);
    if (data && data_len != 0)
    {
        sha.update(data, data_len);
    }
    sha.finalizeHMAC(key, key_len, out_hash, kFullHashSize);
#endif
}

void xorBlock(uint8_t* dst, const uint8_t* src)
{
    if (!dst || !src)
    {
        return;
    }
    for (size_t i = 0; i < kAesBlockSize; ++i)
    {
        dst[i] ^= src[i];
    }
}

bool constantTimeEquals(const uint8_t* a, const uint8_t* b, size_t len)
{
    if ((!a || !b) && len != 0)
    {
        return false;
    }
    uint8_t diff = 0;
    for (size_t i = 0; i < len; ++i)
    {
        diff |= static_cast<uint8_t>(a[i] ^ b[i]);
    }
    return diff == 0;
}

size_t pkcs7Pad(const uint8_t* input, size_t input_len,
                uint8_t* out, size_t out_len)
{
    const size_t pad_len = kAesBlockSize - (input_len % kAesBlockSize);
    const size_t total_len = input_len + ((pad_len == 0) ? kAesBlockSize : pad_len);
    if (!out || out_len < total_len)
    {
        return 0;
    }

    if (input && input_len != 0)
    {
        memcpy(out, input, input_len);
    }
    const uint8_t applied = static_cast<uint8_t>((pad_len == 0) ? kAesBlockSize : pad_len);
    for (size_t i = input_len; i < total_len; ++i)
    {
        out[i] = applied;
    }
    return total_len;
}

bool pkcs7Unpad(const uint8_t* input, size_t input_len,
                uint8_t* out, size_t* inout_len)
{
    if (!input || input_len == 0 || !out || !inout_len)
    {
        return false;
    }

    const uint8_t pad_len = input[input_len - 1];
    if (pad_len == 0 || pad_len > kAesBlockSize || pad_len > input_len)
    {
        return false;
    }

    for (size_t i = 0; i < pad_len; ++i)
    {
        if (input[input_len - 1 - i] != pad_len)
        {
            return false;
        }
    }

    const size_t plain_len = input_len - pad_len;
    if (*inout_len < plain_len)
    {
        *inout_len = plain_len;
        return false;
    }

    if (plain_len != 0)
    {
        memcpy(out, input, plain_len);
    }
    *inout_len = plain_len;
    return true;
}

void aesCbcEncrypt(const uint8_t* key, size_t key_len,
                   const uint8_t iv[kTokenIvSize],
                   const uint8_t* plaintext, size_t plaintext_len,
                   uint8_t* out_ciphertext)
{
    Aes256CbcCipher cipher;
    cipher.setKey(key, key_len);
    if (!cipher.valid() || !iv || !out_ciphertext)
    {
        return;
    }

    uint8_t previous[kAesBlockSize] = {};
    memcpy(previous, iv, sizeof(previous));

    for (size_t offset = 0; offset < plaintext_len; offset += kAesBlockSize)
    {
        uint8_t block[kAesBlockSize] = {};
        memcpy(block, plaintext + offset, kAesBlockSize);
        xorBlock(block, previous);
        cipher.encryptBlock(out_ciphertext + offset, block);
        memcpy(previous, out_ciphertext + offset, kAesBlockSize);
    }
}

void aesCbcDecrypt(const uint8_t* key, size_t key_len,
                   const uint8_t iv[kTokenIvSize],
                   const uint8_t* ciphertext, size_t ciphertext_len,
                   uint8_t* out_plaintext)
{
    Aes256CbcCipher cipher;
    cipher.setKey(key, key_len);
    if (!cipher.valid() || !iv || !out_plaintext)
    {
        return;
    }

    uint8_t previous[kAesBlockSize] = {};
    memcpy(previous, iv, sizeof(previous));

    for (size_t offset = 0; offset < ciphertext_len; offset += kAesBlockSize)
    {
        uint8_t block[kAesBlockSize] = {};
        cipher.decryptBlock(block, ciphertext + offset);
        xorBlock(block, previous);
        memcpy(out_plaintext + offset, block, kAesBlockSize);
        memcpy(previous, ciphertext + offset, kAesBlockSize);
    }
}

void appendAscii(char* out, size_t out_len, size_t& index, const char* text)
{
    if (!out || out_len == 0 || !text)
    {
        return;
    }
    while (*text != '\0' && index + 1 < out_len)
    {
        out[index++] = *text++;
    }
    out[index] = '\0';
}

} // namespace

size_t paddedTokenPlaintextSize(size_t plaintext_len)
{
    const size_t remainder = plaintext_len % kAesBlockSize;
    return plaintext_len + ((remainder == 0) ? kAesBlockSize : (kAesBlockSize - remainder));
}

size_t tokenSizeForPlaintext(size_t plaintext_len)
{
    return kTokenOverhead + paddedTokenPlaintextSize(plaintext_len);
}

void fullHash(const uint8_t* data, size_t len, uint8_t out_hash[kFullHashSize])
{
    if (!out_hash)
    {
        return;
    }

#if defined(TRAIL_MATE_RETICULUM_PARSE_ONLY)
    memset(out_hash, 0, kFullHashSize);
#elif defined(TRAIL_MATE_RETICULUM_HASH_ONLY)
    static constexpr uint8_t kEmpty = 0;
    const uint8_t* input = (data && len != 0) ? data : &kEmpty;
    sha256Hash(input, len, out_hash);
#elif defined(ESP_PLATFORM)
    static constexpr uint8_t kEmpty = 0;
    const uint8_t* input = (data && len != 0) ? data : &kEmpty;
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
    mbedtls_sha256_update(&sha, input, len);
    mbedtls_sha256_finish(&sha, out_hash);
    mbedtls_sha256_free(&sha);
#elif defined(TRAIL_MATE_HAS_OPENSSL)
    static constexpr uint8_t kEmpty = 0;
    const uint8_t* input = (data && len != 0) ? data : &kEmpty;
    if (!SHA256(input, len, out_hash))
    {
        memset(out_hash, 0, kFullHashSize);
    }
#else
    SHA256 sha;
    if (data && len != 0)
    {
        sha.update(data, len);
    }
    sha.finalize(out_hash, kFullHashSize);
#endif
}

void truncatedHash(const uint8_t* data, size_t len, uint8_t out_hash[kTruncatedHashSize])
{
    uint8_t hash[kFullHashSize] = {};
    fullHash(data, len, hash);
    memcpy(out_hash, hash, kTruncatedHashSize);
}

void computeNameHash(const char* app_name, const char* aspect,
                     uint8_t out_hash[kNameHashSize])
{
    char expanded[64] = {};
    size_t index = 0;
    appendAscii(expanded, sizeof(expanded), index, app_name ? app_name : "");
    if (aspect && aspect[0] != '\0' && index + 1 < sizeof(expanded))
    {
        expanded[index++] = '.';
        expanded[index] = '\0';
        appendAscii(expanded, sizeof(expanded), index, aspect);
    }

    uint8_t full[kFullHashSize] = {};
    fullHash(reinterpret_cast<const uint8_t*>(expanded), strlen(expanded), full);
    memcpy(out_hash, full, kNameHashSize);
}

void computeIdentityHash(const uint8_t public_key[kCombinedPublicKeySize],
                         uint8_t out_hash[kTruncatedHashSize])
{
    truncatedHash(public_key, kCombinedPublicKeySize, out_hash);
}

void computePlainDestinationHash(const uint8_t name_hash[kNameHashSize],
                                 uint8_t out_hash[kTruncatedHashSize])
{
    truncatedHash(name_hash, kNameHashSize, out_hash);
}

void computeDestinationHash(const uint8_t name_hash[kNameHashSize],
                            const uint8_t identity_hash[kTruncatedHashSize],
                            uint8_t out_hash[kTruncatedHashSize])
{
    uint8_t material[kNameHashSize + kTruncatedHashSize] = {};
    memcpy(material, name_hash, kNameHashSize);
    memcpy(material + kNameHashSize, identity_hash, kTruncatedHashSize);
    truncatedHash(material, sizeof(material), out_hash);
}

void computePacketHash(const uint8_t* raw_packet, size_t len,
                       uint8_t out_hash[kFullHashSize])
{
    if (!raw_packet || len < kHeader1Size || !out_hash)
    {
        if (out_hash)
        {
            memset(out_hash, 0, kFullHashSize);
        }
        return;
    }

    uint8_t hashable[kReticulumMtu] = {};
    size_t hashable_len = 0;
    hashable[hashable_len++] = static_cast<uint8_t>(raw_packet[0] & 0x0FU);

    const uint8_t header_type = static_cast<uint8_t>((raw_packet[0] >> 6) & 0x01U);
    if (header_type == kHeaderType2)
    {
        if (len < kHeader2Size)
        {
            memset(out_hash, 0, kFullHashSize);
            return;
        }

        memcpy(hashable + hashable_len,
               raw_packet + 2 + kTruncatedHashSize,
               len - (2 + kTruncatedHashSize));
        hashable_len += (len - (2 + kTruncatedHashSize));
    }
    else
    {
        memcpy(hashable + hashable_len, raw_packet + 2, len - 2);
        hashable_len += (len - 2);
    }

    fullHash(hashable, hashable_len, out_hash);
}

void computeTruncatedPacketHash(const uint8_t* raw_packet, size_t len,
                                uint8_t out_hash[kTruncatedHashSize])
{
    uint8_t full[kFullHashSize] = {};
    computePacketHash(raw_packet, len, full);
    memcpy(out_hash, full, kTruncatedHashSize);
}

uint32_t nodeIdFromDestinationHash(const uint8_t destination_hash[kTruncatedHashSize])
{
    if (!destination_hash)
    {
        return 0;
    }
    return (static_cast<uint32_t>(destination_hash[12]) << 24) |
           (static_cast<uint32_t>(destination_hash[13]) << 16) |
           (static_cast<uint32_t>(destination_hash[14]) << 8) |
           static_cast<uint32_t>(destination_hash[15]);
}

bool parsePacket(const uint8_t* data, size_t len, ParsedPacket* out_packet)
{
    if (!data || len < kHeader1Size || !out_packet)
    {
        return false;
    }

    ParsedPacket parsed{};
    parsed.raw_flags = data[0];
    parsed.hops = data[1];

    parsed.header_type = static_cast<uint8_t>((parsed.raw_flags >> 6) & 0x01U);
    if (parsed.header_type != kHeaderType1 && parsed.header_type != kHeaderType2)
    {
        return false;
    }

    parsed.context_flag = static_cast<uint8_t>((parsed.raw_flags >> 5) & 0x01U);
    parsed.transport_type = static_cast<TransportType>((parsed.raw_flags >> 4) & 0x01U);
    parsed.destination_type = static_cast<DestinationType>((parsed.raw_flags >> 2) & 0x03U);
    parsed.packet_type = static_cast<PacketType>(parsed.raw_flags & 0x03U);

    if (parsed.header_type == kHeaderType2)
    {
        if (len < kHeader2Size)
        {
            return false;
        }

        parsed.transport_id = data + 2;
        parsed.destination_hash = data + 2 + kTruncatedHashSize;
        parsed.context = data[2 + (kTruncatedHashSize * 2)];
        parsed.payload = data + kHeader2Size;
        parsed.payload_len = len - kHeader2Size;
        parsed.header_len = kHeader2Size;
    }
    else
    {
        parsed.transport_id = nullptr;
        parsed.destination_hash = data + 2;
        parsed.context = data[2 + kTruncatedHashSize];
        parsed.payload = data + kHeader1Size;
        parsed.payload_len = len - kHeader1Size;
        parsed.header_len = kHeader1Size;
    }

    parsed.valid = true;

    *out_packet = parsed;
    return true;
}

bool buildHeader1Packet(PacketType packet_type,
                        DestinationType destination_type,
                        PacketContext context,
                        bool context_flag,
                        const uint8_t destination_hash[kTruncatedHashSize],
                        const uint8_t* payload, size_t payload_len,
                        uint8_t* out_packet, size_t* inout_len,
                        uint8_t hops,
                        TransportType transport_type)
{
    if (!destination_hash || !out_packet || !inout_len)
    {
        return false;
    }

    const size_t total_len = kHeader1Size + payload_len;
    if (*inout_len < total_len || total_len > kReticulumMtu)
    {
        *inout_len = total_len;
        return false;
    }

    const uint8_t flags =
        static_cast<uint8_t>((0U << 6) |
                             ((context_flag ? 1U : 0U) << 5) |
                             ((static_cast<uint8_t>(transport_type) & 0x01U) << 4) |
                             ((static_cast<uint8_t>(destination_type) & 0x03U) << 2) |
                             (static_cast<uint8_t>(packet_type) & 0x03U));

    out_packet[0] = flags;
    out_packet[1] = hops;
    memcpy(out_packet + 2, destination_hash, kTruncatedHashSize);
    out_packet[2 + kTruncatedHashSize] = static_cast<uint8_t>(context);
    if (payload && payload_len != 0)
    {
        memcpy(out_packet + kHeader1Size, payload, payload_len);
    }
    *inout_len = total_len;
    return true;
}

bool buildHeader2Packet(PacketType packet_type,
                        DestinationType destination_type,
                        PacketContext context,
                        bool context_flag,
                        const uint8_t transport_id[kTruncatedHashSize],
                        const uint8_t destination_hash[kTruncatedHashSize],
                        const uint8_t* payload, size_t payload_len,
                        uint8_t* out_packet, size_t* inout_len,
                        uint8_t hops,
                        TransportType transport_type)
{
    if (!transport_id || !destination_hash || !out_packet || !inout_len)
    {
        return false;
    }

    const size_t total_len = kHeader2Size + payload_len;
    if (*inout_len < total_len || total_len > kReticulumMtu)
    {
        *inout_len = total_len;
        return false;
    }

    const uint8_t flags =
        static_cast<uint8_t>((1U << 6) |
                             ((context_flag ? 1U : 0U) << 5) |
                             ((static_cast<uint8_t>(transport_type) & 0x01U) << 4) |
                             ((static_cast<uint8_t>(destination_type) & 0x03U) << 2) |
                             (static_cast<uint8_t>(packet_type) & 0x03U));

    out_packet[0] = flags;
    out_packet[1] = hops;
    memcpy(out_packet + 2, transport_id, kTruncatedHashSize);
    memcpy(out_packet + 2 + kTruncatedHashSize, destination_hash, kTruncatedHashSize);
    out_packet[2 + (kTruncatedHashSize * 2)] = static_cast<uint8_t>(context);
    if (payload && payload_len != 0)
    {
        memcpy(out_packet + kHeader2Size, payload, payload_len);
    }
    *inout_len = total_len;
    return true;
}

bool parseAnnounce(const ParsedPacket& packet, ParsedAnnounce* out_announce)
{
    constexpr size_t kAnnounceFixedPrefixSize =
        kCombinedPublicKeySize + kNameHashSize + 10;
    constexpr size_t kAnnounceNoRatchetMinSize =
        kAnnounceFixedPrefixSize + kSignatureSize;
    constexpr size_t kAnnounceRatchetMinSize =
        kAnnounceFixedPrefixSize + kRatchetSize + kSignatureSize;

    if (!packet.valid || !out_announce ||
        packet.packet_type != PacketType::Announce ||
        packet.payload == nullptr)
    {
        return false;
    }

    const bool has_ratchet = (packet.context_flag != 0);
    const size_t min_len = has_ratchet ? kAnnounceRatchetMinSize : kAnnounceNoRatchetMinSize;
    if (packet.payload_len < min_len)
    {
        return false;
    }

    ParsedAnnounce parsed{};
    parsed.valid = true;
    parsed.has_ratchet = has_ratchet;
    parsed.public_key = packet.payload;
    parsed.name_hash = packet.payload + kCombinedPublicKeySize;
    parsed.random_hash = parsed.name_hash + kNameHashSize;

    if (has_ratchet)
    {
        parsed.ratchet = parsed.random_hash + 10;
        parsed.ratchet_len = kRatchetSize;
        parsed.signature = parsed.ratchet + parsed.ratchet_len;
    }
    else
    {
        parsed.signature = parsed.random_hash + 10;
    }
    parsed.app_data = parsed.signature + kSignatureSize;
    parsed.app_data_len = packet.payload_len - min_len;

    *out_announce = parsed;
    return true;
}

bool hkdfSha256(const uint8_t* ikm, size_t ikm_len,
                const uint8_t* salt, size_t salt_len,
                const uint8_t* info, size_t info_len,
                uint8_t* out_key, size_t out_len)
{
    if (!ikm || ikm_len == 0 || !out_key || out_len == 0)
    {
        return false;
    }

    uint8_t zero_salt[kFullHashSize] = {};
    const uint8_t* actual_salt = (salt && salt_len != 0) ? salt : zero_salt;
    const size_t actual_salt_len = (salt && salt_len != 0) ? salt_len : sizeof(zero_salt);

    uint8_t prk[kFullHashSize] = {};
    hmacSha256(actual_salt, actual_salt_len, ikm, ikm_len, prk);

    uint8_t previous[kFullHashSize] = {};
    size_t generated = 0;
    uint8_t counter = 1;
    size_t previous_len = 0;

    while (generated < out_len)
    {
        uint8_t block_input[kFullHashSize + 64 + 1] = {};
        size_t block_len = 0;
        if (previous_len != 0)
        {
            memcpy(block_input + block_len, previous, previous_len);
            block_len += previous_len;
        }
        if (info && info_len != 0)
        {
            memcpy(block_input + block_len, info, info_len);
            block_len += info_len;
        }
        block_input[block_len++] = counter++;

        hmacSha256(prk, sizeof(prk), block_input, block_len, previous);
        previous_len = sizeof(previous);

        const size_t remaining = out_len - generated;
        const size_t chunk = std::min(remaining, sizeof(previous));
        memcpy(out_key + generated, previous, chunk);
        generated += chunk;
    }

    return true;
}

bool tokenEncrypt(const uint8_t derived_key[kDerivedTokenKeySize],
                  const uint8_t iv[kTokenIvSize],
                  const uint8_t* plaintext, size_t plaintext_len,
                  uint8_t* out_token, size_t* inout_len)
{
#if defined(TRAIL_MATE_RETICULUM_PARSE_ONLY)
    (void)derived_key;
    (void)iv;
    (void)plaintext;
    (void)plaintext_len;
    (void)out_token;
    if (inout_len)
    {
        *inout_len = 0;
    }
    return false;
#else
    if (!derived_key || !iv || !out_token || !inout_len)
    {
        return false;
    }

    const size_t padded_len = paddedTokenPlaintextSize(plaintext_len);
    const size_t total_len = tokenSizeForPlaintext(plaintext_len);
    if (*inout_len < total_len)
    {
        *inout_len = total_len;
        return false;
    }

    std::vector<uint8_t> padded(padded_len, 0);
    if (padded.empty())
    {
        padded.resize(kAesBlockSize, 0);
    }
    if (pkcs7Pad(plaintext, plaintext_len, padded.data(), padded.size()) != padded_len)
    {
        return false;
    }

    memcpy(out_token, iv, kTokenIvSize);
    aesCbcEncrypt(derived_key + 32, 32, iv, padded.data(), padded_len, out_token + kTokenIvSize);

    uint8_t mac[kFullHashSize] = {};
    hmacSha256(derived_key, 32, out_token, kTokenIvSize + padded_len, mac);
    memcpy(out_token + kTokenIvSize + padded_len, mac, sizeof(mac));
    *inout_len = total_len;
    return true;
#endif
}

bool tokenDecrypt(const uint8_t derived_key[kDerivedTokenKeySize],
                  const uint8_t* token, size_t token_len,
                  uint8_t* out_plaintext, size_t* inout_len)
{
#if defined(TRAIL_MATE_RETICULUM_PARSE_ONLY)
    (void)derived_key;
    (void)token;
    (void)token_len;
    (void)out_plaintext;
    if (inout_len)
    {
        *inout_len = 0;
    }
    return false;
#else
    if (!derived_key || !token || token_len <= kTokenOverhead || !out_plaintext || !inout_len)
    {
        return false;
    }

    const size_t cipher_len = token_len - kTokenOverhead;
    if ((cipher_len % kAesBlockSize) != 0)
    {
        return false;
    }

    const uint8_t* iv = token;
    const uint8_t* ciphertext = token + kTokenIvSize;
    const uint8_t* received_hmac = token + kTokenIvSize + cipher_len;

    uint8_t expected_hmac[kFullHashSize] = {};
    hmacSha256(derived_key, 32, token, kTokenIvSize + cipher_len, expected_hmac);
    if (!constantTimeEquals(received_hmac, expected_hmac, sizeof(expected_hmac)))
    {
        return false;
    }

    std::vector<uint8_t> padded(cipher_len, 0);
    if (padded.empty())
    {
        return false;
    }

    aesCbcDecrypt(derived_key + 32, 32, iv, ciphertext, cipher_len, padded.data());
    return pkcs7Unpad(padded.data(), cipher_len, out_plaintext, inout_len);
#endif
}

} // namespace chat::reticulum
