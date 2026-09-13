// Phase G of the renderer upgrade: the scalability curves (G2), and the instrument that makes them
// mean something.  Hidden behind `[.perf]` so they never run in CI.
//
//   tools/gpu-lock.sh ./build/release/tests/avgen_render_tests "[.perf][scalability]"
//
// **The question the whole upgrade is judged on.** Does the frame cost what is *visible*, or what
// *exists*?  A renderer whose cost tracks existence cannot be scaled by authoring more world; a
// renderer whose cost tracks visibility can.  Every other Phase G deliverable is bookkeeping around
// this one.
//
// **Why a curve and not a number.** The question is about a slope, and a slope needs at least two
// points that are separated by more than the machine's own noise.  ADR-131 is the record of what
// happens when that discipline lapses: a single-run sweep found a 52% difference, was written up as
// a decisive minimum, and three repeats dissolved every interval into every other.  The instrument
// was not broken.  One run per arm simply cannot tell a subject that varies from a subject that
// does not.
//
// So this file never reports a curve without its own noise floor.  Three things enforce that, and
// none of them is optional:
//
//   1. **Every arm is measured `kRepeats` times**, and `rendering::SweepPoint` keeps every repeat
//      rather than a mean of them.  `summariseSweep` raises the curve's floor to the worst arm's
//      own repeat spread, so a noisy session cannot certify a difference a quiet one would reject.
//   2. **The repeats are interleaved round-robin across the arms**, not blocked per arm.  A machine
//      that warms up charges its drift to whichever arm is measured last, and that drift then looks
//      exactly like the effect being hunted (docs/renderer-2-benchmark-world.md §5).
//   3. **Every sweep runs a null arm**: the first arm, measured a second time under a second name.
//      Its difference from itself is this session's floor, measured rather than assumed (ADR-113
//      §5).  A null that reports a result is a broken harness, whatever the real arms say.
//
// ---- the three curves ---------------------------------------------------------------------------
//
// The population is a ring of objects around the camera at a fixed radius, so every object is the
// same size on screen and the only thing that changes is how many of them the frustum contains.
// Two knobs, swept independently, which is what separates the two hypotheses:
//
//   * **existence sweep** -- the visible set is held at a fixed count in a narrow wedge in front of
//     the camera, and objects are *added behind it*.  Coverage, triangles drawn and pixels shaded
//     are identical at every arm.  A flat curve means cost tracks visibility.  A rising curve
//     measures, in milliseconds per object, what merely *existing* costs.
//   * **visibility sweep** -- the population is held constant and spread across a widening arc, so a
//     falling fraction of it is in frame.  Cost is expected to fall with the visible count; a flat
//     curve here would mean the renderer is paying for the whole population however little of it is
//     on screen, and would also invalidate the existence sweep's fixture.
//
// They are run on both submission paths, because the two do not cull alike and the difference is
// the finding:
//
//   * **entities** (`scene::Entity`) -- `SceneRenderer` performs no camera-frustum test of its own.
//     `Entity::cameraCulled` is an *input*, written by `scene::Composition` (composition.cpp:1606)
//     and by terrain, per frame.  A scene handed straight to the renderer therefore submits every
//     entity it contains.  Both arms are measured: the flag left alone, and the flag written by the
//     same `world::aabbVisible` test the composition uses.
//   * **procedural instances** (`scene::ProceduralGeometry::instances`) -- culled on the GPU by
//     `shaders/cull.wgsl`, but only when `LodSettings::cull` is set, which defaults to **false**.
//     So the same population, with one boolean changed, gives a paired answer to the headline
//     question on one path in one session.
//
// ---- what this file deliberately does not do -----------------------------------------------------
//
// It states no pass/fail threshold on a millisecond figure.  A hard number here would be a number
// from this machine on this day, and §3.1 of the audit forbids comparing across sessions -- a
// threshold is a cross-session comparison with the other session hidden in a constant.  What it
// does assert is the harness's own integrity: the fixture holds what it claims to hold (the visible
// count really is constant across an existence sweep), the null arm does not report a result, and
// the device logs no errors.  The curves are printed, and the reading of them lives in an ADR where
// it carries its conditions.

