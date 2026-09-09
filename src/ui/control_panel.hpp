#pragma once

// Milestone 0.1 control interface (ADR-007): transport, response gains, generated parameter
// panel, analysis debug plots, and performance readout. Reads and writes the engine only
// through its public API and the parameter system.

#include "app/engine.hpp"
#include "app/render_job.hpp"
#include "app/render_settings.hpp"
#include "rendering/scene_renderer.hpp"

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
};

class ControlPanel {
public:
    // Called when the user asks to open a file. The host shows the dialog and loads the file.
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
    // Offline rendering (1.0): the host owns the settings, the job and the queue.
    app::RenderSettings* renderSettings = nullptr;
    std::function<void()> onStartRender;
    std::function<void()> onCancelRender;
    std::function<void()> onEnqueueRender;
    std::function<void()> onRunQueue;
    std::function<void()> onChooseRenderOutput;
    std::function<void(const std::string&)> onUseAudioInput; // "" = default device
    std::function<void()> onStopAudioInput;
    std::function<app::RenderProgress()> renderProgress; // empty when no job is running
    std::size_t queuedRenders = 0;
    std::string videoBackends; // describeVideoBackends()

    void draw(app::Engine& engine, const FrameStats& stats);

    [[nodiscard]] bool showDemo() const { return showDemo_; }
    [[nodiscard]] const std::string& statusMessage() const { return status_; }
    void setStatus(std::string message) { status_ = std::move(message); }

private:
    void drawTransport(app::Engine& engine);
    void drawResponse(app::Engine& engine);
    void drawParameters(app::Engine& engine);
    void drawAnalysis(app::Engine& engine);
    void drawPerformance(app::Engine& engine, const FrameStats& stats);
    void drawModulation(app::Engine& engine);
    void drawRoutesTab(app::Engine& engine);
    void drawSourcesTab(app::Engine& engine);
    void drawPresetsTab(app::Engine& engine);
    void drawShadersTab(app::Engine& engine);
    void drawSceneTab(app::Engine& engine);
    void drawTimelineTab(app::Engine& engine);
    void drawRender(app::Engine& engine);
    void drawControlTab(app::Engine& engine);

    bool showDemo_ = false;
    bool showParameters_ = true;
    bool showAnalysis_ = true;
    bool showModulation_ = true;
    bool showRender_ = false;
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
