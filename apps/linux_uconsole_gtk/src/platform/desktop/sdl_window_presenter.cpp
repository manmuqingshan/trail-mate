#include "platform/desktop/sdl_window_presenter.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include "app/input_event.h"
#include "core/canvas.h"

#define LODEPNG_NO_COMPILE_CPP
#include "src/libs/lodepng/lodepng.h"

namespace trailmate::uconsole::desktop
{
namespace
{

namespace app = cardputer_zero::app;
namespace core = cardputer_zero::core;

void requireSdl(bool condition, std::string_view step)
{
    if (!condition)
    {
        throw std::runtime_error(std::string(step) + ": " + SDL_GetError());
    }
}

template <typename T>
T* requireSdl(T* pointer, std::string_view step)
{
    if (!pointer)
    {
        throw std::runtime_error(std::string(step) + ": " + SDL_GetError());
    }
    return pointer;
}

[[nodiscard]] std::uint32_t packPixel(core::Color color) noexcept
{
    return (static_cast<std::uint32_t>(color.a) << 24U) |
           (static_cast<std::uint32_t>(color.b) << 16U) |
           (static_cast<std::uint32_t>(color.g) << 8U) |
           static_cast<std::uint32_t>(color.r);
}

void enqueueSpecial(std::vector<app::InputEvent>& queue,
                    app::InputKey key,
                    std::string label)
{
    queue.push_back(app::InputEvent{key, std::move(label), '\0'});
}

void handleKeyboardEvent(std::vector<app::InputEvent>& queue,
                         const SDL_KeyboardEvent& event)
{
    if (event.repeat)
    {
        return;
    }

    switch (event.key)
    {
    case SDLK_BACKSPACE:
        enqueueSpecial(queue, app::InputKey::Backspace, "DEL");
        break;
    case SDLK_RETURN:
        enqueueSpecial(queue, app::InputKey::Enter, "OK");
        break;
    case SDLK_TAB:
        enqueueSpecial(queue, app::InputKey::Tab, "TAB");
        break;
    case SDLK_HOME:
        enqueueSpecial(queue, app::InputKey::Home, "HOME");
        break;
    case SDLK_END:
        enqueueSpecial(queue, app::InputKey::Next, "NEXT");
        break;
    case SDLK_ESCAPE:
        enqueueSpecial(queue, app::InputKey::Power, "POWER");
        break;
    case SDLK_LSHIFT:
    case SDLK_RSHIFT:
        enqueueSpecial(queue, app::InputKey::Shift, "SHIFT");
        break;
    case SDLK_LCTRL:
    case SDLK_RCTRL:
        enqueueSpecial(queue, app::InputKey::Ctrl, "CTRL");
        break;
    case SDLK_LALT:
    case SDLK_RALT:
        enqueueSpecial(queue, app::InputKey::Alt, "ALT");
        break;
    case SDLK_LEFT:
        enqueueSpecial(queue, app::InputKey::Left, "LEFT");
        break;
    case SDLK_RIGHT:
        enqueueSpecial(queue, app::InputKey::Right, "RIGHT");
        break;
    case SDLK_UP:
        enqueueSpecial(queue, app::InputKey::Up, "UP");
        break;
    case SDLK_DOWN:
        enqueueSpecial(queue, app::InputKey::Down, "DOWN");
        break;
    default:
        break;
    }
}

void handleTextInput(std::vector<app::InputEvent>& queue,
                     const SDL_TextInputEvent& event)
{
    if (event.text == nullptr)
    {
        return;
    }

    for (const char* current = event.text; *current != '\0'; ++current)
    {
        const unsigned char value = static_cast<unsigned char>(*current);
        if (value > 127U || !std::isprint(value))
        {
            continue;
        }

        const char ch = static_cast<char>(value);
        std::string label{};
        if (ch == ' ')
        {
            label = "SPACE";
        }
        else
        {
            label.push_back(static_cast<char>(std::toupper(value)));
        }
        queue.push_back(app::makeCharacterInput(ch, std::move(label)));
    }
}

void saveRendererPng(SDL_Renderer* renderer,
                     const std::filesystem::path& path)
{
    SDL_Surface* captured =
        requireSdl(SDL_RenderReadPixels(renderer, nullptr),
                   "SDL_RenderReadPixels");
    SDL_Surface* rgba =
        requireSdl(SDL_ConvertSurface(captured, SDL_PIXELFORMAT_RGBA32),
                   "SDL_ConvertSurface");
    SDL_DestroySurface(captured);

    std::vector<unsigned char> pixels;
    pixels.resize(static_cast<std::size_t>(rgba->w) *
                  static_cast<std::size_t>(rgba->h) * 4U);
    const auto* source = static_cast<const unsigned char*>(rgba->pixels);
    for (int y = 0; y < rgba->h; ++y)
    {
        const auto* source_row =
            source + (static_cast<std::size_t>(y) *
                      static_cast<std::size_t>(rgba->pitch));
        auto* destination_row =
            pixels.data() + (static_cast<std::size_t>(y) *
                             static_cast<std::size_t>(rgba->w) * 4U);
        std::copy_n(source_row,
                    static_cast<std::size_t>(rgba->w) * 4U,
                    destination_row);
    }

    const int width = rgba->w;
    const int height = rgba->h;
    SDL_DestroySurface(rgba);

    if (!path.parent_path().empty())
    {
        std::filesystem::create_directories(path.parent_path());
    }

    unsigned char* encoded = nullptr;
    std::size_t encoded_size = 0;
    const unsigned error =
        lodepng_encode32(&encoded,
                         &encoded_size,
                         pixels.data(),
                         static_cast<unsigned>(width),
                         static_cast<unsigned>(height));
    if (error != 0U)
    {
        throw std::runtime_error("failed to encode SDL screenshot " +
                                 path.string() + ": " +
                                 lodepng_error_text(error));
    }

    std::ofstream output(path, std::ios::binary);
    if (!output)
    {
        std::free(encoded);
        throw std::runtime_error("failed to open SDL screenshot " +
                                 path.string());
    }
    output.write(reinterpret_cast<const char*>(encoded),
                 static_cast<std::streamsize>(encoded_size));
    std::free(encoded);
    if (!output)
    {
        throw std::runtime_error("failed to write SDL screenshot " +
                                 path.string());
    }
}

} // namespace

struct SdlWindowPresenter::Impl
{
    SdlWindowOptions options{};
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    bool running = true;
    int texture_width = 0;
    int texture_height = 0;
    int window_width = 0;
    int window_height = 0;
    int presented_frames = 0;
    bool startup_inputs_queued = false;
    bool startup_shortcut_queued = false;
    std::vector<std::uint32_t> staging{};
    std::vector<app::InputEvent> input_queue{};
};

SdlWindowPresenter::SdlWindowPresenter(SdlWindowOptions options)
    : impl_(std::make_unique<Impl>())
{
    impl_->options = std::move(options);
    impl_->options.width = std::max(1, impl_->options.width);
    impl_->options.height = std::max(1, impl_->options.height);
    impl_->options.scale = std::max(1, impl_->options.scale);
    impl_->window_width = impl_->options.width * impl_->options.scale;
    impl_->window_height = impl_->options.height * impl_->options.scale;

    requireSdl(SDL_Init(SDL_INIT_VIDEO), "SDL_Init");

    SDL_WindowFlags flags = 0;
    if (impl_->options.fullscreen)
    {
        flags |= SDL_WINDOW_FULLSCREEN;
    }
    if (impl_->options.hidden)
    {
        flags |= SDL_WINDOW_HIDDEN;
    }
    impl_->window =
        requireSdl(SDL_CreateWindow(impl_->options.title.c_str(),
                                    impl_->window_width,
                                    impl_->window_height,
                                    flags),
                   "SDL_CreateWindow");
    impl_->renderer =
        requireSdl(SDL_CreateRenderer(impl_->window, nullptr),
                   "SDL_CreateRenderer");
    requireSdl(SDL_StartTextInput(impl_->window), "SDL_StartTextInput");
}

SdlWindowPresenter::~SdlWindowPresenter()
{
    if (impl_->texture != nullptr)
    {
        SDL_DestroyTexture(impl_->texture);
    }
    if (impl_->renderer != nullptr)
    {
        SDL_DestroyRenderer(impl_->renderer);
    }
    if (impl_->window != nullptr)
    {
        SDL_DestroyWindow(impl_->window);
    }
    SDL_Quit();
}

bool SdlWindowPresenter::pump()
{
    if (!impl_->startup_inputs_queued)
    {
        for (int step = 0; step < impl_->options.initial_nav_steps; ++step)
        {
            enqueueSpecial(impl_->input_queue, app::InputKey::Tab, "TAB");
        }
        if (impl_->options.initial_nav_steps > 0)
        {
            enqueueSpecial(impl_->input_queue, app::InputKey::Enter, "OK");
        }
        impl_->startup_inputs_queued = true;
    }
    else if (!impl_->startup_shortcut_queued && impl_->presented_frames >= 5)
    {
        if (impl_->options.initial_shortcut != '\0')
        {
            impl_->input_queue.push_back(
                app::makeCharacterInput(impl_->options.initial_shortcut));
        }
        impl_->startup_shortcut_queued = true;
    }

    SDL_Event event{};
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_EVENT_QUIT)
        {
            impl_->running = false;
            continue;
        }
        if (event.type == SDL_EVENT_KEY_DOWN)
        {
            handleKeyboardEvent(impl_->input_queue, event.key);
            continue;
        }
        if (event.type == SDL_EVENT_TEXT_INPUT)
        {
            handleTextInput(impl_->input_queue, event.text);
            continue;
        }
        if (event.type == SDL_EVENT_WINDOW_RESIZED)
        {
            impl_->window_width = event.window.data1;
            impl_->window_height = event.window.data2;
        }
    }

    return impl_->running;
}

