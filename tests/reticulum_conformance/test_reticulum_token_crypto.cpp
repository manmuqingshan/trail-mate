#include "chat/infra/reticulum/reticulum_wire.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{

namespace reticulum = chat::reticulum;

constexpr const char* kTokenVector =
    "404142434445464748494a4b4c4d4e4f"
    "a54ee5a207f1f14120ff8549d193813e79ac462f30bcf7e2c95e4bb7cacda682"
    "e4d8506d6eb11aec8b5bf5c442990828b29c59f711a450a5a8f83d886332683a";

uint8_t hexNibble(char value)
{
    if (value >= '0' && value <= '9')
    {
        return static_cast<uint8_t>(value - '0');
    }
    if (value >= 'a' && value <= 'f')
    {
        return static_cast<uint8_t>(10 + value - 'a');
    }
    if (value >= 'A' && value <= 'F')
    {
        return static_cast<uint8_t>(10 + value - 'A');
    }
    throw std::runtime_error("invalid hex digit");
}

std::vector<uint8_t> fromHex(std::string_view hex)
{
    if ((hex.size() % 2U) != 0U)
    {
        throw std::runtime_error("odd hex string");
    }

    std::vector<uint8_t> bytes(hex.size() / 2U, 0);
    for (std::size_t index = 0; index < bytes.size(); ++index)
    {
        bytes[index] = static_cast<uint8_t>((hexNibble(hex[index * 2U]) << 4U) |
                                            hexNibble(hex[(index * 2U) + 1U]));
    }
    return bytes;
}

void expectBytes(const uint8_t* actual, std::size_t actual_len,
                 std::string_view expected_hex)
{
    const std::vector<uint8_t> expected = fromHex(expected_hex);
    assert(actual != nullptr || expected.empty());
    assert(actual_len == expected.size());
    if (!expected.empty() &&
        std::memcmp(actual, expected.data(), expected.size()) != 0)
    {
        throw std::runtime_error("byte vector mismatch");
    }
}

void expectTokenEncryptDecryptVector()
{
    std::array<uint8_t, reticulum::kDerivedTokenKeySize> derived_key = {};
    for (std::size_t index = 0; index < derived_key.size(); ++index)
    {
        derived_key[index] = static_cast<uint8_t>(index);
    }

    std::array<uint8_t, reticulum::kTokenIvSize> iv = {};
    for (std::size_t index = 0; index < iv.size(); ++index)
    {
        iv[index] = static_cast<uint8_t>(0x40U + index);
    }

    constexpr const char* kPlaintext = "hello reticulum token";
    const auto* plaintext = reinterpret_cast<const uint8_t*>(kPlaintext);
    const std::size_t plaintext_len = std::strlen(kPlaintext);

    std::vector<uint8_t> token(reticulum::tokenSizeForPlaintext(plaintext_len), 0);
    std::size_t token_len = token.size();
    assert(reticulum::tokenEncrypt(derived_key.data(),
                                   iv.data(),
                                   plaintext,
                                   plaintext_len,
                                   token.data(),
                                   &token_len));
    token.resize(token_len);
    expectBytes(token.data(), token.size(), kTokenVector);

    std::array<uint8_t, 8> too_small = {};
    std::size_t too_small_len = too_small.size();
    assert(!reticulum::tokenEncrypt(derived_key.data(),
                                    iv.data(),
                                    plaintext,
                                    plaintext_len,
                                    too_small.data(),
                                    &too_small_len));
    assert(too_small_len == token.size());

    std::vector<uint8_t> decrypted(plaintext_len, 0);
    std::size_t decrypted_len = decrypted.size();
    assert(reticulum::tokenDecrypt(derived_key.data(),
                                   token.data(),
                                   token.size(),
                                   decrypted.data(),
                                   &decrypted_len));
    assert(decrypted_len == plaintext_len);
    assert(std::memcmp(decrypted.data(), plaintext, plaintext_len) == 0);

    token[reticulum::kTokenIvSize] ^= 0x01U;
    decrypted_len = decrypted.size();
    assert(!reticulum::tokenDecrypt(derived_key.data(),
                                    token.data(),
                                    token.size(),
                                    decrypted.data(),
                                    &decrypted_len));
}

} // namespace

int main()
{
    expectTokenEncryptDecryptVector();
    return 0;
}