#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/render_stats.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/procedural.hpp"
#include "scene/scene.hpp"
#include "support/dense_scene.hpp"
#include "world/terrain.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;

namespace {

// 1280x800 is the audit's resolution (§3.1) and every baseline in the upgrade documents is at it.
// Measuring the curves at a second resolution would produce numbers nothing else can be read
// against, for no gain: these sweeps hold coverage constant on purpose, so resolution is not one of
// their variables.
constexpr std::uint32_t kWidth = 1280;
constexpr std::uint32_t kHeight = 800;

// The frames each measurement takes. The harness discards a leading warm-up for the same reason the
// binary's own headless loop does -- the first frames of an arm pay for pipeline and buffer growth
// that the arm is not being asked about.
constexpr int kWarmup = 8;
constexpr int kMeasured = 24;

// Three is the number ADR-131 needed to overturn a one-run result. Five, because this repository's
// machine is shared between agents and the summary's contention-robust reading takes each arm's two
// fastest repeats: with three, one contended repeat leaves that reading resting on two samples.
constexpr int kRepeats = 5;

// The ring's radius and the objects' size on it, and the count of them held in frame.
//
// **These three are set by the instrument's resolution, not by taste.** The GPU timestamp period on
// this machine is 65.5 us, so a pass that costs 0.3 ms is measured in five ticks and a curve drawn
// through it is drawing through quantisation -- the first version of this file used 48 small boxes
// at 60 m and produced exactly that: every arm reported one of 0.131, 0.262, 0.328 or 0.655 ms, and
// the "curve" was the tick grid. The visible set is therefore made heavy enough that the scene pass
// costs several milliseconds, so one tick is a fraction of a per cent rather than a fifth of the
// signal. It is the same workload at every arm, so making it expensive costs the sweep nothing.
constexpr float kRingRadius = 22.0f;
constexpr float kObjectHalfExtent = 1.1f;
constexpr int kVisible = 384;

// The camera's field of view, set explicitly so the visible arc is arithmetic rather than whatever
// the lens model happened to derive. A 50-degree vertical fov at 1280x800 gives a horizontal half
// angle of atan(tan(25 deg) * 1.6) = 36.7 degrees.
constexpr float kFovY = 0.87f;

std::unique_ptr<gpu::Context> makeContext() {
    static bool logInit = false;
    if (!logInit) {
        log::init(log::Level::Warn);
        logInit = true;
    }
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available");
    }
    return std::move(*ctx);
}

FrameTime frameAt(std::uint64_t index) {
    FrameTime t;
    t.frameIndex = index;
    t.renderTime = static_cast<double>(index) / 60.0;
    t.deltaTime = 1.0 / 60.0;
    return t;
}

// A point on the ring. Angle zero is straight ahead (-Z); positive is to the right.
glm::vec3 onRing(double angleRadians) {
    return {kRingRadius * static_cast<float>(std::sin(angleRadians)), 0.0f,
            -kRingRadius * static_cast<float>(std::cos(angleRadians))};
}

// A deterministic height for the i-th object of a group, so a ring is a wall of objects rather than
// a single row -- coverage, and therefore the fragment cost the sweep needs, comes from stacking as
// well as from count. Deterministic because a random offset would make two arms of the "same"
// workload differ, which is the one thing an existence sweep may not tolerate.
float ringHeight(int index) { return static_cast<float>((index % 9) - 4) * kObjectHalfExtent * 1.7f; }

// `count` angles spread evenly across `arcRadians`, centred on `centreRadians`. A single object
// sits at the centre rather than at an end, so a one-object arm is still in frame.
std::vector<double> spreadAngles(int count, double centreRadians, double arcRadians) {
    std::vector<double> out;
    out.reserve(static_cast<std::size_t>(std::max(count, 0)));
    for (int i = 0; i < count; ++i) {
        const double t = count == 1 ? 0.5 : static_cast<double>(i) / static_cast<double>(count - 1);
        out.push_back(centreRadians + (t - 0.5) * arcRadians);
    }
    return out;
}

