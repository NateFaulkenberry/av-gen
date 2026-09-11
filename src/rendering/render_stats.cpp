#include "rendering/render_stats.hpp"

#include <algorithm>

namespace avgen::rendering {

std::uint64_t drawTriangles(std::uint32_t indexCount, std::uint32_t instanceCount) {
    // Widened before the multiply, not after. A scatter of 114,296 records against a 132-triangle
    // source is 15 million triangles, and a 32-bit product of two plausible 32-bit numbers wraps
    // into a small plausible number rather than an obviously wrong one.
    return static_cast<std::uint64_t>(indexCount / 3u) * static_cast<std::uint64_t>(instanceCount);
}

SubmittedGeometry& SubmittedGeometry::operator+=(const SubmittedGeometry& other) {
    triangles += other.triangles;
    instances += other.instances;
    draws += other.draws;
    estimatedDraws += other.estimatedDraws;
    unmeasuredDraws += other.unmeasuredDraws;
    return *this;
}

void SubmittedGeometry::record(std::uint32_t indexCount, std::uint32_t instanceCount, bool estimated) {
    triangles += drawTriangles(indexCount, instanceCount);
    instances += instanceCount;
    ++draws;
    // The draw is recorded whether or not its instance count is known for this frame; what is
    // conditional is the claim that the count is this frame's.
    if (estimated) {
        ++estimatedDraws;
    }
}

void SubmittedGeometry::recordUnmeasured() {
    ++draws;
    ++unmeasuredDraws;
}

SubmittedGeometry GeometryCounters::total() const {
    SubmittedGeometry sum = camera;
    sum += depth;
    sum += shadow;
    return sum;
}

StateChangeCounters& StateChangeCounters::operator+=(const StateChangeCounters& other) {
    pipelineBinds += other.pipelineBinds;
    bindGroupBinds += other.bindGroupBinds;
    vertexBufferBinds += other.vertexBufferBinds;
    indexBufferBinds += other.indexBufferBinds;
    redundantBindsAvoided += other.redundantBindsAvoided;
    renderPasses += other.renderPasses;
    computePasses += other.computePasses;
    return *this;
}

double CpuFrameBreakdown::stagesMs() const {
    return uploadsMs + lightsMs + objectsMs + fieldsMs + simulationMs + particlesMs + proceduralMs +
           sdfMs + shadowEncodeMs + backgroundEncodeMs + depthEncodeMs + sceneEncodeMs +
           volumeEncodeMs + postEncodeMs + tonemapEncodeMs + finishMs + submitMs + queueWaitMs;
}

double CpuFrameBreakdown::unattributedMs() const {
    // Clamped, because the stages are timed by separate clock reads inside the interval the total
    // is timed over: rounding can put the sum a nanosecond past it, and a residual that reads
    // "-0.0000001 ms" invites someone to believe a stage is being double-counted.
    return std::max(0.0, totalMs - stagesMs());
}

std::vector<gpu::TimelineInterval> sumByLabel(const std::vector<gpu::TimelineInterval>& passes) {
    std::vector<gpu::TimelineInterval> out;
    for (const auto& pass : passes) {
        auto it = std::find_if(out.begin(), out.end(), [&](const auto& e) { return e.label == pass.label; });
        if (it == out.end()) {
            out.push_back(pass);
        } else {
            it->ms += pass.ms;
        }
    }
    return out;
}

std::vector<gpu::TimelineInterval>
medianByLabel(const std::vector<std::vector<gpu::TimelineInterval>>& frames) {
    std::vector<std::string> labels;
    std::vector<std::vector<double>> samples;
    for (const auto& frame : frames) {
        for (const auto& pass : sumByLabel(frame)) {
            auto it = std::find(labels.begin(), labels.end(), pass.label);
            if (it == labels.end()) {
                labels.push_back(pass.label);
                samples.push_back({pass.ms});
            } else {
                samples[static_cast<std::size_t>(it - labels.begin())].push_back(pass.ms);
            }
        }
    }
    std::vector<gpu::TimelineInterval> out;
    out.reserve(labels.size());
    for (std::size_t i = 0; i < labels.size(); ++i) {
        std::vector<double>& v = samples[i];
        std::sort(v.begin(), v.end());
        out.push_back(gpu::TimelineInterval{labels[i], v[v.size() / 2]});
    }
    return out;
}

RemovalAttribution attributeRemoval(const std::vector<gpu::TimelineInterval>& baseline,
                                    const std::vector<gpu::TimelineInterval>& arm,
                                    std::string_view removedLabel) {
    const auto find = [](const std::vector<gpu::TimelineInterval>& list, std::string_view label) {
        const auto it = std::find_if(list.begin(), list.end(), [&](const auto& e) { return e.label == label; });
        return it == list.end() ? 0.0 : it->ms;
    };
    RemovalAttribution out;
    double baselineTotal = 0.0;
    for (const auto& pass : baseline) {
        baselineTotal += pass.ms;
        out.byLabel.push_back(gpu::TimelineInterval{pass.label, pass.ms - find(arm, pass.label)});
    }
    double armTotal = 0.0;
    for (const auto& pass : arm) {
        armTotal += pass.ms;
        // A label the arm has and the baseline does not is a pass that appeared when the phase was
        // removed. Rare, but silently dropping it would make the per-label deltas stop summing to
        // the frame delta, which is the property that makes this attribution worth anything.
        const auto known = std::find_if(baseline.begin(), baseline.end(),
                                        [&](const auto& e) { return e.label == pass.label; });
        if (known == baseline.end()) {
            out.byLabel.push_back(gpu::TimelineInterval{pass.label, -pass.ms});
        }
    }
    out.frameDeltaMs = baselineTotal - armTotal;
    out.removedPassMs = find(baseline, removedLabel);
    out.elsewhereMs = out.frameDeltaMs - out.removedPassMs;
    std::stable_sort(out.byLabel.begin(), out.byLabel.end(),
                     [](const auto& a, const auto& b) { return a.ms > b.ms; });
    return out;
}

} // namespace avgen::rendering
