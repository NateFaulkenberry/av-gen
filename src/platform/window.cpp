#include "platform/window.hpp"

#include "core/log.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <mutex>

namespace avgen::platform {

std::filesystem::path preferencesDirectory() {
    char* pref = SDL_GetPrefPath("avgen", "avgen");
    if (pref == nullptr) {
        return {};
    }
    std::filesystem::path dir(pref);
    SDL_free(pref);
    return dir;
}

namespace {
// SDL's dialog callback may run on any thread; results are marshalled to the main thread by
// pushing a user event and reading the stored results in pumpEvents().
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

// Every live window, so the process-wide queue can be routed. Main thread only.
std::vector<Window*>& liveWindows() {
    static std::vector<Window*> windows;
    return windows;
}

// The video subsystem is reference counted by SDL; windows and displays() share it.
bool initVideo() {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        log::warn("SDL video init failed: {}", SDL_GetError());
        return false;
    }
    // Plain fullscreen windows rather than macOS Spaces: an output per display must not start a
    // Space transition or hide the other windows.
    SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "0");
    return true;
}

void quitVideo() { SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS); }

SDL_DisplayID displayIdForIndex(int index) {
    if (index < 0) {
        return SDL_GetPrimaryDisplay();
    }
    int count = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&count);
    SDL_DisplayID id = 0;
    if (ids != nullptr) {
        if (index < count) {
            id = ids[index];
        }
        SDL_free(ids);
    }
    if (id == 0) {
        log::warn("display index {} out of range ({} displays); using the primary display", index, count);
        id = SDL_GetPrimaryDisplay();
    }
    return id;
}

int indexForDisplayId(SDL_DisplayID target) {
    int count = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&count);
    int index = -1;
    if (ids != nullptr) {
        for (int i = 0; i < count; ++i) {
            if (ids[i] == target) {
                index = i;
                break;
            }
        }
        SDL_free(ids);
    }
    return index;
}
} // namespace

Result<std::unique_ptr<Window>> Window::create(const WindowDesc& desc) {
    if (!initVideo()) {
        return fail("SDL_Init failed: {}", SDL_GetError());
    }
    std::unique_ptr<Window> window(new Window());
    const SDL_DisplayID display = displayIdForIndex(desc.displayIndex);
    const int defaultPos = static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(display));

    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, desc.title.c_str());
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, static_cast<Sint64>(desc.width));
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, static_cast<Sint64>(desc.height));
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, desc.x ? *desc.x : defaultPos);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, desc.y ? *desc.y : defaultPos);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_METAL_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, desc.resizable);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, desc.highDpi);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, desc.borderless);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_ALWAYS_ON_TOP_BOOLEAN, desc.alwaysOnTop);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, desc.fullscreen);
    window->window_ = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (window->window_ == nullptr) {
        quitVideo();
        return fail("SDL_CreateWindow failed: {}", SDL_GetError());
    }
    liveWindows().push_back(window.get()); // the destructor is now responsible for quitVideo
    window->id_ = SDL_GetWindowID(window->window_);
    if (desc.fullscreen) {
        // Desktop (borderless) fullscreen: no exclusive display mode.
        SDL_SetWindowFullscreenMode(window->window_, nullptr);
        SDL_SetWindowFullscreen(window->window_, true);
        SDL_SyncWindow(window->window_);
    }
    window->metalView_ = SDL_Metal_CreateView(window->window_);
    if (window->metalView_ == nullptr) {
        return fail("SDL_Metal_CreateView failed: {}", SDL_GetError());
    }
    window->metalLayer_ = SDL_Metal_GetLayer(window->metalView_);
    if (window->metalLayer_ == nullptr) {
        return fail("SDL_Metal_GetLayer returned null");
    }
    if (dialogState().eventType == 0) {
        dialogState().eventType = SDL_RegisterEvents(1);
    }
    window->updateSize();
    window->focused_ = (SDL_GetWindowFlags(window->window_) & SDL_WINDOW_INPUT_FOCUS) != 0;
    log::info("window '{}' {}x{} points, {}x{} pixels (scale {:.2f}) on display {}{}", desc.title, desc.width,
              desc.height, window->pixelWidth_, window->pixelHeight_, window->pixelScale_, window->displayIndex(),
              desc.fullscreen ? ", fullscreen" : "");
    return window;
}

Window::~Window() {
    if (metalView_ != nullptr) {
        SDL_Metal_DestroyView(metalView_);
    }
    if (window_ != nullptr) {
        SDL_DestroyWindow(window_);
    }
    auto& live = liveWindows();
    if (auto it = std::find(live.begin(), live.end(), this); it != live.end()) {
        live.erase(it);
        quitVideo();
    }
}