// The wedge the visible set occupies: comfortably inside the 36.7-degree horizontal half angle, so
// a rounding difference in the projection cannot move an object in or out of frame and silently
// change the constant the existence sweep is holding.
constexpr double kVisibleWedge = 0.70; // radians, +-20 degrees
// Where the hidden set goes: the arc *behind* the camera, from 120 to 240 degrees. Not merely
// outside the frustum -- behind it, so no near-plane or aspect subtlety can put one back in frame.
constexpr double kHiddenCentre = 3.14159265358979;
constexpr double kHiddenArc = 2.0943951; // 120 degrees

void addRingLightAndCamera(scene::Scene& s) {
    s.environment.showSkybox = false;
    s.environment.backgroundColor = {0.01f, 0.015f, 0.025f};
    s.camera.lens.useExplicitFov = true;
    s.camera.fovYRadians = kFovY;
    s.camera.position = {0.0f, 0.0f, 0.0f};
    s.camera.target = {0.0f, 0.0f, -1.0f};
    s.camera.nearPlane = 0.1f;
    s.camera.farPlane = 400.0f;
    scene::PunctualLight key;
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(-0.3f, -0.6f, -0.7f));
    key.intensity = 3.0f;
    s.addLight(key);
}

// ---- the entity fixture --------------------------------------------------------------------------

// `visible` boxes in the front wedge plus `hidden` boxes behind the camera, as separate entities.
// When `applyCompositionCulling` is set, `Entity::cameraCulled` is written by the same
// `world::aabbVisible` test `scene::Composition` runs -- which is the only thing that makes the
// renderer skip an entity, since `SceneRenderer` never computes the flag itself.
scene::Scene entityRing(int visible, int hidden, bool applyCompositionCulling) {
    scene::Scene s;
    addRingLightAndCamera(s);
    const auto mesh = s.addMesh(testing::denseSceneBox(kObjectHalfExtent));
    int index = 0;
    const auto place = [&](double angle, const char* group, int within) {
        auto& e = s.addEntity(std::string(group) + "-" + std::to_string(index++), mesh);
        e.transform.position = onRing(angle) + glm::vec3(0.0f, ringHeight(within), 0.0f);
        e.material.baseColor = {0.55f, 0.5f, 0.45f};
        e.material.roughness = 0.6f;
    };
    {
        int within = 0;
        for (const double a : spreadAngles(visible, 0.0, kVisibleWedge)) {
            place(a, "front", within++);
        }
        within = 0;
        for (const double a : spreadAngles(hidden, kHiddenCentre, kHiddenArc)) {
            place(a, "back", within++);
        }
    }
    if (applyCompositionCulling) {
        const float aspect = static_cast<float>(kWidth) / static_cast<float>(kHeight);
        const world::FrustumPlanes planes =
            world::frustumPlanes(s.camera.projection(aspect) * s.camera.view());
        for (scene::Entity& e : s.entities) {
            const scene::CullBounds bounds = scene::entityCullBounds(s, e);
            e.cameraCulled = !world::aabbVisible(planes, bounds.min, bounds.max);
        }
    }
    return s;
}

// ---- the instance fixture ------------------------------------------------------------------------

std::uint64_t boxSpecHash() { return 0x5CA1AB1E0001ull; }

scene::InstanceRecord instanceAt(glm::vec3 position) {
    scene::InstanceRecord r{};
    r.position = glm::vec4(position, 1.0f);
    r.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    r.scale = {1.0f, 1.0f, 1.0f, 0.0f};
    r.random = {0.25f, 0.5f, 0.75f, 0.125f};
    r.color = {1.0f, 1.0f, 1.0f, 0.0f};
    r.emissive = {1.0f, 1.0f, 1.0f, 0.0f};
    return r;
}

