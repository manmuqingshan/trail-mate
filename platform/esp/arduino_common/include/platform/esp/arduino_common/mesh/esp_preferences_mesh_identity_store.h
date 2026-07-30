#pragma once

#include "mesh/ports/i_local_identity_store.h"

#include <stddef.h>
#include <stdint.h>

namespace platform::esp::arduino_common::mesh
{

class EspPreferencesLocalIdentityStore final : public ::mesh::ILocalIdentityStore
{
  public:
    struct Options
    {
        const char* ns = "chat";
        const char* public_key = "pki_pub";
        const char* private_key = "pki_priv";
    };

    EspPreferencesLocalIdentityStore();
    explicit EspPreferencesLocalIdentityStore(const Options& options);

    ::mesh::StoreResult load(::mesh::LocalIdentity& out) override;
    ::mesh::StoreResult save(const ::mesh::LocalIdentity& identity) override;
    ::mesh::StoreResult clear() override;

  private:
    Options options_;
};

} // namespace platform::esp::arduino_common::mesh
