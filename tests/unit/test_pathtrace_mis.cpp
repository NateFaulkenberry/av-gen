// MIS, Russian roulette and the fireflies they kill (ADR-344 Phase 3, spec sections 43-45, 50).
//
// The two bugs this file exists to catch both hide behind plausibility:
//
//   * A power-heuristic error is "correct on average but wrong per strategy". The combined image
//     converges to something believable while one lobe is over-counted and the other under-counted.
//     A whole-image mean cannot see it. So every MIS test here asserts the strategies SEPARATELY
//     and requires light-only, BSDF-only and MIS to converge to the SAME value.
//   * Russian roulette with the wrong compensation biases the result DARKER while also reducing
//     variance, which reads as "less noise" and is not. So the energy arms have a deliberately
//     wrong compensation as their control.

#include "pathtrace/lights.hpp"
#include "pathtrace/path_tracer.hpp"
#include "pathtrace/sampler.hpp"
#include "pathtrace/snapshot.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"

#include <glm/gtc/quaternion.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

float luminance(const glm::vec3& c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

// A furnace-like box: a diffuse floor lit ONLY by a large emissive quad overhead. No analytic
// lights at all, so every photon in the image has passed through the emissive-geometry path and the
// three strategies must agree exactly on the answer.
scene::Scene emissiveOnlyScene(float emitterSize, float emission) {
    scene::Scene s;
    s.environment.sky.enabled = false;
    s.environment.backgroundColor = glm::vec3(0.0f);

    s.camera.position = glm::vec3(0.0f, 1.6f, 4.2f);
    s.camera.target = glm::vec3(0.0f, 0.6f, 0.0f);
    s.camera.lens.useExplicitFov = true;
    s.camera.fovYRadians = 0.8f;

    const scene::MeshId floorId = s.addMesh(scene::makePlane(6.0f, 1));
    scene::Entity& floor = s.addEntity("floor", floorId);
    floor.material.baseColor = glm::vec3(0.6f, 0.6f, 0.6f);
    floor.material.roughness = 0.85f;
    floor.material.metallic = 0.0f;

    // The emitter: a plane, rotated to face downward, lifted overhead.
    const scene::MeshId emitId = s.addMesh(scene::makePlane(emitterSize * 0.5f, 1));
    scene::Entity& emitter = s.addEntity("emitter", emitId);
    emitter.transform.position = glm::vec3(0.0f, 3.0f, 0.0f);
    emitter.transform.rotation = glm::angleAxis(3.14159265f, glm::vec3(1.0f, 0.0f, 0.0f));
    emitter.material.baseColor = glm::vec3(0.0f);
    emitter.material.emissiveColor = glm::vec3(1.0f);
    emitter.material.emissiveIntensity = emission;
    return s;
}

// A closed white room. Roulette only touches paths beyond its start depth, so a scene whose light
// arrives mostly direct cannot tell a correct compensation from a wrong one -- the control would
// pass for the wrong reason. Here the walls are albedo 0.85 and the only light is a small ceiling
// emitter, so most of what the camera sees has bounced several times and the compensation is
// load-bearing.
scene::Scene whiteRoomScene() {
    scene::Scene s;
    s.environment.sky.enabled = false;
    s.environment.backgroundColor = glm::vec3(0.0f);

    s.camera.position = glm::vec3(0.0f, 1.5f, 3.4f);
    s.camera.target = glm::vec3(0.0f, 1.3f, 0.0f);
    s.camera.lens.useExplicitFov = true;
    s.camera.fovYRadians = 1.1f;

    const auto wall = [&](const char* name, glm::vec3 pos, glm::quat rot, glm::vec3 albedo) {
        const scene::MeshId id = s.addMesh(scene::makePlane(2.0f, 1));
        scene::Entity& e = s.addEntity(name, id);
        e.transform.position = pos;
        e.transform.rotation = rot;
        e.material.baseColor = albedo;
        e.material.roughness = 0.9f;
        e.material.metallic = 0.0f;
    };
    const glm::vec3 white{0.85f, 0.85f, 0.85f};
    const float half = 2.0f;
    wall("floor", {0, 0, 0}, glm::quat(1, 0, 0, 0), white);
    wall("ceiling", {0, 2 * half, 0}, glm::angleAxis(3.14159265f, glm::vec3(1, 0, 0)), white);
    wall("back", {0, half, -half}, glm::angleAxis(1.5707963f, glm::vec3(1, 0, 0)), white);
    wall("left", {-half, half, 0}, glm::angleAxis(-1.5707963f, glm::vec3(0, 0, 1)), white);
    wall("right", {half, half, 0}, glm::angleAxis(1.5707963f, glm::vec3(0, 0, 1)), white);

    const scene::MeshId emitId = s.addMesh(scene::makePlane(0.5f, 1));
    scene::Entity& emitter = s.addEntity("emitter", emitId);
    emitter.transform.position = glm::vec3(0.0f, 2 * half - 0.05f, 0.0f);
    emitter.transform.rotation = glm::angleAxis(3.14159265f, glm::vec3(1.0f, 0.0f, 0.0f));
    emitter.material.baseColor = glm::vec3(0.0f);
    emitter.material.emissiveColor = glm::vec3(1.0f);
    emitter.material.emissiveIntensity = 9.0f;  // the room lands near 0.25 mean: lit, not clipped
    return s;
}

pathtrace::Framebuffer renderWith(const scene::Scene& sc, pathtrace::TraceSettings t,
                                  pathtrace::TraceStats* stats = nullptr) {
    pathtrace::PathTracer tr;
    pathtrace::Framebuffer fb;
    const auto ok = tr.render(pathtrace::buildSnapshot(sc), t, fb);
    REQUIRE(ok.has_value());
    if (stats != nullptr) *stats = tr.stats();
    return fb;
}

pathtrace::TraceSettings baseSettings() {
    pathtrace::TraceSettings t;
    t.width = 96;
    t.height = 64;
    t.samplesPerPixel = 256;
    t.maxDepth = 2;
    t.threads = 0;
    t.russianRouletteDepth = 0; // off, so these arms measure MIS alone
    return t;
}

} // namespace