std::size_t Window::liveCount() { return liveWindows().size(); }

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

void Window::pumpEvents(const std::function<void(const SDL_Event&)>& sink) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (sink) {
            sink(event);
        }
        auto& live = liveWindows();
        if (event.type == SDL_EVENT_QUIT) {
            for (Window* w : live) {
                w->pending_.quit = true;
            }
            continue;
        }
        if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) {
            for (Window* w : live) {
                if (w->id_ == event.window.windowID) {
                    w->handleEvent(event);
                    break;
                }
            }
            continue;
        }
        if (event.type == SDL_EVENT_DROP_FILE) {
            for (Window* w : live) {
                if (w->id_ == event.drop.windowID) {
                    w->handleEvent(event);
                    break;
                }
            }
            continue;
        }
        if (event.type == dialogState().eventType && event.type != 0) {
            // Whichever window opened the dialog gets the result (one dialog at a time).
            for (Window* w : live) {
                if (w->pendingDialog_) {
                    w->handleEvent(event);
                    break;
                }
            }
        }
    }
}

void Window::handleEvent(const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        pending_.quit = true;
        pending_.closeRequested = true;
        wantsClose_ = true;
        break;
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_MAXIMIZED:
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
        updateSize();
        pending_.resized = true;
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        focused_ = true;
        pending_.focusChanged = true;
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        focused_ = false;
        pending_.focusChanged = true;
        break;
    case SDL_EVENT_DROP_FILE:
        if (event.drop.data != nullptr) {
            pending_.droppedFiles.emplace_back(event.drop.data);
        }
        break;
    default:
        if (event.type == dialogState().eventType && pendingDialog_) {
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

FrameEvents Window::takeEvents() {
    FrameEvents events = std::move(pending_);
    pending_ = FrameEvents{};
    return events;
}

FrameEvents Window::pollEvents(const std::function<void(const SDL_Event&)>& sink) {
    pumpEvents(sink);
    return takeEvents();
}

bool Window::fullscreen() const { return (SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN) != 0; }

int Window::displayIndex() const { return indexForDisplayId(SDL_GetDisplayForWindow(window_)); }

void Window::setTitle(const std::string& title) { SDL_SetWindowTitle(window_, title.c_str()); }

void Window::setFullscreen(bool on, int display) {
    if (display >= 0) {
        moveToDisplay(display);
    }
    if (on) {
        SDL_SetWindowFullscreenMode(window_, nullptr);
    }
    if (!SDL_SetWindowFullscreen(window_, on)) {
        log::warn("fullscreen {}: {}", on ? "on" : "off", SDL_GetError());
    }
    SDL_SyncWindow(window_);
    updateSize();
    pending_.resized = true;
}

void Window::moveToDisplay(int display) {
    const SDL_DisplayID id = displayIdForIndex(display);
    const int pos = static_cast<int>(SDL_WINDOWPOS_CENTERED_DISPLAY(id));
    if (!SDL_SetWindowPosition(window_, pos, pos)) {
        log::warn("move to display {}: {}", display, SDL_GetError());
    }
    SDL_SyncWindow(window_);
    updateSize();
    pending_.resized = true;
}

void Window::setAlwaysOnTop(bool on) { SDL_SetWindowAlwaysOnTop(window_, on); }

void Window::setBorderless(bool on) { SDL_SetWindowBordered(window_, !on); }

std::vector<DisplayInfo> Window::displays() {
    std::vector<DisplayInfo> out;
    if (!initVideo()) {
        return out;
    }
    int count = 0;
    SDL_DisplayID* ids = SDL_GetDisplays(&count);
    const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    if (ids != nullptr) {
        for (int i = 0; i < count; ++i) {
            DisplayInfo info;
            info.index = i;
            if (const char* name = SDL_GetDisplayName(ids[i])) {
                info.name = name;
            }
            SDL_Rect bounds{};
            if (SDL_GetDisplayBounds(ids[i], &bounds)) {
                info.x = bounds.x;
                info.y = bounds.y;
                info.width = bounds.w;
                info.height = bounds.h;
            }
            if (const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(ids[i])) {
                info.refreshRate = mode->refresh_rate;
            }
            info.scale = SDL_GetDisplayContentScale(ids[i]);
            if (info.scale <= 0.0f) {
                info.scale = 1.0f;
            }
            info.primary = ids[i] == primary;
            out.push_back(std::move(info));
        }
        SDL_free(ids);
    }
    quitVideo();
    return out;
}

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
