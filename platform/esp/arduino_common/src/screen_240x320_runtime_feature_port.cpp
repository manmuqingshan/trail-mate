#include "platform/ui/screen_240x320_runtime_feature_port.h"

#include "platform/ui/sstv_runtime.h"
#include "platform/ui/tracker_runtime.h"
#include "platform/ui/usb_support_runtime.h"
#include "platform/ui/walkie_runtime.h"
#include "ui/mono/screens/screen_240x320/runtime_feature_port.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace platform::ui
{
namespace
{

class EspRuntimeFeaturePort final
    : public ::ui::mono::screens::screen_240x320::RuntimeFeaturePort
{
  public:
    void readTracker(::ui::mono::screens::screen_240x320::TrackerView& out) const override
    {
        out = {};
        out.supported = ::platform::ui::tracker::is_supported();
        out.recording = out.supported && ::platform::ui::tracker::is_recording();
        if (!out.supported ||
            !::platform::ui::tracker::current_path(tracker_path_scratch_))
        {
            return;
        }
        out.has_path = true;
        std::snprintf(out.path, sizeof(out.path), "%s", tracker_path_scratch_.c_str());
    }

    bool startTracker() override
    {
        return ::platform::ui::tracker::start_recording();
    }

    void stopTracker() override
    {
        ::platform::ui::tracker::stop_recording();
    }

    void readWalkie(::ui::mono::screens::screen_240x320::WalkieView& out) const override
    {
        out = {};
        out.supported = ::platform::ui::walkie::is_supported();
        const ::platform::ui::walkie::Status status = ::platform::ui::walkie::get_status();
        out.active = status.active;
        out.tx = status.tx;
        out.monitor_enabled = status.monitor_enabled;
        out.rx_level = status.rx_level;
        out.frequency_mhz = status.freq_mhz;
    }

    bool startWalkie() override
    {
        return ::platform::ui::walkie::start();
    }

    void stopWalkie() override
    {
        ::platform::ui::walkie::stop();
    }

    bool setWalkieMonitorEnabled(bool enabled) override
    {
        return ::platform::ui::walkie::set_monitor_enabled(enabled);
    }

    void readSstv(::ui::mono::screens::screen_240x320::SstvView& out) const override
    {
        out = {};
        out.supported = ::platform::ui::sstv::is_supported();
        const ::platform::ui::sstv::Status status = ::platform::ui::sstv::get_status();
        switch (status.state)
        {
        case ::platform::ui::sstv::State::Waiting:
            out.state = ::ui::mono::screens::screen_240x320::SstvViewState::Waiting;
            break;
        case ::platform::ui::sstv::State::Receiving:
            out.state = ::ui::mono::screens::screen_240x320::SstvViewState::Receiving;
            break;
        case ::platform::ui::sstv::State::Complete:
            out.state = ::ui::mono::screens::screen_240x320::SstvViewState::Complete;
            break;
        case ::platform::ui::sstv::State::Error:
            out.state = ::ui::mono::screens::screen_240x320::SstvViewState::Error;
            break;
        case ::platform::ui::sstv::State::Idle:
        default:
            out.state = ::ui::mono::screens::screen_240x320::SstvViewState::Idle;
            break;
        }
        out.line = status.line;
        out.progress = status.progress;
        out.has_image = status.has_image;
        std::snprintf(out.mode, sizeof(out.mode), "%s", ::platform::ui::sstv::mode_name());
        const char* const path = ::platform::ui::sstv::last_saved_path();
        std::snprintf(out.last_saved_path, sizeof(out.last_saved_path), "%s", path ? path : "");
    }

    bool startSstv() override
    {
        return ::platform::ui::sstv::start();
    }

    void stopSstv() override
    {
        ::platform::ui::sstv::stop();
    }

    void readUsbStorage(::ui::mono::screens::screen_240x320::UsbStorageView& out) const override
    {
        out = {};
        out.supported = ::platform::ui::usb_support::is_supported();
        const ::platform::ui::usb_support::Status status = ::platform::ui::usb_support::get_status();
        out.active = status.active;
        out.stop_requested = status.stop_requested;
        std::snprintf(out.message, sizeof(out.message), "%s",
                      status.message ? status.message : "");
    }

    bool startUsbStorage() override
    {
        return ::platform::ui::usb_support::start();
    }

    void stopUsbStorage() override
    {
        ::platform::ui::usb_support::stop();
    }

  private:
    // The platform implementation is static and never allocates a path
    // string on an ESP task stack.
    mutable std::string tracker_path_scratch_;
};

EspRuntimeFeaturePort s_runtime_feature_port;

} // namespace

void install_screen_240x320_runtime_feature_port()
{
    ::ui::mono::screens::screen_240x320::installRuntimeFeaturePort(
        &s_runtime_feature_port);
}

} // namespace platform::ui
