#pragma once

// Milestone 0.1 control interface (ADR-007): transport, response gains, generated parameter
// panel, analysis debug plots, and performance readout. Reads and writes the engine only
// through its public API and the parameter system.
//
// Since the editor shell (ADR-076) this also owns the workspace around the world: the menu bar,
// the dockspace the world shows through, the status bar, and which panels are open. The panels
// themselves are unchanged -- they are docked instead of floating, and nothing else about them
// moved.

#include <array>
#include "app/camera_director.hpp"
#include "app/engine.hpp"
#include "assets/asset_catalog.hpp"
#include "app/asset_browser.hpp"
#include "app/examples.hpp"
#include "app/render_job.hpp"
#include "app/output_manager.hpp"
#include "app/render_settings.hpp"
#include "rendering/scene_renderer.hpp"
#include "rendering/sdf_renderer.hpp"
#include "ui/composition_panel.hpp"
#include "ui/ai_panel.hpp"
#include "ui/sequence_panel.hpp"
#include "ui/settings_panel.hpp"
#include "ui/transport_bar.hpp"
#include "ui/editor_layout.hpp"
#include "ui/theme.hpp"
#include "ui/ui_logic.hpp"

#include <algorithm>
#include "ui/graph_editor.hpp"
#include "ui/help_panel.hpp"
#include "ui/world_builder_panel.hpp"
#include "ui/world_edit_panel.hpp"
#include "app/edit_system.hpp"
#include "ui/world_editor.hpp"
#include "ui/world_effects_panel.hpp"
#include "ui/world_panel.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace avgen::ui {

struct FrameStats {
    double fps = 0.0;
    double cpuFrameMs = 0.0;      // work only (excludes vsync wait)
    double frameIntervalMs = 0.0; // wall time between frames
    double gpuFrameMs = -1.0;
    std::uint32_t drawCalls = 0;
    std::uint32_t triangles = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::string adapter;
    std::string backend;
    rendering::ProceduralStats procedural;
    rendering::SdfStats sdf;
    rendering::ParticleStats particles;
};

class ControlPanel {
public:
    // Called when the user asks to open a file. The host shows the dialog and loads the file.
    // Cuts the camera to the loaded track (ADR-075). A callback rather than something this panel
    // does itself, because directing mutates the engine's timeline and the panel does not own it.
    std::function<void()> onDirectCamera;
    // Clears whatever the director installed, handing the camera back to the viewport.
    std::function<void()> onClearCameraAutomation;
    // The Auto-director's settings (section 9). Held here rather than inside the panel so the host
    // owns them and a re-cut uses what the user last chose; the panel edits them in place and calls
    // `onDirectCamera` when one changes while the camera is already directed.
    app::AutoDirectorSettings* autoDirector = nullptr;
    // What the last cut actually does, set by the host after each direct (ADR-203). A cap is a
    // request the geometry can refuse -- the camera still has to cross the ground between subjects
    // in the time the music gave the shot -- and a slider whose limit is invisible reads as a
    // slider that does not work, which is how this one was reported.
    std::string directorSummary;
    // The application's edit system (ADR-101). The menu is a consumer of it, never an owner: an
    // item asks `canExecute` to decide whether to grey itself and calls `execute` to act, so the
    // menu and the keyboard cannot come to disagree about what is available or what it does.
    app::EditSystem* edits = nullptr;
    std::function<void()> onOpenAudio;
    std::function<void()> onOpenScene;
    std::function<void()> onOpenEnvironment;
    std::function<void()> onOrbScene;
    std::function<void()> onOpenProject;
    std::function<void()> onOpenShader;
    std::function<void()> onSaveScene;
    std::function<void()> onAddGltfNode;
    std::function<void()> onAddSceneNode;
    std::function<void()> onOpenPostShader;
    // Compile error lookup for a shader layer id (from the GPU side); may be empty.
    std::function<std::string(std::uint32_t)> shaderErrorFor;
    std::function<void()> onSaveProject;      // Save As
    std::function<void()> onSaveProjectHere;  // Save to the current project path
    std::function<void()> onNewProject;
    std::function<void()> onExportBundle;
    std::function<void(const std::filesystem::path&)> onOpenRecent;
    std::vector<std::filesystem::path> recentProjects; // shown in File > Open Recent
    std::vector<app::ExampleInfo> examples;             // File > Examples
    std::function<void(const app::ExampleInfo&)> onOpenExample;
    // Asset browser (ADR-031): the host scans directories and opens what the user picks.
    std::vector<app::AssetEntry> assets;
    std::vector<assets::AssetRecord> catalogAssets;
    std::function<void()> onRescanAssets;
    std::function<void(const app::AssetEntry&)> onOpenAsset;
    // Offline rendering (1.0): the host owns the settings, the job and the queue.
    app::RenderSettings* renderSettings = nullptr;
    std::function<void()> onStartRender;
    std::function<void()> onCancelRender;
    std::function<void()> onEnqueueRender;
    std::function<void()> onRunQueue;
    std::function<void()> onChooseRenderOutput;
    std::function<void(const std::string&)> onUseAudioInput; // "" = default device
    std::function<void()> onStopAudioInput;
    // Outputs (1.2): the host owns the OutputManager; the tab edits descriptors and asks to reopen.
    app::OutputManager* outputs = nullptr;
    std::function<void()> onOutputsChanged; // re-open windows after add/remove/edit
    std::string shareStatus;                // describe() + live stats from the host
    std::function<void(const std::string&, const std::string&)> onShare; // kind ("syphon"/"ndi"/"off"), name
    std::function<app::RenderProgress()> renderProgress; // empty when no job is running
    std::size_t queuedRenders = 0;
    std::string videoBackends; // describeVideoBackends()

