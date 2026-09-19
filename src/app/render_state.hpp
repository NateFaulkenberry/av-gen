#pragma once

// What the application is doing, and what the viewport is therefore allowed to cost (ADR-364).
//
// Until this existed there was no render state in this program at all: no enum, no `paused`, no
// `needsRedraw`, no idle throttle, no frame limiter. The only three behaviours that ever shed work
// were a 50 ms sleep when the window is minimised, a 16 ms backoff on a failed swapchain acquire,
// and `PresentMode::Fifo`. An in-app render was pumped from the main loop as `job_->step(4, 0.010)`
// while the world went on being drawn behind it at full cost, into the same device the render was
// using -- and the Render panel advertised that as a feature, because for an author iterating it
// is one.
//
// Header-only and free of ImGui, the GPU and the engine, for the reason `output_preview.hpp` is:
// the decision is four lines and it is the part that will be got wrong, so it belongs where a test
// can reach it without a device.

#include <cstdint>

namespace avgen::app {

// Deliberately NOT called "offline": ADR-351 spends that word on `QualityTier::Offline` and on the
// ADR-020 batch pipeline, and a third meaning in a state enum is the collision that ADR exists to
// prevent. This is what the program is *doing*, not how good a frame is.
enum class RenderActivity : std::uint8_t {
    Interactive,       // an author at the editor; the viewport is the product
    OfflineRaster,     // app::RenderJob is stepping; the deliverable is the product
    OfflinePathTrace,  // pathtrace::TraceJob is running; CPU-only, but the viewport still costs
};

[[nodiscard]] constexpr const char* renderActivityName(RenderActivity a) {
    switch (a) {
    case RenderActivity::OfflineRaster: return "offline render";
    case RenderActivity::OfflinePathTrace: return "path trace";
    case RenderActivity::Interactive: break;
    }
    return "interactive";
}

struct ViewportPolicy {
    // Record and submit the world's passes into the viewport's target. False leaves the last frame
    // on the canvas, which is honest -- it is the last frame -- and the panel has to say so.
    bool drawWorld = true;
    // ALWAYS true, and a test asserts it for every combination. Progress and Cancel live in the UI,
    // and a render that cannot be cancelled because the interface stopped being drawn is a worse
    // failure than the one this file exists to fix. The field is here so that invariant is written
    // down somewhere a change has to argue with, rather than being true by accident.
    bool drawUi = true;
};

[[nodiscard]] constexpr ViewportPolicy viewportPolicyFor(RenderActivity activity, bool suspendSetting) {
    ViewportPolicy p;
    p.drawWorld = activity == RenderActivity::Interactive || !suspendSetting;
    p.drawUi = true;
    return p;
}

} // namespace avgen::app
