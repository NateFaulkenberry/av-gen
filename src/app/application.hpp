#pragma once

// Application lifecycle: window + GPU + engine + UI in live mode, or a headless deterministic
// frame loop in offline mode (ADR-012). Both modes drive the same Engine and SceneRenderer.

#include "app/ai_edit_sink.hpp"
#include "app/edit_system.hpp"
#include "app/camera_director.hpp"
#include "app/engine.hpp"
#include "app/viewport_camera.hpp"
#include "app/placement.hpp"
#include "app/viewport_pick.hpp"
#include "app/job_system.hpp"
#include "app/world_builder.hpp"
#include "app/recent_files.hpp"
#include "app/render_job.hpp"
#include "app/output_manager.hpp"
#include "app/render_settings.hpp"
#include "app/settings.hpp"
#include "ai/control_plane.hpp"
#include "rendering/composition_renderer.hpp"
#include "app/ui_script.hpp"
#include "core/phase_profiler.hpp"
#include "rendering/output_mapper.hpp"
#include "rendering/transform_history.hpp"
#include "ui/editor_layout.hpp"
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

union SDL_Event;

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
    // Direct the camera from the loaded track's musical structure and the world's heroes, replacing
    // whatever camera automation the project carries (ADR-075).
    bool directCamera = false;
    // `--director k=v,...`: the Auto-director panel's settings, from the command line. The panel is
    // the only other way to set these, so before this flag nothing about the director could be
    // measured, reproduced or regression-tested without a human at a GUI.
    std::string directorSettings;
    std::optional<std::filesystem::path> saveProject;  // write on exit
    bool autoplay = false;
    int frames = -1; // exit after this many frames (-1 = run until closed)
    std::uint64_t stressSeed = 0; // > 0: apply random UI-like actions every frame (crash reproduction)
    // Performance work (docs/application-performance.md). --ui-script drives the editor with a
    // repeatable interaction so "it feels sluggish" can be measured rather than described;
    // --profile-cpu prints the main thread's per-phase distribution on the way out.
    std::string uiScript;
    // The AI control plane (ADR-094). `--ai-prompt` runs one task at start-up against whatever
    // provider Settings has configured; `--ai-script` swaps in a scripted provider so the whole
    // path -- context, agent loop, main-thread dispatch, transaction, validation -- can be run and
    // diffed deterministically without a key or a network. The script replaces only the model's
    // judgement: every tool it calls is the real tool against the real engine.
    std::string aiPrompt;
    std::optional<std::filesystem::path> aiScript;
    float canvasScale = 1.0f; // --canvas-scale: the world's share of the canvas's pixels

    bool profileCpu = false;
    std::optional<std::filesystem::path> profileCsv; // --profile-csv <file>: one row per frame
    bool headless = false;
    double offlineFps = 60.0;
    bool fpsGiven = false;
    std::optional<std::filesystem::path> capture; // PPM written after the last frame
    // ADR-035: display one auxiliary target instead of the shaded frame; ADR-033/034: the quality
    // tier that scales shadow, occlusion and cluster sample counts.
    std::string debugTarget;
    std::string qualityTier;
    // ADR-186: "tier" | "live" | "unlimited" -- which distance-based detail reductions a `--render`
    // runs under. Empty leaves whatever the project asked for.
    std::string renderLimits;
    // A/B attribution: comma-separated phases to switch off for this run
    // (shadows, ao, volume, post). Logged at start-up so a run's own output proves which arm it is.
    std::string disablePasses;
    // ADR-113, the A/B protocol. `abArm` names one phase to compare against the baseline by running
    // A/B/A/B *in this process* -- interleaved, so a machine that drifts during the session charges
    // the drift to both arms instead of to the change. Cross-session comparison is not offered:
    // the audit found two Glowmere figures 28% apart that run-to-run variance cannot explain, so a
    // number from a previous run of this program is not a baseline.
    std::string abArm;
    int abBlocks = 2; // A/B pairs; one pair cannot show whether the difference held
    // ADR-142: quality arms (ADR-117) applied to an *ordinary* run, comma separated, so that a
    // frame can be captured and looked at under the same arm the A/B timed. Spec 50 requires a
    // quality reduction be inspected and not only timed, and until this existed an arm could only
    // be reached inside `--ab`, which captures nothing.
    std::string qualityArms;
    // Where to write the machine-readable record of this run. The human log is unchanged.
    std::optional<std::filesystem::path> benchJson;
    // ADR-114: compute froxel-grid occupancy every frame. CPU work inside the measured frames, so
    // it perturbs the wall clock and the record it writes says so.
    bool clusterStats = false;
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
    // Whether --size was given. Without it the editor opens maximised (ADR-076); with it the size
    // asked for is the size you get, because --size is how a screenshot or a bug report is made
    // reproducible and a window that silently ignored it would not be.
    bool sizeGiven = false;
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
    // The newest input event consumed this frame, on SDL's nanosecond clock. Subtracting it from
    // the clock at present time gives input-to-present latency, which is the number "the UI feels
    // sluggish" is actually about -- a frame rate says how often the picture changes, not how old
    // the picture is. Zero when no input arrived this frame.
    std::uint64_t newestInputNs_ = 0;
    // A resize that arrived during the late input pump, i.e. after this frame's surface was
    // already configured and its image already acquired. It has to be carried to the next
    // frame rather than acted on here, and carried explicitly: the FrameEvents that held it
    // is a per-frame local, so without this the resize would simply be dropped.
    bool pendingResize_ = false;
    std::map<std::uint32_t, std::uint64_t> uiEventTypes_;
    // Offline rendering: settings from the project + CLI overrides; a job runs to completion
    // headless, or a few frames per UI frame in the live app.
    [[nodiscard]] RenderSettings renderSettingsFromOptions() const;
    // ADR-066: load a recipe, compose it and install it. Runs the composition inline rather than
    // through the job system because at start-up there is no frame to keep responsive, and blocking
    // for the tens of milliseconds this takes is simpler than deferring it a frame.
    [[nodiscard]] Result<void> generateWorldFromRecipe(const std::filesystem::path& path);
    // Opens anything: a project, a composition, a mesh, a shader -- or a world recipe, which is
    // generated rather than loaded and so cannot go through `Engine::loadFile`.
    [[nodiscard]] Result<void> openAny(const std::filesystem::path& path);

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
    // The selected entity's recent transforms, cameras and screen positions (forensics 4.3/9.3).
    // Recorded here rather than in the renderer because it is a *reading* of frames, not part of
    // drawing one: the renderer publishes its diagnosis, and this keeps the last few seconds of it
    // so "did it move or did the camera" has an answer.
    rendering::TransformHistory transformHistory_;
    // The 2D composition over the finished frame (ADR-083). Installed on the renderer as its one
    // overlay hook; fed the engine's layer stack and timeline clock once per frame.
    std::unique_ptr<rendering::CompositionRenderer> compositor_;
    std::unique_ptr<ui::ImGuiLayer> imgui_;
    // The application's one edit history, clipboard and action dispatch (ADR-101). Owned here
    // rather than by any editor: an editor that owns a history can only answer for its own edits,
    // and the user's question is "take back the last thing I did".
    EditSystem edits_;
    // Bridges the AI control plane's transactions into the one history (ADR-101). Declared after
    // `edits_` and before `ai_`, so it outlives the control plane that points at it.
    std::unique_ptr<EditHistoryTransactionSink> aiEditSink_;
    std::unique_ptr<ui::ControlPanel> panel_;

    // ---- viewport interaction (ADR-068) --------------------------------------------------------
    // Mouse gestures over the scene, and what the last click selected. The camera pose itself is
    // *not* stored here: it lives in the `camera/position` and `camera/target` parameters, which
    // already save, load, automate and route, and a copy beside them would be a second source of
    // truth for the same two vectors.
    // Where the editor put the world this frame (ADR-076). Everything that maps between the screen
    // and the scene reads it: how big to render, what aspect the camera has, and where a click
    // landed. Copied from the panel once per frame rather than read through it per event, so the
    // event handler does not reach into UI state and a scripted caller can run without a panel.
    // The main thread's own cost, phase by phase. Not GPU time and never reported as such: see
    // core/phase_profiler.hpp. Always collected -- a scope is two steady_clock reads and an add,
    // which measured below the clock's own resolution against an uninstrumented build -- so the
    // editor can show the distribution live and any run can be asked what it spent its frames on.
    core::PhaseProfiler cpuProfile_;
    UiScript uiScript_;

    ui::CanvasRect canvas_;
    std::uint32_t renderWidth_ = 0;   // canvas size in framebuffer pixels; what the renderer is sized to
    std::uint32_t renderHeight_ = 0;
    std::uint32_t pendingWidth_ = 0;  // a canvas size waiting to settle before it is acted on
    std::uint32_t pendingHeight_ = 0;
    int pendingFrames_ = 0;

    ViewportGesture viewportGesture_ = ViewportGesture::None;
    glm::vec2 viewportLastMouse_{0.0f};
    glm::vec2 viewportDragTotal_{0.0f};   // how far this drag has travelled, to tell a click from a drag
    // A click asks a question about the frame that was on screen when it happened, so it is
    // answered after the next frame is drawn rather than inside the event handler, where the
    // identifier target still holds the previous frame and the camera may already have moved.
    bool viewportPickPending_ = false;
    glm::uvec2 viewportPickPixel_{0u};
    // The modifiers the click carried, held with it: the pick is answered a frame later and
    // SDL_GetModState by then is whatever the keyboard happens to be doing, not what was held down
    // when the button went up. Shift adds to the selection; alt reaches inside a group.
    bool viewportPickAdditive_ = false;
    bool viewportPickInsideGroup_ = false;
    std::string viewportSelectedNode_;
    glm::vec3 viewportPickPosition_{0.0f};
    bool viewportFreeModeAnnounced_ = false;

    // The armed placement tool (ADR-069). Empty asset id means the click selects instead of placing,
    // which is the default: a viewport that places something every time you click on it is a
    // viewport you cannot look around in.
    PlacementSettings placement_;
    std::string placementAssetId_;
    std::uint32_t placementSeed_ = 1u;

    // Places whatever the tool is armed with at a picked surface. Returns how many nodes it made.
    //
    // Superseded for interactive use by the world editor (ADR-092), which plans and commits inside
    // the UI pass against the CPU ground probe and so can show a ghost before the click. Kept
    // because it is the one entry point a scripted or headless caller can place through without a
    // pointer, and because `--ui-script`'s placement arm drives it.
    std::size_t placeAt(glm::vec3 position, glm::vec3 normal);
    // The editor's keyboard shortcuts (§43). Returns true when the key was the editor's, so the
    // application's own bindings do not also fire on it.
    bool handleEditorShortcut(const SDL_Event& event);
    // The transport's keys (ADR-102): space, home/end, the arrows and the loop toggle. Separate from
    // the editor's because they are allowed to repeat and because they mean the same thing whichever
    // panel has the focus.
    bool handleTransportShortcut(const SDL_Event& event);

    // Reads `camera/position` and `camera/target`. Returns the scene camera's own pose when the
    // parameters are missing, so a gesture over a scene without them still does something sensible.
    [[nodiscard]] CameraPose viewportPose() const;
    void setViewportPose(const CameraPose& pose);
    // Puts the camera in free mode, because position and target are ignored in orbit mode and a
    // gesture that silently moves nothing is indistinguishable from a dead input.
    void ensureFreeCamera();
    // One raw SDL event on its way to ImGui, the viewport and the shortcut table.
    void handleInputEvent(const SDL_Event& event);
    void handleViewportEvent(const SDL_Event& event);
    // Scripted mouse input for checking the viewport end to end; see the definition.
    void runViewportProbe(int frameIndex);
    void serviceViewportPick();
    // Cuts the camera to the loaded track. Main thread; mutates the timeline.
    Result<void> directCameraFromTrack();
    // ADR-064/066: one job system for the application, and the world builder that submits to it.
    // Declared after the panel so they outlive it during teardown -- the panel holds raw pointers
    // to both, and a job finishing while the panel is being destroyed would otherwise be a race.
    std::unique_ptr<JobSystem> jobs_;
    std::unique_ptr<WorldBuilder> worldBuilder_;
    std::unique_ptr<Engine> engine_;
    FileWatcher engineShaderWatcher_{0.5};

    // ---- the AI control plane (ADR-094) ---------------------------------------------------------
    // Optional in the strongest sense: null in a headless run, and with no provider configured the
    // rest of the application behaves exactly as it did. The frame loop's only obligation to it is
    // `pump()`, which runs queued tool bodies on this thread under a time budget -- everything
    // else happens on a job worker. Reset first in the destructor, because it cancels the running
    // task and waits for it, and the panel holds a raw pointer to it.
    std::unique_ptr<ai::ControlPlane> ai_;
    AppSettings settings_;
    std::filesystem::path settingsPath_;
    void saveSettings();
    void initControlPlane();
    // Runs `--ai-prompt` / `--ai-script` to completion on this thread, servicing the control
    // plane's queue the way the frame loop would. Returns a process exit code.
    int runAiTask();
    // The frame numbers the performance tool reports, copied once per frame from the same values
    // the status bar shows. Plain doubles rather than a `ui::FrameStats`, so this header does not
    // have to pull the UI layer in for four numbers.
    double lastCpuFrameMs_ = 0.0;
    double lastFrameIntervalMs_ = 0.0;
    double lastFps_ = 0.0;
    double lastGpuFrameMs_ = -1.0;
    std::uint64_t lastTransportDiscontinuity_ = 0;
    // The shot the director cut, so starring an object re-cuts it (see `refreshDirection`).
    DirectorState cameraDirection_;
    // The Auto-director's controls have two homes and neither can be told when the other moves: the
    // panel edits `cameraDirection_.settings` in place, and `Engine::autoDirector()` is the copy
    // `saveProject` writes and `loadProject` replaces. One function reconciles them once a frame,
    // which is why no load site has to remember to -- a project opened from the command line, from
    // the menu or by a file drop all arrive the same way, as the engine's copy no longer matching
    // what was last seen.
    void syncDirectorSettings();
    AutoDirectorSettings lastDirectorSync_;
};

} // namespace avgen::app
