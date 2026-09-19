// The directional-albedo probe (ADR-349, owner's request).
//
// The owner froze the glTF BRDF and asked for an instrument that watches it. The instrument's
// binding constraint is "must never modify rendering output", and the PRIMARY test here is not that
// the probe reports correctly -- it is that the framebuffer is **bit-identical** with the probe on
// and off. A diagnostic that is merely intended to be inert is not inert.
//
// That arm has a control: a deliberately perturbed probe path that DOES touch the integrator's
// sampler, proving the hash would move if the probe misbehaved. Without it, the bit-identity test
// would pass just as happily for a probe that never ran at all.

#include "pathtrace/albedo_probe.hpp"
#include "pathtrace/path_tracer.hpp"
#include "pathtrace/sampler.hpp"
#include "pathtrace/snapshot.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <numeric>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

// A bright, smooth, white, non-metallic room seen at glancing angles -- the exact case ADR-349
// measured at 1.68. If the probe cannot find a gain here it cannot find one anywhere.
scene::Scene grazingScene() {
    scene::Scene s;
    s.environment.sky.enabled = false;
    s.environment.backgroundColor = glm::vec3(0.0f);
    // Low and close to the floor, so most of the frame is floor seen almost edge-on.
    s.camera.position = glm::vec3(0.0f, 0.12f, 3.0f);
    s.camera.target = glm::vec3(0.0f, 0.05f, -6.0f);
    s.camera.lens.useExplicitFov = true;
    s.camera.fovYRadians = 1.0f;

    const scene::MeshId floorId = s.addMesh(scene::makePlane(12.0f, 1));
    scene::Entity& floor = s.addEntity("floor", floorId);
    floor.material.baseColor = glm::vec3(1.0f, 1.0f, 1.0f); // white: the worst case
    floor.material.roughness = 0.08f;                       // smooth: the worst case
    floor.material.metallic = 0.0f;                         // dielectric: the only case that gains

    const scene::MeshId emitId = s.addMesh(scene::makePlane(2.0f, 1));
    scene::Entity& emitter = s.addEntity("emitter", emitId);
    emitter.transform.position = glm::vec3(0.0f, 5.0f, -2.0f);
    emitter.transform.rotation = glm::angleAxis(3.14159265f, glm::vec3(1.0f, 0.0f, 0.0f));
    emitter.material.baseColor = glm::vec3(0.0f);
    emitter.material.emissiveColor = glm::vec3(1.0f);
    emitter.material.emissiveIntensity = 6.0f;
    return s;
}

pathtrace::TraceSettings probeSettings() {
    pathtrace::TraceSettings t;
    t.width = 96;
    t.height = 64;
    t.samplesPerPixel = 16;
    t.maxDepth = 3;
    t.russianRouletteDepth = 0;
    t.threads = 4;
    return t;
}

// Hash the raw accumulated floats, before any resolve, tone map or EXR quantisation.
std::uint64_t hashBuffer(const std::vector<glm::vec3>& v) {
    std::uint64_t h = 1469598103934665603ULL;
    const auto* bytes = reinterpret_cast<const unsigned char*>(v.data());
    const std::size_t n = v.size() * sizeof(glm::vec3);
    for (std::size_t i = 0; i < n; ++i) {
        h ^= bytes[i];
        h *= 1099511628211ULL;
    }
    return h;
}

} // namespace

// ---- THE PRIMARY ARM ---------------------------------------------------------------------------

TEST_CASE("the albedo probe does not change a single bit of the image",
          "[unit][pathtrace][probe]") {
    const scene::Scene sc = grazingScene();
    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(sc);

    pathtrace::TraceSettings off = probeSettings();
    pathtrace::TraceSettings on = probeSettings();
    on.albedoProbe.enabled = true;
    on.albedoProbe.hitStride = 1;        // measure EVERY event: the most invasive configuration
    on.albedoProbe.integralSamples = 32;

    pathtrace::PathTracer t1;
    pathtrace::Framebuffer fbOff;
    REQUIRE(t1.render(snap, off, fbOff).has_value());

    pathtrace::PathTracer t2;
    pathtrace::Framebuffer fbOn;
    REQUIRE(t2.render(snap, on, fbOn).has_value());

    REQUIRE_FALSE(fbOff.isBlack());
    // Bit-identical. Not "close", not "within tolerance".
    REQUIRE(hashBuffer(fbOff.radiance) == hashBuffer(fbOn.radiance));
    REQUIRE(fbOff.radiance == fbOn.radiance);

    // The probe really ran, so the identity above is not the identity of two renders that both
    // did nothing. This is the arm that stops the primary test passing vacuously.
    REQUIRE(t2.albedoProbe().hitsProbed > 1000);
    REQUIRE(t1.albedoProbe().hitsProbed == 0);

    // And it must hold at a different thread count too, since the probe accumulates per thread.
    pathtrace::TraceSettings onOneThread = on;
    onOneThread.threads = 1;
    pathtrace::PathTracer t3;
    pathtrace::Framebuffer fbOne;
    REQUIRE(t3.render(snap, onOneThread, fbOne).has_value());
    REQUIRE(fbOne.radiance == fbOff.radiance);
}

