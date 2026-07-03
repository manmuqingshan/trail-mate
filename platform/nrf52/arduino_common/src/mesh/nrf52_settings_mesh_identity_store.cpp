#include "platform/nrf52/arduino_common/mesh/nrf52_settings_mesh_identity_store.h"

#include "platform/ui/settings_store.h"

#include <cstring>

namespace platform::nrf52::arduino_common::mesh
{
namespace
{

struct PeerKeyEntry
{
    uint32_t node_id;
    uint32_t updated_at_ms;
    uint8_t key[32];
} __attribute__((packed));

bool isZero(const uint8_t* data, size_t size)
{
    if (!data)
    {
        return true;
    }
    for (size_t index = 0; index < size; ++index)
    {
        if (data[index] != 0)
        {
            return false;
        }
    }
    return true;
}

bool validIdentity(const ::mesh::LocalIdentity& identity)
{
    return identity.valid &&
           !isZero(identity.private_key, sizeof(identity.private_key));
}

constexpr size_t effectivePeerKeyLimit(size_t requested)
{
    return requested == 0
               ? 0
               : (requested < Nrf52SettingsPeerKeyStore::kMaxStoredPeerKeys
                      ? requested
                      : Nrf52SettingsPeerKeyStore::kMaxStoredPeerKeys);
}

struct PeerKeyStoreScratch
{
    ::mesh::PeerPublicKey keys[Nrf52SettingsPeerKeyStore::kMaxStoredPeerKeys]{};
    PeerKeyEntry entries[Nrf52SettingsPeerKeyStore::kMaxStoredPeerKeys]{};
};

PeerKeyStoreScratch& peerKeyScratch()
{
    static PeerKeyStoreScratch scratch;
    return scratch;
}

PeerKeyEntry peerKeyToEntry(const ::mesh::PeerPublicKey& key)
{
    PeerKeyEntry entry{};
    entry.node_id = key.node_id.value;
    entry.updated_at_ms = key.updated_at_ms;
    std::memcpy(entry.key, key.public_key, sizeof(entry.key));
    return entry;
}

::mesh::PeerPublicKey peerKeyFromEntry(const PeerKeyEntry& entry)
{
    ::mesh::PeerPublicKey key{};
    key.node_id = ::mesh::NodeId{entry.node_id};
    key.updated_at_ms = entry.updated_at_ms;
    key.verified = false;
    std::memcpy(key.public_key, entry.key, sizeof(key.public_key));
    return key;
}

size_t oldestPeerKeyIndex(const ::mesh::PeerPublicKey* keys, size_t count)
{
    size_t oldest = 0;
    for (size_t index = 1; index < count; ++index)
    {
        if (keys[index].updated_at_ms < keys[oldest].updated_at_ms)
        {
            oldest = index;
        }
    }
    return oldest;
}

bool insertPeerKeyIntoWindow(::mesh::PeerPublicKey* keys,
                             size_t* count,
                             size_t limit,
                             const ::mesh::PeerPublicKey& key)
{
    if (!keys || !count || limit == 0)
    {
        return false;
    }

    for (size_t index = 0; index < *count; ++index)
    {
        if (keys[index].node_id == key.node_id)
        {
            keys[index] = key;
            return true;
        }
    }

    if (*count < limit)
    {
        keys[(*count)++] = key;
        return true;
    }

    const size_t oldest = oldestPeerKeyIndex(keys, *count);
    if (key.updated_at_ms <= keys[oldest].updated_at_ms)
    {
        return false;
    }
    keys[oldest] = key;
    return true;
}

} // namespace

Nrf52SettingsLocalIdentityStore::Nrf52SettingsLocalIdentityStore()
    : options_(Options{})
{
}

Nrf52SettingsLocalIdentityStore::Nrf52SettingsLocalIdentityStore(const Options& options)
    : options_(options)
{
}

::mesh::StoreResult Nrf52SettingsLocalIdentityStore::load(::mesh::LocalIdentity& out)
{
    out = ::mesh::LocalIdentity{};

    uint8_t public_blob[sizeof(out.public_key)] = {};
    uint8_t private_blob[sizeof(out.private_key)] = {};
    size_t public_len = 0;
    size_t private_len = 0;
    const bool have_public =
        ::platform::ui::settings_store::get_blob_into(options_.ns,
                                                      options_.public_key,
                                                      public_blob,
                                                      sizeof(public_blob),
                                                      &public_len);
    const bool have_private =
        ::platform::ui::settings_store::get_blob_into(options_.ns,
                                                      options_.private_key,
                                                      private_blob,
                                                      sizeof(private_blob),
                                                      &private_len);
    if (!have_public && !have_private)
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::NotFound);
    }
    if (!have_public || !have_private ||
        public_len != sizeof(out.public_key) ||
        private_len != sizeof(out.private_key))
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::Corrupt);
    }

    std::memcpy(out.public_key, public_blob, sizeof(out.public_key));
    std::memcpy(out.private_key, private_blob, sizeof(out.private_key));
    out.valid = !isZero(out.private_key, sizeof(out.private_key));
    if (!out.valid)
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::Corrupt);
    }
    return ::mesh::StoreResult::success();
}

