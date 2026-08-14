#pragma once

#include "ui/app_screen.h"

namespace ui::mono::screens::screen_240x320
{

// The catalogue identity and application lifecycle are shared across targets.
// This class selects only the 240x320 monochrome projection; it has no board
// or display-driver dependency.
enum class PageKind : unsigned char
{
    Map,
    SkyPlot,
    Network,
    Settings,
    Tracker,
    Walkie,
    Sstv,
    UsbStorage,
    Chat,
    Team,
    Contacts,
    Extensions,
    ProtocolProbe,
};

class ScreenApp final : public AppScreen
{
  public:
    ScreenApp(const char* stable_id,
              const char* name,
              PageKind page_kind,
              ui::AppLaunchMode launch_mode = ui::AppLaunchMode::Screen);

    const char* stable_id() const override;
    const char* name() const override;
    const lv_image_dsc_t* icon() const override;
    ui::AppLaunchMode launch_mode() const override;
    void enter(lv_obj_t* parent) override;
    void exit(lv_obj_t* parent) override;

    PageKind page_kind() const { return page_kind_; }

  private:
    const char* stable_id_ = "";
    const char* name_ = "";
    PageKind page_kind_ = PageKind::Map;
    ui::AppLaunchMode launch_mode_ = ui::AppLaunchMode::Screen;
};

} // namespace ui::mono::screens::screen_240x320
