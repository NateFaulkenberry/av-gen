#pragma once

// Application lifecycle: window + GPU + engine + UI in live mode, or a headless deterministic
// frame loop in offline mode (ADR-012). Both modes drive the same Engine and SceneRenderer.

#include "app/ai_edit_sink.hpp"
#include "app/edit_system.hpp"
#include "app/camera_director.hpp"
#include "app/engine.hpp"
#include "app/viewport_camera.hpp"
#include "scene/camera_rig.hpp"
#include "app/placement.hpp"
#include "app/viewport_pick.hpp"
#include "app/job_system.hpp"
#include "app/world_builder.hpp"
#include "app/recent_files.hpp"
#include "app/render_job.hpp"
#include "app/trace_sequence.hpp"
#include "pathtrace/trace_job.hpp"
#include "app/output_manager.hpp"
#include "app/render_settings.hpp"
#include "app/render_state.hpp"
#include "app/settings.hpp"
#include "labs/case.hpp"
#include "ai/control_plane.hpp"
#include "rendering/composition_renderer.hpp"
#include "app/ui_script.hpp"
#include "core/phase_profiler.hpp"
#include "rendering/debug_visualizer.hpp"
#include "rendering/output_mapper.hpp"
#include "rendering/transform_history.hpp"
#include "ui/editor_layout.hpp"
#include "ui/output_preview.hpp"
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
    // `--song-plan <file>`: the song plan Song Mode directs to (ADR-249), for a run whose project
    // does not carry one. The same reason `--director` exists: a mode that can only be reached
    // through a GUI is a mode nobody can measure, reproduce or regression-test.
    std::optional<std::filesystem::path> songPlan;
    std::optional<std::filesystem::path> saveProject;  // write on exit
    // `--save-scene <file>`: write the composition on exit, the symmetry `--save-project`
    // has always lacked. Needed by Song Mode (ADR-249), whose camera track lives in the
    // *scene* document (ADR-245) and therefore cannot be persisted by saving the project.
    std::optional<std::filesystem::path> saveScene;
    bool autoplay = false;
    int frames = -1; // exit after this many frames (-1 = run until closed)
    std::uint64_t stressSeed = 0; // > 0: apply random UI-like actions every frame (crash reproduction)
    // Performance work (docs/application-performance.md). --ui-script drives the editor with a
    // repeatable interaction so "it feels sluggish" can be measured rather than described;
    // --profile-cpu prints the main thread's per-phase distribution on the way out.
    std::string uiScript;
    // --ui-ab <arm>[:<arm>...]: the same idea as `--ab` (ADR-113), for the main thread's frame.
    // Each arm is a `--ui-script` spec, and the run cycles through them in blocks *inside this one
    // process*, so the columns of the report were measured against each other rather than against
    // a different run on a differently-loaded machine. docs/application-performance.md §3 rule 1
    // is the reason this exists rather than a shell loop over `--ui-script`.
    std::string uiAb;
    // A multiple of 60, and that is a requirement rather than a taste: the pointer arms repeat on a
    // 60-frame cycle (select, press, drag, release), so a block length that does not divide by it
    // hands an arm a cycle chopped in half -- measured, a 90-frame block gave the gizmo arm three
    // blocks in which it never completed a press, and the arm's own probe is what said so.
    int uiAbFrames = 120; // frames per block
    int uiAbBlocks = 4;    // passes over the whole arm list
    int uiAbSettle = 12;   // frames discarded after each switch: an arm must not be charged for
                           // the deferral, the resize or the first-use pipeline of its predecessor
    // The AI control plane (ADR-094). `--ai-prompt` runs one task at start-up against whatever
    // provider Settings has configured; `--ai-script` swaps in a scripted provider so the whole
    // path -- context, agent loop, main-thread dispatch, transaction, validation -- can be run and
    // diffed deterministically without a key or a network. The script replaces only the model's
    // judgement: every tool it calls is the real tool against the real engine.
    std::string aiPrompt;
    std::optional<std::filesystem::path> aiScript;
    float canvasScale = 1.0f; // --canvas-scale: the world's share of the canvas's pixels
    // --supersample: an offline render's multiple of the output size (ADR-212). 1 = off.
    float supersample = 1.0f;
    // --particle-warmup: ADR-360's bounded particle warm-up, in frames. 0 = off, which is the
    // behaviour every render has had: a range that starts at t > 0 opens with empty particle pools
    // and the field blooms in over one lifetime. Set it to at least the longest particle lifetime
    // in the scene, in frames, and the head of the range holds what a full render would have held.
    std::uint32_t particleWarmUpFrames = 0;
    std::optional<std::string> aovs; // ADR-242: --aov, auxiliary passes beside the beauty frames
    // ADR-277: --post-stages, every intermediate the post chain rendered, as scene-linear EXRs.
    std::filesystem::path postStages;
    bool liftViewportLimits = false; // --viewport-matches-render: lift ADR-186's limits live too
    // --preview-mode: which of ADR-246's three canvas modes to open in. Unset keeps whatever the
    // settings file remembers, which is what a person's own session should do.
    std::optional<ui::PreviewViewMode> previewMode;

    bool profileCpu = false;
    std::optional<std::filesystem::path> profileCsv; // --profile-csv <file>: one row per frame
    // --latency: the interaction-latency report on exit. One record per interaction, not per frame;
    // see `src/core/interaction_latency.hpp` for why that is a different instrument rather than a
    // different view of the same one.
    bool latencyReport = false;
    std::optional<std::filesystem::path> latencyCsv; // --latency-csv <file>: one row per interaction
    // --latency-inject <kind>:<ms>. ADR-182's control: make a named interaction deliberately slower
    // by a known amount, inside the engine work it performs, and require the harness to see it.
    std::vector<std::pair<std::string, double>> latencyInject;
    bool headless = false;
    double offlineFps = 60.0;
    bool fpsGiven = false;
    std::optional<std::filesystem::path> capture; // PPM written after the last frame
    // The *editor*, captured: the swapchain after Dear ImGui has drawn into it, which is the only
    // surface the interface exists on. `--capture` re-renders the scene through `renderToImage` and
    // therefore shows the world with no interface over it -- which is why "I cannot see ImGui" has
    // been a standing limitation of every agent that has worked on this repository.
    std::optional<std::filesystem::path> captureUi;
    int captureUiFrame = 90;      // which frame to grab; late enough for a layout to settle
    bool captureUiQuit = true;    // exit once it is written, so a script is one command
    // Panels to bring to the front before the capture, in order, so a docked panel that shares a tab
    // bar with five others can be photographed. Names are the panel titles exactly as the View menu
    // spells them ("Sequence", "Auto-director", "Camera track"...).
    //
    // Focus rather than a layout change: a panel raised this way is the panel the user would see
    // after clicking its tab, in the layout they actually have, rather than one torn out of it.
    std::vector<std::string> captureUiPanels;
    // ADR-035: display one auxiliary target instead of the shaded frame; ADR-033/034: the quality
    // tier that scales shadow, occlusion and cluster sample counts.
    std::string debugTarget;
    // ADR-262. Comma-separated debug *overlays* (`beams`, `entityOrigins`, `entityBounds`,
    // `worldAxes`, `skeletons`, `entityIds`), as distinct from `--debug-target`'s auxiliary render
    // targets. They were reachable only from the World panel's Debug tab, which means they were
    // unreachable to `--render`, to `--headless` and to anybody diagnosing from a rendered frame --
    // which is every agent working on this repository and, for a filmed sequence, the owner too.
    std::string debugDraw;
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

    // ---- path tracer (ADR-351) -----------------------------------------------------------------
    //
    // Spelled like `--render`, deliberately: `--pathtrace <out.exr>` takes the output path the same
    // way, sets headless the same way, and borrows `--width`/`--height` rather than introducing a
    // parallel size vocabulary. The flags that are genuinely new are the ones a rasteriser has no
    // equivalent for -- samples per pixel, path depth, and the seed.
    std::optional<std::filesystem::path> pathtrace;
    // Optional, and every one of them, because the project now carries a `pathtrace` block and a
    // flag has to be distinguishable from its own default to override it (ADR-366). This is the
    // shape `renderSettingsFromOptions` already had: start from what the project says, then apply
    // what the command line asked for. Before, `--pathtrace out.exr` on a project authored at 512
    // samples silently traced 32, because 32 was a struct default nobody had typed.
    std::optional<std::uint32_t> ptSamples;
    std::optional<std::uint32_t> ptDepth;
    std::optional<double> ptSeconds;
    std::optional<std::uint64_t> ptSeed;
    std::optional<unsigned> ptThreads;
    std::optional<bool> ptDenoise;
    std::optional<bool> ptAovs;
    std::optional<bool> ptProbe;
    std::optional<std::filesystem::path> queue;
    std::optional<double> rangeStart, rangeEnd;
    std::optional<std::string> codec;
    std::optional<RenderOutput> renderOutput; // --output png|exr|video
    // `--render-in-app <dir|file>`: start the project's render as an in-app job, in the window,
    // the way the Render button does -- rather than as the headless job `--render` runs. The whole
    // mid-render state of the Render panel (the progress rows, the estimate, Cancel, and ADR-320's
    // frame preview) was reachable only by clicking that button, which means it could not be
    // photographed, profiled or captured by anything. ADR-262's shape exactly: the one state
    // everybody diagnoses from was the one state no tool could reach.
    std::optional<std::filesystem::path> renderInApp;
    // `--render-preview`: force ADR-320's frame preview on for this session, whatever the settings
    // file remembers. Same reason `--preview-mode` exists and is written the same way -- a capture
    // or a benchmark has to be able to say which state it is photographing rather than depending
    // on how this machine's settings happen to be left.
    bool renderPreview = false;
    std::optional<int> quality;
    std::optional<std::uint32_t> renderWidth, renderHeight;
    // Live control (1.1): --input [device], --osc-port <n>, --list-audio-devices, --list-midi
    std::optional<std::string> input;
    std::optional<int> oscPort;
    bool listAudioDevices = false;
    bool listMidi = false;
    // The Engineering Lab Suite (ADR-261). `--labs` prints the registry and exits; `--lab-case`
    // resolves `<lab>:<number>` into the flags the case is equivalent to, which is why almost
    // nothing downstream of `parseArgs` knows a lab case exists -- by the time the options leave
    // the parser a case has become a project, a size, a tier and a set of arms.
    bool listLabs = false;
    std::optional<labs::LabCase> labCase;
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
    // Asks for `path` to be opened at the top of the next frame, so the canvas can say so first.
    // See the note at the definition for why the load itself stays on the main thread.
    void loadAny(const std::filesystem::path& path);
    void servicePendingOpen();
    // The blocking half of `loadAny`.
    void performOpen(const std::filesystem::path& path);
    std::optional<std::filesystem::path> pendingOpen_;
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
    void startPathTraceFromUi();
    int runPathTrace();
    int runTraceSequence(const std::filesystem::path& projectFile, const PathTraceSettings& authored);
    int runQueue(const std::filesystem::path& queueFile);
    void startRenderFromUi();
    bool renderInAppStarted_ = false; // `--render-in-app` fires once
    // Outputs (1.2): keeps the offscreen final texture sized to the main window, (re)opens the
    // output windows from the engine's project block, and stores them back before saves.
    // `--debug-draw`'s list onto the World panel's own switches, which is where the overlays live
    // and where both the live and the offline frame loops read them from. Fails on an unknown name
    // rather than ignoring it: a diagnostic switch that silently did nothing is the defect this
    // repository keeps shipping, and it is worse than no switch at all.
    [[nodiscard]] Result<void> applyDebugDraw();
    // The overlays this frame should draw. The World panel's switches when there is a panel; the
    // `--debug-draw` set when there is not -- which is every headless render, and which is exactly
    // the case the overlays were previously unreachable in.
    [[nodiscard]] const rendering::DebugViewOptions& debugOptions() const;
    [[nodiscard]] Result<void> ensureFinalTexture(std::uint32_t width, std::uint32_t height);
    // ADR-320. Collects the newest frame the running render has tapped and puts it on the Render
    // panel. Called once per UI frame beside the job's step, and does nothing at all when the
    // toggle is off -- which is the default, and which is why a render with the panel closed is
    // the same render it always was.
    void serviceRenderPreview();
    rendering::DebugViewOptions cliDebug_{}; // `--debug-draw`, for the windowless path
    void applyOutputsFromProject();
    void storeOutputsToProject();
    void applyShare(const std::string& kind, const std::string& name); // "syphon" | "ndi" | "off"
    Result<void> captureFrame(const FrameTime& time, const std::filesystem::path& path);
    // Set once `--capture-ui` has written its file, so the loop leaves after the frame it captured
    // has been presented rather than in the middle of it.
    bool uiCaptureDone_ = false;

    AppOptions options_;
    RecentFiles recent_{{}};
    std::unique_ptr<RenderJob> job_;             // in-app render in progress

    // ---- path tracing from the UI (ADR-351) -----------------------------------------------------
    //
    // The job owns every piece of the trace's state; these are the settings the panel edits and a
    // handle to the running job. `lastPathTrace_` keeps the terminal progress after the job is
    // destroyed so the panel can still say "done in 1:04" or why it failed -- a snapshot of what
    // the job reported, never a second place the state is decided.
    std::unique_ptr<pathtrace::TraceJob> ptJob_;
    // The authored set, copied from the project at load and written back at save (ADR-366). This
    // was five loose members with no reader and no writer; `pathtrace::TraceSettings` is now built
    // from it in one place, `traceSettingsFrom`, so the persisted form and the renderer's argument
    // cannot drift apart in three call sites.
    PathTraceSettings uiPathTrace_;
    pathtrace::TraceProgress lastPathTrace_;
    // ADR-383: a path-traced SEQUENCE, on its own thread for the reason `TraceJob` has one -- it
    // is minutes of CPU and the editor has to keep its frame.
    std::unique_ptr<TraceSequence> ptSequence_;
    std::thread ptSeqThread_;
    std::atomic<bool> ptSeqDone_{true};
    std::string ptSeqError_;
    RenderSettings uiRender_;                     // the Render window's settings
    // ADR-364: how many frames the viewport has NOT drawn the world in, over the life of
    // this process. Shown in the Render panel, because a count that stays at zero while a
    // render runs is how a person finds out the suspension is not working.
    std::uint64_t viewportFramesSuspended_ = 0;
    bool viewportWasSuspended_ = false;
    std::uint64_t viewportSuspendedAtStart_ = 0;
    // ADR-186's limits, lifted in the viewport as well as in a render. A working default of false
    // because lifting them costs real frame time on a wide shot -- which is the whole reason live
    // playback has them -- but the editor showing a different world from the deliverable is a
    // worse trap than a slower editor, so it is one checkbox away.
    bool liftViewportLimits_ = false;
    std::deque<std::pair<std::filesystem::path, RenderSettings>> uiQueue_;
    std::filesystem::path renderProjectTemp_;
    RenderProgress lastRender_;
    // ADR-320's upload target: ONE 512x512 RGBA8 texture, created on the first frame that needs it
    // and then written into in place for the life of the process. Not resized per render and not
    // recreated per frame: ImGui's WGPU backend caches a bind group per texture id, and the only
    // way to release those is `ImGui_ImplWGPU_InvalidateDeviceObjects`, which throws away the
    // pipeline and the font atlas too. A preview caps at 480 px on its long axis, so every frame
    // any output shape can produce fits in one corner of this and the panel is given the uv.
    wgpu::Texture renderPreviewTexture_;
    wgpu::TextureView renderPreviewView_;
    RenderJob::FramePreview renderPreviewFrame_;
    std::uint64_t renderPreviewJobId_ = 0; // which job the panel's frame came from (never an address)
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
    // When the preview's view state was last written to the settings file (ADR-246). Throttled for
    // the same reason the editor layout is: a zoom combo dragged through its stops must not be a
    // file write per frame.
    double lastPreviewSave_ = 0.0;
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
    // One press of Left or Right: the playhead moves by the sequencer's current snap unit, or by the
    // next unit up with Shift (ADR-357). `direction` is -1 or +1.
    void nudgePlayhead(int direction, bool coarse);

    // Where the viewport is looking, and how a gesture moves it.
    //
    // **Both are mode-aware, and they are the only seam that needs to be** (ADR-391): every
    // navigation gesture in this editor -- a drag, the wheel, frame-selected, "go to camera" --
    // goes through this pair, so pointing them at the editor's own pose points all of them at it
    // at once. Under `ViewportCamera::Editor` they read and write `Composition::editorCamera`, and
    // `camera/*` is not touched at all; under `Film` they read and write `camera/position` and
    // `camera/target` exactly as they always have.
    [[nodiscard]] CameraPose viewportPose() const;
    void setViewportPose(const CameraPose& pose);
    // What the viewport is looking through *right now*, which is the user's choice narrowed by what
    // the canvas is currently for. Pushed onto the composition once per frame, before the update
    // that places the camera; see the definition for the two things that overrule the choice.
    [[nodiscard]] scene::ViewportView effectiveViewportView() const;
    void applyViewportView();
    // The user's choice. Editor by default -- navigating the view is not an edit to the film -- and
    // session state: it is never written to a project, which is what makes a render immune to it.
    scene::ViewportView viewportView_{scene::ViewportCamera::Editor, scene::kNoCamera};
    // Puts the camera in free mode, because position and target are ignored in orbit mode and a
    // gesture that silently moves nothing is indistinguishable from a dead input.
    // `deliberate` is whether the user asked for the camera in words rather than by moving the
    // mouse. Only a deliberate gesture may stand the director down while the camera is locked; see
    // `ui::viewportMayReleaseDirector` for what that protects and why it is not a preference.
    //
    // ADR-391: **no longer on the path of an ordinary drag.** It returns at once unless the frame
    // on screen is the film's, which is the only case where moving the view is an edit to it.
    void ensureFreeCamera(bool deliberate = false);
    // Locked by default, because the destructive direction is the one worth defending. Nothing
    // reads this but `ensureFreeCamera`; the Camera panel toggles it.
    bool cameraLocked_ = true;
    bool cameraLockAnnounced_ = false; // the log line is once a session, not once a frame
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
    // The per-instance LOD rung behind `DebugViewOptions::lod`, read from the cull pass's own
    // `lodIndex` buffer. Empty unless the overlay is on: it is a blocking readback per scattered
    // object and it belongs to the frame owner to decide when that is worth paying for.
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