// The same ring, as one procedural object's instance list. `cull` is the whole experiment on this
// path: `LodSettings::cull` defaults to false, so the identical population is submitted whole or
// culled on the GPU depending on one boolean, in one session, with nothing else different.
scene::Scene instanceRing(int visible, int hidden, bool cull) {
    scene::Scene s;
    addRingLightAndCamera(s);
    scene::ProceduralGeometry g;
    g.name = "ring";
    g.source.kind = scene::PrimitiveKind::Box;
    g.source.size = {kObjectHalfExtent * 2.0f, kObjectHalfExtent * 2.0f, kObjectHalfExtent * 2.0f};
    g.source.subdivisions = 1;
    g.meshHash = boxSpecHash();
    // **Not a constant.** `ProceduralRenderer` re-uploads an instance buffer only when
    // `structureVersion` changes or the instance *count* changes (procedural_renderer.cpp:1473), so
    // two arms with the same name, the same count and different positions are the same arm: the
    // second silently renders the first's instances. The visibility sweep below is exactly that
    // shape -- one population, moved -- and it reported an unculled 8,192 at every arc until this
    // line existed. The version is derived from the arm's own parameters so it cannot be forgotten.
    g.structureVersion = static_cast<std::uint64_t>(visible) * 1000003ull + static_cast<std::uint64_t>(hidden);
    g.material.baseColor = {0.55f, 0.5f, 0.45f};
    g.material.roughness = 0.6f;
    g.lod.cull = cull;
    // No distance or screen-size limit and one LOD level: the only rejection the cull pass may make
    // is the frustum test. A minScreenRadius here would reject by size as well, and the curve would
    // then be measuring two decisions at once.
    g.lod.maxDistance = 0.0f;
    g.lod.minScreenRadius = 0.0f;
    g.lod.lodCount = 1;
    int within = 0;
    for (const double a : spreadAngles(visible, 0.0, kVisibleWedge)) {
        g.instances.push_back(instanceAt(onRing(a) + glm::vec3(0.0f, ringHeight(within++), 0.0f)));
    }
    within = 0;
    for (const double a : spreadAngles(hidden, kHiddenCentre, kHiddenArc)) {
        g.instances.push_back(instanceAt(onRing(a) + glm::vec3(0.0f, ringHeight(within++), 0.0f)));
    }
    s.procedurals.push_back(std::move(g));
    return s;
}

// A ring of `count` instances spread across `arcDegrees`, all of them existing, a computable
// fraction of them in frame. This is the visibility sweep's fixture.
scene::Scene instanceArc(int count, double arcDegrees, bool cull) {
    scene::Scene s;
    addRingLightAndCamera(s);
    scene::ProceduralGeometry g;
    g.name = "arc";
    g.source.kind = scene::PrimitiveKind::Box;
    g.source.size = {kObjectHalfExtent * 2.0f, kObjectHalfExtent * 2.0f, kObjectHalfExtent * 2.0f};
    g.source.subdivisions = 1;
    g.meshHash = boxSpecHash();
    // See instanceRing: the arc *is* the structure here, since the count never moves.
    g.structureVersion = static_cast<std::uint64_t>(arcDegrees * 1000.0) + 1ull;
    g.material.baseColor = {0.55f, 0.5f, 0.45f};
    g.material.roughness = 0.6f;
    g.lod.cull = cull;
    g.lod.lodCount = 1;
    const double arc = arcDegrees * 3.14159265358979 / 180.0;
    int within = 0;
    for (const double a : spreadAngles(count, 0.0, arc)) {
        g.instances.push_back(instanceAt(onRing(a) + glm::vec3(0.0f, ringHeight(within++), 0.0f)));
    }
    s.procedurals.push_back(std::move(g));
    return s;
}

// ---- measuring -----------------------------------------------------------------------------------

