// C1 of the renderer upgrade's Phase C: **how much quad overdraw does Glowmere actually have, at
// the triangle count it has now?**
//
//   avgen_tests "[.analysis][triangles]" --success
//
// Hidden behind a `.` tag: it loads a world, generates every scatter and walks a hundred thousand
// instances, which is not something CI should do on every push. It is committed rather than run
// once and pasted into a document because §4.5's own lesson was that an instrument living in a
// screenshot is not an instrument.
//
// ---- why this and not the Overdraw debug view --------------------------------------------------
//
// ADR-115 added an `Overdraw` and a `FragmentDensity` view that count fragment invocations per
// pixel in a dedicated pass. They are the right instrument for the question and they cannot answer
// it here: that pass draws **plain, non-skinned opaque entities only** -- "procedural instances,
// SDFs, particles and transparency are outside its scope", in its own words -- and Glowmere's
// geometry is overwhelmingly procedural scatter. The view is not blank, but what it shows is the
// terrain and the handful of authored meshes, which is the part of the frame that is *not* the
// problem. That limit is recorded in the docs rather than worked around here, because extending
// the counting pass to the procedural path is a change to the procedural renderer and belongs to
// whoever owns it.
//
// ---- what this measures instead -----------------------------------------------------------------
//
// Every drawable the camera submits, with the projected size of its triangles, computed the same
// way the renderer computes projected radius (shaders/cull.wgsl), and with the LOD level the GPU
// ladder would pick for each instance -- the classification is transcribed from `cullLodLevel` in
// procedural_renderer.cpp. Triangles are then bucketed by pixels per triangle and weighted by the
// screen area they cover.
//
// **What it is and is not.** It is a CPU estimate of a GPU quantity. `pixelsPerTriangle` rests on
// Cauchy's formula and a closed-mesh assumption (rendering/importance.hpp states both); coverage is
// summed across drawables and so counts a pixel once per drawable over it, which is invocations
// before hidden-surface removal rather than pixels stored. It does not replace a GPU counter. What
// it does do is place Glowmere on §4.5's measured curve, which is the decision C1 exists to inform.

#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "rendering/importance.hpp"
#include "scene/composition.hpp"
#include "scene/mesh_metrics.hpp"
#include "scene/procedural.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;

