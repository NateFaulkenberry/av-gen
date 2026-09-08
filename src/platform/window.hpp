#pragma once

// SDL3 window (ADR-002). The only file that includes SDL outside the ImGui glue. Exposes the
// CAMetalLayer for WebGPU surface creation and a minimal event summary for the application.

#include "core/error.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

struct SDL_Window;
union SDL_Event;

namespace avgen::platform {

struct WindowDesc {
    std::string title = "avgen";
    std::uint32_t width = 1440;  // logical points
    std::uint32_t height = 900;
    bool resizable = true;
    bool highDpi = true;
};

struct FrameEvents {
    bool quit = false;
    bool resized = false;
    std::vector<std::string> droppedFiles;
};

class Window {
public:
    static Result<std::unique_ptr<Window>> create(const WindowDesc& desc);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Pumps the event queue. `sink` receives every raw event (for the UI layer) before the
    // window interprets it.
    FrameEvents pollEvents(const std::function<void(const SDL_Event&)>& sink);

    [[nodiscard]] std::uint32_t pixelWidth() const { return pixelWidth_; }
    [[nodiscard]] std::uint32_t pixelHeight() const { return pixelHeight_; }
    [[nodiscard]] float pixelScale() const { return pixelScale_; } // pixels per logical point
    [[nodiscard]] bool minimised() const { return pixelWidth_ == 0 || pixelHeight_ == 0; }

    [[nodiscard]] void* metalLayer() const { return metalLayer_; } // CAMetalLayer*
    [[nodiscard]] SDL_Window* handle() const { return window_; }

    void setTitle(const std::string& title);

    enum class DialogKind { Audio, Scene, Environment, Any };
    // Opens a native file-open dialog asynchronously; `onChosen` runs on the main thread from
    // pollEvents() with the selected path (empty string on cancel).
    void openFileDialog(DialogKind kind, std::function<void(std::string)> onChosen);
    // Native save dialog (JSON projects). Same delivery contract as openFileDialog.
    void saveFileDialog(std::function<void(std::string)> onChosen);

private:
    Window() = default;
    void updateSize();

    SDL_Window* window_ = nullptr;
    void* metalView_ = nullptr;  // SDL_MetalView
    void* metalLayer_ = nullptr;
    std::uint32_t pixelWidth_ = 0;
    std::uint32_t pixelHeight_ = 0;
    float pixelScale_ = 1.0f;
    std::function<void(std::string)> pendingDialog_;
    std::uint32_t dialogEventType_ = 0;
};

} // namespace avgen::platform
