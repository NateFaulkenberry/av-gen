// **Is a proxy system worth building on this content?** The question C5/C6/C7 have to answer before
// they are written, and the one ADR-126 left half open.
//
//   avgen_tests "[.analysis][bands]" --success
//
// ---- why this instrument and not the one ADR-124 named ----------------------------------------
//
// ADR-124 asked for a four-arm sweep of the synthetic plane between 500 and 512,000 px/triangle.
// ADR-131 ran it: every interval overlaps every other, spreads up to 120%, no measurable minimum.
// **The cheap half of the fragment-cost curve cannot be resolved by that instrument**, so repeating
// it -- or calibrating anything new against it -- produces a confident number about half the time.
// What ADR-131 also found is that the *expensive* half, below ~8 px/triangle, is reproducible to
// the microsecond. So this analysis stays entirely on the half that reproduces, and asks a
// different question of it.
//
// ADR-126 measured Glowmere's estimated fragment-cost excess -- 1.52x whole frame, 2.02x on the
// ecology -- and bucketed it by pixels per *triangle*. That says how much excess exists. It does
// **not** say whether the proposed machinery can reach it, because the machinery is selected by
// projected *radius* (ADR-123: kind is a radius question, rung is a px/triangle question) and the
// two are independent. A 400-triangle fern eight metres from the camera and a 400-triangle fern
// eighty metres away can share a px/triangle bucket and land in different representation bands.
//
// So this cross-tabulates the excess **by the band that would recover it**:
//
//   radius > 40 px    mesh -- the existing ladder's territory, C5/C6 never touch it
//   8 - 40 px         C6, HLOD proxy
//   2 - 8 px          C5, impostor
//   <= 2 px           cull -- already gone, or would be
//
// and then costs two hypotheticals against §4.5's curve:
//
//   * **ideal proxy**: every drawable in the C5/C6 bands still covers exactly the pixels it covers
//     now, but at the cheapest triangle size measured. A real proxy cannot beat this -- it draws at
//     least the same silhouette, and it cannot get a better cost per covered pixel than the
//     measured minimum.
//   * **deleted**: those drawables cost nothing at all. Not a proposal -- it is the ceiling of every
//     possible representation system, including ones nobody has thought of, and it is the number a
//     null result has to be measured against.
//
// If the ideal-proxy saving is small, no implementation of C5 or C6 can be large, and that is a
// result rather than a failure to find one.

#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "rendering/importance.hpp"
#include "rendering/representation.hpp"
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
constexpr std::uint32_t kWidth = 1280; // the baseline's resolution (§3.1)
constexpr std::uint32_t kHeight = 800;

// §4.5's sweep, depth control subtracted, normalised by its own minimum -- the same eight points
// tests/unit/test_triangle_size_analysis.cpp uses, deliberately identical so the two analyses can
// be compared. ADR-131 is why the right-hand end is treated as a shape and never as a threshold:
// everything from 500 up is one measured point and an interpolation, and the region has since been
// measured *unresolvable* rather than merely unsampled.
struct CurvePoint {
    float pixelsPerTriangle;
    float relativeCost;
};
constexpr std::array<CurvePoint, 8> kGateCurve{{
    {0.49f, 5.05f}, {0.99f, 4.73f}, {1.95f, 4.05f}, {3.95f, 2.82f},
    {7.8f, 2.18f},  {63.0f, 1.78f}, {500.0f, 1.00f}, {512000.0f, 2.01f},
}};

