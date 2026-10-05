// ADR-1090..1093: `avgen --live-profile`, the two loops that fill a record (docs/live-optimizer/02-plan.md, Stage 1).
//
// Headless: a fixed-step loop of its own, deterministic, no present (for agents iterating on a scene). Live: the editor's
// own loop runs unchanged and hands each frame to `noteLiveProfileFrame` -- present, Fifo, the projection output and the
// UI are all in the frame, which is what predicts a touring frame. Both call `buildLiveProfile` (app/live_profile.cpp),
// and both verify candidates with the same interleaved, counterbalanced A/B the `--ab` bench uses (`compareArms`).

#include "app/application.hpp"

#include "app/live_profile.hpp"
#include "app/live_profile_session.hpp"
#include "assets/image.hpp"
#include "core/log.hpp"
#include "core/phase2_probe.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/resource_stats.hpp"
#include "platform/window.hpp"
#include "rendering/importance.hpp"
#include "rendering/scene_renderer.hpp"
#include "rendering/sdf_renderer.hpp"
#include "scene/composition.hpp"

#include "avgen_build_info.hpp"

#include <fmt/format.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <thread>
#include <unistd.h>

namespace avgen::app {

namespace {

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

std::vector<gpu::TimelineInterval> framePasses(rendering::SceneRenderer& renderer) {
    std::vector<gpu::TimelineInterval> out;
    for (const auto& entry : renderer.timeline().passes()) {
        auto it = std::find_if(out.begin(), out.end(), [&](const auto& e) { return e.label == entry.label; });
        if (it == out.end()) {
            out.push_back({entry.label, entry.ms});
        } else {
            it->ms += entry.ms;
        }
    }
    return out;
}

void fillFromProbe(LiveProfileFrame& f) {
    const probe2::Frame& pr = probe2::frame();
    f.updControlMs = pr.updControlMs;
    f.updSignalsMs = pr.updSignalsMs;
    f.updModulationMs = pr.updModulationMs;
    f.updControllerMs = pr.updControllerMs;
    f.updOtherMs = pr.updOtherMs;
    f.analysisCatchupMs = pr.analysisCatchupMs;
    f.meshUploadMs = pr.meshUploadMs;
    f.textureUploadMs = pr.textureUploadMs;
    f.environmentMs = pr.environmentMs;
}

Result<void> writeImage(const gpu::Image8& image, const std::filesystem::path& path) {
    if (path.extension() == ".png") {
        return assets::writePng(path, image.width, image.height, image.rgba);
    }
    return gpu::writePpm(image, path);
}

// Applies a candidate's lever as an A/B arm. A quality arm changes a setting; a pass arm switches a pass off.
bool applyArm(const std::string& lever, rendering::QualitySettings& q, rendering::SceneRenderer::PassToggles& t) {
    return rendering::SceneRenderer::setQualityArm(q, lever) || rendering::SceneRenderer::setPassArm(t, lever, false);
}

void settleCandidate(LiveProfileCandidate& c, const rendering::AbSummary& ab) {
    c.verified = ab.blocks > 0;
    c.measuredPairs = ab.blocks;
    c.measuredSavingMs = ab.gpu.deltaMs; // baseline - arm: positive is a saving
    c.measuredSavingPercent = ab.gpu.deltaPercent;
    c.noiseFloorPercent = ab.gpu.noiseFloorPercent;
    c.measuredIsResult = ab.gpu.isResult();
    c.measuredVoid = ab.gpuDrift.voids(ab.gpu.deltaMs);
    c.measuredVerdict = !c.verified        ? "not measured"
                        : c.measuredVoid     ? "VOID: the machine drifted more than the effect"
                        : !c.measuredIsResult ? "inside the noise: no measurable saving"
                        : ab.gpu.deltaMs > 0.0 ? "a saving, outside the noise"
                                               : "SLOWER, outside the noise";
}

template <typename T>
double medianOfStats(const std::vector<rendering::RenderStats>& stats, T pick) {
    std::vector<double> v;
    v.reserve(stats.size());
    for (const auto& s : stats) {
        v.push_back(static_cast<double>(pick(s)));
    }
    if (v.empty()) {
        return 0.0;
    }
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(v.size() / 2), v.end());
    return v[v.size() / 2];
}

} // namespace

// ---- shared ------------------------------------------------------------------------------------------------------