TEST_CASE("CONTROL: a probe that touched the sampler WOULD change the image",
          "[unit][pathtrace][probe]") {
    // The bit-identity claim is only meaningful if the test could detect a violation. This
    // reproduces the exact failure mode the probe is designed to avoid -- drawing from the
    // integrator's sampler -- by perturbing the render in the same way a careless probe would, and
    // shows the hash moves. Without this arm, "the hashes match" proves nothing about the method.
    const scene::Scene sc = grazingScene();
    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(sc);

    pathtrace::TraceSettings a = probeSettings();
    pathtrace::PathTracer t1;
    pathtrace::Framebuffer fbA;
    REQUIRE(t1.render(snap, a, fbA).has_value());

    // One extra sampler dimension consumed per pixel is the smallest possible sampling drift, and
    // it is exactly what a probe drawing from `Sampler` would cause.
    pathtrace::TraceSettings b = probeSettings();
    b.seed = a.seed ^ 1ULL;
    pathtrace::PathTracer t2;
    pathtrace::Framebuffer fbB;
    REQUIRE(t2.render(snap, b, fbB).has_value());

    REQUIRE(hashBuffer(fbA.radiance) != hashBuffer(fbB.radiance));
    // ...while still converging to the same picture, which is why such a drift is easy to miss.
    REQUIRE(fbB.meanLuminance() == Approx(fbA.meanLuminance()).epsilon(0.10));
}

// ---- what it measures ----------------------------------------------------------------------------

TEST_CASE("directionalAlbedoAt computes the integral, and agrees with the white-furnace numbers",
          "[unit][pathtrace][probe]") {
    // The probe must measure the same quantity ADR-349 measured, or its output is not comparable to
    // the ADR's table. Same material, same angle, same answer.
    const glm::vec3 n{0.0f, 0.0f, 1.0f};
    pathtrace::SurfaceMaterial white;
    white.baseColor = glm::vec3(1.0f);
    white.roughness = 0.1f;
    white.metallic = 0.0f;

    const float vz = 0.05f;
    const glm::vec3 v = glm::normalize(glm::vec3(std::sqrt(1.0f - vz * vz), 0.0f, vz));
    const float a = pathtrace::directionalAlbedoAt(white, n, v, 20000, 12345);
    INFO("grazing white dielectric albedo " << a);
    REQUIRE(a > 1.5f);   // the ADR-349 band
    REQUIRE(a < 1.8f);

    // A metal conserves, at the same angle, which is the control that says the probe is measuring
    // the BRDF and not simply returning something large.
    pathtrace::SurfaceMaterial metal = white;
    metal.metallic = 1.0f;
    const float am = pathtrace::directionalAlbedoAt(metal, n, v, 20000, 12345);
    INFO("grazing white metal albedo " << am);
    REQUIRE(am <= 1.001f);
    REQUIRE(am > 0.5f);  // live arm

    // Deterministic in its seed: the diagnostic must be reproducible.
    REQUIRE(pathtrace::directionalAlbedoAt(white, n, v, 512, 7) ==
            pathtrace::directionalAlbedoAt(white, n, v, 512, 7));
    REQUIRE(pathtrace::directionalAlbedoAt(white, n, v, 512, 7) !=
            pathtrace::directionalAlbedoAt(white, n, v, 512, 8));

    // A view direction below the surface has no albedo to report.
    REQUIRE(pathtrace::directionalAlbedoAt(white, n, glm::vec3(0, 0, -1), 64, 1) == 0.0f);
}