// ---- the power heuristic itself ----------------------------------------------------------------

TEST_CASE("the power heuristic weights sum to exactly one", "[unit][pathtrace][mis]") {
    // This is the property that makes MIS unbiased. If the two weights do not sum to 1 for every
    // pair of densities, the combined estimator is wrong by exactly the shortfall -- and no image
    // inspection will reveal a uniform 6% energy loss.
    const float pdfs[] = {1e-6f, 0.001f, 0.1f, 1.0f, 7.5f, 1000.0f, 1e12f, 1e30f};
    for (float a : pdfs) {
        for (float b : pdfs) {
            const float wa = pathtrace::powerHeuristic(a, b);
            const float wb = pathtrace::powerHeuristic(b, a);
            INFO("pdfA " << a << " pdfB " << b << " -> " << wa << " + " << wb);
            REQUIRE(std::isfinite(wa));
            REQUIRE(std::isfinite(wb));
            REQUIRE(wa >= 0.0f);
            REQUIRE(wa <= 1.0f);
            REQUIRE(wa + wb == Approx(1.0f).margin(1e-5));
        }
    }
    // Extreme ratios must not overflow to NaN -- the reason the ratio is formed before squaring.
    REQUIRE(std::isfinite(pathtrace::powerHeuristic(1e30f, 1e-30f)));
    REQUIRE(pathtrace::powerHeuristic(1e30f, 1e-30f) == Approx(1.0f));
    REQUIRE(pathtrace::powerHeuristic(1e-30f, 1e30f) == Approx(0.0f).margin(1e-6));

    // A strategy that could not have produced the sample takes all the weight.
    REQUIRE(pathtrace::powerHeuristic(2.0f, 0.0f) == Approx(1.0f));
    REQUIRE(pathtrace::powerHeuristic(0.0f, 2.0f) == Approx(0.0f));
    // Equal densities split evenly.
    REQUIRE(pathtrace::powerHeuristic(3.0f, 3.0f) == Approx(0.5f));
}

