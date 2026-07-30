#include "ble/ble_manager.h"

namespace ble
{

BleManager::BleManager(app::IAppBleFacade& ctx)
    : ctx_(ctx),
      active_protocol_(ctx.getMeshProtocol())
{
}

BleManager::~BleManager() = default;

void BleManager::begin()
{
}

void BleManager::setEnabled(bool enabled)
{
    (void)enabled;
}

void BleManager::update()
{
}

void BleManager::applyProtocol(chat::MeshProtocol protocol)
{
    active_protocol_ = protocol;
}

bool BleManager::getPairingStatus(BlePairingStatus* out) const
{
    if (out)
    {
        *out = BlePairingStatus{};
    }
    return false;
}

void BleManager::restartService(chat::MeshProtocol protocol)
{
    active_protocol_ = protocol;
}

void BleManager::shutdownNimble()
{
}

std::string BleManager::buildDeviceName(chat::MeshProtocol protocol) const
{
    (void)protocol;
    return {};
}

} // namespace ble