namespace {

constexpr const char* kScene = AVGEN_SOURCE_DIR "/examples/world/glowmere-stylized.scene.json";
constexpr std::uint32_t kWidth = 1280;  // the baseline's resolution (§3.1)
constexpr std::uint32_t kHeight = 800;

// §4.5's sweep, depth control subtracted and normalised by its own minimum. The cost of a pixel of
// coverage, relative to the cheapest tessellation measured, as a function of pixels per triangle.
//
// Read it as the shape of a curve rather than as milliseconds: the sweep's shader is simpler than
// Glowmere's, so the absolute times do not transfer and §4.5 says so. Both ends are load-bearing --
// the left is quad overdraw, and the right is the caveat that two screen-filling triangles cost
// *more* than 2,048 because they are poor for tile parallelism.
struct CurvePoint {
    float pixelsPerTriangle;
    float relativeCost;
};
constexpr std::array<CurvePoint, 8> kGateCurve{{
    {0.49f, 5.05f},
    {0.99f, 4.73f},
    {1.95f, 4.05f},
    {3.95f, 2.82f},
    {7.8f, 2.18f},
    {63.0f, 1.78f},
    {500.0f, 1.00f},
    {512000.0f, 2.01f},
}};

// Log-linear interpolation, clamped at both ends. Clamped rather than extrapolated: past either end
// the curve has no measurements behind it and a confident extrapolation would be exactly the kind of
// number this exercise exists to avoid.
float relativeCost(float pixelsPerTriangle) {
    const float p = std::max(pixelsPerTriangle, 1e-6f);
    if (p <= kGateCurve.front().pixelsPerTriangle) {
        return kGateCurve.front().relativeCost;
    }
    if (p >= kGateCurve.back().pixelsPerTriangle) {
        return kGateCurve.back().relativeCost;
    }
    for (std::size_t i = 1; i < kGateCurve.size(); ++i) {
        if (p <= kGateCurve[i].pixelsPerTriangle) {
            const float lo = std::log(kGateCurve[i - 1].pixelsPerTriangle);
            const float hi = std::log(kGateCurve[i].pixelsPerTriangle);
            const float t = (std::log(p) - lo) / (hi - lo);
            return kGateCurve[i - 1].relativeCost +
                   t * (kGateCurve[i].relativeCost - kGateCurve[i - 1].relativeCost);
        }
    }
    return kGateCurve.back().relativeCost;
}

// Buckets, in pixels per triangle. The boundaries are the ones the measurement named: the quad
// threshold at 4, the bracket the knee falls in (3.95 to 7.8), and the cheap band around 500.
constexpr std::array<float, 8> kBucketEdges{{0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 64.0f, 512.0f, 4096.0f}};

struct Bucket {
    std::uint64_t triangles = 0;
    double coverage = 0.0;
    std::uint64_t drawables = 0;
};

std::size_t bucketOf(float pixelsPerTriangle) {
    for (std::size_t i = 0; i < kBucketEdges.size(); ++i) {
        if (pixelsPerTriangle < kBucketEdges[i]) {
            return i;
        }
    }
    return kBucketEdges.size();
}

struct Totals {
    std::array<Bucket, kBucketEdges.size() + 1> buckets{};
    std::uint64_t triangles = 0;
    std::uint64_t drawables = 0;
    double coverage = 0.0;
    double weightedCost = 0.0; // sum of coverage * relativeCost(px/tri)
    // The same sum, split at the cheap band's own position. Everything below 500 px/triangle rests
    // on seven measured points; everything above it rests on **one** -- the 2-triangle case at
    // 512,000 px/triangle -- with nothing between. So the two halves of the excess are not equally
    // well evidenced and are never added up without being shown apart.
    double weightedCostBelow = 0.0;
    double coverageBelow = 0.0;
    double weightedCostAbove = 0.0;
    double coverageAbove = 0.0;