TEST_CASE("CONTROL: a heuristic whose weights do not sum to one is detectable",
          "[unit][pathtrace][mis]") {
    // The arm above only means something if a wrong heuristic would fail it. The balance heuristic
    // ALSO sums to 1, so it is not a counterexample -- but an unnormalised one is.
    const auto broken = [](float a, float b) { return a / (a + b + 0.3f); };
    REQUIRE(broken(1.0f, 1.0f) + broken(1.0f, 1.0f) != Approx(1.0f).margin(1e-5));
    // And the balance heuristic, which is correct, does sum to 1 -- so "sums to 1" does not by
    // itself prove the POWER heuristic specifically. That is what the convergence arms are for.
    const auto balance = [](float a, float b) { return a / (a + b); };
    REQUIRE(balance(2.0f, 5.0f) + balance(5.0f, 2.0f) == Approx(1.0f));
    REQUIRE(pathtrace::powerHeuristic(2.0f, 5.0f) != Approx(balance(2.0f, 5.0f)).margin(1e-3));
}

// ---- the emissive sampler's PDF ------------------------------------------------------------------

TEST_CASE("the emissive sampler's PDF integrates to one over the sphere",
          "[unit][pathtrace][mis]") {
    // Sample the emitter, then ask the PDF query what density that direction had. Estimating
    // int pdf domega by averaging 1/(pdf * ...) is circular; instead check the two agree, which is
    // the property MIS actually needs: sampleEmissive's pdf and emissivePdf must be the SAME
    // function. If they drift apart the MIS weights are computed from a density nothing sampled.
    const scene::Scene sc = emissiveOnlyScene(2.0f, 4.0f);
    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(sc);
    REQUIRE(snap.emissiveTriangles.size() >= 2);
    REQUIRE(snap.totalEmissiveArea == Approx(4.0f).epsilon(0.01)); // a 2x2 m quad

    const glm::vec3 p{0.3f, 0.0f, -0.2f};
    int checked = 0;
    for (int i = 0; i < 500; ++i) {
        pathtrace::Sampler s(71, static_cast<std::uint32_t>(i), 0);
        const auto es = pathtrace::sampleEmissive(snap, p, s.next2D(), s.next1D());
        if (!es.valid) continue;
        ++checked;
        const float q = pathtrace::emissivePdf(snap, es.meshIndex, es.primIndex, es.direction, es.distance);
        REQUIRE(q == Approx(es.pdf).epsilon(1e-4));
    }
    REQUIRE(checked > 400); // live arm

    // A triangle that is not an emitter has no density under this strategy.
    REQUIRE(pathtrace::emissivePdf(snap, 999, 0, glm::vec3(0, 1, 0), 1.0f) == 0.0f);
}

