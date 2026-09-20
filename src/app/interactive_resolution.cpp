#include "app/interactive_resolution.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace avgen::app {

void InteractiveResolution::configure(const InteractiveResolutionSettings& s) {
    settings_ = s;
    settings_.floorRung = std::min(settings_.floorRung, kRenderScaleRungs.size() - 1);
    settings_.windowFrames = std::clamp(settings_.windowFrames, 1, static_cast<int>(gpu_.size()));
    settings_.dwellFrames = std::max(1, settings_.dwellFrames);
    if (!settings_.enabled) {
        reset();
    }
    rung_ = std::min(rung_, settings_.floorRung);
}

void InteractiveResolution::reset() {
    rung_ = 0;
    sinceDecision_ = 0;
    count_ = 0;
    cursor_ = 0;
}

double InteractiveResolution::medianGpu() const {
    // The median of the window, not the mean: one 200 ms frame where a shader compiled or another
    // agent's process landed is not evidence that the world is too big, and a mean would let it
    // move the rung on its own.
    const std::size_t n = std::min(count_, static_cast<std::size_t>(settings_.windowFrames));
    if (n == 0) {
        return -1.0;
    }
    std::vector<double> v;
    v.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        v.push_back(gpu_[(cursor_ + gpu_.size() - 1 - i) % gpu_.size()]);
    }
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(n / 2), v.end());
    return v[n / 2];
}

InteractiveResolution::Decision InteractiveResolution::note(double gpuMs, double wallMs) {
    Decision d{rung_, false};
    if (!settings_.enabled) {
        return d;
    }
    ++stats_.framesSeen;
    if (rung_ > 0) {
        ++stats_.framesReduced;
    }
    // A frame the GPU timeline could not report is not a free frame. FrameTimeline has no completed
    // frame for the first two or three of a session and none at all on a build without timestamp
    // queries, and counting those as 0 ms would walk the ladder straight back to the top.
    if (!(gpuMs >= 0.0)) {
        return d;
    }
    gpu_[cursor_] = gpuMs;
    wall_[cursor_] = wallMs;
    cursor_ = (cursor_ + 1) % gpu_.size();
    ++count_;
    ++sinceDecision_;

    const auto window = static_cast<std::size_t>(settings_.windowFrames);
    if (sinceDecision_ < settings_.dwellFrames || count_ < window) {
        return d;
    }
    const double gpu = medianGpu();
    if (!(gpu > 0.0)) {
        return d;
    }
    const double budget = settings_.budgetMs;
    // The pixel ratio between two rungs. `renderScale` is linear, so the ratio of pixels is the
    // ratio of the squares; `SceneRenderer::resize` rounds each axis to even, which moves this by
    // well under a percent and is not worth modelling here.
    const auto pixelRatio = [](std::size_t from, std::size_t to) {
        const double a = kRenderScaleRungs[to];
        const double b = kRenderScaleRungs[from];
        return (a * a) / (b * b);
    };

    if (gpu > budget && rung_ < settings_.floorRung) {
        // The GPU has to be what the frame is waiting for. When the wall clock is much longer than
        // the GPU frame the binding constraint is the main thread, and a smaller world would cost
        // image quality to change nothing -- §2 again: the profiling outranks the proposed
        // solution, and the proposed solution is a GPU lever.
        const std::size_t n = std::min(count_, window);
        std::vector<double> w;
        w.reserve(n);
        for (std::size_t i = 0; i < n; ++i) {
            w.push_back(wall_[(cursor_ + wall_.size() - 1 - i) % wall_.size()]);
        }
        std::nth_element(w.begin(), w.begin() + static_cast<std::ptrdiff_t>(n / 2), w.end());
        const double wall = w[n / 2];
        if (wall > 0.0 && gpu < wall * settings_.gpuShareToAct) {
            ++stats_.heldByCpu;
            sinceDecision_ = 0;
            return d;
        }
        // How far down to go. The prediction is the naive proportional model, which the affine law
        // measured in the header says always *under*-states the saving -- so the step this chooses
        // is never larger than the evidence supports, and a frame that needs more takes another
        // step at the next decision rather than being over-corrected now. Capped at two rungs so
        // that somebody at 5 fps is not made to watch the ladder walk down for eight seconds.
        std::size_t want = rung_;
        for (std::size_t step = 1; step <= 2 && rung_ + step <= settings_.floorRung; ++step) {
            want = rung_ + step;
            if (gpu * pixelRatio(rung_, want) <= budget) {
                break;
            }
        }
        if (want != rung_) {
            rung_ = want;
            ++stats_.drops;
            d = {rung_, true};
        }
        sinceDecision_ = 0;
        return d;
    }

    if (rung_ > 0) {
        // One rung at a time on the way up, and only when the higher rung is predicted to fit with
        // margin. Here the proportional model *over*-states the higher rung's cost -- the fixed
        // term does not grow -- so this is conservative in the same direction: it climbs later than
        // it could rather than climbing into a budget it cannot hold and dropping straight back.
        const std::size_t up = rung_ - 1;
        if (gpu / pixelRatio(up, rung_) <= budget * settings_.raiseMargin) {
            rung_ = up;
            ++stats_.raises;
            d = {rung_, true};
        }
        sinceDecision_ = 0;
    }
    return d;
}

} // namespace avgen::app