struct Measurement {
    double sceneMs = -1.0;
    double gpuFrameMs = -1.0;
    double draws = 0.0;
    double triangles = 0.0;
    double visibleInstances = 0.0;
    double culledInstances = 0.0;
    double entities = 0.0;
};

double medianOf(std::vector<double> v) {
    if (v.empty()) {
        return -1.0;
    }
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

// One repeat of one arm: build, warm up, measure. The renderer is *not* rebuilt between arms, and
// that is deliberate -- constructing a `SceneRenderer` compiles pipelines, and an arm that paid for
// that would report it as its own cost. The consequence is that the buffers are sized by the
// largest arm seen so far, which round-robin interleaving settles after the first round.
Measurement measureOnce(rendering::SceneRenderer& renderer, const scene::Scene& s) {
    renderer.procedurals().setViewport(kWidth, kHeight);
    std::vector<double> sceneMs;
    std::vector<double> frameMs;
    Measurement out;
    for (int i = 0; i < kWarmup + kMeasured; ++i) {
        const auto drawn = renderer.renderFrame(s, frameAt(static_cast<std::uint64_t>(i)), kWidth, kHeight);
        REQUIRE(drawn.has_value());
        if (i < kWarmup) {
            continue;
        }
        const double pass = renderer.timeline().msFor("scene");
        if (pass >= 0.0) {
            sceneMs.push_back(pass);
        }
        const rendering::RenderStats& st = renderer.stats();
        if (st.gpuFrameMs >= 0.0) {
            frameMs.push_back(st.gpuFrameMs);
        }
    }
    const rendering::RenderStats& st = renderer.stats();
    out.sceneMs = medianOf(std::move(sceneMs));
    out.gpuFrameMs = medianOf(std::move(frameMs));
    out.draws = st.drawCalls;
    out.triangles = static_cast<double>(st.geometry.camera.triangles);
    out.visibleInstances = static_cast<double>(st.visibleInstances);
    out.culledInstances = static_cast<double>(st.culledInstances);
    out.entities = st.entities;
    return out;
}

struct Arm {
    std::string name;
    double x = 0.0;
    std::function<scene::Scene()> build;
};

// Runs every arm `kRepeats` times, round-robin, and returns the curve. Round-robin rather than
// arm-at-a-time: the machine drifts, and blocking gives the drift a preferred victim.
rendering::SweepSummary runSweep(rendering::SceneRenderer& renderer, const std::string& subject,
                                 const std::string& xLabel, const std::vector<Arm>& arms) {
    std::vector<rendering::SweepPoint> points(arms.size());
    for (std::size_t i = 0; i < arms.size(); ++i) {
        points[i].arm = arms[i].name;
        points[i].x = arms[i].x;
    }
    for (int repeat = 0; repeat < kRepeats; ++repeat) {
        for (std::size_t i = 0; i < arms.size(); ++i) {
            const scene::Scene s = arms[i].build();
            const Measurement m = measureOnce(renderer, s);
            if (m.sceneMs >= 0.0) {
                points[i].repeats.push_back(m.sceneMs);
            }
            // The counters are re-recorded every repeat rather than trusted from the first: a
            // counter that moves between repeats means the fixture is not the fixed thing the
            // curve claims, and overwriting makes the last repeat's value the one reported, which
            // the assertions below then check against the intended constant.
            points[i].draws = m.draws;
            points[i].triangles = m.triangles;
            points[i].visibleInstances = m.visibleInstances;
            points[i].culledInstances = m.culledInstances;
            points[i].entities = m.entities;
        }
    }
    return rendering::summariseSweep(subject, xLabel, "scene ms", std::move(points));
}

// The null experiment (ADR-113 §5). The first arm is duplicated under a second name, so the last
// two entries of every sweep are the same workload measured twice. Their difference is what this
// session can resolve; a curve reporting a real difference smaller than it has reported noise.
void appendNullArm(std::vector<Arm>& arms) {
    REQUIRE(!arms.empty());
    Arm nullArm = arms.front();
    nullArm.name = "null (" + arms.front().name + ")";
    arms.push_back(std::move(nullArm));
}

// The null's own reading, taken off the finished summary. Returns the percentage difference between
// the first arm and its duplicate.
double nullDifferencePercent(const rendering::SweepSummary& summary) {
    if (summary.points.size() < 2) {
        return 0.0;
    }
    const rendering::SweepPoint& a = summary.points.front();
    const rendering::SweepPoint& b = summary.points.back();
    if (!a.stats.valid() || !b.stats.valid() || a.stats.p50 <= 0.0) {
        return 0.0;
    }
    return (b.stats.p50 - a.stats.p50) / a.stats.p50 * 100.0;
}

void reportNull(const rendering::SweepSummary& summary) {
    const double diff = nullDifferencePercent(summary);
    std::printf("  null arm: %+.2f%% against a floor of %.2f%% -- %s\n\n", diff,
                summary.noiseFloorPercent,
                std::abs(diff) < summary.noiseFloorPercent
                    ? "the harness measures itself as no result, which is what it must do"
                    : "THE HARNESS REPORTS A RESULT AGAINST ITSELF: nothing in this table is evidence");
    // ADR-113 §5: a null that clears the floor means the session cannot certify anything, and that
    // is the finding rather than an excuse to reach for the real arms anyway.
    CHECK(std::abs(diff) < summary.noiseFloorPercent);
}

} // namespace

