#include "platform/gtk/gtk_canvas_presenter.h"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>

namespace trailmate::uconsole::gtk
{
using InputEvent = ::trailmate::cardputer_zero::app::InputEvent;
using InputKey = ::trailmate::cardputer_zero::app::InputKey;
using Canvas = ::trailmate::cardputer_zero::core::Canvas;

struct GtkCanvasPresenter::Impl
{
    GtkCanvasPresenterOptions options{};
    GtkWindow* window = nullptr;
    GtkDrawingArea* drawing_area = nullptr;
    GtkEventController* key_controller = nullptr;
    GtkEventController* motion_controller = nullptr;
    GtkGesture* click_gesture = nullptr;
    bool running = true;
    bool fullscreen = false;
    bool startup_inputs_queued = false;
    bool startup_shortcut_queued = false;
    PointerState pointer{};
    int frame_width = 0;
    int frame_height = 0;
    int presented_frames = 0;
    std::vector<std::uint32_t> frame{};
    std::vector<InputEvent> input{};
};

namespace
{

void queueSpecial(std::vector<InputEvent>& input,
                  InputKey key,
                  const char* label)
{
    input.push_back(InputEvent{key, label, '\0'});
}

void updatePointer(GtkCanvasPresenter::Impl& impl,
                   double widget_x,
                   double widget_y,
                   bool pressed)
{
    const int widget_width =
        gtk_widget_get_width(GTK_WIDGET(impl.drawing_area));
    const int widget_height =
        gtk_widget_get_height(GTK_WIDGET(impl.drawing_area));
    if (widget_width <= 0 || widget_height <= 0 ||
        impl.frame_width <= 0 || impl.frame_height <= 0)
    {
        return;
    }
    const double scale_x =
        static_cast<double>(widget_width) / impl.frame_width;
    const double scale_y =
        static_cast<double>(widget_height) / impl.frame_height;
    const double scale = std::min(scale_x, scale_y);
    const double draw_width = impl.frame_width * scale;
    const double draw_height = impl.frame_height * scale;
    const double offset_x = (widget_width - draw_width) / 2.0;
    const double offset_y = (widget_height - draw_height) / 2.0;
    impl.pointer.x = std::clamp(
        static_cast<int>((widget_x - offset_x) / scale), 0,
        std::max(0, impl.frame_width - 1));
    impl.pointer.y = std::clamp(
        static_cast<int>((widget_y - offset_y) / scale), 0,
        std::max(0, impl.frame_height - 1));
    impl.pointer.pressed = pressed;
}

void onMotion(GtkEventControllerMotion*, double x, double y, gpointer data)
{
    auto* impl = static_cast<GtkCanvasPresenter::Impl*>(data);
    updatePointer(*impl, x, y, impl->pointer.pressed);
}

void onPressed(GtkGestureClick*, int, double x, double y, gpointer data)
{
    auto* impl = static_cast<GtkCanvasPresenter::Impl*>(data);
    updatePointer(*impl, x, y, true);
}

void onReleased(GtkGestureClick*, int, double x, double y, gpointer data)
{
    auto* impl = static_cast<GtkCanvasPresenter::Impl*>(data);
    updatePointer(*impl, x, y, false);
}

gboolean onKeyPressed(GtkEventControllerKey*,
                      guint keyval,
                      guint,
                      GdkModifierType modifiers,
                      gpointer data)
{
    auto* impl = static_cast<GtkCanvasPresenter::Impl*>(data);
    if ((modifiers & GDK_CONTROL_MASK) != 0 &&
        (keyval == GDK_KEY_m || keyval == GDK_KEY_M))
    {
        gtk_window_minimize(impl->window);
        return TRUE;
    }
    if (((modifiers & GDK_CONTROL_MASK) != 0 &&
         (keyval == GDK_KEY_q || keyval == GDK_KEY_Q)) ||
        ((modifiers & GDK_ALT_MASK) != 0 && keyval == GDK_KEY_F4))
    {
        impl->running = false;
        gtk_window_destroy(impl->window);
        return TRUE;
    }
    if (keyval == GDK_KEY_F11)
    {
        impl->fullscreen = !impl->fullscreen;
        if (impl->fullscreen)
            gtk_window_fullscreen(impl->window);
        else
            gtk_window_unfullscreen(impl->window);
        return TRUE;
    }
    switch (keyval)
    {
    case GDK_KEY_BackSpace:
        queueSpecial(impl->input, InputKey::Backspace, "DEL");
        return TRUE;
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
        queueSpecial(impl->input, InputKey::Enter, "OK");
        return TRUE;
    case GDK_KEY_Tab:
        queueSpecial(impl->input, InputKey::Tab, "TAB");
        return TRUE;
    case GDK_KEY_Home:
        queueSpecial(impl->input, InputKey::Home, "HOME");
        return TRUE;
    case GDK_KEY_End:
        queueSpecial(impl->input, InputKey::Next, "NEXT");
        return TRUE;
    case GDK_KEY_Escape:
        queueSpecial(impl->input, InputKey::Power, "POWER");
        return TRUE;
    case GDK_KEY_Left:
        queueSpecial(impl->input, InputKey::Left, "LEFT");
        return TRUE;
    case GDK_KEY_Right:
        queueSpecial(impl->input, InputKey::Right, "RIGHT");
        return TRUE;
    case GDK_KEY_Up:
        queueSpecial(impl->input, InputKey::Up, "UP");
        return TRUE;
    case GDK_KEY_Down:
        queueSpecial(impl->input, InputKey::Down, "DOWN");
        return TRUE;
    case GDK_KEY_F1:
        queueSpecial(impl->input, InputKey::F1, "F1");
        return TRUE;
    default:
        break;
    }

    const gunichar unicode = gdk_keyval_to_unicode(keyval);
    if (unicode >= 0x20U && unicode <= 0x7EU)
    {
        const char text = static_cast<char>(unicode);
        impl->input.push_back(::trailmate::cardputer_zero::app::
                                  makeCharacterInput(text, std::string(1, text)));
        return TRUE;
    }
    return FALSE;
}

gboolean onCloseRequest(GtkWindow*, gpointer data)
{
    auto* impl = static_cast<GtkCanvasPresenter::Impl*>(data);
    impl->running = false;
    return TRUE;
}

void onDraw(GtkDrawingArea*,
            cairo_t* cairo,
            int width,
            int height,
            gpointer data)
{
    auto* impl = static_cast<GtkCanvasPresenter::Impl*>(data);
    cairo_set_source_rgb(cairo, 0.0, 0.0, 0.0);
    cairo_paint(cairo);
    if (impl->frame.empty() || impl->frame_width <= 0 ||
        impl->frame_height <= 0)
    {
        return;
    }

    cairo_surface_t* surface = cairo_image_surface_create_for_data(
        reinterpret_cast<unsigned char*>(impl->frame.data()),
        CAIRO_FORMAT_ARGB32,
        impl->frame_width,
        impl->frame_height,
        impl->frame_width * static_cast<int>(sizeof(std::uint32_t)));
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
    {
        cairo_surface_destroy(surface);
        return;
    }

    const double scale_x =
        static_cast<double>(width) / static_cast<double>(impl->frame_width);
    const double scale_y =
        static_cast<double>(height) / static_cast<double>(impl->frame_height);
    const double scale = std::min(scale_x, scale_y);
    const double render_width =
        static_cast<double>(impl->frame_width) * scale;
    const double render_height =
        static_cast<double>(impl->frame_height) * scale;
    cairo_translate(cairo,
                    (static_cast<double>(width) - render_width) / 2.0,
                    (static_cast<double>(height) - render_height) / 2.0);
    cairo_scale(cairo, scale, scale);
    cairo_set_source_surface(cairo, surface, 0.0, 0.0);
    cairo_pattern_set_filter(cairo_get_source(cairo), CAIRO_FILTER_NEAREST);
    cairo_paint(cairo);
    cairo_surface_destroy(surface);
}

} // namespace

GtkCanvasPresenter::GtkCanvasPresenter(GtkCanvasPresenterOptions options)
    : impl_(std::make_unique<Impl>())
{
    impl_->options = std::move(options);
    impl_->options.width = std::max(320, impl_->options.width);
    impl_->options.height = std::max(240, impl_->options.height);

    gtk_init();
    impl_->fullscreen = impl_->options.fullscreen;
    impl_->window = GTK_WINDOW(gtk_window_new());
    gtk_window_set_title(impl_->window, impl_->options.title.c_str());
    gtk_window_set_default_size(impl_->window, impl_->options.width,
                                impl_->options.height);
    gtk_window_set_resizable(impl_->window, TRUE);
    impl_->drawing_area = GTK_DRAWING_AREA(gtk_drawing_area_new());
    gtk_widget_set_hexpand(GTK_WIDGET(impl_->drawing_area), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(impl_->drawing_area), TRUE);
    gtk_widget_set_focusable(GTK_WIDGET(impl_->drawing_area), TRUE);
    gtk_drawing_area_set_draw_func(impl_->drawing_area, onDraw, impl_.get(),
                                   nullptr);
    gtk_window_set_child(impl_->window, GTK_WIDGET(impl_->drawing_area));

    impl_->key_controller = gtk_event_controller_key_new();
    g_signal_connect(impl_->key_controller, "key-pressed",
                     G_CALLBACK(onKeyPressed), impl_.get());
    gtk_widget_add_controller(GTK_WIDGET(impl_->drawing_area),
                              impl_->key_controller);
    impl_->motion_controller = gtk_event_controller_motion_new();
    g_signal_connect(impl_->motion_controller, "motion", G_CALLBACK(onMotion),
                     impl_.get());
    gtk_widget_add_controller(GTK_WIDGET(impl_->drawing_area),
                              impl_->motion_controller);
    impl_->click_gesture = gtk_gesture_click_new();
    g_signal_connect(impl_->click_gesture, "pressed", G_CALLBACK(onPressed),
                     impl_.get());
    g_signal_connect(impl_->click_gesture, "released",
                     G_CALLBACK(onReleased), impl_.get());
    gtk_widget_add_controller(GTK_WIDGET(impl_->drawing_area),
                              GTK_EVENT_CONTROLLER(impl_->click_gesture));
    g_signal_connect(impl_->window, "close-request",
                     G_CALLBACK(onCloseRequest), impl_.get());
    gtk_window_present(impl_->window);
    if (impl_->options.fullscreen)
    {
        gtk_window_fullscreen(impl_->window);
    }
    gtk_widget_grab_focus(GTK_WIDGET(impl_->drawing_area));
}

GtkCanvasPresenter::~GtkCanvasPresenter()
{
    if (impl_ != nullptr && impl_->window != nullptr)
    {
        gtk_window_destroy(impl_->window);
    }
}

bool GtkCanvasPresenter::pump()
{
    if (!impl_->startup_inputs_queued)
    {
        for (int step = 0; step < impl_->options.initial_nav_steps; ++step)
        {
            queueSpecial(impl_->input, InputKey::Tab, "TAB");
        }
        if (impl_->options.initial_nav_steps > 0)
        {
            queueSpecial(impl_->input, InputKey::Enter, "OK");
        }
        impl_->startup_inputs_queued = true;
    }
    else if (!impl_->startup_shortcut_queued &&
             impl_->presented_frames >= 5 &&
             impl_->options.initial_shortcut != '\0')
    {
        impl_->input.push_back(::trailmate::cardputer_zero::app::
                                   makeCharacterInput(
                                       impl_->options.initial_shortcut));
        impl_->startup_shortcut_queued = true;
    }
    while (g_main_context_pending(nullptr))
    {
        g_main_context_iteration(nullptr, FALSE);
    }
    return impl_->running;
}

std::vector<InputEvent> GtkCanvasPresenter::drainInput()
{
    auto drained = std::move(impl_->input);
    impl_->input.clear();
    return drained;
}

bool GtkCanvasPresenter::supportsPointer() const noexcept
{
    return true;
}

GtkCanvasPresenter::PointerState GtkCanvasPresenter::pointerState() const
    noexcept
{
    return impl_->pointer;
}

void GtkCanvasPresenter::present(const Canvas& canvas)
{
    impl_->frame_width = canvas.width();
    impl_->frame_height = canvas.height();
    impl_->frame.resize(static_cast<std::size_t>(canvas.width()) *
                        static_cast<std::size_t>(canvas.height()));
    const auto& pixels = canvas.pixels();
    for (std::size_t index = 0; index < pixels.size(); ++index)
    {
        const auto& pixel = pixels[index];
        impl_->frame[index] =
            (static_cast<std::uint32_t>(pixel.a) << 24U) |
            (static_cast<std::uint32_t>(pixel.r) << 16U) |
            (static_cast<std::uint32_t>(pixel.g) << 8U) |
            static_cast<std::uint32_t>(pixel.b);
    }
    ++impl_->presented_frames;
    if (!impl_->options.screenshot_path.empty() &&
        impl_->presented_frames >=
            std::max(1, impl_->options.screenshot_after_frames))
    {
        cairo_surface_t* surface = cairo_image_surface_create_for_data(
            reinterpret_cast<unsigned char*>(impl_->frame.data()),
            CAIRO_FORMAT_ARGB32,
            impl_->frame_width,
            impl_->frame_height,
            impl_->frame_width * static_cast<int>(sizeof(std::uint32_t)));
        if (cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS)
        {
            cairo_surface_write_to_png(surface,
                                       impl_->options.screenshot_path.c_str());
        }
        cairo_surface_destroy(surface);
        impl_->options.screenshot_path.clear();
    }
    gtk_widget_queue_draw(GTK_WIDGET(impl_->drawing_area));
}

} // namespace trailmate::uconsole::gtk
