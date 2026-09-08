#pragma once

// Application lifecycle: window + GPU + engine + UI in live mode, or a headless deterministic
// frame loop in offline mode (ADR-012). Both modes drive the same Engine and SceneRenderer.

#include "app/engine.hpp"
#include "core/error.hpp"
#include "core/log.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace avgen::gpu {
class Context;
class ShaderLibrary;
} // namespace avgen::gpu
namespace avgen::rendering {
class SceneRenderer;
}
namespace avgen::platform {
class Window;
}
namespace avgen::ui {
class ImGuiLayer;
class ControlPanel;
} // namespace avgen::ui

namespace avgen::app {

struct AppOptions {
    std::optional<std::filesystem::path> audio;
    std::optional<std::filesystem::path> scene;
    std::optional<std::filesystem::path> environment;
    bool autoplay = false;
    int frames = -1; // exit after this many frames (-1 = run until closed)
    bool headless = false;
    double offlineFps = 60.0;
    std::optional<std::filesystem::path> capture; // PPM written after the last frame
    std::uint32_t width = 1440;
    std::uint32_t height = 900;
    log::Level logLevel = log::Level::Info;
    bool showHelp = false;
};

Result<AppOptions> parseArgs(int argc, char** argv);
std::string usageText();

class Application {
public:
    Application();
    ~Application();
    [[nodiscard]] Result<void> init(const AppOptions& options, const std::filesystem::path& executablePath);
    int run();

private:
    int runLive();
    int runHeadless();
    void loadAudio(const std::filesystem::path& path);
    void loadAny(const std::filesystem::path& path);
    Result<void> captureFrame(const FrameTime& time, const std::filesystem::path& path);

    AppOptions options_;
    std::unique_ptr<platform::Window> window_;
    std::unique_ptr<gpu::Context> context_;
    std::unique_ptr<gpu::ShaderLibrary> shaders_;
    std::unique_ptr<rendering::SceneRenderer> renderer_;
    std::unique_ptr<ui::ImGuiLayer> imgui_;
    std::unique_ptr<ui::ControlPanel> panel_;
    std::unique_ptr<Engine> engine_;
};

} // namespace avgen::app
