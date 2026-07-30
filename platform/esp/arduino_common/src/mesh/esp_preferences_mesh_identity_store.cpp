#include "platform/esp/arduino_common/mesh/esp_preferences_mesh_identity_store.h"

#include "../chat/internal/blob_store_io.h"

#include <cstring>
#include <vector>

namespace platform::esp::arduino_common::mesh
{
namespace
{

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

} // namespace

EspPreferencesLocalIdentityStore::EspPreferencesLocalIdentityStore()
    : options_(Options{})
{
}

EspPreferencesLocalIdentityStore::EspPreferencesLocalIdentityStore(const Options& options)
    : options_(options)
{
}

::mesh::StoreResult EspPreferencesLocalIdentityStore::load(::mesh::LocalIdentity& out)
{
    out = ::mesh::LocalIdentity{};

    std::vector<uint8_t> public_blob;
    std::vector<uint8_t> private_blob;
    const bool have_public =
        ::chat::infra::loadRawBlobFromPreferences(options_.ns, options_.public_key, public_blob);
    const bool have_private =
        ::chat::infra::loadRawBlobFromPreferences(options_.ns, options_.private_key, private_blob);
    if (!have_public && !have_private)
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::NotFound);
    }
    if (!have_public || !have_private ||
        public_blob.size() != sizeof(out.public_key) ||
        private_blob.size() != sizeof(out.private_key))
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::Corrupt);
    }

    std::memcpy(out.public_key, public_blob.data(), sizeof(out.public_key));
    std::memcpy(out.private_key, private_blob.data(), sizeof(out.private_key));
    out.valid = !isZero(out.private_key, sizeof(out.private_key));
    if (!out.valid)
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::Corrupt);
    }
    return ::mesh::StoreResult::success();
}

::mesh::StoreResult EspPreferencesLocalIdentityStore::save(
    const ::mesh::LocalIdentity& identity)
{
    if (!validIdentity(identity))
    {
        return ::mesh::StoreResult::fail(::mesh::StoreFailure::InvalidArgument);
    }

    const bool public_ok = ::chat::infra::saveRawBlobToPreferences(
        options_.ns,
        options_.public_key,
        identity.public_key,
        sizeof(identity.public_key));
    const bool private_ok = ::chat::infra::saveRawBlobToPreferences(
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

::mesh::StoreResult EspPreferencesLocalIdentityStore::clear()
{
    ::chat::infra::clearPreferencesKeys(options_.ns, options_.public_key, options_.private_key);
    return ::mesh::StoreResult::success();
}

} // namespace platform::esp::arduino_common::mesh