::mesh::StoreResult Nrf52SettingsLocalIdentityStore::save(
    const ::mesh::LocalIdentity& identity)
{
    if (!validIdentity(identity))
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::InvalidArgument);
    }

    const bool public_ok = ::platform::ui::settings_store::put_blob(
        options_.ns,
        options_.public_key,
        identity.public_key,
        sizeof(identity.public_key));
    const bool private_ok = ::platform::ui::settings_store::put_blob(
        options_.ns,
        options_.private_key,
        identity.private_key,
        sizeof(identity.private_key));
    if (!public_ok || !private_ok)
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::IoError);
    }
    return ::mesh::StoreResult::success();
}

::mesh::StoreResult Nrf52SettingsLocalIdentityStore::clear()
{
    const char* keys[] = {options_.public_key, options_.private_key};
    ::platform::ui::settings_store::remove_keys(options_.ns, keys, 2);
    return ::mesh::StoreResult::success();
}

Nrf52SettingsPeerKeyStore::Nrf52SettingsPeerKeyStore()
    : options_(Options{})
{
}

Nrf52SettingsPeerKeyStore::Nrf52SettingsPeerKeyStore(const Options& options)
    : options_(options)
{
}

::mesh::StoreResult Nrf52SettingsPeerKeyStore::get(::mesh::NodeId node_id,
                                                   ::mesh::PeerPublicKey& out)
{
    if (!node_id.isValidUnicast())
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::InvalidArgument);
    }

    auto& scratch = peerKeyScratch();
    size_t count = 0;
    auto loaded = loadAll(scratch.keys,
                          effectivePeerKeyLimit(options_.max_keys),
                          &count);
    if (!loaded.ok)
    {
        return loaded;
    }

    for (size_t index = 0; index < count; ++index)
    {
        if (scratch.keys[index].node_id == node_id)
        {
            out = scratch.keys[index];
            return ::mesh::StoreResult::success();
        }
    }
    return ::mesh::StoreResult::fail(::mesh::StoreFailure::NotFound);
}

::mesh::StoreResult Nrf52SettingsPeerKeyStore::put(const ::mesh::PeerPublicKey& key)
{
    if (!key.node_id.isValidUnicast())
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::InvalidArgument);
    }

    auto& scratch = peerKeyScratch();
    size_t count = 0;
    const size_t limit = effectivePeerKeyLimit(options_.max_keys);
    auto loaded = loadAll(scratch.keys, limit, &count);
    if (!loaded.ok && loaded.failure != ::mesh::StoreFailure::NotFound)
    {
        return loaded;
    }
    if (!loaded.ok)
    {
        count = 0;
    }

    if (!insertPeerKeyIntoWindow(scratch.keys, &count, limit, key))
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::InvalidArgument);
    }
    return saveAll(scratch.keys, count);
}

::mesh::StoreResult Nrf52SettingsPeerKeyStore::remove(::mesh::NodeId node_id)
{
    if (!node_id.isValidUnicast())
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::InvalidArgument);
    }

    auto& scratch = peerKeyScratch();
    size_t count = 0;
    auto loaded = loadAll(scratch.keys,
                          effectivePeerKeyLimit(options_.max_keys),
                          &count);
    if (!loaded.ok)
    {
        return loaded.failure == ::mesh::StoreFailure::NotFound
                   ? ::mesh::StoreResult::success()
                   : loaded;
    }

    size_t write = 0;
    for (size_t read = 0; read < count; ++read)
    {
        if (scratch.keys[read].node_id == node_id)
        {
            continue;
        }
        if (write != read)
        {
            scratch.keys[write] = scratch.keys[read];
        }
        ++write;
    }
    return saveAll(scratch.keys, write);
}