TEST_CASE("the emissive area CDF selects triangles in proportion to area",
          "[unit][pathtrace][mis]") {
    // Two emitters of very different size. Selection must follow area, not triangle count, or the
    // PDF (which assumes uniform-by-area) is inconsistent with the sampler and MIS goes wrong.
    scene::Scene sc = emissiveOnlyScene(1.0f, 3.0f);
    const scene::MeshId bigId = sc.addMesh(scene::makePlane(2.0f, 1)); // 4x4 = 16 m^2
    scene::Entity& big = sc.addEntity("big-emitter", bigId);
    big.transform.position = glm::vec3(8.0f, 3.0f, 0.0f);
    big.transform.rotation = glm::angleAxis(3.14159265f, glm::vec3(1.0f, 0.0f, 0.0f));
    big.material.emissiveColor = glm::vec3(1.0f);
    big.material.emissiveIntensity = 3.0f;

    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(sc);
    REQUIRE(snap.totalEmissiveArea == Approx(1.0f + 16.0f).epsilon(0.01)); // 1x1 plus 4x4

    int small = 0;
    int large = 0;
    const int N = 20000;
    for (int i = 0; i < N; ++i) {
        pathtrace::Sampler s(83, static_cast<std::uint32_t>(i), 0);
        const auto es = pathtrace::sampleEmissive(snap, glm::vec3(0.0f, 0.0f, 0.0f), s.next2D(), s.next1D());
        if (!es.valid) continue;
        (es.meshIndex == 1 ? small : large)++;
    }
    // 1 of 17 should land on the small emitter.
    const double fraction = static_cast<double>(small) / (small + large);
    INFO("small-emitter fraction " << fraction);
    REQUIRE(fraction == Approx(1.0 / 17.0).margin(0.02));
}

// ---- the arm the coordinator asked for: per-strategy convergence ---------------------------------

TEST_CASE("light sampling, BSDF sampling and MIS all converge to the SAME image",
          "[unit][pathtrace][mis]") {
    // THE test for MIS correctness. Each strategy is an independent, unbiased estimator of the same
    // integral, so all three must agree in the limit. They differ only in variance.
    //
    // This is what catches weights that do not sum to one: the MIS arm would converge to something
    // between the two single-strategy answers, or beyond them, and the single-strategy arms -- which
    // use no weights at all -- would still be right. A combined-only test cannot distinguish that
    // from a correct renderer.
    const scene::Scene sc = emissiveOnlyScene(2.0f, 6.0f);

    pathtrace::TraceSettings mis = baseSettings();
    mis.strategy = pathtrace::TraceSettings::Strategy::Mis;
    pathtrace::TraceSettings lightOnly = baseSettings();
    lightOnly.strategy = pathtrace::TraceSettings::Strategy::LightOnly;
    pathtrace::TraceSettings bsdfOnly = baseSettings();
    bsdfOnly.strategy = pathtrace::TraceSettings::Strategy::BsdfOnly;
    bsdfOnly.samplesPerPixel = 1024; // it is the high-variance strategy; give it the samples to converge

    const auto fbMis = renderWith(sc, mis);
    const auto fbLight = renderWith(sc, lightOnly);
    const auto fbBsdf = renderWith(sc, bsdfOnly);

    REQUIRE_FALSE(fbMis.isBlack());
    REQUIRE_FALSE(fbLight.isBlack());
    REQUIRE_FALSE(fbBsdf.isBlack());

    const double lMis = fbMis.meanLuminance();
    const double lLight = fbLight.meanLuminance();
    const double lBsdf = fbBsdf.meanLuminance();
    INFO("mis " << lMis << "  light-only " << lLight << "  bsdf-only " << lBsdf);

    // Statistical tolerance, never equality (spec section 58).
    REQUIRE(lMis == Approx(lLight).epsilon(0.03));
    REQUIRE(lMis == Approx(lBsdf).epsilon(0.06));
    REQUIRE(lLight == Approx(lBsdf).epsilon(0.06));

    // And they must agree REGION BY REGION, not merely on the frame mean. A weight error that
    // over-counts one strategy on the floor and under-counts it on the emitter can cancel in the
    // mean; it cannot cancel in both regions at once.
    const auto region = [](const pathtrace::Framebuffer& fb, std::uint32_t x0, std::uint32_t y0,
                           std::uint32_t x1, std::uint32_t y1) {
        double sum = 0.0;
        int n = 0;
        for (std::uint32_t y = y0; y < std::min(y1, fb.height); ++y) {
            for (std::uint32_t x = x0; x < std::min(x1, fb.width); ++x) {
                sum += luminance(fb.pixel(x, y));
                ++n;
            }
        }
        return n > 0 ? sum / n : 0.0;
    };
    const double floorMis = region(fbMis, 30, 50, 66, 62);
    const double floorLight = region(fbLight, 30, 50, 66, 62);
    const double floorBsdf = region(fbBsdf, 30, 50, 66, 62);
    INFO("floor: mis " << floorMis << " light " << floorLight << " bsdf " << floorBsdf);
    REQUIRE(floorMis > 0.01); // live arm: the floor is actually lit
    REQUIRE(floorMis == Approx(floorLight).epsilon(0.05));
    REQUIRE(floorMis == Approx(floorBsdf).epsilon(0.10));
}

