#pragma once

#include "chat/ble/meshtastic_phone_config_bridge.h"

namespace ble
{

struct MeshtasticBlePeerIdentity
{
    uint8_t addr_type = 0;
    uint8_t bonded = 0;
    uint8_t addr[6] = {};
};

bool loadMeshtasticBlePersistedState(meshtastic_config_bridge::PersistedState* out);
bool saveMeshtasticBlePersistedState(const meshtastic_Config_BluetoothConfig& bluetooth,
                                     const meshtastic_LocalModuleConfig& module);
bool loadMeshtasticBlePeerIdentity(MeshtasticBlePeerIdentity* out);
bool saveMeshtasticBlePeerIdentity(const MeshtasticBlePeerIdentity& peer);
void logMeshtasticBlePersistenceStatus();

} // namespace ble