    void add(float pixelsPerTriangle, std::uint64_t triangles_, double coverage_) {
        buckets[bucketOf(pixelsPerTriangle)].triangles += triangles_;
        buckets[bucketOf(pixelsPerTriangle)].coverage += coverage_;
        buckets[bucketOf(pixelsPerTriangle)].drawables += 1;
        triangles += triangles_;
        drawables += 1;
        coverage += coverage_;
        const double cost = coverage_ * static_cast<double>(relativeCost(pixelsPerTriangle));
        weightedCost += cost;
        if (pixelsPerTriangle < 500.0f) {
            weightedCostBelow += cost;
            coverageBelow += coverage_;
        } else {
            weightedCostAbove += cost;
            coverageAbove += coverage_;
        }
    }
    // The multiplier over the same coverage drawn entirely in the cheap band. 1.0 means there is
    // nothing here for a representation system to recover.
    [[nodiscard]] double excess() const { return coverage > 0.0 ? weightedCost / coverage : 1.0; }
    // The part of the excess carried by triangles that are too small -- the measured half.
    [[nodiscard]] double excessBelow() const {
        return coverage > 0.0 ? (weightedCostBelow + coverageAbove) / coverage : 1.0;
    }
    // The part carried by triangles that are too large -- the half with one measurement behind it.
    [[nodiscard]] double excessAbove() const {
        return coverage > 0.0 ? (weightedCostAbove + coverageBelow) / coverage : 1.0;
    }
    // Triangles this walk saw but did not model, and why. Printed rather than dropped: a total that
    // silently omits geometry is exactly the shape of §4.5's first, wrong, null result.
    std::uint64_t skippedBehindCamera = 0;
};

// shaders/cull.wgsl / procedural_renderer.cpp `cullLodLevel`, transcribed. -1 means culled.
// Transcribed rather than called because it lives in an anonymous namespace inside the renderer;
// the alternative -- exporting it for a test -- would widen the renderer's surface for a
// diagnostic, which is the trade ADR-115 already refused once.
int lodLevelFor(const scene::LodSettings& lod, const std::array<glm::vec4, 6>& planes,
                glm::vec3 cameraPosition, float projScale, glm::vec3 center, float radius) {
    const int lodCount = std::clamp(lod.lodCount, 1, scene::kMaxLodLevels);
    const float distance = glm::length(center - cameraPosition);
    const float screenRadius = radius / std::max(distance, 1e-4f) * projScale;
    if (lod.cull) {
        for (const glm::vec4& plane : planes) {
            if (glm::dot(glm::vec3(plane), center) + plane.w < -radius) {
                return -1;
            }
        }
        if (lod.maxDistance > 0.0f && distance - radius > lod.maxDistance) {
            return -1;
        }
        if (lod.minScreenRadius > 0.0f && screenRadius < lod.minScreenRadius) {
            return -1;
        }
    }
    int level = 0;
    for (int k = 0; k + 1 < lodCount && k < 3; ++k) {
        const float threshold = lod.lodDistances[k];
        if (!(threshold > 0.0f)) {
            break;
        }
        const bool take = lod.lodByScreenSize ? screenRadius <= threshold : distance >= threshold;
        if (!take) {
            break;
        }
        level = k + 1;
    }
    return level;
}

std::array<glm::vec4, 6> frustumOf(const glm::mat4& viewProj) {
    std::array<glm::vec4, 6> planes{};
    const glm::mat4 m = glm::transpose(viewProj);
    planes[0] = m[3] + m[0];
    planes[1] = m[3] - m[0];
    planes[2] = m[3] + m[1];
    planes[3] = m[3] - m[1];
    planes[4] = m[2];
    planes[5] = m[3] - m[2];
    for (glm::vec4& p : planes) {
        const float len = glm::length(glm::vec3(p));
        if (len > 0.0f) {
            p /= len;
        }
    }
    return planes;
}

void report(const char* label, const Totals& t) {
    std::printf("\n%s\n", label);
    std::printf("  drawables %llu   triangles %llu   coverage %.0f px (%.2fx the frame)\n",
                static_cast<unsigned long long>(t.drawables),
                static_cast<unsigned long long>(t.triangles), t.coverage,
                t.coverage / (static_cast<double>(kWidth) * kHeight));
    std::printf("  %-18s %12s %8s %12s %8s\n", "px/triangle", "triangles", "share", "coverage", "share");
    for (std::size_t i = 0; i <= kBucketEdges.size(); ++i) {
        const Bucket& b = t.buckets[i];
        if (b.triangles == 0 && b.coverage == 0.0) {
            continue;
        }
        char range[32];
        if (i == 0) {
            std::snprintf(range, sizeof(range), "< %.2g", static_cast<double>(kBucketEdges[0]));
        } else if (i == kBucketEdges.size()) {
            std::snprintf(range, sizeof(range), ">= %.6g", static_cast<double>(kBucketEdges.back()));
        } else {
            std::snprintf(range, sizeof(range), "%.6g - %.6g", static_cast<double>(kBucketEdges[i - 1]),
                          static_cast<double>(kBucketEdges[i]));
        }
        std::printf("  %-18s %12llu %7.1f%% %12.0f %7.1f%%\n", range,
                    static_cast<unsigned long long>(b.triangles),
                    100.0 * static_cast<double>(b.triangles) / static_cast<double>(std::max<std::uint64_t>(t.triangles, 1)),
                    b.coverage, 100.0 * b.coverage / std::max(t.coverage, 1e-9));
    }
    double belowQuad = 0.0;
    std::uint64_t trisBelowQuad = 0;
    for (std::size_t i = 0; i < bucketOf(rendering::ImportanceRecord::kQuadThreshold); ++i) {
        belowQuad += t.buckets[i].coverage;
        trisBelowQuad += t.buckets[i].triangles;
    }
    std::printf("  below the 4 px quad threshold: %.1f%% of triangles, %.1f%% of coverage\n",
                100.0 * static_cast<double>(trisBelowQuad) / static_cast<double>(std::max<std::uint64_t>(t.triangles, 1)),
                100.0 * belowQuad / std::max(t.coverage, 1e-9));
    std::printf("  estimated fragment-cost multiplier over the same coverage in the cheap band: %.2fx\n",
                t.excess());
    std::printf("    of which triangles too SMALL (< 500 px/tri, seven measured points): %.2fx\n",
                t.excessBelow());
    std::printf("    of which triangles too LARGE (>= 500 px/tri, ONE measured point at 512,000,\n"
                "      everything between interpolated and therefore not evidence): %.2fx\n",
                t.excessAbove());
    std::printf("  => an ideal representation system could recover at most %.0f%% of the\n"
                "     triangle-size-sensitive part of the fragment cost (%.0f%% of it from the\n"
                "     measured, too-small half).\n",
                100.0 * (1.0 - 1.0 / std::max(t.excess(), 1e-9)),
                100.0 * (1.0 - 1.0 / std::max(t.excessBelow(), 1e-9)));
    if (t.skippedBehindCamera > 0) {
        std::printf("  not modelled: %llu triangles whose bounding centre is behind the camera but\n"
                    "     whose bounds the renderer's frustum test may still keep.\n",
                    static_cast<unsigned long long>(t.skippedBehindCamera));
    }
}

} // namespace

