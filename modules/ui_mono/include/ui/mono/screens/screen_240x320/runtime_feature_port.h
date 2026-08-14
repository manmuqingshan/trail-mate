#pragma once

#include <cstdint>

namespace ui::mono::screens::screen_240x320
{

// This port separates reusable 240x320 page projections from device services.
// A runtime adapter owns radios, storage modes, filesystems and hardware
// capability detection; pages consume only these compact snapshots and intents.
enum class SstvViewState : uint8_t
{
    Idle,
    Waiting,
    Receiving,
    Complete,
    Error,
};

struct TrackerView
{
    bool supported = false;
    bool recording = false;
    bool has_path = false;
    char path[96]{};
};

struct WalkieView
{
    bool supported = false;
    bool active = false;
    bool tx = false;
    bool monitor_enabled = false;
    uint8_t rx_level = 0;
    float frequency_mhz = 0.0F;
};

struct SstvView
{
    bool supported = false;
    SstvViewState state = SstvViewState::Idle;
    uint16_t line = 0;
    float progress = 0.0F;
    bool has_image = false;
    char mode[24]{};
    char last_saved_path[96]{};
};

struct UsbStorageView
{
    bool supported = false;
    bool active = false;
    bool stop_requested = false;
    char message[96]{};
};

class RuntimeFeaturePort
{
  public:
    virtual ~RuntimeFeaturePort() = default;

    virtual void readTracker(TrackerView& out) const = 0;
    virtual bool startTracker() = 0;
    virtual void stopTracker() = 0;

    virtual void readWalkie(WalkieView& out) const = 0;
    virtual bool startWalkie() = 0;
    virtual void stopWalkie() = 0;
    virtual bool setWalkieMonitorEnabled(bool enabled) = 0;

    virtual void readSstv(SstvView& out) const = 0;
    virtual bool startSstv() = 0;
    virtual void stopSstv() = 0;

    virtual void readUsbStorage(UsbStorageView& out) const = 0;
    virtual bool startUsbStorage() = 0;
    virtual void stopUsbStorage() = 0;
};

void installRuntimeFeaturePort(RuntimeFeaturePort* port);
RuntimeFeaturePort* runtimeFeaturePort();

} // namespace ui::mono::screens::screen_240x320