std::vector<cardputer_zero::app::InputEvent> SdlWindowPresenter::drainInput()
{
    auto drained = std::move(impl_->input_queue);
    impl_->input_queue.clear();
    return drained;
}

void SdlWindowPresenter::present(const core::Canvas& canvas)
{
    if (canvas.width() != impl_->texture_width ||
        canvas.height() != impl_->texture_height || impl_->texture == nullptr)
    {
        if (impl_->texture != nullptr)
        {
            SDL_DestroyTexture(impl_->texture);
        }
        impl_->texture_width = canvas.width();
        impl_->texture_height = canvas.height();
        impl_->staging.resize(
            static_cast<std::size_t>(impl_->texture_width) *
            static_cast<std::size_t>(impl_->texture_height));
        impl_->texture = requireSdl(
            SDL_CreateTexture(impl_->renderer,
                              SDL_PIXELFORMAT_RGBA32,
                              SDL_TEXTUREACCESS_STREAMING,
                              impl_->texture_width,
                              impl_->texture_height),
            "SDL_CreateTexture");
        requireSdl(SDL_SetTextureScaleMode(impl_->texture,
                                           SDL_SCALEMODE_LINEAR),
                   "SDL_SetTextureScaleMode");
    }

    const auto& pixels = canvas.pixels();
    for (std::size_t index = 0; index < pixels.size(); ++index)
    {
        impl_->staging[index] = packPixel(pixels[index]);
    }

    requireSdl(SDL_UpdateTexture(
                   impl_->texture,
                   nullptr,
                   impl_->staging.data(),
                   impl_->texture_width *
                       static_cast<int>(sizeof(std::uint32_t))),
               "SDL_UpdateTexture");
    requireSdl(SDL_SetRenderDrawColor(impl_->renderer, 0, 0, 0, 255),
               "SDL_SetRenderDrawColor");
    requireSdl(SDL_RenderClear(impl_->renderer), "SDL_RenderClear");

    if (!impl_->options.fullscreen)
    {
        SDL_GetWindowSize(impl_->window, &impl_->window_width,
                          &impl_->window_height);
    }
    const float scale_x = static_cast<float>(impl_->window_width) /
                          static_cast<float>(impl_->texture_width);
    const float scale_y = static_cast<float>(impl_->window_height) /
                          static_cast<float>(impl_->texture_height);
    const float scale = std::min(scale_x, scale_y);
    const float render_width = static_cast<float>(impl_->texture_width) * scale;
    const float render_height =
        static_cast<float>(impl_->texture_height) * scale;
    const SDL_FRect destination{
        (static_cast<float>(impl_->window_width) - render_width) / 2.0F,
        (static_cast<float>(impl_->window_height) - render_height) / 2.0F,
        render_width,
        render_height,
    };

    requireSdl(SDL_RenderTexture(impl_->renderer, impl_->texture, nullptr,
                                 &destination),
               "SDL_RenderTexture");
    requireSdl(SDL_RenderPresent(impl_->renderer), "SDL_RenderPresent");

    ++impl_->presented_frames;
    if (!impl_->options.screenshot_path.empty() &&
        impl_->presented_frames >=
            std::max(1, impl_->options.screenshot_after_frames))
    {
        saveRendererPng(impl_->renderer, impl_->options.screenshot_path);
        impl_->running = false;
    }
}

} // namespace trailmate::uconsole::desktop
