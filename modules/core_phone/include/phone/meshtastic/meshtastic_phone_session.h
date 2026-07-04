#pragma once

#include "phone/meshtastic/meshtastic_phone_core.h"

#include <cstddef>

namespace phone::meshtastic
{

class MeshtasticPhoneSession
{
  public:
#if defined(ESP_PLATFORM)
    static void* operator new(std::size_t size);
    static void operator delete(void* ptr) noexcept;
    static void operator delete(void* ptr, std::size_t) noexcept;
#endif

    MeshtasticPhoneSession(IPhoneAppFacade& app,
                           MeshtasticPhoneTransport& transport,
                           MeshtasticPhoneBluetoothConfigHooks* bluetooth_config_hooks,
                           MeshtasticPhoneModuleConfigHooks* module_config_hooks,
                           MeshtasticPhoneConfigLifecycleHooks* config_lifecycle_hooks,
                           MeshtasticPhoneStatusHooks* status_hooks,
                           MeshtasticPhoneMqttHooks* mqtt_hooks,
                           MeshtasticPhoneDeviceRuntimeHooks* device_runtime_hooks);

    void close();
    void pumpIncomingAppData();
    bool isSendingPackets() const;
    bool isConfigFlowActive() const;
    void debugLogMemoryLayout(const char* stage) const;
    bool handleToRadio(const uint8_t* buf, size_t len);
    bool popToPhone(MeshtasticBleFrame* out);
    void onIncomingText(const chat::MeshIncomingText& msg);
    void onIncomingData(const chat::MeshIncomingData& msg);

  private:
    MeshtasticPhoneCore core_;
};

} // namespace phone::meshtastic
