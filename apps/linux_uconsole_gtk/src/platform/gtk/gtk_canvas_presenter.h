#pragma once

#include <memory>
#include <string>

#include "platform/surface_presenter.h"

namespace trailmate::uconsole::gtk
{

struct GtkCanvasPresenterOptions
{
    int width = 1280;
    int height = 720;
    bool fullscreen = true;
    std::string title = "Trail Mate uConsole";
    std::string screenshot_path{};
    int screenshot_after_frames = 45;
    int initial_nav_steps = 0;
    char initial_shortcut = '\0';
};

class GtkCanvasPresenter final
    : public ::trailmate::cardputer_zero::platform::SurfacePresenter
{
  public:
    struct Impl;

    explicit GtkCanvasPresenter(GtkCanvasPresenterOptions options = {});
    ~GtkCanvasPresenter() override;

    [[nodiscard]] bool pump() override;
    [[nodiscard]] std::vector<
        ::trailmate::cardputer_zero::app::InputEvent>
    drainInput() override;
    [[nodiscard]] bool supportsPointer() const noexcept override;
    [[nodiscard]] PointerState pointerState() const noexcept override;
    void present(
        const ::trailmate::cardputer_zero::core::Canvas& canvas) override;

  private:
    std::unique_ptr<Impl> impl_{};
};

} // namespace trailmate::uconsole::gtk
