#pragma once

// Application lifecycle: window + GPU + engine + UI in live mode, or a headless deterministic
// frame loop in offline mode (ADR-012). Both modes drive the same Engine and SceneRenderer.

#include "app/engine.hpp"
#include "app/recent_files.hpp"
#include "app/render_job.hpp"
#include "app/output_manager.hpp"
#include "app/render_settings.hpp"
#include "rendering/output_mapper.hpp"
#include "share/texture_share.hpp"

#include <webgpu/webgpu_cpp.h>
#include "core/error.hpp"
#include "core/file_watcher.hpp"
#include "core/log.hpp"

#include <cstdint>
#include <filesystem>
#include <deque>
#include <memory>
#include <optional>
#include <map>
#include <string>
#include <utility>
#include <vector>

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
    std::optional<std::filesystem::path> composition; // scene file (avgen-scene JSON)
    std::optional<std::filesystem::path> bundle;      // --export-bundle <dir>
    std::vector<std::pair<std::filesystem::path, bool>> shaders; // (file, isPost)
    std::optional<std::filesystem::path> project;      // load at start-up
    // ADR-066: compose a world from a recipe at start-up. The same path the World Builder
    // panel takes, reachable without a window so it can be rendered and diffed like anything else.
    std::optional<std::filesystem::path> generateRecipe;
    std::optional<std::filesystem::path> saveProject;  // write on exit
    bool autoplay = false;
    int frames = -1; // exit after this many frames (-1 = run until closed)
    std::uint64_t stressSeed = 0; // > 0: apply random UI-like actions every frame (crash reproduction)
    bool headless = false;
    double offlineFps = 60.0;
    bool fpsGiven = false;
    std::optional<std::filesystem::path> capture; // PPM written after the last frame
    // ADR-035: display one auxiliary target instead of the shaded frame; ADR-033/034: the quality
    // tier that scales shadow, occlusion and cluster sample counts.
    std::string debugTarget;
    std::string qualityTier;
    // A/B attribution: comma-separated phases to switch off for this run
    // (shadows, ao, volume, post). Logged at start-up so a run's own output proves which arm it is.
    std::string disablePasses;
    // Offline rendering (1.0): --render <dir|video file>, --range a:b, --codec, --quality, --queue <file>
    std::optional<std::filesystem::path> render;
    std::optional<std::filesystem::path> queue;
    std::optional<double> rangeStart, rangeEnd;
    std::optional<std::string> codec;
    std::optional<RenderOutput> renderOutput; // --output png|exr|video
    std::optional<int> quality;
    std::optional<std::uint32_t> renderWidth, renderHeight;
    // Live control (1.1): --input [device], --osc-port <n>, --list-audio-devices, --list-midi
    std::optional<std::string> input;
    std::optional<int> oscPort;
    bool listAudioDevices = false;
    bool listMidi = false;
    std::vector<std::string> outputs; // --output <display>[:fullscreen|:WxH]
    std::optional<std::string> syphon; // --syphon <name>
    std::optional<std::string> example; // --example <name>
    std::optional<std::string> ndi;    // --ndi <name>
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
    double lastEngineUpdateMs_ = 0.0; // CPU cost of rebuilding the scene, per frame
    int runLive();
    int runHeadless();
    void loadAudio(const std::filesystem::path& path);
    void loadAny(const std::filesystem::path& path);
    void rememberProject(const std::filesystem::path& path); // recent list + window title
    // Input diagnostics (AVGEN_UI_SELFTEST=1): raw SDL mouse events seen this run.
    std::uint64_t uiMotionEvents_ = 0;
    std::uint64_t uiButtonEvents_ = 0;
    std::uint64_t uiFilteredEvents_ = 0;
    bool uiSelfTestEvents_ = false;
    std::map<std::uint32_t, std::uint64_t> uiEventTypes_;
    // Offline rendering: settings from the project + CLI overrides; a job runs to completion
    // headless, or a few frames per UI frame in the live app.
    [[nodiscard]] RenderSettings renderSettingsFromOptions() const;
    // ADR-066: load a recipe, compose it and install it. Runs the composition inline rather than
    // through the job system because at start-up there is no frame to keep responsive, and blocking
    // for the tens of milliseconds this takes is simpler than deferring it a frame.
    [[nodiscard]] Result<void> generateWorldFromRecipe(const std::filesystem::path& path);

    [[nodiscard]] Result<std::unique_ptr<RenderJob>> makeRenderJob(const std::filesystem::path& projectFile,
                                                                   RenderSettings settings);
    int runQueue(const std::filesystem::path& queueFile);
    void startRenderFromUi();
    // Outputs (1.2): keeps the offscreen final texture sized to the main window, (re)opens the
    // output windows from the engine's project block, and stores them back before saves.
    [[nodiscard]] Result<void> ensureFinalTexture(std::uint32_t width, std::uint32_t height);
    void applyOutputsFromProject();
    void storeOutputsToProject();
    void applyShare(const std::string& kind, const std::string& name); // "syphon" | "ndi" | "off"
    Result<void> captureFrame(const FrameTime& time, const std::filesystem::path& path);

    AppOptions options_;
    RecentFiles recent_{{}};
    std::unique_ptr<RenderJob> job_;             // in-app render in progress
    RenderSettings uiRender_;                     // the Render window's settings
    std::deque<std::pair<std::filesystem::path, RenderSettings>> uiQueue_;
    std::filesystem::path renderProjectTemp_;
    RenderProgress lastRender_;
    std::unique_ptr<rendering::OutputMapper> mapper_;
    OutputManager outputs_;
    share::TextureShare share_;
    wgpu::Texture finalTexture_;
    wgpu::TextureView finalView_;
    std::uint32_t finalWidth_ = 0;
    std::uint32_t finalHeight_ = 0;
    std::unique_ptr<platform::Window> window_;
    std::unique_ptr<gpu::Context> context_;
    std::unique_ptr<gpu::ShaderLibrary> shaders_;
    std::unique_ptr<rendering::SceneRenderer> renderer_;
    std::unique_ptr<ui::ImGuiLayer> imgui_;
    std::unique_ptr<ui::ControlPanel> panel_;
    std::unique_ptr<Engine> engine_;
    FileWatcher engineShaderWatcher_{0.5};
};

} // namespace avgen::app