    // The frame to show in the canvas, as the ImGui texture id for the render target the host just
    // drew into (a WGPUTextureView, in this backend). Zero before the first frame exists.
    std::uint64_t canvasTexture = 0;
    rendering::SceneRenderer* renderer = nullptr; // selected-object diagnostics, developer-only
    // §13: a rolling window of recent frames, so the dashboard shows a distribution rather than
    // whatever this frame happened to be. A single sample of a frame time says very little on this
    // machine -- the same scene reads 10.9 ms and 13.7 ms in consecutive runs.
    static constexpr std::size_t kPerfHistory = 180;
    std::array<float, kPerfHistory> frameMsHistory_{};
    std::array<float, kPerfHistory> gpuMsHistory_{};
    std::size_t perfCursor_ = 0;
    std::uint64_t perfSamples_ = 0;

    void draw(app::Engine& engine, const FrameStats& stats);

    // Where the canvas ended up this frame. The host sizes the next frame's render target, the
    // camera's aspect and every click's coordinates from this, so it is the shell's one output.
    [[nodiscard]] const CanvasRect& canvas() const { return canvas_; }

    // Where the open-panel set is kept, and whether ImGui found a dock tree of its own. Without a
    // dock tree the shell has to build the default one before the first panel is submitted, or
    // every panel spends its first frame floating in the middle of the world.
    void setLayoutStore(std::filesystem::path file, bool imguiHasSavedLayout);
    // Writes the open-panel set now, whether or not the throttle is due. The host calls this on
    // the way out; ImGui saves its own ini from DestroyContext.
    void saveLayout();
    // Default panels open, default dock tree rebuilt on the next frame.
    void restoreDefaultLayout();

    // World authoring (ADR-031): layers, overview, inspector, states, macros, debug options.
    // The host reads `world.debug` to build the debug-draw geometry each frame.
    WorldPanel world;
    // ADR-207: the world effects library -- the effects a scene declares, their styles, and the one
    // slider that writes a beat route.
    WorldEffectsPanel worldEffects;
    // ADR-066: the recipe, Generate World, and the job monitor. Given the job system
    // and builder by the host so the panel owns no scheduling of its own.
    WorldBuilderPanel worldBuilder;
    // ADR-092: the world editor -- modes, the ghost, selection, gizmos and undo -- and the one
    // panel that shows its controls. The editor is public because the host has to ask it whether
    // the mouse belongs to it before starting a camera gesture, and has to hand it what the GPU
    // picker resolved a click to.
    WorldEditor editor;
    WorldEditPanel editPanel;
    app::JobSystem* jobs = nullptr;
    app::WorldBuilder* builder = nullptr;
    // Procedural graph editor (ADR-028); the host re-installs the graph when it changes.
    GraphEditor graphEditor;
    // The 2D composition over the frame (ADR-083): the layer stack and its inspector.
    CompositionPanel composition;
    SequencePanel sequence;
    // The application's transport (ADR-102): drawn full across the top of the Sequence panel and
    // compact in Control. One instance, so the time format chosen in one place is the one the other
    // shows.
    TransportBar transport;
    // The documentation (help-system spec). Owns its own content database and loads it lazily on
    // the first frame it is drawn, so a session that never opens Help pays nothing for it.
    HelpPanel help;
    // The AI control plane's two panels (ADR-094). Both are inert until the host installs a
    // control plane, which is what makes AI optional rather than load-bearing.
    AiPanel ai;
    SettingsPanel settings;