void Application::fillLiveProfileConditions(LiveProfileRecord& record, bool live) {
    const LiveProfileOptions& o = options_.liveProfile;
    LiveProfileConditions& c = record.conditions;
    const HostDescription host = describeHost();
    c.scene = options_.project       ? options_.project->string()
              : options_.composition ? options_.composition->string()
              : options_.scene       ? options_.scene->string()
              : options_.example     ? *options_.example
                                     : std::string("(built-in)");
    c.mode = live ? "live" : "headless";
    c.machine = host.machine;
    c.cpu = host.cpu;
    c.os = host.os;
    c.gpu = context_->capabilities().adapterName;
    c.backend = "WebGPU / Dawn / " + context_->capabilities().backendName;
    c.buildType = AVGEN_BUILD_TYPE;
    c.gitRevision = AVGEN_GIT_REVISION;
    c.gitDirty = AVGEN_GIT_DIRTY != 0;
    c.targetFps = o.targetFps;
    c.budgetMs = 1000.0 / o.targetFps;
    c.qualityBudgetMs = liveBudget(o.targetFps).qualityBudgetMs;
    c.qualityRequested = o.quality;
    c.strategy = std::string(liveQualityStrategyToken(engine_->liveQualityStrategy()));
    c.renderScale = renderer_->qualitySettings().renderScale;
    c.internalWidth = renderer_->stats().width;
    c.internalHeight = renderer_->stats().height;
    c.camera = o.camera.empty() ? engine_->scene().camera.name : o.camera;
    c.startSeconds = o.startSeconds;
    c.warmupCapSeconds = o.warmupSeconds;
    c.measureSeconds = o.measureSeconds;
    c.deep = o.deep;
    c.audioState = !o.audio                  ? "off (the audio file was not played)"
                   : engine_->hasLiveInput() ? "live input"
                   : engine_->hasAudio()     ? (live ? "file, playing to the output device" : "file, analysed offline")
                                             : "none";
    c.midi = options_.midi ? fmt::format("open ('{}')", *options_.midi) : std::string("off");
    {
        const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm local{};
        ::localtime_r(&now, &local);
        char stamp[32] = {};
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%dT%H:%M:%S", &local);
        c.startedAt = stamp;
        c.sessionId = fmt::format("{}-{}", stamp, static_cast<long long>(::getpid()));
    }
    if (!live) {
        c.window = "offscreen";
        c.outputWidth = o.outputWidth;
        c.outputHeight = o.outputHeight;
    }
}

void Application::fillLiveProfileResources(LiveProfileRecord& record, const std::vector<rendering::RenderStats>& stats) {
    using RS = rendering::RenderStats;
    LiveProfileResources& r = record.resources;
    const auto m = [&](auto pick) { return medianOfStats(stats, pick); };
    r.draws = m([](const RS& s) { return s.drawCalls; });
    r.shadowDraws = m([](const RS& s) { return s.shadowDraws; });
    r.triangles = m([](const RS& s) { return s.triangles; });
    r.logicalTriangles = m([](const RS& s) { return s.geometry.logicalTriangles; });
    r.visibleInstances = m([](const RS& s) { return s.visibleInstances; });
    r.culledInstances = m([](const RS& s) { return s.culledInstances; });
    r.entities = m([](const RS& s) { return s.entities; });
    for (int l = 0; l < 4; ++l) {
        r.lod[l] = m([l](const RS& s) { return s.lodCounts[l]; });
    }
    r.entityLodDrawables = m([](const RS& s) { return s.entityLod.drawables; });
    r.entityLodDemoted = m([](const RS& s) { return s.entityLod.demoted; });
    r.shadowCasters = m([](const RS& s) { return s.shadowCasters; });
    r.shadowViews = m([](const RS& s) { return s.shadows.views; });
    r.cascades = m([](const RS& s) { return s.shadows.cascades; });
    r.spotMaps = m([](const RS& s) { return s.shadows.spots; });
    r.pointMaps = m([](const RS& s) { return s.shadows.points; });
    r.shadowResolution = m([](const RS& s) { return s.shadows.resolution; });
    r.shadowLights = m([](const RS& s) { return (s.shadows.cascades > 0 ? 1u : 0u) + s.shadows.spots + s.shadows.points; });
    r.lights = m([](const RS& s) { return s.shadedLights; });
    r.directionalLights = m([](const RS& s) { return s.directionalLights; });
    r.clusteredLights = m([](const RS& s) { return s.clusteredLights; });
    r.particleSystems = m([](const RS& s) { return s.particles.systems; });
    r.particleCapacity = m([](const RS& s) { return s.particles.capacity; });
    r.particlesEmitted = m([](const RS& s) { return s.particles.emittedThisFrame; });
    r.particleDispatches = m([](const RS& s) { return s.particles.dispatches; });
    r.particleSimSteps = m([](const RS& s) { return s.particles.systems > 0 ? s.particles.simulationSteps : 0u; });
    r.postPasses = m([](const RS& s) { return s.post.passes; });
    r.bloomLevels = m([](const RS& s) { return s.post.bloomLevels; });
    r.volumeSteps = m([](const RS& s) { return s.volume.steps; });
    r.sdfRaymarchObjects = m([](const RS& s) { return s.sdf.raymarchObjects; });
    r.sdfAvgSteps = m([](const RS& s) { return s.sdf.avgSteps; });
    r.computeDispatches = m([](const RS& s) { return s.computeDispatches; });
    r.gpuPasses = m([](const RS& s) { return s.gpuPasses; });
    r.transientTextures = m([](const RS& s) { return s.transientTextures; });
    if (!stats.empty()) {
        const RS& last = stats.back();
        r.postWidth = last.post.width;
        r.postHeight = last.post.height;
        r.volumeWidth = last.volume.marchWidth;
        r.volumeHeight = last.volume.marchHeight;
        r.aoWidth = last.ao.width;
        r.aoHeight = last.ao.height;
        r.shadowPassSkipped = last.shadows.skippedNothingLit;
    }
    const gpu::MemoryReport mem = gpu::memoryReport(context_->device(), options_.liveProfile.deep ? 12 : 6);
    r.haveMemory = mem.available;
    r.textureBytes = mem.textureBytes;
    r.bufferBytes = mem.bufferBytes;
    r.depthStencilBytes = mem.depthStencilBytes;
    r.totalBytes = mem.totalBytes;
    r.renderTargetBytes = mem.renderTargetBytes;
    r.textureCount = mem.textures;
    r.bufferCount = mem.buffers;
    for (const auto& t : mem.largest) {
        r.largestTextures.push_back({t.label, t.bytes, t.detail});
    }
    const gpu::PipelineCounters pc = gpu::pipelineCounters();
    r.renderPipelines = pc.renderPipelines;
    r.computePipelines = pc.computePipelines;
    r.shaderModules = pc.shaderModules;
    r.sdfVariants = renderer_->sdfs().compiledVariantCount();
    for (const auto& program : engine_->scene().materialPrograms) {
        ++r.materialPrograms;
        r.materialProgramOps += program.ops.size();
    }
}