// ---- G2, curve 1: what does an entity cost merely by existing? ------------------------------------

TEST_CASE("scene-pass cost against entity count at constant visibility", "[.perf][scalability][entities]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    const std::vector<int> hiddenCounts = {0, 256, 1024, 4096, 8192};

    // The entity path's two arms are two *curves*, not two points, because the claim being tested is
    // about a slope: the culled curve should be flat and the uncrossed one should rise.
    for (const bool culled : {false, true}) {
        std::vector<Arm> arms;
        for (const int hidden : hiddenCounts) {
            arms.push_back({"E=" + std::to_string(kVisible + hidden),
                            static_cast<double>(kVisible + hidden),
                            [hidden, culled] { return entityRing(kVisible, hidden, culled); }});
        }
        appendNullArm(arms);
        const rendering::SweepSummary summary = runSweep(
            renderer,
            std::string("entities, ") + (culled ? "composition culling applied" : "no culling: the renderer's own behaviour") +
                " -- " + std::to_string(kVisible) + " in frame at every arm, the rest behind the camera",
            "entities", arms);
        std::printf("\n%s", rendering::sweepTable(summary).c_str());
        reportNull(summary);

        // The fixture's own contract. Without this the sweep could be flat because nothing was ever
        // drawn, which is indistinguishable from the result it is hoping for.
        // `RenderStats::entities` counts the entities the frame *submitted*, not the entities the
        // scene contains -- which is itself the finding this curve is about, and is checked in both
        // directions rather than assumed in one.
        for (const rendering::SweepPoint& point : summary.points) {
            INFO("arm " << point.arm);
            CHECK(point.entities == (culled ? static_cast<double>(kVisible) : point.x));
            CHECK(point.draws > 0.0);
        }
        if (culled) {
            // Culling applied: the submitted triangle count must be the *visible* set's, identical
            // at every arm. If this moves, the arms are not the same workload and the curve is
            // measuring the fixture.
            const double first = summary.points.front().triangles;
            for (const rendering::SweepPoint& point : summary.points) {
                INFO("arm " << point.arm << " submitted " << point.triangles << " triangles");
                CHECK(point.triangles == first);
            }
        }
    }
    CHECK(ctx->errorCount() == 0);
}

// ---- G2, curve 2: the same question on the instanced path -----------------------------------------

