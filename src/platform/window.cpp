#include "platform/window.hpp"

#include "core/log.hpp"

#include <SDL3/SDL.h>

#include <mutex>

namespace avgen::platform {

namespace {
// SDL's dialog callback may run on any thread; results are marshalled to the main thread by
// pushing a user event and reading the stored results in pollEvents().
struct DialogState {
    std::mutex mutex;
    std::vector<std::string> results;
    bool cancelled = false;
    std::uint32_t eventType = 0;
};

void SDLCALL dialogCallback(void* userdata, const char* const* filelist, int /*filter*/) {
    auto* state = static_cast<DialogState*>(userdata);
    {
        std::lock_guard lock(state->mutex);
        state->results.clear();
        state->cancelled = filelist == nullptr || filelist[0] == nullptr;
        if (filelist != nullptr) {
            for (const char* const* it = filelist; *it != nullptr; ++it) {
                state->results.emplace_back(*it);
            }
        }
    }
    SDL_Event event{};
    event.type = state->eventType;
    SDL_PushEvent(&event);
}

DialogState& dialogState() {
    static DialogState state;
    return state;
}
} // namespace

Result<std::unique_ptr<Window>> Window::create(const WindowDesc& desc) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        return fail("SDL_Init failed: {}", SDL_GetError());
    }
    std::unique_ptr<Window> window(new Window());
    SDL_WindowFlags flags = SDL_WINDOW_METAL;
    if (desc.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (desc.highDpi) {
        flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
    }
    window->window_ = SDL_CreateWindow(desc.title.c_str(), static_cast<int>(desc.width),
                                       static_cast<int>(desc.height), flags);
    if (window->window_ == nullptr) {
        return fail("SDL_CreateWindow failed: {}", SDL_GetError());
    }
    window->metalView_ = SDL_Metal_CreateView(window->window_);
    if (window->metalView_ == nullptr) {
        return fail("SDL_Metal_CreateView failed: {}", SDL_GetError());
    }
    window->metalLayer_ = SDL_Metal_GetLayer(window->metalView_);
    if (window->metalLayer_ == nullptr) {
        return fail("SDL_Metal_GetLayer returned null");
    }
    window->dialogEventType_ = SDL_RegisterEvents(1);
    dialogState().eventType = window->dialogEventType_;
    window->updateSize();
    log::info("window {}x{} points, {}x{} pixels (scale {:.2f})", desc.width, desc.height, window->pixelWidth_,
              window->pixelHeight_, window->pixelScale_);
    return window;
}

Window::~Window() {
    if (metalView_ != nullptr) {
        SDL_Metal_DestroyView(metalView_);
    }
    if (window_ != nullptr) {
        SDL_DestroyWindow(window_);
    }
    SDL_Quit();
}

void Window::updateSize() {
    int w = 0;
    int h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    pixelWidth_ = w > 0 ? static_cast<std::uint32_t>(w) : 0;
    pixelHeight_ = h > 0 ? static_cast<std::uint32_t>(h) : 0;
    pixelScale_ = SDL_GetWindowPixelDensity(window_);
    if (pixelScale_ <= 0.0f) {
        pixelScale_ = 1.0f;
    }
}

FrameEvents Window::pollEvents(const std::function<void(const SDL_Event&)>& sink) {
    FrameEvents events;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (sink) {
            sink(event);
        }
        switch (event.type) {
        case SDL_EVENT_QUIT:
            events.quit = true;
            break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (event.window.windowID == SDL_GetWindowID(window_)) {
                events.quit = true;
            }
            break;
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        case SDL_EVENT_WINDOW_MINIMIZED:
        case SDL_EVENT_WINDOW_RESTORED:
            updateSize();
            events.resized = true;
            break;
        case SDL_EVENT_DROP_FILE:
            if (event.drop.data != nullptr) {
                events.droppedFiles.emplace_back(event.drop.data);
            }
            break;
        default:
            if (event.type == dialogEventType_ && pendingDialog_) {
                std::vector<std::string> results;
                bool cancelled = false;
                {
                    auto& state = dialogState();
                    std::lock_guard lock(state.mutex);
                    results = state.results;
                    cancelled = state.cancelled;
                }
                auto callback = std::move(pendingDialog_);
                pendingDialog_ = nullptr;
                callback(cancelled || results.empty() ? std::string{} : results.front());
            }
            break;
        }
    }
    return events;
}

void Window::setTitle(const std::string& title) { SDL_SetWindowTitle(window_, title.c_str()); }

void Window::openFileDialog(DialogKind kind, std::function<void(std::string)> onChosen) {
    if (pendingDialog_) {
        log::warn("a file dialog is already open");
        return;
    }
    pendingDialog_ = std::move(onChosen);
    static const SDL_DialogFileFilter audioFilters[] = {{"Audio files", "wav;flac;mp3;ogg;aif;aiff"}, {"All files", "*"}};
    static const SDL_DialogFileFilter sceneFilters[] = {{"glTF scenes", "glb;gltf"}, {"All files", "*"}};
    static const SDL_DialogFileFilter envFilters[] = {{"HDR environments", "hdr"}, {"All files", "*"}};
    static const SDL_DialogFileFilter shaderFilters[] = {{"WGSL shaders", "wgsl;isf"}, {"All files", "*"}};
    static const SDL_DialogFileFilter anyFilters[] = {{"Supported files", "wav;flac;mp3;ogg;glb;gltf;hdr;json;wgsl"}, {"All files", "*"}};
    const SDL_DialogFileFilter* filters = kind == DialogKind::Audio ? audioFilters
                                          : kind == DialogKind::Scene ? sceneFilters
                                          : kind == DialogKind::Environment ? envFilters
                                          : kind == DialogKind::Shader ? shaderFilters : anyFilters;
    SDL_ShowOpenFileDialog(dialogCallback, &dialogState(), window_, filters, 2, nullptr, false);
}

void Window::saveFileDialog(std::function<void(std::string)> onChosen) {
    if (pendingDialog_) {
        log::warn("a file dialog is already open");
        return;
    }
    pendingDialog_ = std::move(onChosen);
    static const SDL_DialogFileFilter filters[] = {{"avgen project", "json"}};
    SDL_ShowSaveFileDialog(dialogCallback, &dialogState(), window_, filters, 1, nullptr);
}

} // namespace avgen::platform