void Application::fillLiveProfileEntities(LiveProfileRecord& record) {
    // Stage 5 groundwork, and ADR-1108's contribution analysis: every entity's bounding sphere seen from the camera.
    // The radius in pixels is the caster floor's own (radius x pixels-per-unit at its distance, on the render target),
    // so the analysis predicts exactly which casters `shadowCasterMinPixels` removes; the coverage is the sphere's disc
    // over the frame; the box is on the OUTPUT (what the A/B frames are).
    const scene::Scene& sc = engine_->scene();
    const scene::Camera& cam = sc.camera;
    const std::uint32_t ow = std::max<std::uint32_t>(record.conditions.outputWidth, 1);
    const std::uint32_t oh = std::max<std::uint32_t>(record.conditions.outputHeight, 1);
    const std::uint32_t rh = record.conditions.internalHeight > 0 ? record.conditions.internalHeight : oh;
    const std::uint32_t rw = record.conditions.internalWidth > 0 ? record.conditions.internalWidth : ow;
    const rendering::ViewContext renderView = rendering::ViewContext::fromCamera(cam, rw, rh);
    const rendering::ViewContext outputView = rendering::ViewContext::fromCamera(cam, ow, oh);
    std::vector<LiveProfileEntity> all;
    for (const scene::Entity& e : sc.entities) {
        if (e.mesh == scene::kInvalidMesh) {
            continue;
        }
        const auto& [lo, hi] = sc.meshBounds(e.mesh);
        const glm::mat4 m = e.transform.matrix();
        const glm::vec3 centre = glm::vec3(m * glm::vec4((lo + hi) * 0.5f, 1.0f));
        // The renderer's caster-floor radius: the bounds' half diagonal times the largest authored axis scale.
        const float scale = std::max({std::abs(e.transform.scale.x), std::abs(e.transform.scale.y),
                                      std::abs(e.transform.scale.z)});
        const float radius = glm::length(hi - lo) * 0.5f * scale;
        const float distance = glm::length(centre - cam.position);
        LiveProfileEntity out;
        out.name = e.name;
        out.distance = distance;
        out.hero = e.importance == scene::Importance::Hero;
        out.importance = scene::importanceName(e.importance);
        out.leverWeight = scene::importanceLeverWeight(e.importance);
        out.castsShadow = e.castsShadow;
        out.visible = e.visible && !e.cameraCulled;
        if (distance <= radius) {
            out.projectedArea = 1.0; // the camera is inside it
            out.radiusPx = 0.0;      // and the caster floor never removes it
            out.onScreen = true;
        } else {
            out.radiusPx = static_cast<double>(radius * renderView.pixelsPerUnitAt(distance));
            const double r = static_cast<double>(radius * outputView.pixelsPerUnitAt(distance));
            out.projectedArea = std::min(1.0, 3.14159265358979 * r * r / (static_cast<double>(ow) * oh));
            glm::vec2 px{0.0f};
            if (outputView.projectToScreen(centre, px)) {
                const double x0 = std::clamp(static_cast<double>(px.x) - r, 0.0, static_cast<double>(ow));
                const double x1 = std::clamp(static_cast<double>(px.x) + r, 0.0, static_cast<double>(ow));
                const double y0 = std::clamp(static_cast<double>(px.y) - r, 0.0, static_cast<double>(oh));
                const double y1 = std::clamp(static_cast<double>(px.y) + r, 0.0, static_cast<double>(oh));
                if (x1 > x0 && y1 > y0) {
                    out.onScreen = true;
                    out.haveBox = true;
                    out.x0 = static_cast<std::uint32_t>(x0);
                    out.y0 = static_cast<std::uint32_t>(y0);
                    out.x1 = static_cast<std::uint32_t>(std::ceil(x1));
                    out.y1 = static_cast<std::uint32_t>(std::ceil(y1));
                }
            }
        }
        out.contribution = contributionOf(out.onScreen ? out.projectedArea : 0.0, out.leverWeight, out.hero);
        all.push_back(std::move(out));
    }
    std::stable_sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.projectedArea > b.projectedArea; });
    // ADR-1108: the particle emitters, with what `particlelod`'s 60 m cull would stop (the renderer's test).
    std::vector<ContributionEmitter> emitters;
    for (const scene::ParticleSystem& sys : sc.particles) {
        ContributionEmitter em;
        em.name = sys.name;
        em.distance = glm::length(sys.position - cam.position);
        em.reach = std::max({sys.extent.x, sys.extent.y, sys.extent.z, 0.0f});
        em.importance = scene::importanceName(sys.importance);
        em.leverWeight = scene::importanceLeverWeight(sys.importance);
        em.hero = sys.importance == scene::Importance::Hero;
        em.enabled = sys.enabled;
        em.beyondCull = em.leverWeight > 0.0f && sys.shape != scene::EmitterShape::Spline &&
                        !sys.scatterAnchor.active() && em.distance - em.reach > 60.0 / em.leverWeight;
        emitters.push_back(std::move(em));
    }
    record.contribution = analyseContribution(all, emitters);
    const std::size_t keep = options_.liveProfile.deep ? 1000 : 100;
    if (all.size() > keep) {
        record.notes.push_back(fmt::format("entities: the {} largest on screen of {} are listed (the contribution "
                                           "analysis read all of them)",
                                           keep, all.size()));
        all.resize(keep);
    }
    record.entities = std::move(all);
}