TEST_CASE("scene-pass cost against instance count at constant visibility",
          "[.perf][scalability][instances]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    // An order of magnitude further than the entity sweep, because this is the path Glowmere's
    // ecology uses and it carries 116,978 instances there (§3.2).
    const std::vector<int> hiddenCounts = {0, 256, 1024, 4096, 16384, 65536};

    for (const bool cull : {false, true}) {
        std::vector<Arm> arms;
        for (const int hidden : hiddenCounts) {
            arms.push_back({"N=" + std::to_string(kVisible + hidden),
                            static_cast<double>(kVisible + hidden),
                            [hidden, cull] { return instanceRing(kVisible, hidden, cull); }});
        }
        appendNullArm(arms);
        const rendering::SweepSummary summary =
            runSweep(renderer,
                     std::string("procedural instances, LodSettings::cull = ") + (cull ? "true" : "false") +
                         " -- " + std::to_string(kVisible) + " in frame at every arm, the rest behind the camera",
                     "instances", arms);
        std::printf("\n%s", rendering::sweepTable(summary).c_str());
        reportNull(summary);

        if (cull) {
            for (const rendering::SweepPoint& point : summary.points) {
                INFO("arm " << point.arm);
                // The cull pass must find exactly the front wedge, at every population size.
                CHECK(point.visibleInstances == static_cast<double>(kVisible));
            }
        } else {
            // With culling off there is no cull pass, so `visibleInstances` is never written and
            // stays zero -- "not measured", not "nothing visible". What proves the whole population
            // was submitted is the triangle count: twelve per box, every instance, every arm.
            for (const rendering::SweepPoint& point : summary.points) {
                INFO("arm " << point.arm);
                CHECK(point.visibleInstances == 0.0);
                CHECK(point.triangles == point.x * 12.0);
            }
        }
    }
    CHECK(ctx->errorCount() == 0);
}

// ---- G2, curve 3: cost against how much of a fixed population is on screen -------------------------

TEST_CASE("scene-pass cost against the visible fraction of a fixed population",
          "[.perf][scalability][visibility]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    // One population, spread over a widening arc. Existence never changes; the frustum simply holds
    // less of it. This is the control for the two existence sweeps: if cost does not fall here, a
    // flat existence curve means nothing was being culled rather than that culling worked.
    constexpr int kPopulation = 8192;
    const std::vector<double> arcs = {70.0, 120.0, 240.0, 360.0};

    std::vector<Arm> arms;
    for (const double arc : arcs) {
        arms.push_back({"arc=" + std::to_string(static_cast<int>(arc)) + "deg", arc,
                        [arc] { return instanceArc(kPopulation, arc, true); }});
    }
    appendNullArm(arms);
    const rendering::SweepSummary summary =
        runSweep(renderer,
                 "procedural instances, culled -- " + std::to_string(kPopulation) +
                     " instances at every arm, spread across a widening arc so fewer are in frame",
                 "arc deg", arms);
    std::printf("\n%s", rendering::sweepTable(summary).c_str());
    reportNull(summary);

    // The fixture proof: existence really is constant and visibility really does fall.
    for (const rendering::SweepPoint& point : summary.points) {
        INFO("arm " << point.arm);
        CHECK(point.visibleInstances + point.culledInstances == static_cast<double>(kPopulation));
    }
    // The narrowest arc fits inside the frustum, so nothing is culled there; the widest wraps the
    // camera and most of the population is behind it. If this does not hold, the fixture is not
    // varying visibility and the curve is measuring nothing -- which is precisely what happened
    // before `structureVersion` was made a function of the arm (see instanceRing's note).
    CHECK(summary.points.front().visibleInstances == static_cast<double>(kPopulation));
    CHECK(summary.points[arcs.size() - 1].visibleInstances < static_cast<double>(kPopulation) / 2.0);
    CHECK(ctx->errorCount() == 0);
}

// ---- the statistics themselves --------------------------------------------------------------------
//
// GPU-free, and not hidden: the arithmetic that decides whether a curve is a result is the last
// thing that should only be exercised by a benchmark somebody runs by hand.