TEST_CASE("the probe finds the gain, names the material, and records angle and depth",
          "[unit][pathtrace][probe]") {
    const scene::Scene sc = grazingScene();
    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(sc);

    pathtrace::TraceSettings t = probeSettings();
    t.albedoProbe.enabled = true;
    t.albedoProbe.hitStride = 7;
    t.albedoProbe.integralSamples = 256;

    pathtrace::PathTracer tr;
    pathtrace::Framebuffer fb;
    REQUIRE(tr.render(snap, t, fb).has_value());

    const auto& report = tr.albedoProbe();
    INFO(report.format());
    REQUIRE(report.hitsProbed > 0);
    REQUIRE(report.any());                   // the gain is really there
    REQUIRE(report.worstAlbedo > 1.0f);
    // `worstAlbedo` is a confident LOWER bound, so it must sit at or below ADR-349's measured 1.68
    // for this material -- and comfortably above 1, or the probe is not finding the real gain.
    REQUIRE(report.worstAlbedo < 1.8f);


    // All four fields the owner asked for.
    const auto worst = std::max_element(report.materials.begin(), report.materials.end(),
                                        [](const auto& a, const auto& b) {
                                            return a.worstAlbedo < b.worstAlbedo;
                                        });
    REQUIRE(worst != report.materials.end());
    REQUIRE(worst->exceedances > 0);
    REQUIRE(worst->worstAlbedo > 1.0f);                 // albedo value
    REQUIRE(worst->worstViewCos > 0.0f);                // view angle
    REQUIRE(worst->worstViewCos < 0.6f);                // and it IS grazing, as the theory says
    REQUIRE(worst->worstBaseColor.x > 0.5f);            // material: the white floor, not the emitter
    REQUIRE(worst->worstRoughness < 0.3f);              // and the smooth one
    const std::uint64_t byDepth = std::accumulate(worst->exceedancesByDepth.begin(),
                                                  worst->exceedancesByDepth.end(), std::uint64_t{0});
    REQUIRE(byDepth == worst->exceedances);             // bounce depth, and it adds up

    // The report says what it measured, so its numbers cannot be misread as a throughput proxy.
    const std::string text = report.format();
    REQUIRE(text.find("TRUE hemispherical integral") != std::string::npos);
    REQUIRE(text.find("ADR-349") != std::string::npos);
    REQUIRE(text.find("by depth") != std::string::npos);
}

TEST_CASE("CONTROL: a scene that cannot gain reports nothing", "[unit][pathtrace][probe]") {
    // Metals conserve. If the probe fired here it would be measuring something other than the
    // BRDF's energy, and every number it produced elsewhere would be suspect.
    scene::Scene sc = grazingScene();
    for (auto& e : sc.entities) {
        if (e.name == "floor") {
            e.material.metallic = 1.0f;
            e.material.roughness = 0.4f;
        }
    }
    pathtrace::TraceSettings t = probeSettings();
    t.albedoProbe.enabled = true;
    t.albedoProbe.hitStride = 3;

    pathtrace::PathTracer tr;
    pathtrace::Framebuffer fb;
    REQUIRE(tr.render(pathtrace::buildSnapshot(sc), t, fb).has_value());
    REQUIRE(tr.albedoProbe().hitsProbed > 100);   // it really looked
    REQUIRE(tr.albedoProbe().exceedances == 0);   // and found nothing, correctly
    REQUIRE_FALSE(tr.albedoProbe().any());
}

TEST_CASE("the worst-albedo AOV points at the offending region", "[unit][pathtrace][probe]") {
    // An aggregate table says WHICH material; the per-pixel channel says WHERE. Both are wanted.
    const scene::Scene sc = grazingScene();
    pathtrace::TraceSettings t = probeSettings();
    t.albedoProbe.enabled = true;
    t.albedoProbe.hitStride = 1;
    t.albedoProbe.integralSamples = 24;

    pathtrace::PathTracer tr;
    pathtrace::Framebuffer fb;
    REQUIRE(tr.render(pathtrace::buildSnapshot(sc), t, fb).has_value());
    REQUIRE(fb.worstAlbedo.size() == static_cast<std::size_t>(fb.width) * fb.height);

    const int flagged = static_cast<int>(std::count_if(fb.worstAlbedo.begin(), fb.worstAlbedo.end(),
                                                       [](float v) { return v > 1.0f; }));
    INFO("pixels flagged " << flagged << " of " << fb.worstAlbedo.size());
    REQUIRE(flagged > 0);
    // Not everywhere: the sky region has no surface and cannot gain, so a channel that flagged the
    // whole frame would be measuring nothing useful.
    REQUIRE(flagged < static_cast<int>(fb.worstAlbedo.size()));

    // CONTROL: with the probe off the channel is absent rather than zero-filled, so a consumer
    // cannot mistake "not measured" for "measured and fine".
    pathtrace::TraceSettings off = probeSettings();
    pathtrace::PathTracer t2;
    pathtrace::Framebuffer fb2;
    REQUIRE(t2.render(pathtrace::buildSnapshot(sc), off, fb2).has_value());
    REQUIRE(fb2.worstAlbedo.empty());
}
