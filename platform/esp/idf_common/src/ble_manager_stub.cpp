#include "ble/ble_manager.h"

#include "app/app_config.h"

namespace ble
{

BleManager::BleManager(app::IAppBleFacade& ctx)
    : ctx_(ctx), active_protocol_(ctx.getConfig().mesh_protocol)
{
}

BleManager::~BleManager() = default;

void BleManager::begin() {}

void BleManager::setEnabled(bool enabled)
{
    if (!enabled)
    {
        service_.reset();
        nimble_initialized_ = false;
    }
}

void BleManager::update() {}

void BleManager::applyProtocol(chat::MeshProtocol protocol)
{
    active_protocol_ = protocol;
}

bool BleManager::getPairingStatus(BlePairingStatus* out) const
{
    if (!out)
    {
        return false;
    }

    *out = BlePairingStatus{};
    return false;
}

void BleManager::restartService(chat::MeshProtocol protocol)
{
    active_protocol_ = protocol;
    service_.reset();
}

void BleManager::shutdownNimble()
{
    nimble_initialized_ = false;
    service_.reset();
}

std::string BleManager::buildDeviceName(chat::MeshProtocol protocol) const
{
    (void)protocol;
    return {};
}

} // namespace ble