::mesh::StoreResult Nrf52SettingsPeerKeyStore::clear()
{
    const char* keys[] = {options_.key};
    ::platform::ui::settings_store::remove_keys(options_.ns, keys, 1);
    return ::mesh::StoreResult::success();
}

::mesh::StoreResult Nrf52SettingsPeerKeyStore::loadAll(
    std::vector<::mesh::PeerPublicKey>& out)
{
    out.clear();

    auto& scratch = peerKeyScratch();
    size_t count = 0;
    auto loaded = loadAll(scratch.keys,
                          effectivePeerKeyLimit(options_.max_keys),
                          &count);
    if (!loaded.ok)
    {
        return loaded;
    }

    out.reserve(count);
    for (size_t index = 0; index < count; ++index)
    {
        out.push_back(scratch.keys[index]);
    }
    return ::mesh::StoreResult::success();
}

::mesh::StoreResult Nrf52SettingsPeerKeyStore::loadAll(
    ::mesh::PeerPublicKey* out,
    size_t capacity,
    size_t* out_count)
{
    if (out_count)
    {
        *out_count = 0;
    }
    if (!out && capacity != 0)
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::InvalidArgument);
    }

    auto& scratch = peerKeyScratch();
    const size_t max_keys = effectivePeerKeyLimit(options_.max_keys);
    const size_t byte_capacity = max_keys * sizeof(PeerKeyEntry);
    size_t blob_len = 0;
    if (!::platform::ui::settings_store::get_blob_into(options_.ns,
                                                       options_.key,
                                                       scratch.entries,
                                                       byte_capacity,
                                                       &blob_len))
    {
        return blob_len > byte_capacity
                   ? ::mesh::StoreResult::fail(::mesh::StoreFailure::Corrupt)
                   : ::mesh::StoreResult::fail(::mesh::StoreFailure::NotFound);
    }
    if (blob_len == 0)
    {
        return ::mesh::StoreResult::success();
    }
    if ((blob_len % sizeof(PeerKeyEntry)) != 0)
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::Corrupt);
    }

    size_t count = blob_len / sizeof(PeerKeyEntry);
    if (count > max_keys)
    {
        count = max_keys;
    }
    if (count > capacity)
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::InvalidArgument);
    }

    size_t written = 0;
    for (size_t index = 0; index < count; ++index)
    {
        if (scratch.entries[index].node_id == 0)
        {
            continue;
        }
        ::mesh::PeerPublicKey key = peerKeyFromEntry(scratch.entries[index]);
        if (!key.node_id.isValidUnicast())
        {
            continue;
        }
        out[written++] = key;
    }
    if (out_count)
    {
        *out_count = written;
    }
    return ::mesh::StoreResult::success();
}

::mesh::StoreResult Nrf52SettingsPeerKeyStore::replaceAll(
    const ::mesh::PeerPublicKey* keys,
    size_t count)
{
    if (count > 0 && !keys)
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::InvalidArgument);
    }

    for (size_t index = 0; index < count; ++index)
    {
        if (!keys[index].node_id.isValidUnicast())
        {
            return ::mesh::StoreResult::fail(::mesh::StoreFailure::InvalidArgument);
        }
    }
    return saveAll(keys, count);
}

::mesh::StoreResult Nrf52SettingsPeerKeyStore::saveAll(
    const ::mesh::PeerPublicKey* keys,
    size_t count)
{
    if (count == 0)
    {
        return clear();
    }

    auto& scratch = peerKeyScratch();
    const size_t limit = effectivePeerKeyLimit(options_.max_keys);
    if (limit == 0)
    {
        return clear();
    }
    size_t written = 0;
    for (size_t index = 0; index < count; ++index)
    {
        const auto& key = keys[index];
        if (!key.node_id.isValidUnicast())
        {
            return ::mesh::StoreResult::fail(::mesh::StoreFailure::InvalidArgument);
        }
        (void)insertPeerKeyIntoWindow(scratch.keys, &written, limit, key);
    }

    for (size_t index = 0; index < written; ++index)
    {
        scratch.entries[index] = peerKeyToEntry(scratch.keys[index]);
    }

    const bool persisted = ::platform::ui::settings_store::put_blob(
        options_.ns,
        options_.key,
        scratch.entries,
        written * sizeof(PeerKeyEntry));
    if (!persisted)
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::IoError);
    }
    return ::mesh::StoreResult::success();
}

} // namespace platform::nrf52::arduino_common::mesh
