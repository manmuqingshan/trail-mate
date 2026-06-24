#include "chat/infra/meshcore/meshcore_identity_crypto.h"
#include "chat/runtime/meshcore_direct_secret_core.h"

#include <cassert>
#include <cstring>

int main()
{
    {
        uint8_t shared_secret[chat::meshcore::kMeshCorePubKeySize] = {};
        for (size_t i = 0; i < sizeof(shared_secret); ++i)
        {
            shared_secret[i] = static_cast<uint8_t>(i + 1);
        }

        uint8_t key16[16] = {};
        uint8_t key32[32] = {};
        assert(chat::runtime::MeshCoreDirectSecretCore::expandSharedSecret(
            shared_secret,
            sizeof(shared_secret),
            key16,
            sizeof(key16),
            key32,
            sizeof(key32)));
        assert(std::memcmp(key16, key32, sizeof(key16)) == 0);
        assert(!chat::meshcore::meshcoreIsZeroBytes(key32, sizeof(key32)));
    }

    {
        uint8_t seed_a[chat::meshcore::kMeshCoreSeedSize] = {};
        uint8_t seed_b[chat::meshcore::kMeshCoreSeedSize] = {};
        for (size_t i = 0; i < sizeof(seed_a); ++i)
        {
            seed_a[i] = static_cast<uint8_t>(0xA0U + i);
            seed_b[i] = static_cast<uint8_t>(0xC0U + i);
        }

        uint8_t pub_a[chat::meshcore::kMeshCorePubKeySize] = {};
        uint8_t priv_a[chat::meshcore::kMeshCorePrivKeySize] = {};
        uint8_t pub_b[chat::meshcore::kMeshCorePubKeySize] = {};
        uint8_t priv_b[chat::meshcore::kMeshCorePrivKeySize] = {};
        assert(chat::meshcore::meshcoreCreateKeypair(seed_a, pub_a, priv_a));
        assert(chat::meshcore::meshcoreCreateKeypair(seed_b, pub_b, priv_b));

        uint8_t key16_ab[16] = {};
        uint8_t key32_ab[32] = {};
        uint8_t key16_ba[16] = {};
        uint8_t key32_ba[32] = {};
        assert(chat::runtime::MeshCoreDirectSecretCore::derivePeerKeys(
            priv_a, sizeof(priv_a), pub_b, sizeof(pub_b),
            key16_ab, sizeof(key16_ab), key32_ab, sizeof(key32_ab)));
        assert(chat::runtime::MeshCoreDirectSecretCore::derivePeerKeys(
            priv_b, sizeof(priv_b), pub_a, sizeof(pub_a),
            key16_ba, sizeof(key16_ba), key32_ba, sizeof(key32_ba)));
        assert(std::memcmp(key16_ab, key16_ba, sizeof(key16_ab)) == 0);
        assert(std::memcmp(key32_ab, key32_ba, sizeof(key32_ab)) == 0);
    }

    {
        uint8_t key16[16] = {};
        uint8_t key32[32] = {};
        assert(!chat::runtime::MeshCoreDirectSecretCore::expandSharedSecret(
            nullptr, 0, key16, sizeof(key16), key32, sizeof(key32)));
    }

    return 0;
}
