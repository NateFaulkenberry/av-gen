// ADR-1100/1101: the host side of the Performance panel's Live Performance section and the Optimize review, and the
// Live panel's project choices. The panel only draws; everything it shows is computed here from the frames the editor
// rendered, with the live profiler's own builder and rules (app/live_profile.cpp), so the panel and
// `avgen --live-profile` cannot disagree about what a category or a candidate is.

#include "app/application.hpp"

#include "app/live_profile.hpp"
#include "gpu/context.hpp"
#include "gpu/resource_stats.hpp"
#include "rendering/scene_renderer.hpp"
#include "ui/control_panel.hpp"

#include <fmt/format.h>

#include <algorithm>

namespace avgen::app {

namespace {
constexpr std::size_t kInsightFrames = 120; // about two seconds at 60 fps
}

void Application::notePerformanceFrame(const LiveProfileFrame& frame) {
    if (panel_ == nullptr) {
        return;
    }
    perfFrames_.push_back(frame);
    perfStats_.push_back(renderer_->stats());
    while (perfFrames_.size() > kInsightFrames) {
        perfFrames_.pop_front();
        perfStats_.pop_front();
    }
    // Twice a second or so, not every frame: medians over two seconds do not move faster than that.
    if (++perfFramesSeen_ % 30 != 0 || perfFrames_.size() < 10) {
        return;
    }
    const InteractiveResolutionSettings rs = liveQualitySettings();
    LiveProfileRecord r;
    r.conditions.targetFps = rs.targetFps;
    r.conditions.budgetMs = 1000.0 / rs.targetFps;
    const std::vector<LiveProfileFrame> frames(perfFrames_.begin(), perfFrames_.end());
    buildLiveProfile(r, frames, true);
    const std::vector<rendering::RenderStats> stats(perfStats_.begin(), perfStats_.end());
    LiveProfileResources& res = r.resources;
    {
        // The inspector's numbers: the last frame's, which is what a person is looking at.
        const rendering::RenderStats& s = stats.back();
        res.shadowCasters = s.shadowCasters;
        res.shadowViews = s.shadows.views;
        res.cascades = s.shadows.cascades;
        res.spotMaps = s.shadows.spots;
        res.pointMaps = s.shadows.points;
        res.shadowResolution = s.shadows.resolution;
        res.shadowDraws = s.shadowDraws;
        res.shadowPassSkipped = s.shadows.skippedNothingLit;
        res.particleSystems = s.particles.systems;
        res.particleCapacity = s.particles.capacity;
        res.particlesEmitted = s.particles.emittedThisFrame;
        res.particleSimSteps = s.particles.simulationSteps;
        res.postPasses = s.post.passes;
        res.postWidth = s.post.width;
        res.postHeight = s.post.height;
        res.bloomLevels = s.post.bloomLevels;
        res.volumeWidth = s.volume.marchWidth;
        res.volumeHeight = s.volume.marchHeight;
        res.volumeSteps = s.volume.steps;
        res.logicalTriangles = s.geometry.logicalTriangles;
        res.triangles = s.triangles;
        res.draws = s.drawCalls;
        res.visibleInstances = static_cast<double>(s.visibleInstances);
        res.entityLodDrawables = s.entityLod.drawables;
        for (const auto& program : engine_->scene().materialPrograms) {
            ++res.materialPrograms;
            res.materialProgramOps += program.ops.size();
        }
        auto& view = panel_->perfInsight;
        view.available = true;
        view.targetFps = rs.targetFps;
        view.budgetMs = r.conditions.budgetMs;
        view.medianFrameMs = r.frameMs.valid() ? r.frameMs.p50 : -1.0;
        view.medianGpuMs = r.critical.gpuMs;
        view.medianCpuMs = r.cpuWorkMs.valid() ? r.cpuWorkMs.p50 : -1.0;
        view.gpuCategories.clear();
        for (const auto& g : r.gpu) {
            view.gpuCategories.emplace_back(g.name, g.medianMs);
        }
        view.resources.clear();
        const auto ms = [&](std::string_view cat) {
            for (const auto& g : r.gpu) {
                if (g.name == cat) return g.medianMs;
            }
            return 0.0;
        };
        view.resources.push_back(
            {fmt::format("Shadows -- {:.2f} ms", ms("shadows")),
             {fmt::format("{} cascade(s), {} spot map(s), {} point light(s), maps {} px", s.shadows.cascades,
                          s.shadows.spots, s.shadows.points, s.shadows.resolution),
              fmt::format("{} casters drawn in {} shadow draws; {} small casters skipped by the floor", s.shadowCasters,
                          s.shadowDraws, s.shadows.castersBelowFloor),
              s.shadows.skippedNothingLit ? std::string("shadow maps skipped: nothing visible is lit")
                                          : std::string("shadow maps rendered")}});
        view.resources.push_back(
            {fmt::format("Particles -- {:.2f} ms", ms("particles/simulation")),
             {fmt::format("{} system(s), room for {} particles, {} spawned this frame", s.particles.systems,
                          s.particles.capacity, s.particles.emittedThisFrame),
              fmt::format("{} simulation step(s) a frame; {} far emitter(s) stopped", s.particles.simulationSteps,
                          s.particles.culledByDistance)}});
        view.resources.push_back(
            {fmt::format("Post effects -- {:.2f} ms",
                         ms("post: bloom") + ms("post: depth of field") + ms("post: motion blur") + ms("post: other")),
             {fmt::format("{} passes at {}x{}, bloom {} levels", s.post.passes, s.post.width, s.post.height,
                          s.post.bloomLevels),
              fmt::format("fog march {}x{}, {} steps ({:.2f} ms)", s.volume.marchWidth, s.volume.marchHeight,
                          s.volume.steps, ms("volumetrics"))}});
        view.resources.push_back(
            {fmt::format("Geometry -- {:.2f} ms", ms("geometry/opaque")),
             {fmt::format("{} draws, {} triangles drawn of {} in the world", s.drawCalls, s.triangles,
                          s.geometry.logicalTriangles),
              fmt::format("{} entities with LOD chains, {} drawn below full detail", s.entityLod.drawables,
                          s.entityLod.demoted),
              fmt::format("{} material program(s), {} ops", res.materialPrograms, res.materialProgramOps)}});
    }
    perfRecord_ = std::move(r);
}

void Application::wireLivePerformancePanel() {
    panel_->onOptimizeReview = [this] {
        std::vector<ui::ControlPanel::OptimizeCandidateView> out;
        CandidateInputs in;
        const rendering::QualitySettings& q = renderer_->qualitySettings();
        in.gpu = perfRecord_.gpu;
        in.passes = perfRecord_.passMedians;
        in.resources = perfRecord_.resources;
        in.renderScale = q.renderScale;
        in.volumeResolutionScale = q.volumeResolutionScale;
        in.volumeStepScale = q.volumeStepScale;
        in.motionBlur = q.motionBlur;
        in.depthOfField = q.depthOfField;
        in.softShadows = q.softShadows;
        in.postEffectQuality = q.postEffectQuality;
        in.lodBias = q.lodBias;
        in.drawDistanceScale = q.drawDistanceScale;
        in.shadowCasterMinPixels = q.shadowCasterMinPixels;
        in.particleSpawnScale = q.particleSpawnScale;
        in.gpuMs = perfRecord_.critical.gpuMs;
        in.budgetMs = perfRecord_.conditions.budgetMs;
        for (const LiveProfileCandidate& c : optimizationCandidates(in)) {
            LiveQualityRung probe{};
            out.push_back({c.id, c.title, c.suggestion, c.lever, c.risk, c.estimateBasis, c.costMs, c.estimatedLowMs,
                           c.estimatedHighMs, applyLeverToCeiling(probe, c.lever)});
        }
        return out;
    };
    panel_->onApplyOptimization = [this](const std::vector<std::string>& levers) {
        if (levers.empty()) {
            panel_->optimizationStatus = "Nothing was ticked; nothing changed.";
            return;
        }
        LiveProjectSettings next = engine_->liveSettings();
        optimizationUndo_ = next;
        LiveQualityRung ceiling = next.overrides.value_or(LiveQualityRung{});
        std::string applied;
        for (const std::string& lever : levers) {
            if (applyLeverToCeiling(ceiling, lever)) {
                applied += (applied.empty() ? "" : ", ") + lever;
            }
        }
        next.overrides = ceiling;
        engine_->setLiveSettings(next);
        panel_->canUndoOptimization = true;
        panel_->optimizationStatus =
            "Applied to this project's live limits: " + applied +
            ". The scene is unchanged; save the project to keep them, or Undo optimization to put them back.";
        log::info("optimize: applied {} -> live.overrides {}", applied, ceilingJsonText(ceiling));
    };
    panel_->onUndoOptimization = [this] {
        if (optimizationUndo_) {
            engine_->setLiveSettings(*optimizationUndo_);
            optimizationUndo_.reset();
            panel_->canUndoOptimization = false;
            panel_->optimizationStatus = "The live limits are back as they were.";
        }
    };
    panel_->onSetLiveProfile = [this](int profile) {
        LiveProjectSettings next = engine_->liveSettings();
        if (profile < 0) {
            next.profile.reset();
        } else {
            next.profile = static_cast<QualityProfile>(std::clamp(profile, 0, 2));
        }
        engine_->setLiveSettings(next);
    };
    panel_->onSetLiveMinimum = [this](int minimum) {
        LiveProjectSettings next = engine_->liveSettings();
        if (minimum < 0 || minimum >= static_cast<int>(kLiveQualityLevels) - 1) {
            next.minimumLevel.reset(); // Emergency is the ladder's own bottom: no need to state it
        } else {
            next.minimumLevel = static_cast<LiveQualityLevel>(minimum);
        }
        engine_->setLiveSettings(next);
    };
    panel_->onSaveLiveProfile = [this] {
        LiveProjectSettings next = engine_->liveSettings();
        next.targetFps = options_.liveTargetFps > 0 ? options_.liveTargetFps : settings_.liveTargetFps;
        if (!next.profile) {
            next.profile = QualityProfile::Quality;
        }
        engine_->setLiveSettings(next);
        engine_->setLiveQualityStrategy(engine_->liveQualityStrategy()); // stated, so it is written
        panel_->setStatus(fmt::format("live profile set in the project: {} fps, {}; save the project to keep it",
                                      *next.targetFps, qualityProfileLabel(*next.profile)));
    };
    panel_->onMeasureMemory = [this] {
        const gpu::MemoryReport m = gpu::memoryReport(context_->device(), 3);
        std::string text = m.available ? fmt::format("Textures {:.1f} MB (render targets {:.1f} MB, depth {:.1f} MB), "
                                                     "buffers {:.1f} MB; Dawn's estimate.",
                                                     m.textureBytes / 1.0e6, m.renderTargetBytes / 1.0e6,
                                                     m.depthStencilBytes / 1.0e6, m.bufferBytes / 1.0e6)
                                       : std::string("Memory is not available on this backend.");
        for (const auto& t : m.largest) {
            text += fmt::format(" {} {:.1f} MB;", t.label, t.bytes / 1.0e6);
        }
        panel_->memoryReport = text;
    };
}

} // namespace avgen::app
