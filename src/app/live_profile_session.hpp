#pragma once

// ADR-1090: the live-mode profile's state, between frames of the editor loop. Its own header so `application.cpp`
// (which destroys the Application, and with it the session) sees the complete type.

#include "app/application.hpp"
#include "app/live_profile.hpp"
#include "gpu/resource_stats.hpp"
#include "rendering/render_stats.hpp"
#include "rendering/scene_renderer.hpp"

#include <chrono>
#include <vector>

namespace avgen::app {

struct Application::LiveProfileSession {
    enum class Phase { WaitingForOutput, Warmup, Measure, Verify, Done };
    Phase phase = Phase::WaitingForOutput;
    SteadyStateDetector steady{30, 0.05, 2, 60};
    std::chrono::steady_clock::time_point phaseStart = std::chrono::steady_clock::now();
    int warmFrames = 0;
    int waitFrames = 0;
    LiveProfileRecord record;
    std::vector<LiveProfileFrame> frames;
    std::vector<rendering::RenderStats> stats;
    gpu::PipelineCounters compilesBefore;
    std::chrono::steady_clock::time_point measureStart;
    // Verification state.
    std::vector<std::size_t> queue; // candidate indices to verify
    std::size_t current = 0;
    int pair = 0, half = 0, frameInBlock = 0;
    std::vector<double> wall, gpu;
    std::vector<rendering::AbBlock> baseBlocks, armBlocks;
    rendering::QualitySettings baseQ, armQ;
    rendering::SceneRenderer::PassToggles baseT, armT;
    static constexpr int kSettle = 30;
    static constexpr int kBlockFrames = 150;
    static constexpr int kPairs = 2;
};

} // namespace avgen::app