    // How much of the canvas's pixel count the world is actually rendered at, before being shown
    // stretched to fill it (ADR-084). One means every canvas pixel, which is what the editor has
    // always done and remains the default.
    //
    // Worth a control because of what the canvas actually is. The editor renders the world at the
    // canvas's size times the display's backing scale, and on this machine that is 2880x1166 --
    // 3.36 Mpx, against the 1.30 Mpx of the "1440x900" the renderer benchmarks quote. The world is
    // 2.6x the size everyone thinks it is, and at 0.75 or 0.5 it is 1.9 Mpx or 0.84 Mpx, which is
    // the difference between an editor whose frame is the GPU's and one whose frame is the
    // display's. Sharpness for responsiveness is a trade the person doing the work should get to
    // make, and it belongs to the editor rather than the renderer: nothing about the picture
    // changes, only how many pixels of it are computed before it is shown.
    float canvasRenderScale = 1.0f;

    [[nodiscard]] const EditorLayout& layout() const { return layout_; }
    // Writable for the host: the scripted-interaction driver (app/ui_script.hpp) toggles
    // panels through the same flags the View menu writes, so a benchmark opens and closes a
    // panel exactly as a person does rather than through a side door of its own.
    [[nodiscard]] EditorLayout& layout() { return layout_; }
    [[nodiscard]] const std::string& statusMessage() const { return status_; }
    // A status message stays on screen until something replaces it, and before this it stayed
    // *forever*: "opened night-shift.json" was still in the bar an hour later, beside a frame rate
    // that had moved on, which is how a status bar stops being read at all.
    //
    // It is not cleared on a timer either, because some of these are errors and an error a person
    // was not looking at when it arrived must not vanish. It ages instead: full strength while it
    // is news, then muted, so the bar can be scanned for *fresh* information without anything ever
    // being taken away.
    void setStatus(std::string message) {
        status_ = std::move(message);
        statusAgeSeconds_ = 0.0;
    }

    // ---- what the world is doing (the brief's sections 4 and 5) ----------------------------------
    //
    // Two different things, deliberately kept apart because they behave differently and one of them
    // cannot do what the other can.
    //
    // **Processing** is work that happens *across* frames while the editor keeps running: a
    // procedural object holding its regeneration back until a slider stops moving (ADR-084), a
    // world-generation job on the job system, a song being analysed. The canvas can show this
    // properly -- it fades in after a threshold, it animates, and the scene underneath stays live
    // and interactive. This is the case the brief's section 4 describes and the one where an
    // indicator is worth having.
    //
    // **Loading** is a single synchronous call that owns the main thread until it returns. The
    // editor is not running during it and no indicator drawn from inside it could be seen, because
    // nothing presents a frame. What the editor can honestly do is say so *before* it begins: the
    // host defers an open by one frame, paints "Opening <name>" over the canvas, and does the work
    // on the frame after. The window then holds that picture for the length of the load instead of
    // a stale editor that looks like it ignored the click.
    //
    // Which is worth stating plainly rather than dressing up: the freeze is the same length. What
    // changed is that it stops being a mystery -- and separately, `world::scatter` being threaded
    // made it about a third shorter, which is the part that is actually a fix.
    struct Loading {
        bool active = false;
        std::string what;   // "glowmere-stylized.json"
        std::string stage;  // the last stage the loader reported
        int index = 0;
        int count = 1;
    };
    Loading loading;

private:
    void drawTransport(app::Engine& engine);
    void drawResponse(app::Engine& engine);
    void drawParameters(app::Engine& engine);
    void drawAnalysis(app::Engine& engine);
    void drawPerformance(app::Engine& engine, const FrameStats& stats);
    // §13. The dashboard: where the frame's time goes, what it contains, and the arms that take a
    // subsystem away. Separate from `drawPerformance`, which is Control's compact summary line.
    void drawPerformanceDashboard(app::Engine& engine, const FrameStats& stats);
    // The forensic isolation arms, drawn in both Control and Performance. One function, so the two
    // surfaces cannot come to offer different arms -- which is how an inert checkbox appears.
    void drawForensicArms();
    void drawModulation(app::Engine& engine);
    void drawRoutesTab(app::Engine& engine);
    void drawSourcesTab(app::Engine& engine);
    void drawPresetsTab(app::Engine& engine);
    void drawShadersTab(app::Engine& engine);
    void drawSceneTab(app::Engine& engine);
    void drawTimelineTab(app::Engine& engine);
    void drawRender(app::Engine& engine);
    void drawAutoDirector(app::Engine& engine);
    void drawControlTab(app::Engine& engine);
    void drawOutputsTab(app::Engine& engine);
    void drawWorldWindow(app::Engine& engine);
    void drawAssetsWindow();
    void drawWorldBuilderWindow(app::Engine& engine);
    void drawEditWindow(app::Engine& engine);
    // Runs the world editor and draws it over the world, inside the canvas window.
    void drawViewportEditor(app::Engine& engine, const CanvasRect& rect);
    // The processing / loading indicator over the canvas. Inside the canvas window's draw list,
    // never in `ui::drawViewportOverlay` -- see the note at the definition.
    void drawCanvasActivity(app::Engine& engine, const CanvasRect& rect);
    void drawCanvasContextMenu(app::Engine& engine, const CanvasRect& rect);
    void drawGraphWindow(app::Engine& engine);
    // ---- the shell (ADR-076) ----
    void drawMenuBar(app::Engine& engine);
    void drawEditMenu(app::Engine& engine);
    void drawViewMenu();
    void drawHelpMenu();
    void drawStatusBar(app::Engine& engine, const FrameStats& stats);
    void drawPanels(app::Engine& engine, const FrameStats& stats);
    // Writes the open-panel set when it has moved and the throttle is due.
    void serviceLayoutStore();