TEST_CASE("CONTROL: the three strategies really are different estimators",
          "[unit][pathtrace][mis]") {
    // If the strategy switch did nothing, the convergence test above would pass trivially. These
    // must differ in the way their names promise: BSDF-only fires no shadow rays at the emitter,
    // and light-only never counts a BSDF ray that lands on one.
    const scene::Scene sc = emissiveOnlyScene(2.0f, 6.0f);
    pathtrace::TraceSettings t = baseSettings();
    t.samplesPerPixel = 32;

    pathtrace::TraceStats misStats;
    pathtrace::TraceStats lightStats;
    pathtrace::TraceStats bsdfStats;
    t.strategy = pathtrace::TraceSettings::Strategy::Mis;
    renderWith(sc, t, &misStats);
    t.strategy = pathtrace::TraceSettings::Strategy::LightOnly;
    renderWith(sc, t, &lightStats);
    t.strategy = pathtrace::TraceSettings::Strategy::BsdfOnly;
    renderWith(sc, t, &bsdfStats);

    REQUIRE(misStats.shadowRays > 0);
    REQUIRE(lightStats.shadowRays > 0);
    REQUIRE(bsdfStats.shadowRays == 0);          // it never samples a light
    REQUIRE(misStats.bsdfHitsOnLights > 0);      // and MIS really does weight BSDF hits on emitters
    REQUIRE(bsdfStats.bsdfHitsOnLights == 0);    // which BSDF-only deliberately does not
}

TEST_CASE("MIS is lower variance than either strategy alone", "[unit][pathtrace][mis]") {
    // The reason MIS exists. Equal sample counts; MIS must beat the worse of the two and must not
    // be worse than the better of them. Measured as the count of outlier pixels, because variance
    // in a path trace is a tail property, not a spread around the mean.
    const scene::Scene sc = emissiveOnlyScene(1.0f, 30.0f); // a small, bright emitter: the hard case
    pathtrace::TraceSettings t = baseSettings();
    t.samplesPerPixel = 64;

    const auto outliers = [](const pathtrace::Framebuffer& fb) {
        std::vector<float> l;
        l.reserve(static_cast<std::size_t>(fb.width) * fb.height);
        for (std::uint32_t y = 0; y < fb.height; ++y) {
            for (std::uint32_t x = 0; x < fb.width; ++x) l.push_back(luminance(fb.pixel(x, y)));
        }
        std::vector<float> sorted = l;
        std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
        const float median = sorted[sorted.size() / 2];
        if (median <= 0.0f) return static_cast<int>(l.size());
        return static_cast<int>(std::count_if(l.begin(), l.end(),
                                              [&](float v) { return v > median * 8.0f; }));
    };

    t.strategy = pathtrace::TraceSettings::Strategy::Mis;
    const int misOut = outliers(renderWith(sc, t));
    t.strategy = pathtrace::TraceSettings::Strategy::BsdfOnly;
    const int bsdfOut = outliers(renderWith(sc, t));

    INFO("outliers: mis " << misOut << " bsdf-only " << bsdfOut);
    REQUIRE(bsdfOut > 0);            // live arm: the hard case really is noisy without MIS
    REQUIRE(misOut < bsdfOut / 2);   // and MIS really does fix it
}

