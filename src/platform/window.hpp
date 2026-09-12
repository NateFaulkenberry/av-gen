#pragma once

// SDL3 window (ADR-002). The only file that includes SDL outside the ImGui glue. Exposes the
// CAMetalLayer for WebGPU surface creation and a minimal event summary for the application.
// Since milestone 1.2 several windows may be alive at once (projection outputs): the process-wide
// SDL queue is pumped once per frame by Window::pumpEvents, which routes each window event to the
// Window it belongs to; pollEvents keeps the single-window contract for the primary window.

#include "core/error.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

struct SDL_Window;
union SDL_Event;

namespace avgen::platform {

// Per-user writable directory for avgen settings (SDL_GetPrefPath); empty when unavailable.
[[nodiscard]] std::filesystem::path preferencesDirectory();

struct WindowDesc {
    std::string title = "avgen";
    std::uint32_t width = 1440;  // logical points
    std::uint32_t height = 900;
    bool resizable = true;
    bool highDpi = true;
    int displayIndex = -1;       // index into Window::displays(); -1 = the default display
    bool fullscreen = false;     // borderless desktop fullscreen on that display
    // Open zoomed to the display's work area (ADR-076). Asked for at creation rather than by
    // zooming afterwards, because a window that appears at one size and jumps to another has
    // already configured its surface at the first size and has to do it twice.
    bool maximised = false;
    bool borderless = false;
    std::optional<int> x;        // position in points (global desktop space); unset = centred on the display
    std::optional<int> y;
    bool alwaysOnTop = false;
};

struct DisplayInfo {
    int index = 0;               // position in Window::displays(); stable while the display set is unchanged
    std::string name;
    int x = 0;                   // bounds in points, global desktop space
    int y = 0;
    int width = 0;
    int height = 0;
    float refreshRate = 0.0f;    // Hz; 0 when unknown
    float scale = 1.0f;          // content scale (points -> pixels)
    bool primary = false;
};

struct FrameEvents {
    bool quit = false;           // SDL_EVENT_QUIT (application-wide) or this window's close request
    bool resized = false;
    bool closeRequested = false; // this window's close button / Cmd-W
    bool focusChanged = false;
    std::vector<std::string> droppedFiles;
};

class Window {
public:
    static Result<std::unique_ptr<Window>> create(const WindowDesc& desc);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Pumps the whole SDL queue: `sink` receives every raw event (for the UI layer) before the
    // event is routed to the window it belongs to (resize, close, focus, drops; quit goes to
    // every window). Call once per frame, then read each window with takeEvents().
    static void pumpEvents(const std::function<void(const SDL_Event&)>& sink);
    // Returns and clears the events routed to this window since the last call.
    FrameEvents takeEvents();
    // pumpEvents + takeEvents for this window (the primary window's per-frame call).
    FrameEvents pollEvents(const std::function<void(const SDL_Event&)>& sink);

    [[nodiscard]] std::uint32_t pixelWidth() const { return pixelWidth_; }
    [[nodiscard]] std::uint32_t pixelHeight() const { return pixelHeight_; }
    [[nodiscard]] float pixelScale() const { return pixelScale_; } // pixels per logical point
    [[nodiscard]] bool minimised() const { return pixelWidth_ == 0 || pixelHeight_ == 0; }
    [[nodiscard]] bool wantsClose() const { return wantsClose_; }
    [[nodiscard]] bool focused() const { return focused_; }
    [[nodiscard]] bool fullscreen() const;
    [[nodiscard]] std::uint32_t id() const { return id_; } // SDL_WindowID
    // Index of the display the window is on (into displays()), -1 when unknown.
    [[nodiscard]] int displayIndex() const;

    [[nodiscard]] void* metalLayer() const { return metalLayer_; } // CAMetalLayer*
    [[nodiscard]] SDL_Window* handle() const { return window_; }

    void setTitle(const std::string& title);
    // Borderless desktop fullscreen (SDL's fullscreen with no exclusive mode). A display index
    // >= 0 moves the window there first.
    void setFullscreen(bool on, int displayIndex = -1);
    // Centres the window on the given display (also while fullscreen: it re-targets that display).
    void moveToDisplay(int displayIndex);
    void setAlwaysOnTop(bool on);
    void setBorderless(bool on);

    // Connected displays in SDL order. Empty when no video subsystem is available (headless CI).
    static std::vector<DisplayInfo> displays();
    // Windows currently alive in this process.
    static std::size_t liveCount();

    enum class DialogKind { Audio, Scene, Environment, Shader, Any };
    // Opens a native file-open dialog asynchronously; `onChosen` runs on the main thread from
    // pollEvents() with the selected path (empty string on cancel).
    void openFileDialog(DialogKind kind, std::function<void(std::string)> onChosen);

    // What a save dialog is saving, which decides the extension the platform offers and appends.
    //
    // It is not cosmetic. macOS adds the filter's extension to whatever name is typed, so a render
    // output chosen through the project filter came back as "my-take.json" -- and the render
    // settings then read that extension, saw no video in it, and switched the output back to a PNG
    // sequence in front of the person who had just chosen Video.
    enum class SaveKind { Project, Video };
    // Native save dialog. Same delivery contract as openFileDialog.
    void saveFileDialog(SaveKind kind, std::function<void(std::string)> onChosen);
    // Native folder picker, for the outputs that are a directory rather than a file -- an image
    // sequence writes many files into one place, so asking for a file name is asking the wrong
    // question.
    void chooseFolderDialog(std::function<void(std::string)> onChosen);

private:
    Window() = default;
    void updateSize();
    void handleEvent(const SDL_Event& event);

    SDL_Window* window_ = nullptr;
    void* metalView_ = nullptr;  // SDL_MetalView
    void* metalLayer_ = nullptr;
    std::uint32_t id_ = 0;
    std::uint32_t pixelWidth_ = 0;
    std::uint32_t pixelHeight_ = 0;
    float pixelScale_ = 1.0f;
    bool wantsClose_ = false;
    bool focused_ = false;
    FrameEvents pending_;
    std::function<void(std::string)> pendingDialog_;
};

} // namespace avgen::platform