float relativeCost(float pixelsPerTriangle) {
    const float p = std::max(pixelsPerTriangle, 1e-6f);
    if (p <= kGateCurve.front().pixelsPerTriangle) return kGateCurve.front().relativeCost;
    if (p >= kGateCurve.back().pixelsPerTriangle) return kGateCurve.back().relativeCost;
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

// The cheapest cost per covered pixel the sweep ever measured. An ideal proxy is charged this and
// no less: it still covers the pixels, and 1.0 is the floor of the curve by construction.
constexpr double kIdealRelativeCost = 1.0;

enum class Band { Mesh, Proxy, Impostor, Cull, Count };
constexpr std::array<const char*, 4> kBandNames{{"mesh (> 40 px radius)", "C6 proxy (8-40 px)",
                                                 "C5 impostor (2-8 px)", "cull (<= 2 px)"}};

Band bandOf(const rendering::RepresentationPolicy& policy, float projectedRadius) {
    if (projectedRadius <= policy.cullRadius) return Band::Cull;
    if (projectedRadius <= policy.impostorRadius) return Band::Impostor;
    if (projectedRadius <= policy.proxyRadius) return Band::Proxy;
    return Band::Mesh;
}

struct Slice {
    std::uint64_t drawables = 0;
    std::uint64_t triangles = 0;
    double coverage = 0.0;
    double weightedCost = 0.0;

    void add(float pixelsPerTriangle, std::uint64_t tris, double cover) {
        drawables += 1;
        triangles += tris;
        coverage += cover;
        weightedCost += cover * static_cast<double>(relativeCost(pixelsPerTriangle));
    }
    [[nodiscard]] double excess() const { return coverage > 0.0 ? weightedCost / coverage : 1.0; }
};

struct Frame {
    std::array<Slice, 4> band{};
    Slice all;
    // The same drawables, costed at the rung ADR-124's *measured* rule would pick instead of the
    // one the shipped radius ladder picked. Not a hypothetical mechanism -- this is
    // RepresentationSelector, which C3/C4 built, calibrated and unit-tested, and which ADR-125
    // records as deliberately not yet wired to anything.
    Slice selector;
    // Scatter instances the shipped ADR-029 ladder put on rung 2 or rung 3. Deliberately NOT
    // called "billboards": whether rung 2 *is* a billboard depends on the source kind, and the
    // ladder report below is what settles that per layer rather than assuming it.
    std::uint64_t atRung2 = 0;
    std::uint64_t atRung3 = 0;
    std::uint64_t scatterDrawables = 0;

    void add(Band b, float pixelsPerTriangle, std::uint64_t tris, double cover) {
        band[static_cast<std::size_t>(b)].add(pixelsPerTriangle, tris, cover);
        all.add(pixelsPerTriangle, tris, cover);
    }
};

// cull.wgsl / procedural_renderer.cpp `cullLodLevel`, transcribed -- see
// tests/unit/test_triangle_size_analysis.cpp, which transcribes it for the same reason.
int lodLevelFor(const scene::LodSettings& lod, const std::array<glm::vec4, 6>& planes,
                glm::vec3 cameraPosition, float projScale, glm::vec3 center, float radius) {
    const int lodCount = std::clamp(lod.lodCount, 1, scene::kMaxLodLevels);
    const float distance = glm::length(center - cameraPosition);
    const float screenRadius = radius / std::max(distance, 1e-4f) * projScale;
    if (lod.cull) {
        for (const glm::vec4& plane : planes) {
            if (glm::dot(glm::vec3(plane), center) + plane.w < -radius) return -1;
        }
        if (lod.maxDistance > 0.0f && distance - radius > lod.maxDistance) return -1;
        if (lod.minScreenRadius > 0.0f && screenRadius < lod.minScreenRadius) return -1;
    }
    int level = 0;
    for (int k = 0; k + 1 < lodCount && k < 3; ++k) {
        const float threshold = lod.lodDistances[k];
        if (!(threshold > 0.0f)) break;
        const bool take = lod.lodByScreenSize ? screenRadius <= threshold : distance >= threshold;
        if (!take) break;
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
        if (len > 0.0f) p /= len;
    }
    return planes;
}

} // namespace