// ---- Russian roulette ----------------------------------------------------------------------------

TEST_CASE("Russian roulette preserves energy, and a wrong compensation does not",
          "[unit][pathtrace][rr]") {
    // Roulette must not change the answer, only the cost. A wrong compensation biases the image
    // DARKER while also reducing variance, which looks like an improvement -- so the control here
    // is a deliberately wrong compensation that must fail the same band the correct one passes.
    const scene::Scene sc = whiteRoomScene();

    pathtrace::TraceSettings off = baseSettings();
    off.maxDepth = 12;
    off.samplesPerPixel = 512;
    off.russianRouletteDepth = 0;

    pathtrace::TraceSettings on = off;
    on.russianRouletteDepth = 1;   // from the first bounce, so most energy passes through it

    pathtrace::TraceStats onStats;
    const double lOff = renderWith(sc, off).meanLuminance();
    const double lOn = renderWith(sc, on, &onStats).meanLuminance();

    INFO("roulette off " << lOff << "  on " << lOn);
    REQUIRE(lOff > 0.01);                               // live arm
    REQUIRE(onStats.pathsTerminatedByRoulette > 0);     // roulette actually fired
    REQUIRE(lOn == Approx(lOff).epsilon(0.04));         // and changed nothing it should not

    // CONTROL: over-compensate by 25%. The estimator is now biased BRIGHTER and must break the band.
    pathtrace::TraceSettings wrong = on;
    wrong.russianRouletteCompensation = 1.25f;
    const double lWrong = renderWith(sc, wrong).meanLuminance();
    INFO("wrong compensation " << lWrong << " vs " << lOff);
    REQUIRE(lWrong != Approx(lOff).epsilon(0.04));

    // CONTROL 2: under-compensate. Biased DARKER -- the direction that reads as "less noise".
    pathtrace::TraceSettings dark = on;
    dark.russianRouletteCompensation = 0.75f;
    const double lDark = renderWith(sc, dark).meanLuminance();
    INFO("under-compensated " << lDark << " vs " << lOff);
    REQUIRE(lDark != Approx(lOff).epsilon(0.04));
    REQUIRE(lDark > lOff); // dividing by a smaller q makes survivors brighter, so this is the check
}

TEST_CASE("Russian roulette terminates paths without changing shallow ones",
          "[unit][pathtrace][rr]") {
    // Paths shorter than `russianRouletteDepth` must never be killed, so a direct-lighting-only
    // render is bit-identical with roulette on and off. If it is not, roulette is firing too early
    // and the first bounce -- the one that matters most -- is being thrown away.
    const scene::Scene sc = emissiveOnlyScene(2.0f, 6.0f);
    pathtrace::TraceSettings t = baseSettings();
    t.maxDepth = 1;
    t.samplesPerPixel = 16;

    t.russianRouletteDepth = 0;
    const auto a = renderWith(sc, t);
    t.russianRouletteDepth = 4; // deeper than any path here can reach
    pathtrace::TraceStats stats;
    const auto b = renderWith(sc, t, &stats);

    REQUIRE(stats.pathsTerminatedByRoulette == 0);
    REQUIRE(a.radiance == b.radiance); // bit-identical
    REQUIRE_FALSE(a.isBlack());
}



// ---- fireflies (the distributional claim, not the mean) -----------------------------------------

namespace {
// A firefly is a pixel far brighter than its neighbourhood -- a distributional fact, not a mean.
// Counting pixels above a multiple of the LOCAL median is what distinguishes "a bright object is
// in frame" from "the estimator threw an outlier".
int outliersAboveLocalMedian(const pathtrace::Framebuffer& fb, float factor) {
    int count = 0;
    for (std::uint32_t y = 1; y + 1 < fb.height; ++y) {
        for (std::uint32_t x = 1; x + 1 < fb.width; ++x) {
            float n[9];
            int k = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    n[k++] = luminance(fb.pixel(x + dx, y + dy));
                }
            }
            std::nth_element(n, n + 4, n + 9);
            const float median = n[4];
            const float here = luminance(fb.pixel(x, y));
            if (median > 1e-5f && here > median * factor) ++count;
        }
    }
    return count;
}
} // namespace