int Application::writeLiveProfile(LiveProfileRecord& record) {
    const LiveProfileOptions& o = options_.liveProfile;
    if (o.text) {
        std::fputs(liveProfileText(record).c_str(), stdout);
        std::fflush(stdout);
    }
    if (o.json) {
        std::ofstream out(*o.json, std::ios::binary);
        if (!out) {
            log::error("--live-profile: cannot write {}", o.json->string());
            return 4;
        }
        out << liveProfileJsonText(record);
        log::info("live profile written to {}", o.json->string());
    }
    return context_->errorCount() == 0 ? 0 : 5;
}

namespace {

// The rules' inputs from a built record and the renderer's quality in force.
CandidateInputs candidateInputs(const LiveProfileRecord& record, const rendering::QualitySettings& q) {
    CandidateInputs in;
    in.gpu = record.gpu;
    in.passes = record.passMedians;
    in.resources = record.resources;
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
    in.gpuMs = record.gpuMs.valid() ? record.gpuMs.p50 : 0.0;
    in.budgetMs = record.conditions.budgetMs;
    return in;
}

} // namespace

// ---- headless ----------------------------------------------------------------------------------------------------

int Application::runLiveProfileHeadless() {
    const LiveProfileOptions& o = options_.liveProfile;
    const std::uint32_t w = o.outputWidth;
    const std::uint32_t h = o.outputHeight;
    if (auto r = renderer_->resize(w, h); !r) {
        log::error("resize: {}", r.error().message);
        return 2;
    }
    // The live picture's settings: the tier the run asked for (the editor's own when none), the live antialiasing
    // (ADR-1024) unless --live-aa said otherwise, then the live level (ADR-1083) on top.
    rendering::QualitySettings base = renderer_->qualitySettings();
    if (!options_.liveAntialias.has_value()) {
        base.antialiasFloor = rendering::kLiveAntialiasFloor;
    }
    if (const auto profile = liveQualityProfile()) {
        base = applyQualityProfile(base, *profile);
    }
    const rendering::QualitySettings baseWithoutCeilings = base; // ADR-1110: `--compare project`'s ORIGINAL
    if (engine_->liveSettings().overrides) {
        base = applyCeiling(base, *engine_->liveSettings().overrides); // ADR-1100: what Optimize applied
    }
    renderer_->setQualitySettings(base);
    InteractiveResolutionSettings rs = liveQualitySettings();
    const auto pinned = livePinnedQuality();
    const bool automatic = !pinned.has_value() && o.quality == "auto";
    rs.enabled = automatic;
    InteractiveResolution controller;
    controller.configure(rs);
    const LiveQualityLadder& ladder = controller.ladder();
    std::size_t rung = pinned ? static_cast<std::size_t>(*pinned) : 0;
    const auto applyRung = [&](std::size_t k) {
        const rendering::QualitySettings q = applyLiveRung(renderer_->qualitySettings(), base, ladder[k], rs.scaleFloor);
        renderer_->setQualitySettings(q);
        scene::DetailLimits limits = engine_->detailLimits();
        limits.distanceScale = q.drawDistanceScale; // ADR-1094's CPU half, as the live editor does
        engine_->setDetailLimits(limits);
    };
    applyRung(rung);

    // ADR-1102: as live -- SDF variants compile on Dawn's workers.
    renderer_->sdfs().setAsyncCompile(o.prewarm);
    renderer_->sdfs().setPrewarm(o.prewarm);
    FixedStepClock clock(o.targetFps);
    clock.restartAt(o.startSeconds);
    renderer_->resetTemporalHistory();
    std::uint64_t lastDiscontinuity = engine_->transport().discontinuityRevision();
    const auto step = [&](LiveProfileFrame& f, std::optional<gpu::Image8>* image) -> bool {
        const auto start = Clock::now();
        probe2::frame().clear(); // per frame, as the live loop does (the stages accumulate otherwise)
        const FrameTime time = engine_->tick(clock);
        if (const std::uint64_t d = engine_->transport().discontinuityRevision(); d != lastDiscontinuity) {
            renderer_->resetTemporalHistory();
            lastDiscontinuity = d;
        }
        const auto updateStart = Clock::now();
        engine_->setViewport(w, h);
        engine_->update(time);
        f.engineUpdateMs = msSince(updateStart);
        if (ai_) {
            ai_->pump();
        }
        const rendering::ShaderFrameInputs inputs{&engine_->shaderLayers(),
                                                  engine_->hasFrame() ? &engine_->latestFrame() : nullptr,
                                                  engine_->barPhase()};
        if (image != nullptr) {
            auto rendered = renderer_->renderToImage(engine_->scene(), time, w, h, &inputs);
            if (!rendered) {
                log::error("render: {}", rendered.error().message);
                return false;
            }
            *image = std::move(*rendered);
        } else if (auto r = renderer_->renderFrame(engine_->scene(), time, w, h, &inputs); !r) {
            log::error("render: {}", r.error().message);
            return false;
        }
        context_->processEvents(); // async pipeline callbacks (ADR-1102), as the live loop pumps them
        const rendering::RenderStats& st = renderer_->stats();
        f.frameMs = msSince(start);
        f.gpuMs = st.gpuFrameMs;
        f.waitMs = st.cpu.queueWaitMs;
        f.cpuWorkMs = std::max(0.0, f.frameMs - f.waitMs);
        f.render = st.cpu;
        f.renderRecordMs = std::max(0.0, st.cpu.totalMs - st.cpu.queueWaitMs);
        f.passes = framePasses(*renderer_);
        fillFromProbe(f);
        return true;
    };

    LiveProfileRecord record;
    // ---- warm-up: until the frame settles, or the cap ----
    auto warmStart = Clock::now();
    SteadyStateDetector steady(20, 0.05, 2, 40);
    int warmFrames = 0;
    int lastChange = 0;
    while (true) {
        LiveProfileFrame f;
        if (!step(f, nullptr)) {
            return 2;
        }
        if (warmFrames == 0) {
            record.cold.firstFrameMs = f.frameMs;
            record.cold.loadMs = std::chrono::duration<double, std::milli>(warmStart - initStart_).count();
            // ADR-1102: the pre-warm. The first frame asked for every variant the scene holds; wait for them here,
            // before warm-up, and report the wait apart from the frames.
            const auto prewarmStart = Clock::now();
            const std::size_t variantsBefore = renderer_->sdfs().compiledVariantCount();
            while (renderer_->sdfs().pendingCompiles() > 0 && msSince(prewarmStart) < 60000.0) {
                context_->processEvents();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            record.cold.prewarmMs = msSince(prewarmStart);
            record.cold.prewarmVariants =
                static_cast<int>(renderer_->sdfs().compiledVariantCount() - std::min(variantsBefore, renderer_->sdfs().compiledVariantCount()));
            warmStart = Clock::now(); // the warm-up cap counts from here, not from before the pre-warm
        }
        ++warmFrames;
        const double cost = std::max(f.gpuMs, f.cpuWorkMs);
        if (automatic) {
            // The live controller, fed the pipelined cost (headless serialises CPU and GPU).
            const auto decision = controller.note(f.gpuMs, cost);
            if (decision.changed) {
                rung = decision.rung;
                applyRung(rung);
                steady = SteadyStateDetector(20, 0.05, 2, 40); // the level moved, so the frame did
                lastChange = warmFrames;
            }
        }
        const bool settled = steady.note(cost);
        const double elapsed = msSince(warmStart) / 1000.0;
        // Auto: still settling if the level moved in the last 60 frames; the cap stretches to three times.
        const bool moving = automatic && lastChange > 0 && warmFrames - lastChange < 60;
        const double cap = moving ? 3.0 * o.warmupSeconds : o.warmupSeconds;
        if ((settled && !moving && elapsed >= std::min(1.0, o.warmupSeconds)) || elapsed >= cap) {
            record.cold.steadyReached = settled;
            break;
        }
    }
    record.cold.warmupFrames = warmFrames;
    record.cold.warmupSeconds = msSince(warmStart) / 1000.0;

    // ---- measure ----
    const int frames = std::max(30, static_cast<int>(std::lround(o.measureSeconds * o.targetFps)));
    const double measureStartPiece = clock.current().renderTime;
    const gpu::PipelineCounters compilesBefore = gpu::pipelineCounters();
    std::vector<LiveProfileFrame> measured;
    std::vector<rendering::RenderStats> stats;
    measured.reserve(static_cast<std::size_t>(frames));
    stats.reserve(static_cast<std::size_t>(frames));
    const auto measureStart = Clock::now();
    for (int i = 0; i < frames; ++i) {
        LiveProfileFrame f;
        std::optional<gpu::Image8> image;
        const bool capture = o.capture && i == frames - 1;
        if (!step(f, capture ? &image : nullptr)) {
            return 2;
        }
        f.atSeconds = msSince(measureStart) / 1000.0;
        measured.push_back(std::move(f));
        stats.push_back(renderer_->stats());
        if (capture && image) {
            if (auto r = writeImage(*image, *o.capture); !r) {
                log::error("capture: {}", r.error().message);
            } else {
                log::info("captured the last measured frame to {}", o.capture->string());
            }
        }
    }
    record.cold.compilesDuringMeasure =
        static_cast<int>(gpu::pipelineCounters().compiles() - compilesBefore.compiles());

    fillLiveProfileConditions(record, false);
    record.conditions.startSeconds = measureStartPiece;
    record.conditions.liveLevel = liveQualityLevelName(ladder[rung].level);
    record.conditions.profile = liveQualityProfile() ? qualityProfileLabel(*liveQualityProfile()) : std::string();
    record.conditions.renderScale = renderer_->qualitySettings().renderScale;
    record.conditions.internalWidth = renderer_->stats().width;
    record.conditions.internalHeight = renderer_->stats().height;
    buildLiveProfile(record, measured, false);
    fillLiveProfileResources(record, stats);
    fillLiveProfileEntities(record);
    record.candidates = optimizationCandidates(candidateInputs(record, renderer_->qualitySettings()));
    if (automatic) {
        record.notes.push_back(fmt::format("auto: the live controller ran during warm-up on the headless GPU span and "
                                           "settled at {}; that level was held while measuring",
                                           liveQualityLevelName(ladder[rung].level)));
    }
    record.notes.push_back(fmt::format("headless measured {} frames of piece time at a fixed {:.0f} fps step, from "
                                       "{:.2f} s",
                                       frames, o.targetFps, measureStartPiece));

    // ---- verify: the top candidates, measured with the --ab machinery ----
    if (o.verifyCandidates > 0) {
        const rendering::QualitySettings baseQ = renderer_->qualitySettings();
        const rendering::SceneRenderer::PassToggles baseT = renderer_->passToggles();
        const int blockFrames = std::clamp(frames / 2, 30, 120);
        constexpr int kSettle = 10;
        constexpr int kPairs = 2;
        int verified = 0;
        for (auto& cand : record.candidates) {
            if (verified >= o.verifyCandidates) {
                break;
            }
            rendering::QualitySettings armQ = baseQ;
            rendering::SceneRenderer::PassToggles armT = baseT;
            if (cand.lever.empty() || !applyArm(cand.lever, armQ, armT)) {
                continue;
            }
            ++verified;
            std::vector<rendering::AbBlock> baseBlocks, armBlocks;
            for (int pair = 0; pair < kPairs; ++pair) {
                for (int half = 0; half < 2; ++half) {
                    const bool isArm = (half == 0) == (pair % 2 == 1); // counterbalanced (ADR-181)
                    renderer_->setQualitySettings(isArm ? armQ : baseQ);
                    renderer_->setPassToggles(isArm ? armT : baseT);
                    clock.restartAt(measureStartPiece);
                    renderer_->resetTemporalHistory();
                    std::vector<double> wall, gpuMs;
                    for (int i = 0; i < kSettle + blockFrames; ++i) {
                        LiveProfileFrame f;
                        if (!step(f, nullptr)) {
                            return 2;
                        }
                        if (i >= kSettle) {
                            wall.push_back(f.frameMs);
                            if (f.gpuMs >= 0.0) gpuMs.push_back(f.gpuMs);
                        }
                    }
                    rendering::AbBlock block{rendering::describe(wall), rendering::describe(gpuMs)};
                    (isArm ? armBlocks : baseBlocks).push_back(block);
                }
            }
            settleCandidate(cand, rendering::compareArms(cand.lever, baseBlocks, armBlocks));
            log::info("verify '{}' ({}): {:+.2f} ms GPU -> {}", cand.id, cand.lever, cand.measuredSavingMs,
                      cand.measuredVerdict);
        }
        renderer_->setQualitySettings(baseQ);
        renderer_->setPassToggles(baseT);
        record.verificationMode =
            fmt::format("headless, in this process: {} counterbalanced baseline/arm pair(s) per candidate, {} frames "
                        "each after {} settling frames, from the measured start; GPU span medians compared "
                        "(rendering::compareArms, the --ab machinery)",
                        kPairs, blockFrames, kSettle);
    }
    // ---- Phase 5: ORIGINAL vs OPTIMIZED, and the search (ADR-1110/1111) ----
    if (!o.compare.empty() || o.optimize) {
        LiveAbDriver driver;
        driver.step = step;
        driver.restart = [&](double piece) {
            clock.restartAt(piece);
            renderer_->resetTemporalHistory();
        };
        driver.startPiece = measureStartPiece;
        driver.base = renderer_->qualitySettings();
        driver.withoutCeilings =
            applyLiveRung(renderer_->qualitySettings(), baseWithoutCeilings, ladder[rung], rs.scaleFloor);
        if (const int rc = runLivePhase5(record, driver); rc != 0) {
            return rc;
        }
        renderer_->setQualitySettings(driver.base);
        scene::DetailLimits limits = engine_->detailLimits();
        limits.distanceScale = driver.base.drawDistanceScale;
        engine_->setDetailLimits(limits);
    }
    return writeLiveProfile(record);
}

// ---- live ----------------------------------------------------------------------------------------------------------


void Application::beginLiveProfile() {
    liveProfileSession_ = std::make_unique<LiveProfileSession>();
    const LiveProfileOptions& o = options_.liveProfile;
    // The projection window at the output size, in points on its display (never written to the settings file).
    // The editor window's backing scale: the display list's content scale reads 1 on this 5K display.
    const float scale = window_ != nullptr && window_->pixelScale() > 0.0f ? window_->pixelScale() : 1.0f;
    for (const auto& d : platform::Window::displays()) {
        if (d.primary) {
            liveProfileSession_->record.conditions.displayRefreshHz = d.refreshRate;
        }
    }
    projectionWidthOverride_ = static_cast<std::uint32_t>(std::lround(o.outputWidth / scale));
    projectionHeightOverride_ = static_cast<std::uint32_t>(std::lround(o.outputHeight / scale));
    if (!o.audio) {
        engine_->setVolume(0.0f);
    }
    log::info("live profile: target {:.0f} fps, output {}x{} px ({}x{} pt), warm-up cap {:.1f} s, measure {:.1f} s",
              o.targetFps, o.outputWidth, o.outputHeight, projectionWidthOverride_, projectionHeightOverride_,
              o.warmupSeconds, o.measureSeconds);
}

bool Application::noteLiveProfileFrame(const LiveProfileFrame& frame) {
    LiveProfileSession& s = *liveProfileSession_;
    const LiveProfileOptions& o = options_.liveProfile;
    using P = LiveProfileSession::Phase;
    switch (s.phase) {
    case P::WaitingForOutput: {
        const Output* out = outputs_.find(kProjectionOutputName);
        ++s.waitFrames;
        if (s.waitFrames == 1) {
            s.record.cold.firstFrameMs = frame.frameMs;
            s.record.cold.loadMs = std::chrono::duration<double, std::milli>(Clock::now() - initStart_).count() -
                                   frame.frameMs;
        }
        // ADR-1102: wait for the pre-warm (variants compiling on Dawn's workers) too, and time it.
        if (renderer_->sdfs().pendingCompiles() > 0 && s.waitFrames < 3000) {
            return true;
        }
        if (s.record.cold.prewarmMs < 0.0) {
            s.record.cold.prewarmMs = msSince(s.phaseStart);
            s.record.cold.prewarmVariants = static_cast<int>(renderer_->sdfs().compiledVariantCount());
        }
        if ((out != nullptr && out->open()) || s.waitFrames > 300) {
            if (out == nullptr || !out->open()) {
                s.record.notes.push_back("the projection window did not open; the editor canvas was measured");
            }
            s.phase = P::Warmup;
            s.phaseStart = Clock::now();
        }
        return true;
    }
    case P::Warmup: {
        ++s.warmFrames;
        const bool settled = s.steady.note(frame.frameMs);
        const double elapsed = msSince(s.phaseStart) / 1000.0;
        // A live level change restarts the settling: the frame moved because the picture did.
        if (liveTransitions_ != s.transitionsSeen) {
            s.transitionsSeen = liveTransitions_;
            s.steady = SteadyStateDetector(30, 0.05, 2, 60);
            s.lastChangeFrame = s.warmFrames;
        }
        // Auto: a level that moved in the last 60 frames is still settling, so the cap stretches (to three times)
        // rather than measure across a level change.
        const bool moving = !livePinnedQuality() && s.lastChangeFrame > 0 && s.warmFrames - s.lastChangeFrame < 60;
        const double cap = moving ? 3.0 * o.warmupSeconds : o.warmupSeconds;
        if ((settled && !moving && elapsed >= std::min(1.0, o.warmupSeconds)) || elapsed >= cap) {
            s.record.cold.steadyReached = settled;
            s.record.cold.warmupFrames = s.warmFrames;
            s.record.cold.warmupSeconds = elapsed;
            s.phase = P::Measure;
            s.measureStart = Clock::now();
            s.compilesBefore = gpu::pipelineCounters();
            s.record.conditions.startSeconds = engine_->positionSeconds();
            cpuProfile_.setFrameGroup(core::PhaseProfiler::kMaxGroups - 1);
            cpuProfile_.nameGroup(core::PhaseProfiler::kMaxGroups - 1, "live-profile");
        }
        return true;
    }
    case P::Measure: {
        LiveProfileFrame f = frame;
        f.atSeconds = msSince(s.measureStart) / 1000.0;
        s.frames.push_back(std::move(f));
        s.stats.push_back(renderer_->stats());
        if (msSince(s.measureStart) / 1000.0 < o.measureSeconds) {
            return true;
        }
        cpuProfile_.setFrameGroup(core::PhaseProfiler::kNoGroup);
        s.record.cold.compilesDuringMeasure =
            static_cast<int>(gpu::pipelineCounters().compiles() - s.compilesBefore.compiles());
        LiveProfileRecord& r = s.record;
        const double refresh = r.conditions.displayRefreshHz;
        const double measuredFrom = r.conditions.startSeconds;
        fillLiveProfileConditions(r, true);
        r.conditions.displayRefreshHz = refresh;
        r.conditions.startSeconds = measuredFrom; // where the playhead was when measurement began
        const Output* out = outputs_.find(kProjectionOutputName);
        if (out != nullptr && out->open()) {
            r.conditions.window = out->desc.fullscreen ? "fullscreen" : "windowed (projection)";
            r.conditions.outputWidth = out->pixelWidth();
            r.conditions.outputHeight = out->pixelHeight();
        } else {
            r.conditions.window = "windowed (editor canvas)";
            r.conditions.outputWidth = renderWidth_;
            r.conditions.outputHeight = renderHeight_;
        }
        const auto pinned = livePinnedQuality();
        const std::size_t rung = pinned ? static_cast<std::size_t>(*pinned) : autoResolution_.rung();
        r.conditions.liveLevel = liveQualityLevelName(autoResolution_.ladder()[rung].level);
        r.conditions.profile = liveQualityProfile() ? qualityProfileLabel(*liveQualityProfile()) : std::string();
        buildLiveProfile(r, s.frames, true);
        fillLiveProfileResources(r, s.stats);
        fillLiveProfileEntities(r);
        r.candidates = optimizationCandidates(candidateInputs(r, renderer_->qualitySettings()));
        if (liveTransitions_ > s.transitionsSeen) {
            r.notes.push_back(fmt::format("the live level changed {} time(s) WHILE MEASURING; the statistics mix "
                                          "levels and the level at the end is the one reported",
                                          liveTransitions_ - s.transitionsSeen));
        }
        if (o.verifyCandidates <= 0) {
            s.phase = P::Done;
            return false;
        }
        // Verification: the controller is pinned where it settled, so it cannot move under the A/B.
        options_.liveQuality = {true, static_cast<LiveQualityLevel>(rung)};
        s.baseQ = renderer_->qualitySettings();
        s.baseT = renderer_->passToggles();
        for (std::size_t k = 0; k < r.candidates.size() && static_cast<int>(s.queue.size()) < o.verifyCandidates; ++k) {
            rendering::QualitySettings q = s.baseQ;
            rendering::SceneRenderer::PassToggles t = s.baseT;
            if (!r.candidates[k].lever.empty() && applyArm(r.candidates[k].lever, q, t)) {
                s.queue.push_back(k);
            }
        }
        if (s.queue.empty()) {
            s.phase = P::Done;
            return false;
        }
        s.phase = P::Verify;
        s.current = 0;
        s.pair = s.half = s.frameInBlock = 0;
        s.armQ = s.baseQ;
        s.armT = s.baseT;
        applyArm(r.candidates[s.queue[0]].lever, s.armQ, s.armT);
        const bool isArm = false;
        renderer_->setQualitySettings(isArm ? s.armQ : s.baseQ);
        renderer_->setPassToggles(isArm ? s.armT : s.baseT);
        return true;
    }
    case P::Verify: {
        const bool isArm = (s.half == 0) == (s.pair % 2 == 1);
        if (s.frameInBlock >= LiveProfileSession::kSettle) {
            s.wall.push_back(frame.frameMs);
            if (frame.gpuMs >= 0.0) {
                // ADR-1085's reading: the span, capped by the interval (frames overlap on the GPU).
                s.gpu.push_back(std::min(frame.gpuMs, frame.frameMs));
            }
        }
        if (++s.frameInBlock < LiveProfileSession::kSettle + LiveProfileSession::kBlockFrames) {
            return true;
        }
        (isArm ? s.armBlocks : s.baseBlocks).push_back({rendering::describe(s.wall), rendering::describe(s.gpu)});
        s.wall.clear();
        s.gpu.clear();
        s.frameInBlock = 0;
        if (++s.half == 2) {
            s.half = 0;
            ++s.pair;
        }
        if (s.pair == LiveProfileSession::kPairs) {
            LiveProfileCandidate& cand = s.record.candidates[s.queue[s.current]];
            settleCandidate(cand, rendering::compareArms(cand.lever, s.baseBlocks, s.armBlocks));
            log::info("verify '{}' ({}): {:+.2f} ms GPU -> {}", cand.id, cand.lever, cand.measuredSavingMs,
                      cand.measuredVerdict);
            s.baseBlocks.clear();
            s.armBlocks.clear();
            s.pair = 0;
            if (++s.current == s.queue.size()) {
                renderer_->setQualitySettings(s.baseQ);
                renderer_->setPassToggles(s.baseT);
                s.record.verificationMode = fmt::format(
                    "live, in this process: the level pinned where it settled, {} counterbalanced baseline/arm "
                    "pair(s) per candidate, {} frames each after {} settling frames, while playing on; GPU span "
                    "(capped by the interval) medians compared (rendering::compareArms, the --ab machinery)",
                    LiveProfileSession::kPairs, LiveProfileSession::kBlockFrames, LiveProfileSession::kSettle);
                s.phase = P::Done;
                return false;
            }
            s.armQ = s.baseQ;
            s.armT = s.baseT;
            applyArm(s.record.candidates[s.queue[s.current]].lever, s.armQ, s.armT);
        }
        const bool nextIsArm = (s.half == 0) == (s.pair % 2 == 1);
        renderer_->setQualitySettings(nextIsArm ? s.armQ : s.baseQ);
        renderer_->setPassToggles(nextIsArm ? s.armT : s.baseT);
        return true;
    }
    case P::Done:
        return false;
    }
    return false;
}

int Application::finishLiveProfile() {
    LiveProfileSession& s = *liveProfileSession_;
    if (s.phase != LiveProfileSession::Phase::Done) {
        s.record.notes.push_back("the run ended before measurement finished (the window was closed?)");
        if (s.frames.empty()) {
            log::error("live profile: no frame was measured");
            return 2;
        }
        buildLiveProfile(s.record, s.frames, true);
    }
    return writeLiveProfile(s.record);
}

} // namespace avgen::app
