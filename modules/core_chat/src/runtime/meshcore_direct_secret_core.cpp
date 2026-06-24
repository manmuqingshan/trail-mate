#include "chat/runtime/meshcore_direct_secret_core.h"

#include "chat/infra/meshcore/meshcore_identity_crypto.h"
#include "chat/infra/meshcore/meshcore_protocol_helpers.h"

namespace chat::runtime
{

bool MeshCoreDirectSecretCore::expandSharedSecret(const uint8_t* shared_secret,
                                                  size_t shared_secret_len,
                                                  uint8_t* out_key16,
                                                  size_t out_key16_len,
                                                  uint8_t* out_key32,
                                                  size_t out_key32_len)
{
    if (!shared_secret ||
        shared_secret_len != chat::meshcore::kMeshCorePubKeySize ||
        !out_key16 ||
        out_key16_len < 16 ||
        !out_key32 ||
        out_key32_len < 32)
    {
        return false;
    }

    chat::meshcore::sharedSecretToKeys(shared_secret, out_key16, out_key32);
    return true;
}

bool MeshCoreDirectSecretCore::derivePeerKeys(const uint8_t* local_private_key,
                                              size_t local_private_key_len,
                                              const uint8_t* peer_public_key,
                                              size_t peer_public_key_len,
                                              uint8_t* out_key16,
                                              size_t out_key16_len,
                                              uint8_t* out_key32,
                                              size_t out_key32_len)
{
    if (!local_private_key ||
        local_private_key_len != chat::meshcore::kMeshCorePrivKeySize ||
        !peer_public_key ||
        peer_public_key_len != chat::meshcore::kMeshCorePubKeySize)
    {
        return false;
    }

    uint8_t shared_secret[chat::meshcore::kMeshCorePubKeySize] = {};
    if (!chat::meshcore::meshcoreDeriveSharedSecret(local_private_key,
                                                    peer_public_key,
                                                    shared_secret))
    {
        return false;
    }

    return expandSharedSecret(shared_secret,
                              sizeof(shared_secret),
                              out_key16,
                              out_key16_len,
                              out_key32,
                              out_key32_len);
}

} // namespace chat::runtime