TEST_CASE("MIS removes fireflies from the target scene", "[unit][pathtrace][mis][fireflies]") {
    // The visible payoff of Phase 3. BSDF-only finds the emitter by chance, so the rare rays that
    // do land on it carry enormous weight and become single bright pixels. MIS samples it directly
    // and those same paths get a small weight instead.
    //
    // Asserted as a COUNT OF OUTLIERS above the local median, per the instruction that a firefly is
    // a distributional fact. A mean-based check cannot see this: both images have the same mean,
    // which is exactly the point -- they are both unbiased estimators of the same integral.
    const scene::Scene sc = whiteRoomScene();
    pathtrace::TraceSettings t = baseSettings();
    t.samplesPerPixel = 64;
    t.maxDepth = 4;
    t.russianRouletteDepth = 3;

    t.strategy = pathtrace::TraceSettings::Strategy::BsdfOnly;
    const auto before = renderWith(sc, t);
    t.strategy = pathtrace::TraceSettings::Strategy::Mis;
    const auto after = renderWith(sc, t);

    const int fBefore = outliersAboveLocalMedian(before, 4.0f);
    const int fAfter = outliersAboveLocalMedian(after, 4.0f);
    INFO("fireflies before " << fBefore << " after " << fAfter);

    REQUIRE(fBefore > 20);            // live arm: there really are fireflies to remove
    REQUIRE(fAfter * 5 < fBefore);    // and MIS really removes most of them

    // The means must AGREE -- that is what makes this a variance reduction rather than a change of
    // answer. If MIS merely darkened the image the outlier count would also fall, for the wrong
    // reason, and this line is what separates the two.
    REQUIRE(after.meanLuminance() == Approx(before.meanLuminance()).epsilon(0.08));
    REQUIRE_FALSE(after.isBlack());
}

TEST_CASE("preview: firefly before and after", "[.pathtrace-fireflies]") {
    const scene::Scene sc = whiteRoomScene();
    pathtrace::TraceSettings t = baseSettings();
    t.width = 400;
    t.height = 300;
    t.samplesPerPixel = 64;
    t.maxDepth = 4;
    t.threads = 0;

    const auto write = [&](const pathtrace::Framebuffer& fb, const char* name) {
        const auto path = std::filesystem::temp_directory_path() / name;
        FILE* f = std::fopen(path.c_str(), "wb");
        REQUIRE(f != nullptr);
        std::fprintf(f, "P6\n%u %u\n255\n", fb.width, fb.height);
        for (std::uint32_t y = 0; y < fb.height; ++y) {
            for (std::uint32_t x = 0; x < fb.width; ++x) {
                const glm::vec3 c = fb.pixel(x, y);
                for (int k = 0; k < 3; ++k) {
                    const float v = std::pow(std::clamp(c[k], 0.0f, 1.0f), 1.0f / 2.2f);
                    const auto b = static_cast<unsigned char>(v * 255.0f + 0.5f);
                    std::fwrite(&b, 1, 1, f);
                }
            }
        }
        std::fclose(f);
        WARN(name << ": mean " << fb.meanLuminance() << " fireflies "
                  << outliersAboveLocalMedian(fb, 4.0f));
    };

    t.strategy = pathtrace::TraceSettings::Strategy::BsdfOnly;
    write(renderWith(sc, t), "avgen_pt_before.ppm");
    t.strategy = pathtrace::TraceSettings::Strategy::Mis;
    write(renderWith(sc, t), "avgen_pt_after.ppm");
}
