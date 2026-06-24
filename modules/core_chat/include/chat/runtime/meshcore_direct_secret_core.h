#pragma once

#include <cstddef>
#include <cstdint>

namespace chat::runtime
{

class MeshCoreDirectSecretCore final
{
  public:
    static bool expandSharedSecret(const uint8_t* shared_secret,
                                   size_t shared_secret_len,
                                   uint8_t* out_key16,
                                   size_t out_key16_len,
                                   uint8_t* out_key32,
                                   size_t out_key32_len);

    static bool derivePeerKeys(const uint8_t* local_private_key,
                               size_t local_private_key_len,
                               const uint8_t* peer_public_key,
                               size_t peer_public_key_len,
                               uint8_t* out_key16,
                               size_t out_key16_len,
                               uint8_t* out_key32,
                               size_t out_key32_len);
};

} // namespace chat::runtime
