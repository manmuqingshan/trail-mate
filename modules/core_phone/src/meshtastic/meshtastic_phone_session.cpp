#include "phone/meshtastic/meshtastic_phone_session.h"

#if defined(ESP_PLATFORM)
#include "esp_heap_caps.h"
#include <new>
#endif

namespace phone::meshtastic
{

#if defined(ESP_PLATFORM)
void* MeshtasticPhoneSession::operator new(std::size_t size)
{
    void* ptr = heap_caps_malloc_prefer(size,
                                        2,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return ptr != nullptr ? ptr : ::operator new(size);
}

void MeshtasticPhoneSession::operator delete(void* ptr) noexcept
{
    heap_caps_free(ptr);
}

void MeshtasticPhoneSession::operator delete(void* ptr, std::size_t) noexcept
{
    operator delete(ptr);
}
#endif

MeshtasticPhoneSession::MeshtasticPhoneSession(IPhoneAppFacade& app, MeshtasticPhoneTransport& transport,
                                               MeshtasticPhoneBluetoothConfigHooks* bluetooth_config_hooks,
                                               MeshtasticPhoneModuleConfigHooks* module_config_hooks,
                                               MeshtasticPhoneConfigLifecycleHooks* config_lifecycle_hooks,
                                               MeshtasticPhoneStatusHooks* status_hooks,
                                               MeshtasticPhoneMqttHooks* mqtt_hooks,
                                               MeshtasticPhoneDeviceRuntimeHooks* device_runtime_hooks)
    : core_(app,
            transport,
            bluetooth_config_hooks,
            module_config_hooks,
            config_lifecycle_hooks,
            status_hooks,
            mqtt_hooks,
            device_runtime_hooks)
{
}

void MeshtasticPhoneSession::close()
{
    core_.reset();
}

void MeshtasticPhoneSession::pumpIncomingAppData()
{
    core_.pumpIncomingAppData();
}

bool MeshtasticPhoneSession::isSendingPackets() const
{
    return core_.isSendingPackets();
}

bool MeshtasticPhoneSession::isConfigFlowActive() const
{
    return core_.isConfigFlowActive();
}

void MeshtasticPhoneSession::debugLogMemoryLayout(const char* stage) const
{
    core_.debugLogMemoryLayout(stage);
}

bool MeshtasticPhoneSession::handleToRadio(const uint8_t* buf, size_t len)
{
    return core_.handleToRadio(buf, len);
}

bool MeshtasticPhoneSession::popToPhone(MeshtasticBleFrame* out)
{
    return core_.popToPhone(out);
}

void MeshtasticPhoneSession::onIncomingText(const chat::MeshIncomingText& msg)
{
    core_.onIncomingText(msg);
}

void MeshtasticPhoneSession::onIncomingData(const chat::MeshIncomingData& msg)
{
    core_.onIncomingData(msg);
}

} // namespace phone::meshtastic