TEST_CASE("Where Glowmere's fragment-cost excess lives, by representation band",
          "[.analysis][bands]") {
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
    // The shipped realtime bands, read from the policy rather than restated, so this analysis
    // moves when the calibration moves instead of quietly disagreeing with it.
    const auto policy = rendering::RepresentationPolicy::forTier(rendering::QualityTier::Realtime);

    Frame frame;
    Frame scatterOnly;
    // The rung decision alone. The kind bands are collapsed to zero on purpose: proxies and
    // impostors do not exist, so asking the selector which *kind* to use would be costing
    // machinery that is not built. What is being asked is the one question it can answer today --
    // which rung of the ladder the px/triangle rule picks -- and every drawable stays a mesh.
    rendering::RepresentationPolicy rungOnly = policy;
    rungOnly.proxyRadius = 0.0f;
    rungOnly.impostorRadius = 0.0f;
    rungOnly.cullRadius = 0.0f;
    // Triangles per rung, per scatter layer. The ladder's *rungs*, not the instances on them: what
    // a distant instance is actually asked to draw.
    std::vector<std::pair<std::string, std::array<std::uint32_t, scene::kMaxLodLevels>>> ladder;
    // Who is actually in the two bands C5 and C6 would serve, by layer. The bands' totals say how
    // big the prize is; this says which content it would have to be taken from, which is the part
    // that decides whether one mechanism can reach it or four would be needed.
    std::map<std::string, Slice> targetByLayer;

    scene::MeshMetricsCache cache;
    for (const scene::Entity& e : scn.entities) {
        if (!e.visible || e.cameraCulled || e.mesh == scene::kInvalidMesh) continue;
        const scene::MeshMetrics& m = cache.metrics(scn, e.mesh);
        if (!m.valid()) continue;
        const glm::vec3 scale = e.transform.scale;
        const float maxScale = std::max({std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)});
        rendering::ImportanceInput in;
        in.center = glm::vec3(e.transform.matrix() * glm::vec4(m.boundsCenter, 1.0f));
        in.radius = m.boundsRadius * maxScale;
        in.surfaceArea = m.surfaceArea * scene::MeshMetrics::areaScale(scale);
        in.triangles = m.triangles;
        const auto r = rendering::ImportanceEvaluator::evaluate(view, in);
        if (!r.inFront || r.pixelsPerTriangle <= 0.0f) continue;
        const double coverage = std::min(static_cast<double>(r.projectedArea), frameArea);
        frame.add(bandOf(policy, r.projectedRadius), r.pixelsPerTriangle, m.triangles, coverage);
        // An authored entity has no ladder here, so there is no other rung to pick: it is carried
        // into the selector arm unchanged rather than being silently dropped from its denominator.
        frame.selector.add(r.pixelsPerTriangle, m.triangles, coverage);
    }

    for (const scene::ProceduralGeometry& object : scn.procedurals) {
        if (!object.visible || object.instances.empty()) continue;
        scene::GenerationContext ctx;
        ctx.objects = &scn.procedurals;
        ctx.splines = &scn.splines;
        auto source = object.resolveSourceMesh(ctx);
        if (!source) continue;
        const int levels = std::clamp(object.lod.lodCount, 1, scene::kMaxLodLevels);
        std::array<scene::MeshMetrics, scene::kMaxLodLevels> rung{};
        rung[0] = scene::meshMetrics(*source);
        for (int level = 1; level < levels; ++level) {
            auto reduced = scene::makeLodMesh(object.source, level, object.lod.impostorSize);
            rung[static_cast<std::size_t>(level)] = reduced ? scene::meshMetrics(*reduced) : rung[0];
        }
        if (!rung[0].valid()) continue;
        {
            std::array<std::uint32_t, scene::kMaxLodLevels> tris{};
            for (int level = 0; level < scene::kMaxLodLevels; ++level) {
                tris[static_cast<std::size_t>(level)] =
                    level < levels ? rung[static_cast<std::size_t>(level)].triangles : 0u;
            }
            ladder.emplace_back(object.name, tris);
        }
        for (const scene::InstanceRecord& record : object.instances) {
            const glm::vec3 center(record.position);
            const glm::vec3 scale = glm::abs(glm::vec3(record.scale));
            const float maxScale = std::max({scale.x, scale.y, scale.z});
            const float radius = rung[0].boundsRadius * maxScale;
            const int level = lodLevelFor(object.lod, planes, view.cameraPosition, view.pixelsPerUnit,
                                          center, radius);
            if (level < 0) continue;
            const scene::MeshMetrics& m = rung[static_cast<std::size_t>(level)];
            if (!m.valid()) continue;
            rendering::ImportanceInput in;
            in.center = center;
            in.radius = radius;
            in.surfaceArea = m.surfaceArea * scene::MeshMetrics::areaScale(scale);
            in.triangles = m.triangles;
            const auto r = rendering::ImportanceEvaluator::evaluate(view, in);
            if (!r.inFront || r.pixelsPerTriangle <= 0.0f) continue;
            const double coverage = std::min(static_cast<double>(r.projectedArea), frameArea);
            const Band b = bandOf(policy, r.projectedRadius);
            frame.add(b, r.pixelsPerTriangle, m.triangles, coverage);
            scatterOnly.add(b, r.pixelsPerTriangle, m.triangles, coverage);
            // What the px/triangle rule would pick from this object's own ladder, at this
            // instance's distance. The rungs are the real ones -- the meshes `makeLodMesh` returns
            // -- so this is not an estimate of a mechanism, it is the mechanism run on the data.
            {
                std::array<rendering::LodRung, scene::kMaxLodLevels> rungs{};
                std::size_t rungCount = 0;
                for (int lv = 0; lv < levels; ++lv) {
                    const scene::MeshMetrics& lm = rung[static_cast<std::size_t>(lv)];
                    if (!lm.valid()) break;
                    rungs[rungCount++] = rendering::LodRung{
                        lm.surfaceArea * scene::MeshMetrics::areaScale(scale), lm.triangles};
                }
                const auto choice = rendering::RepresentationSelector::decide(
                    r, std::span<const rendering::LodRung>(rungs.data(), rungCount), rungOnly,
                    rendering::RepresentationChoice{});
                const std::size_t picked =
                    std::min<std::size_t>(choice.lodLevel, rungCount > 0 ? rungCount - 1 : 0);
                const scene::MeshMetrics& pm = rung[picked];
                const float ppt = rendering::ImportanceEvaluator::pixelsPerTriangleFor(
                    r, pm.surfaceArea * scene::MeshMetrics::areaScale(scale), pm.triangles);
                // Coverage is the silhouette, which a coarser rung of the same object still fills;
                // charging the *new* rung's projected area instead would credit the arm with
                // shrinking the object, which is a different and much larger claim.
                frame.selector.add(ppt, pm.triangles, coverage);
                scatterOnly.selector.add(ppt, pm.triangles, coverage);
            }
            if (b == Band::Proxy || b == Band::Impostor) {
                targetByLayer[object.name].add(r.pixelsPerTriangle, m.triangles, coverage);
            }
            scatterOnly.scatterDrawables += 1;
            if (level == 2) scatterOnly.atRung2 += 1;
            if (level == 3) scatterOnly.atRung3 += 1;
        }
    }

    const auto reportFrame = [&](const char* label, const Frame& f) {
        std::printf("\n%s\n", label);
        std::printf("  %-24s %10s %11s %13s %8s %9s\n", "band", "drawables", "triangles", "coverage px",
                    "cov %", "excess");
        for (std::size_t i = 0; i < f.band.size(); ++i) {
            const Slice& s = f.band[i];
            std::printf("  %-24s %10llu %11llu %13.0f %7.1f%% %8.2fx\n", kBandNames[i],
                        static_cast<unsigned long long>(s.drawables),
                        static_cast<unsigned long long>(s.triangles), s.coverage,
                        100.0 * s.coverage / std::max(f.all.coverage, 1e-9), s.excess());
        }
        std::printf("  %-24s %10llu %11llu %13.0f %7.1f%% %8.2fx\n", "TOTAL",
                    static_cast<unsigned long long>(f.all.drawables),
                    static_cast<unsigned long long>(f.all.triangles), f.all.coverage, 100.0,
                    f.all.excess());

        // The two hypotheticals. Both are ceilings; neither is a proposal.
        const Slice& proxy = f.band[static_cast<std::size_t>(Band::Proxy)];
        const Slice& impostor = f.band[static_cast<std::size_t>(Band::Impostor)];
        const double targetCoverage = proxy.coverage + impostor.coverage;
        const double targetCost = proxy.weightedCost + impostor.weightedCost;

        const double idealCost = f.all.weightedCost - targetCost + targetCoverage * kIdealRelativeCost;
        const double deletedCost = f.all.weightedCost - targetCost;
        const double base = std::max(f.all.weightedCost, 1e-9);
        std::printf("\n  the two ceilings, as a share of this frame's triangle-size-weighted cost:\n");
        std::printf("    ideal proxy (C5+C6 bands drawn at the cheapest triangle size measured,\n"
                    "      same coverage -- no real proxy can beat this):        %+6.2f%%\n",
                    -100.0 * (1.0 - idealCost / base));
        std::printf("    deleted     (C5+C6 bands cost nothing at all -- the ceiling of every\n"
                    "      representation system there could be):                %+6.2f%%\n",
                    -100.0 * (1.0 - deletedCost / base));
        std::printf("    for scale, the whole-frame excess a perfect system of any kind\n"
                    "      could address is                                       %6.2f%%\n",
                    100.0 * (1.0 - 1.0 / std::max(f.all.excess(), 1e-9)));
        if (f.selector.coverage > 0.0) {
            std::printf("    and, for comparison, what the ALREADY BUILT selector would do to the\n"
                        "      same frame by picking a different rung of the same ladders:\n"
                        "      excess %.2fx -> %.2fx, triangles %llu -> %llu   (%+6.2f%%)\n",
                        f.all.excess(), f.selector.excess(),
                        static_cast<unsigned long long>(f.all.triangles),
                        static_cast<unsigned long long>(f.selector.triangles),
                        -100.0 * (1.0 - f.selector.weightedCost / base));
        }
    };

    std::printf("\n=========== Glowmere by representation band, %ux%u, realtime tier ===========\n",
                kWidth, kHeight);
    std::printf("bands from RepresentationPolicy::forTier(Realtime): proxy <= %.0f px, impostor <= %.0f px,"
                " cull <= %.0f px of projected radius.\n",
                static_cast<double>(policy.proxyRadius), static_cast<double>(policy.impostorRadius),
                static_cast<double>(policy.cullRadius));
    reportFrame("ALL SUBMITTED GEOMETRY", frame);
    reportFrame("procedural scatter only (the ecology)", scatterOnly);

    std::printf("\n  what the shipped ADR-029 ladder actually hands the far bands:\n");
    std::printf("    scatter instances drawn this frame: %llu, of which %llu on rung 2 and %llu on rung 3\n",
                static_cast<unsigned long long>(scatterOnly.scatterDrawables),
                static_cast<unsigned long long>(scatterOnly.atRung2),
                static_cast<unsigned long long>(scatterOnly.atRung3));
    std::printf("    a rung of 2 triangles is the billboard `makeLodMesh` documents. Anything larger\n"
                "    is a simplified mesh, and the impostor rung is not being reached.\n");
    std::printf("    %-40s %9s %9s %9s %9s\n", "layer", "rung0", "rung1", "rung2", "rung3");
    for (const auto& [name, tris] : ladder) {
        std::printf("    %-40s %9u %9u %9u %9u\n", name.c_str(), tris[0], tris[1], tris[2], tris[3]);
    }
    std::printf("\n");

    std::printf("  the C5 + C6 bands by layer, worst first by recoverable weighted cost:\n");
    std::vector<std::pair<std::string, Slice>> target(targetByLayer.begin(), targetByLayer.end());
    std::sort(target.begin(), target.end(), [](const auto& a, const auto& b) {
        return a.second.weightedCost - a.second.coverage > b.second.weightedCost - b.second.coverage;
    });
    std::printf("    %-30s %9s %10s %11s %8s %11s\n", "layer", "drawables", "triangles", "coverage",
                "excess", "recoverable");
    for (const auto& [name, s2] : target) {
        if (s2.triangles == 0) continue;
        std::printf("    %-30s %9llu %10llu %11.0f %7.2fx %11.0f\n", name.c_str(),
                    static_cast<unsigned long long>(s2.drawables),
                    static_cast<unsigned long long>(s2.triangles), s2.coverage, s2.excess(),
                    s2.weightedCost - s2.coverage);
    }
    std::printf("\n");

    // Instrument checks only. §4.5's first run was a convincing null result produced by rendering
    // nothing, so what is asserted here is that the walk found geometry -- never what it concluded.
    CHECK(frame.all.triangles > 0);
    CHECK(frame.all.coverage > 0.0);
    CHECK(scatterOnly.all.triangles > 0);
}