TEST_CASE("a sweep carries the noise floor its own arms measured", "[perf][scalability][stats]") {
    using rendering::SweepPoint;

    SECTION("a flat curve inside its own noise is not a result") {
        std::vector<SweepPoint> points;
        points.push_back({"a", 1.0, {10.0, 10.1, 9.9}, {}, 0.0, 0, 0, 0, 0, 0});
        points.push_back({"b", 2.0, {10.05, 10.0, 10.1}, {}, 0.0, 0, 0, 0, 0, 0});
        const auto s = rendering::summariseSweep("flat", "x", "ms", points);
        CHECK(s.noiseFloorPercent >= rendering::kGpuNoiseFloorPercent);
        CHECK_FALSE(s.endpointsSeparated);
        CHECK_FALSE(s.haveStep);
    }

    SECTION("a curve that clears its floor is a result, and the slope is per unit x") {
        std::vector<SweepPoint> points;
        points.push_back({"a", 0.0, {10.0, 10.0, 10.0}, {}, 0.0, 0, 0, 0, 0, 0});
        points.push_back({"b", 100.0, {20.0, 20.0, 20.0}, {}, 0.0, 0, 0, 0, 0, 0});
        const auto s = rendering::summariseSweep("rising", "x", "ms", points);
        CHECK(s.endpointsSeparated);
        CHECK(s.endpointChangePercent == 100.0);
        CHECK(s.slopePerUnitX == 0.1);
    }

    SECTION("one noisy arm raises the floor for the whole curve") {
        // The exact failure ADR-131 records: a real-looking 20% endpoint difference, and one arm
        // whose own repeats span more than that. The curve must refuse to call it.
        std::vector<SweepPoint> points;
        points.push_back({"a", 0.0, {10.0, 10.0, 10.0}, {}, 0.0, 0, 0, 0, 0, 0});
        points.push_back({"b", 1.0, {8.0, 12.0, 10.0}, {}, 0.0, 0, 0, 0, 0, 0});
        points.push_back({"c", 2.0, {12.0, 12.0, 12.0}, {}, 0.0, 0, 0, 0, 0, 0});
        const auto s = rendering::summariseSweep("noisy", "x", "ms", points);
        CHECK(s.noiseFloorPercent == 40.0); // arm b: (12 - 8) / 10
        CHECK(s.endpointChangePercent == 20.0);
        CHECK_FALSE(s.endpointsSeparated);
    }

    SECTION("a knee in the middle is found even when the ends agree") {
        std::vector<SweepPoint> points;
        points.push_back({"a", 0.0, {10.0, 10.0, 10.0}, {}, 0.0, 0, 0, 0, 0, 0});
        points.push_back({"b", 1.0, {30.0, 30.0, 30.0}, {}, 0.0, 0, 0, 0, 0, 0});
        points.push_back({"c", 2.0, {10.0, 10.0, 10.0}, {}, 0.0, 0, 0, 0, 0, 0});
        const auto s = rendering::summariseSweep("knee", "x", "ms", points);
        CHECK_FALSE(s.endpointsSeparated);
        CHECK(s.haveStep);
        CHECK(s.largestStepPercent == 200.0);
        CHECK(s.points[s.largestStepIndex].arm == "b");
    }

    SECTION("an arm that failed to measure is kept, and is never an endpoint") {
        std::vector<SweepPoint> points;
        points.push_back({"broken", 0.0, {}, {}, 0.0, 0, 0, 0, 0, 0});
        points.push_back({"a", 1.0, {10.0, 10.0, 10.0}, {}, 0.0, 0, 0, 0, 0, 0});
        points.push_back({"b", 2.0, {20.0, 20.0, 20.0}, {}, 0.0, 0, 0, 0, 0, 0});
        const auto s = rendering::summariseSweep("gappy", "x", "ms", points);
        CHECK(s.points.size() == 3);
        CHECK_FALSE(s.points.front().stats.valid());
        CHECK(s.endpointChangePercent == 100.0);
        CHECK(s.slopePerUnitX == 10.0);
    }
}
