#include "ble/meshtastic_ble_persistence_bridge.h"

#if defined(GAT562_MESH_EVB_PRO)
#include "boards/gat562_mesh_evb_pro/settings_store.h"
#else
#include "platform/ui/settings_store.h"
#endif

#include <Arduino.h>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#if !defined(GAT562_MESH_EVB_PRO)
#include <vector>
#endif

namespace ble
{
namespace
{
#if !defined(GAT562_MESH_EVB_PRO)
constexpr const char* kBleSettingsNamespace = "ble_meshtastic";
constexpr const char* kBluetoothConfigKey = "bt_cfg";
constexpr const char* kModuleConfigKey = "mod_cfg";
constexpr const char* kPeerIdentityKey = "phone_peer";
#endif
constexpr uint32_t kPeerIdentityMagic = 0x544D4250UL;
constexpr uint8_t kPeerIdentityVersion = 1;

struct StoredPeerIdentity
{
    uint32_t magic = kPeerIdentityMagic;
    uint8_t version = kPeerIdentityVersion;
    uint8_t addr_type = 0;
    uint8_t bonded = 0;
    uint8_t reserved = 0;
    uint8_t addr[6] = {};
    uint8_t reserved2[2] = {};
};

static_assert(sizeof(StoredPeerIdentity) == 16, "Stored peer identity layout changed");

bool usbSerialWritable(std::size_t len)
{
    return static_cast<bool>(Serial) && Serial.dtr() != 0 && Serial.availableForWrite() >= static_cast<int>(len);
}

void blePersistenceLog(const char* fmt, ...)
{
    char buffer[192] = {};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (usbSerialWritable(std::strlen(buffer) + 2U))
    {
        Serial.println(buffer);
    }
    Serial2.println(buffer);
}

#if !defined(GAT562_MESH_EVB_PRO)
template <typename T>
bool loadBlobConfigFromUiStore(const char* key, T* out)
{
    if (!out)
    {
        return false;
    }

    std::vector<uint8_t> blob;
    if (!platform::ui::settings_store::get_blob(kBleSettingsNamespace, key, blob))
    {
        return false;
    }
    if (blob.size() != sizeof(T))
    {
        blePersistenceLog("[BLE][nrf52][mt] load cfg size mismatch key=%s got=%u expected=%u",
                          key ? key : "?",
                          static_cast<unsigned>(blob.size()),
                          static_cast<unsigned>(sizeof(T)));
        return false;
    }

    std::memcpy(out, blob.data(), sizeof(T));
    return true;
}

template <typename T>
bool saveBlobConfigToUiStore(const char* key, const T& value)
{
    const bool ok = platform::ui::settings_store::put_blob(kBleSettingsNamespace, key, &value, sizeof(T));
    if (!ok)
    {
        blePersistenceLog("[BLE][nrf52][mt] save cfg failed key=%s size=%u",
                          key ? key : "?",
                          static_cast<unsigned>(sizeof(T)));
    }
    return ok;
}
#endif
} // namespace

bool loadMeshtasticBlePersistedState(meshtastic_config_bridge::PersistedState* out)
{
    if (!out)
    {
        return false;
    }

    *out = meshtastic_config_bridge::PersistedState{};

#if defined(GAT562_MESH_EVB_PRO)
    if (::boards::gat562_mesh_evb_pro::settings_store::loadMeshtasticBleState(&out->bluetooth, &out->module))
    {
        out->has_bluetooth = true;
        out->has_module = true;
        return true;
    }
    return false;
#else
    out->has_bluetooth = loadBlobConfigFromUiStore(kBluetoothConfigKey, &out->bluetooth);
    out->has_module = loadBlobConfigFromUiStore(kModuleConfigKey, &out->module);
    if (out->has_bluetooth || out->has_module)
    {
        blePersistenceLog("[BLE][nrf52][mt] ui settings cfg store bt=%u mod=%u",
                          out->has_bluetooth ? 1U : 0U,
                          out->has_module ? 1U : 0U);
    }
    return out->has_bluetooth || out->has_module;
#endif
}

bool saveMeshtasticBlePersistedState(const meshtastic_Config_BluetoothConfig& bluetooth,
                                     const meshtastic_LocalModuleConfig& module)
{
#if defined(GAT562_MESH_EVB_PRO)
    return ::boards::gat562_mesh_evb_pro::settings_store::saveMeshtasticBleState(bluetooth, module);
#else
    const bool bluetooth_ok = saveBlobConfigToUiStore(kBluetoothConfigKey, bluetooth);
    const bool module_ok = saveBlobConfigToUiStore(kModuleConfigKey, module);
    return bluetooth_ok && module_ok;
#endif
}

bool loadMeshtasticBlePeerIdentity(MeshtasticBlePeerIdentity* out)
{
    if (!out)
    {
        return false;
    }

    *out = MeshtasticBlePeerIdentity{};

#if defined(GAT562_MESH_EVB_PRO)
    return false;
#else
    StoredPeerIdentity stored{};
    if (!loadBlobConfigFromUiStore(kPeerIdentityKey, &stored))
    {
        return false;
    }
    if (stored.magic != kPeerIdentityMagic || stored.version != kPeerIdentityVersion)
    {
        blePersistenceLog("[BLE][nrf52][mt] peer store ignored magic=0x%08lX version=%u",
                          static_cast<unsigned long>(stored.magic),
                          static_cast<unsigned>(stored.version));
        return false;
    }

    out->addr_type = stored.addr_type;
    out->bonded = stored.bonded;
    std::memcpy(out->addr, stored.addr, sizeof(out->addr));
    return true;
#endif
}

bool saveMeshtasticBlePeerIdentity(const MeshtasticBlePeerIdentity& peer)
{
#if defined(GAT562_MESH_EVB_PRO)
    (void)peer;
    return false;
#else
    StoredPeerIdentity stored{};
    stored.addr_type = peer.addr_type;
    stored.bonded = peer.bonded ? 1U : 0U;
    std::memcpy(stored.addr, peer.addr, sizeof(stored.addr));
    return saveBlobConfigToUiStore(kPeerIdentityKey, stored);
#endif
}

void logMeshtasticBlePersistenceStatus()
{
#if defined(GAT562_MESH_EVB_PRO)
    blePersistenceLog("[BLE][nrf52][mt] settings_store load=%s save=%s",
                      ::boards::gat562_mesh_evb_pro::settings_store::statusLabel(
                          ::boards::gat562_mesh_evb_pro::settings_store::lastLoadStatus()),
                      ::boards::gat562_mesh_evb_pro::settings_store::statusLabel(
                          ::boards::gat562_mesh_evb_pro::settings_store::lastSaveStatus()));
#endif
}

} // namespace ble
