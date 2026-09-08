#pragma once

// Milestone 0.1 control interface (ADR-007): transport, response gains, generated parameter
// panel, analysis debug plots, and performance readout. Reads and writes the engine only
// through its public API and the parameter system.

#include "app/engine.hpp"
#include "rendering/scene_renderer.hpp"

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

    bool showDemo_ = false;
    bool showParameters_ = true;
    bool showAnalysis_ = true;
    std::string status_;
    std::vector<float> plotX_;
    std::vector<float> plotY_;
    std::vector<float> waveform_;
    std::vector<float> bandHistory_[5];
    std::size_t bandHistoryHead_ = 0;
    float onsetFlash_ = 0.0f;
};

} // namespace avgen::ui