TEST_CASE("Glowmere's triangles, sized", "[.analysis][triangles]") {
    if (!std::filesystem::exists(kScene)) {
        SKIP("the Glowmere scene is not present");
    }
    assets::AssetRegistry registry;
    registry.setBaseDirectory(std::filesystem::path(kScene).parent_path());
    auto loaded = scene::Composition::loadFile(kScene, registry);
    REQUIRE(loaded);
    auto comp = std::move(*loaded);
    params::ParameterSet params;
    params::Modulator modulator;
    comp->attach(params, modulator);
    comp->setViewport(kWidth, kHeight);
    comp->update(FrameTime{});

    const scene::Scene& scn = comp->scene();
    const auto view = rendering::ViewContext::fromCamera(scn.camera, kWidth, kHeight);
    const float aspect = static_cast<float>(kWidth) / static_cast<float>(kHeight);
    const auto planes = frustumOf(scn.camera.projection(aspect) * scn.camera.view());
    const double frameArea = static_cast<double>(kWidth) * kHeight;

    Totals all;
    Totals entitiesOnly;
    Totals proceduralOnly;
    std::map<std::string, Totals> byLayer;

    // ---- authored entities (terrain chunks, hero meshes, props) ---------------------------------
    scene::MeshMetricsCache cache;
    for (const scene::Entity& e : scn.entities) {
        if (!e.visible || e.cameraCulled || e.mesh == scene::kInvalidMesh) {
            continue;
        }
        const scene::MeshMetrics& m = cache.metrics(scn, e.mesh);
        if (!m.valid()) {
            continue;
        }
        const glm::vec3 scale = e.transform.scale;
        const float maxScale = std::max({std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)});
        rendering::ImportanceInput in;
        in.center = glm::vec3(e.transform.matrix() * glm::vec4(m.boundsCenter, 1.0f));
        in.radius = m.boundsRadius * maxScale;
        in.surfaceArea = m.surfaceArea * scene::MeshMetrics::areaScale(scale);
        in.triangles = m.triangles;
        const auto r = rendering::ImportanceEvaluator::evaluate(view, in);
        if (!r.inFront || r.pixelsPerTriangle <= 0.0f) {
            all.skippedBehindCamera += m.triangles;
            entitiesOnly.skippedBehindCamera += m.triangles;
            continue;
        }
        const double coverage = std::min(static_cast<double>(r.projectedArea), frameArea);
        all.add(r.pixelsPerTriangle, m.triangles, coverage);
        entitiesOnly.add(r.pixelsPerTriangle, m.triangles, coverage);
    }

    // ---- procedural scatter, per instance, at the LOD level the GPU ladder would pick -----------
    for (const scene::ProceduralGeometry& object : scn.procedurals) {
        if (!object.visible || object.instances.empty()) {
            continue;
        }
        scene::GenerationContext ctx;
        ctx.objects = &scn.procedurals;
        ctx.splines = &scn.splines;
        auto source = object.resolveSourceMesh(ctx);
        if (!source) {
            continue;
        }
        const int levels = std::clamp(object.lod.lodCount, 1, scene::kMaxLodLevels);
        // One metric per rung, computed once for the object rather than once per instance.
        std::array<scene::MeshMetrics, scene::kMaxLodLevels> rung{};
        rung[0] = scene::meshMetrics(*source);
        for (int level = 1; level < levels; ++level) {
            auto reduced = scene::makeLodMesh(object.source, level, object.lod.impostorSize);
            rung[static_cast<std::size_t>(level)] = reduced ? scene::meshMetrics(*reduced) : rung[0];
        }
        if (!rung[0].valid()) {
            continue;
        }
        Totals& layer = byLayer[object.name];
        for (const scene::InstanceRecord& record : object.instances) {
            const glm::vec3 center(record.position);
            const glm::vec3 scale = glm::abs(glm::vec3(record.scale));
            const float maxScale = std::max({scale.x, scale.y, scale.z});
            const float radius = rung[0].boundsRadius * maxScale;
            const int level = lodLevelFor(object.lod, planes, view.cameraPosition, view.pixelsPerUnit,
                                          center, radius);
            if (level < 0) {
                continue;
            }
            const scene::MeshMetrics& m = rung[static_cast<std::size_t>(level)];
            if (!m.valid()) {
                continue;
            }
            rendering::ImportanceInput in;
            in.center = center;
            in.radius = radius;
            in.surfaceArea = m.surfaceArea * scene::MeshMetrics::areaScale(scale);
            in.triangles = m.triangles;
            const auto r = rendering::ImportanceEvaluator::evaluate(view, in);
            if (!r.inFront || r.pixelsPerTriangle <= 0.0f) {
                all.skippedBehindCamera += m.triangles;
                proceduralOnly.skippedBehindCamera += m.triangles;
                continue;
            }
            const double coverage = std::min(static_cast<double>(r.projectedArea), frameArea);
            all.add(r.pixelsPerTriangle, m.triangles, coverage);
            proceduralOnly.add(r.pixelsPerTriangle, m.triangles, coverage);
            layer.add(r.pixelsPerTriangle, m.triangles, coverage);
        }
    }

    std::printf("\n================ Glowmere triangle-size analysis, %ux%u ================\n",
                kWidth, kHeight);
    std::printf("reference: the renderer reports 264,305 camera triangles for this scene (§3.2.1).\n"
                "Whatever this walk totals below is its own accounting, from the CPU, and the gap\n"
                "between the two is the part it does not model -- not a correction to either.\n");
    report("ALL SUBMITTED GEOMETRY", all);
    report("authored entities (terrain chunks, heroes, props)", entitiesOnly);
    report("procedural scatter (the ecology)", proceduralOnly);

    std::printf("\nper scatter layer, worst first by estimated excess:\n");
    std::vector<std::pair<std::string, Totals>> layers(byLayer.begin(), byLayer.end());
    std::sort(layers.begin(), layers.end(), [](const auto& a, const auto& b) {
        return a.second.weightedCost - a.second.coverage > b.second.weightedCost - b.second.coverage;
    });
    std::printf("  %-40s %10s %10s %8s %10s\n", "layer", "instances", "triangles", "excess", "coverage");
    for (const auto& [name, t] : layers) {
        if (t.triangles == 0) {
            continue;
        }
        std::printf("  %-40s %10llu %10llu %7.2fx %10.0f\n", name.c_str(),
                    static_cast<unsigned long long>(t.drawables),
                    static_cast<unsigned long long>(t.triangles), t.excess(), t.coverage);
    }
    std::printf("\n");

    // The analysis has to have found the scene at all. Not an assertion about the answer -- that is
    // a measurement and measurements move -- but about the instrument: §4.5's own first run was a
    // clean, convincing null result produced by rendering nothing.
    CHECK(all.triangles > 0);
    CHECK(all.coverage > 0.0);
    CHECK(proceduralOnly.triangles > 0);
}