    EditorLayout layout_;
    CanvasRect canvas_;
    // How long the world has been busy, for the canvas's processing indicator.
    ProcessingTracker processing_;
    // Right-click over the canvas: a menu only if the press did not travel, because a right-drag
    // there is the camera looking around. See `drawCanvasContextMenu`.
    ContextClickTracker canvasContextClick_;
    double statusAgeSeconds_ = 0.0;
    // Full strength while the message is news, then muted. Warm rather than red: most of what goes
    // through here is an outcome rather than a fault, and a bar that shouts at every load teaches
    // the same lesson an indicator with no threshold does.
    [[nodiscard]] ImVec4 statusColour() const {
        constexpr double kFresh = 6.0;
        constexpr double kFade = 2.0;
        const float t = static_cast<float>(
            std::clamp((statusAgeSeconds_ - kFresh) / kFade, 0.0, 1.0));
        const ImVec4 fresh(1.0f, 0.6f, 0.4f, 1.0f);
        const ImVec4 old = ImGui::ColorConvertU32ToFloat4(palette().textMuted);
        return ImVec4(fresh.x + (old.x - fresh.x) * t, fresh.y + (old.y - fresh.y) * t,
                      fresh.z + (old.z - fresh.z) * t, 1.0f);
    }
    std::filesystem::path layoutFile_;
    std::uint64_t storedLayoutSignature_ = 0;
    double lastLayoutSave_ = 0.0;
    bool rebuildLayout_ = false;
    bool firstFrame_ = true;
    int assetKind_ = 0;
    int catalogSource_ = 0;
    int catalogType_ = 0;
    char assetSearch_[96] = "";
    int newRouteSource_ = 0;
    int newRouteTarget_ = 0;
    int newSourceKind_ = 0;
    char newSourceName_[64] = "wobble";
    char presetName_[64] = "preset";
    char nodeName_[64] = "node";
    int newNodeKind_ = 1;
    int keyTarget_ = 0;
    int keyInterp_ = 1;
    int keyBase_ = 0;
    int cuePreset_ = 0;
    float cueMorph_ = 0.5f;
    char cueName_[64] = "cue";
    char learnSignal_[64] = "fader1";
    int learnTarget_ = 0;
    bool learnAsEvent_ = false;
    int inputDevice_ = 0;
    int newOutputDisplay_ = 0;
    bool newOutputFullscreen_ = true;
    char shareName_[64] = "avgen";
    int shareKind_ = 0;
    int morphA_ = 0;
    int morphB_ = 0;
    float morphT_ = 0.0f;
    std::string status_;
    std::vector<float> plotX_;
    std::vector<float> plotY_;
    std::vector<float> waveform_;
    std::vector<float> bandHistory_[5];
    std::size_t bandHistoryHead_ = 0;
    float onsetFlash_ = 0.0f;
};

} // namespace avgen::ui
