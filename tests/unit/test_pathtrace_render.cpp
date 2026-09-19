// Phase 1 of the path tracer, end to end (ADR-344): snapshot, BVH, primary rays, Lambertian direct
// lighting, shadow rays, accumulation, EXR.
//
// The scene is the spec's section 69 first visual target rather than Glowmere: a dark environment,
// a large diffuse ground plane, three spheres, a large area light. Phase 1 has no metallic BSDF and
// no textures, so the "metallic" sphere is a bright grey Lambertian here and gets its own test when
// Phase 2 lands. That is stated rather than quietly glossed.
//
// Every image arm asserts something about WHAT IS IN THE PICTURE, not only that the arithmetic
// closed. The Phase 0 spike passed every numeric check while rendering one triangle instead of two,
// because the second was exactly occluded; these tests are shaped to catch that.

#include "assets/exr.hpp"
#include "pathtrace/path_tracer.hpp"
#include "pathtrace/snapshot.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

using namespace avgen;
using Catch::Approx;

namespace {

constexpr glm::vec3 kBlueAlbedo{0.15f, 0.25f, 0.75f};
constexpr glm::vec3 kGreyAlbedo{0.80f, 0.80f, 0.82f};
constexpr glm::vec3 kEmissiveCyan{0.0f, 0.9f, 1.0f};
constexpr glm::vec3 kGroundAlbedo{0.35f, 0.34f, 0.32f};

// The section 69 scene. Spheres sit at x = -2.2, 0, +2.2 on a ground plane at y = 0.
scene::Scene buildTargetScene() {
    scene::Scene s;

    s.environment.backgroundColor = glm::vec3(0.004f, 0.005f, 0.010f);
    s.environment.sky.enabled = true;
    s.environment.sky.zenithColor = glm::vec3(0.010f, 0.020f, 0.045f);
    s.environment.sky.horizonColor = glm::vec3(0.030f, 0.035f, 0.050f);
    s.environment.sky.groundColor = glm::vec3(0.004f, 0.004f, 0.004f);
    s.environment.sky.sunIntensity = 0.0f;   // a dark environment: the area light does the work
    s.environment.sky.useKeyLight = false;
    s.environment.sky.intensity = 1.0f;

    s.camera.position = glm::vec3(0.0f, 2.2f, 8.5f);
    s.camera.target = glm::vec3(0.0f, 1.0f, 0.0f);
    s.camera.up = glm::vec3(0.0f, 1.0f, 0.0f);
    s.camera.lens.useExplicitFov = true;
    s.camera.fovYRadians = 0.7f;

    const auto addEntity = [&](scene::MeshData mesh, glm::vec3 pos, glm::vec3 albedo, glm::vec3 emissive,
                               float emissiveIntensity, float metallic, float roughness, std::string name) {
        const scene::MeshId id = s.addMesh(std::move(mesh));
        scene::Entity& e = s.addEntity(std::move(name), id);
        e.transform.position = pos;
        e.material.baseColor = albedo;
        e.material.emissiveColor = emissive;
        e.material.emissiveIntensity = emissiveIntensity;
        e.material.metallic = metallic;
        e.material.roughness = roughness;
    };

    addEntity(scene::makePlane(14.0f, 1), glm::vec3(0.0f), kGroundAlbedo, glm::vec3(0.0f), 0.0f, 0.0f, 0.9f,
              "ground");
    addEntity(scene::makeIcosphere(1.0f, 3), glm::vec3(-2.2f, 1.0f, 0.0f), kBlueAlbedo, glm::vec3(0.0f), 0.0f,
              0.0f, 0.6f, "sphere-diffuse-blue");
    addEntity(scene::makeIcosphere(1.0f, 3), glm::vec3(0.0f, 1.0f, 0.0f), kGreyAlbedo, glm::vec3(0.0f), 0.0f,
              1.0f, 0.15f, "sphere-metallic");
    addEntity(scene::makeIcosphere(1.0f, 3), glm::vec3(2.2f, 1.0f, 0.0f), glm::vec3(0.02f), kEmissiveCyan,
              6.0f, 0.0f, 0.5f, "sphere-emissive-cyan");

    scene::PunctualLight key;
    key.type = scene::PunctualLight::Type::Rect;
    key.position = glm::vec3(0.0f, 7.0f, 3.0f);
    key.direction = glm::vec3(0.0f, -1.0f, -0.35f); // the way the light travels
    key.up = glm::vec3(0.0f, 0.0f, -1.0f);
    key.color = glm::vec3(1.0f, 0.97f, 0.92f);
    key.intensity = 12.0f;
    key.width = 5.0f;
    key.height = 5.0f;
    key.castsShadow = true;
    key.temperature = 6500.0f; // neutral; the domain is 1500..12000 K and 0 clamps to deep orange
    s.addLight(key);

    return s;
}

pathtrace::TraceSettings fastSettings() {
    pathtrace::TraceSettings t;
    t.width = 160;
    t.height = 100;
    t.samplesPerPixel = 8;
    t.maxDepth = 1;
    t.threads = 2;
    return t;
}

// Mean radiance in a rectangle of the image, so a test can ask about a REGION rather than a pixel.
glm::vec3 regionMean(const pathtrace::Framebuffer& fb, std::uint32_t x0, std::uint32_t y0,
                     std::uint32_t x1, std::uint32_t y1) {
    glm::vec3 sum{0.0f};
    std::uint32_t n = 0;
    for (std::uint32_t y = y0; y < std::min(y1, fb.height); ++y) {
        for (std::uint32_t x = x0; x < std::min(x1, fb.width); ++x) {
            sum += fb.pixel(x, y);
            ++n;
        }
    }
    return n > 0 ? sum / static_cast<float>(n) : glm::vec3(0.0f);
}

float luminance(const glm::vec3& c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

} // namespace

TEST_CASE("the snapshot reports what it can and cannot see", "[unit][pathtrace][snapshot]") {
    scene::Scene s = buildTargetScene();

    // Add things the tracer must refuse rather than silently drop (spec section 87).
    s.particles.emplace_back();
    const scene::MeshId cube = s.addMesh(scene::makeCube(1.0f));
    s.addEntity("hidden", cube).visible = false;

    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(s);

    REQUIRE(snap.meshes.size() == 4); // ground + three spheres; the invisible cube is not one
    REQUIRE(snap.triangleCount() > 100);
    REQUIRE(snap.lights.size() == 1);
    REQUIRE(snap.skyEnabled);

    const auto find = [&](std::string_view f) -> const pathtrace::Capability* {
        for (const auto& c : snap.capabilities.entries) {
            if (c.feature == f) return &c;
        }
        return nullptr;
    };

    const auto* particles = find("particle system");
    REQUIRE(particles != nullptr);
    REQUIRE(particles->support == pathtrace::Support::Unsupported);
    REQUIRE(particles->count == 1);
    REQUIRE_FALSE(particles->detail.empty());
    REQUIRE(snap.capabilities.anyUnsupported());

    const auto* hidden = find("invisible entity");
    REQUIRE(hidden != nullptr);
    REQUIRE(hidden->count == 1);

    // CONTROL: the same scene without the unsupported feature must NOT report it, or the report is
    // a constant and proves nothing.
    scene::Scene clean = buildTargetScene();
    const pathtrace::Snapshot cleanSnap = pathtrace::buildSnapshot(clean);
    REQUIRE_FALSE(cleanSnap.capabilities.anyUnsupported());
    REQUIRE(cleanSnap.meshes.size() == 4);
}

TEST_CASE("the section 69 target scene renders, and the picture has what it should",
          "[unit][pathtrace][render]") {
    const scene::Scene s = buildTargetScene();
    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(s);
    pathtrace::TraceSettings settings = fastSettings();
    settings.samplesPerPixel = 24;
    settings.debugCheckNonFinite = true;

    pathtrace::PathTracer tracer;
    pathtrace::Framebuffer fb;
    const auto ok = tracer.render(snap, settings, fb);
    REQUIRE(ok.has_value());

    // --- the frame is a frame at all ---
    REQUIRE_FALSE(fb.isBlack());
    REQUIRE(fb.meanLuminance() > 0.0);
    REQUIRE(tracer.stats().primaryRays == 160ull * 100ull * 24ull);
    REQUIRE(tracer.stats().shadowRays > 0);
    // Spec section 59: nothing may be NaN, infinite or negative.
    REQUIRE(tracer.stats().nonFiniteSamples == 0);
    REQUIRE(tracer.stats().negativeSamples == 0);
    for (std::uint32_t y = 0; y < fb.height; ++y) {
        for (std::uint32_t x = 0; x < fb.width; ++x) {
            const glm::vec3 c = fb.pixel(x, y);
            REQUIRE(std::isfinite(c.x));
            REQUIRE(std::isfinite(c.y));
            REQUIRE(std::isfinite(c.z));
            REQUIRE(c.x >= 0.0f);
        }
    }

    // --- the three spheres are actually THERE, and are distinguishable from each other ---
    // The spheres sit at x = -2.2, 0, +2.2 and are ~1 m across; at this camera they land near
    // columns 45, 80 and 115, centred a little above the vertical middle.
    const glm::vec3 blue = regionMean(fb, 38, 42, 54, 58);
    const glm::vec3 grey = regionMean(fb, 73, 42, 89, 58);
    const glm::vec3 cyan = regionMean(fb, 108, 42, 124, 58);

    // The blue sphere must READ as blue: more blue than red, by a real margin.
    REQUIRE(blue.z > blue.x * 2.0f);
    // The grey sphere must be near-neutral and brighter than the blue one.
    REQUIRE(luminance(grey) > luminance(blue));
    REQUIRE(std::abs(grey.x - grey.z) < grey.y * 0.35f);
    // The emissive sphere must be the brightest thing in the frame and must read as cyan.
    REQUIRE(luminance(cyan) > luminance(grey) * 2.0f);
    REQUIRE(cyan.z > cyan.x * 5.0f);
    REQUIRE(cyan.y > cyan.x * 5.0f);

    // --- the ground is lit, and is not the same as the sky ---
    const glm::vec3 groundNear = regionMean(fb, 70, 88, 90, 98);
    const glm::vec3 skyTop = regionMean(fb, 70, 0, 90, 6);
    REQUIRE(luminance(groundNear) > luminance(skyTop) * 2.0f);
    REQUIRE(luminance(skyTop) > 0.0f); // the environment is dark, not absent

    // --- there is a SHADOW: the ground directly under a sphere is darker than ground beside it ---
    // This is the arm that proves shadow rays do something. Without it, an occlusion query that
    // always returned false would pass every other check in this test.
    //
    // Measured under the BLUE sphere, at x = -2.2, rather than under the middle one. Since Phase 3
    // the emissive cyan sphere at x = +2.2 is sampled as a light in its own right, so it fills the
    // middle sphere's shadow from the side and that shadow is no longer the darkest thing around.
    // That is better physics, not a regression -- but it makes the middle sphere the wrong place to
    // ask the question.
    const glm::vec3 underBlue = regionMean(fb, 40, 64, 52, 70);
    const glm::vec3 besideSpheres = regionMean(fb, 4, 64, 20, 70);
    INFO("under blue " << luminance(underBlue) << " beside " << luminance(besideSpheres));
    REQUIRE(luminance(besideSpheres) > 0.01f); // live arm: the open ground really is lit
    REQUIRE(luminance(underBlue) < luminance(besideSpheres) * 0.75f);
}

TEST_CASE("CONTROL: with the light removed the lit scene collapses, and with shadows off the shadow goes",
          "[unit][pathtrace][render]") {
    // Two control arms for the test above. Each changes one thing and predicts the direction.
    const pathtrace::TraceSettings settings = fastSettings();

    scene::Scene lit = buildTargetScene();
    pathtrace::PathTracer t1;
    pathtrace::Framebuffer litFb;
    REQUIRE(t1.render(pathtrace::buildSnapshot(lit), settings, litFb).has_value());

    // Arm 1: no light at all. The emissive sphere and the sky remain, everything else goes dark.
    scene::Scene dark = buildTargetScene();
    dark.lights.clear();
    pathtrace::PathTracer t2;
    pathtrace::Framebuffer darkFb;
    REQUIRE(t2.render(pathtrace::buildSnapshot(dark), settings, darkFb).has_value());
    // Shadow rays do NOT fall to zero any more: since Phase 3 the emissive cyan sphere is sampled
    // as a light in its own right and casts its own shadows. What must fall is the count, because
    // one emitter is now being sampled where two light sources were before.
    REQUIRE(t2.stats().shadowRays > 0);
    REQUIRE(t2.stats().shadowRays < t1.stats().shadowRays);

    // Assert on a region the KEY LIGHT governs, not on the whole-frame mean. The emissive sphere is
    // the brightest thing in this frame by a wide margin and it is unaffected by the key light, so
    // the frame mean only falls to ~55% when the light is removed -- a threshold set against the
    // frame mean is measuring the emitter, not the light. Ground at the left edge, far from the
    // emitter's bounce, falls by a factor of tens.
    const float litGround = luminance(regionMean(litFb, 4, 64, 20, 70));
    const float darkGround = luminance(regionMean(darkFb, 4, 64, 20, 70));
    REQUIRE(litGround > 0.05f);                 // the arm is live: the light really lights it
    REQUIRE(darkGround < litGround * 0.1f);     // and removing it really takes that away
    // The frame mean still falls, just not by as much -- recorded so the weaker claim is explicit.
    REQUIRE(darkFb.meanLuminance() < litFb.meanLuminance() * 0.8);

    // But NOT black: the emissive sphere still emits. A renderer that went to black here would be
    // failing to render emission, and "darker" alone would not have noticed.
    REQUIRE_FALSE(darkFb.isBlack());
    REQUIRE(darkFb.meanLuminance() > 0.0);
    // The emissive sphere must be essentially UNCHANGED by removing the key light: it makes its own.
    const float litCyan = luminance(regionMean(litFb, 108, 42, 124, 58));
    const float darkCyan = luminance(regionMean(darkFb, 108, 42, 124, 58));
    REQUIRE(darkCyan > litCyan * 0.85f);

    // Arm 2: the light stays but stops casting shadows. The ground under the middle sphere must
    // brighten, and the sphere itself must be unchanged.
    scene::Scene noShadow = buildTargetScene();
    noShadow.lights[0].castsShadow = false;
    pathtrace::PathTracer t3;
    pathtrace::Framebuffer noShadowFb;
    REQUIRE(t3.render(pathtrace::buildSnapshot(noShadow), settings, noShadowFb).has_value());
    // The analytic light stops casting, but the emissive sphere still does, so this is a fall
    // rather than a zero -- for the same reason as above.
    REQUIRE(t3.stats().shadowRays < t1.stats().shadowRays);

    const float shadowedGround = luminance(regionMean(litFb, 74, 64, 86, 70));
    const float unshadowedGround = luminance(regionMean(noShadowFb, 74, 64, 86, 70));
    REQUIRE(unshadowedGround > shadowedGround * 1.3f);
}

TEST_CASE("the render is deterministic and independent of the thread count",
          "[unit][pathtrace][determinism]") {
    // Spec section 27: the image is a pure function of (snapshot, settings). The thread arm is the
    // one that catches a sampler seeded from anything the scheduler controls.
    const scene::Scene s = buildTargetScene();
    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(s);

    pathtrace::TraceSettings a = fastSettings();
    a.threads = 1;
    pathtrace::TraceSettings b = fastSettings();
    b.threads = 6;

    pathtrace::PathTracer t1;
    pathtrace::Framebuffer fb1;
    REQUIRE(t1.render(snap, a, fb1).has_value());

    pathtrace::PathTracer t2;
    pathtrace::Framebuffer fb2;
    REQUIRE(t2.render(snap, b, fb2).has_value());

    pathtrace::PathTracer t3;
    pathtrace::Framebuffer fb3;
    REQUIRE(t3.render(snap, a, fb3).has_value());

    REQUIRE_FALSE(fb1.isBlack());
    // Bit-identical, in both arms. Path-traced pixels are never compared by equality across
    // DIFFERENT renders (spec section 58) -- but the same render twice is exactly equality.
    REQUIRE(fb1.radiance == fb3.radiance);
    REQUIRE(fb1.radiance == fb2.radiance);

    // CONTROL: a different seed must produce a different image, or the equality above is vacuous.
    pathtrace::TraceSettings c = fastSettings();
    c.seed = a.seed ^ 0xABCDEF;
    pathtrace::PathTracer t4;
    pathtrace::Framebuffer fb4;
    REQUIRE(t4.render(snap, c, fb4).has_value());
    REQUIRE_FALSE(fb4.radiance == fb1.radiance);
    // ...but the two seeds must agree on the IMAGE, within sampling noise: they are the same scene.
    REQUIRE(fb4.meanLuminance() == Approx(fb1.meanLuminance()).epsilon(0.05));
}

TEST_CASE("more samples converge rather than change the answer", "[unit][pathtrace][render]") {
    const scene::Scene s = buildTargetScene();
    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(s);

    const auto meanAt = [&](std::uint32_t spp) {
        pathtrace::TraceSettings t = fastSettings();
        t.samplesPerPixel = spp;
        pathtrace::PathTracer tracer;
        pathtrace::Framebuffer fb;
        REQUIRE(tracer.render(snap, t, fb).has_value());
        return fb.meanLuminance();
    };

    const double m4 = meanAt(4);
    const double m32 = meanAt(32);
    REQUIRE(m4 > 0.0);
    // Statistical tolerance, never equality (spec section 58). Four samples and thirty-two must
    // agree on the image's brightness to within a few percent; if they do not, something is
    // sample-count dependent that should not be.
    REQUIRE(m32 == Approx(m4).epsilon(0.08));
}

TEST_CASE("the tracer refuses an empty scene rather than delivering a black frame",
          "[unit][pathtrace][render]") {
    // Spec section 54: fail explicitly, never silently fall back to black. A black frame that
    // "rendered successfully" is the failure mode this project has been bitten by.
    scene::Scene empty;
    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(empty);
    REQUIRE(snap.empty());

    pathtrace::PathTracer tracer;
    pathtrace::Framebuffer fb;
    const auto result = tracer.render(snap, fastSettings(), fb);
    REQUIRE_FALSE(result.has_value());
    REQUIRE_FALSE(result.error().message.empty());
}

TEST_CASE("invalid settings are refused with a message", "[unit][pathtrace][render]") {
    const scene::Scene s = buildTargetScene();
    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(s);
    pathtrace::PathTracer tracer;
    pathtrace::Framebuffer fb;

    pathtrace::TraceSettings zeroWidth = fastSettings();
    zeroWidth.width = 0;
    REQUIRE_FALSE(tracer.render(snap, zeroWidth, fb).has_value());

    pathtrace::TraceSettings zeroSamples = fastSettings();
    zeroSamples.samplesPerPixel = 0;
    REQUIRE_FALSE(tracer.render(snap, zeroSamples, fb).has_value());

    // CONTROL: the valid settings these were derived from must succeed.
    REQUIRE(tracer.render(snap, fastSettings(), fb).has_value());
}

TEST_CASE("the framebuffer round-trips through scene-linear EXR", "[unit][pathtrace][exr]") {
    const scene::Scene s = buildTargetScene();
    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(s);
    pathtrace::TraceSettings settings = fastSettings();
    settings.width = 64;
    settings.height = 40;

    pathtrace::PathTracer tracer;
    pathtrace::Framebuffer fb;
    REQUIRE(tracer.render(snap, settings, fb).has_value());
    REQUIRE_FALSE(fb.isBlack());

    const auto path = std::filesystem::temp_directory_path() / "avgen_pathtrace_phase1.exr";
    std::filesystem::remove(path);
    REQUIRE(pathtrace::writeFramebufferExr(fb, path, /*half=*/false).has_value());
    REQUIRE(std::filesystem::exists(path));

    const auto read = assets::readExr(path);
    REQUIRE(read.has_value());
    REQUIRE(read->width == settings.width);
    REQUIRE(read->height == settings.height);

    // The values that come back must be the linear radiance that went in -- not tone mapped, not
    // clamped to 1. The emissive sphere puts values above 1 in the frame, and this is where a
    // sneaky display transform would show up.
    const std::vector<float> expected = fb.resolveRgba();
    const auto* pixels = reinterpret_cast<const float*>(read->data.data());
    double worst = 0.0;
    float peak = 0.0f;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        worst = std::max(worst, static_cast<double>(std::abs(pixels[i] - expected[i])));
        peak = std::max(peak, expected[i]);
    }
    REQUIRE(worst < 1e-6);
    REQUIRE(peak > 1.0f); // there IS over-range data, so the check above is not vacuous
    std::filesystem::remove(path);
}

TEST_CASE("a cancelled render stops and keeps what it had", "[unit][pathtrace][render]") {
    const scene::Scene s = buildTargetScene();
    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(s);
    pathtrace::TraceSettings settings = fastSettings();
    settings.threads = 1;

    pathtrace::PathTracer tracer;
    pathtrace::Framebuffer fb;
    const auto ok = tracer.render(snap, settings, fb, [] { return true; });
    REQUIRE(ok.has_value());
    // Cancelled before any row: the buffer is allocated at the right size and is simply empty.
    REQUIRE(fb.width == settings.width);
    REQUIRE(fb.height == settings.height);
    REQUIRE(fb.isBlack());
}

// A hidden case (the leading dot keeps it out of the default run) that writes the target scene as a
// PPM so a human can look at it. Numerical agreement is not proof of visual alignment; this is how
// the pixel regions the tests above assert on were chosen, rather than guessed.
TEST_CASE("preview: write the section 69 scene to /tmp", "[.pathtrace-preview]") {
    scene::Scene s = buildTargetScene();
    // Phase 2: a checkered sRGB base-colour texture on the ground, so the preview exercises UV
    // interpolation, wrap mode and the sRGB decode at the same time as the metal.
    scene::TextureData checker;
    checker.name = "checker";
    checker.width = 16;
    checker.height = 16;
    checker.format = scene::TextureFormat::Rgba8Srgb;
    checker.data.assign(16 * 16 * 4, 0);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            const std::uint8_t v = ((x + y) % 2 == 0) ? 230 : 60;
            auto* p = &checker.data[(static_cast<std::size_t>(y) * 16 + x) * 4];
            p[0] = v; p[1] = v; p[2] = static_cast<std::uint8_t>(v * 0.9f); p[3] = 255;
        }
    }
    const scene::TextureId checkerId = static_cast<scene::TextureId>(s.textures.size());
    s.textures.push_back(std::move(checker));
    s.entities[0].material.baseColorTexture.texture = checkerId;
    s.entities[0].material.baseColorTexture.wrapU = scene::WrapMode::Repeat;
    s.entities[0].material.baseColorTexture.wrapV = scene::WrapMode::Repeat;
    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(s);
    pathtrace::TraceSettings t = fastSettings();
    t.width = 480;
    t.height = 300;
    t.samplesPerPixel = 48;
    t.threads = 8;

    pathtrace::PathTracer tracer;
    pathtrace::Framebuffer fb;
    REQUIRE(tracer.render(snap, t, fb).has_value());

    const auto path = std::filesystem::temp_directory_path() / "avgen_pathtrace_preview.ppm";
    FILE* f = std::fopen(path.c_str(), "wb");
    REQUIRE(f != nullptr);
    std::fprintf(f, "P6\n%u %u\n255\n", fb.width, fb.height);
    for (std::uint32_t y = 0; y < fb.height; ++y) {
        for (std::uint32_t x = 0; x < fb.width; ++x) {
            const glm::vec3 c = fb.pixel(x, y);
            // Gamma only, no tone curve: this is a look at the data, not a deliverable.
            for (int k = 0; k < 3; ++k) {
                const float v = std::pow(std::clamp(c[k], 0.0f, 1.0f), 1.0f / 2.2f);
                const auto b = static_cast<unsigned char>(v * 255.0f + 0.5f);
                std::fwrite(&b, 1, 1, f);
            }
        }
    }
    std::fclose(f);
    WARN("wrote " << path.string() << "  mean luminance " << fb.meanLuminance());
}

TEST_CASE("diagnose: lit vs unlit region means", "[.pathtrace-diag]") {
    const pathtrace::TraceSettings settings = fastSettings();
    const auto run = [&](scene::Scene sc, const char* label) {
        pathtrace::PathTracer tr;
        pathtrace::Framebuffer fb;
        REQUIRE(tr.render(pathtrace::buildSnapshot(sc), settings, fb).has_value());
        WARN(label << " mean=" << fb.meanLuminance()
                   << " sky=" << luminance(regionMean(fb, 70, 0, 90, 6))
                   << " ground=" << luminance(regionMean(fb, 4, 64, 20, 70))
                   << " blue=" << luminance(regionMean(fb, 38, 42, 54, 58))
                   << " cyan=" << luminance(regionMean(fb, 108, 42, 124, 58))
                   << " underGrey=" << luminance(regionMean(fb, 74, 64, 86, 70)));
        return fb;
    };
    run(buildTargetScene(), "lit  ");
    scene::Scene d = buildTargetScene();
    d.lights.clear();
    run(d, "dark ");
}

// Opportunistic benchmark (hidden). Reports minima over repeats, never means (ADR-170), and prints
// the conditions so a number can never be read without them. This is NOT the section 86 Step F
// pass, which wants a quiet machine and the full resolution/sample matrix.
TEST_CASE("bench: scene build, BVH build, render", "[.pathtrace-bench]") {
    const scene::Scene s = buildTargetScene();

    double bestSnap = 1e9;
    for (int i = 0; i < 5; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        const pathtrace::Snapshot sn = pathtrace::buildSnapshot(s);
        const double dt = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        bestSnap = std::min(bestSnap, dt);
        REQUIRE(sn.meshes.size() == 4);
    }
    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(s);
    WARN("snapshot build (min of 5): " << bestSnap << " ms, " << snap.triangleCount() << " triangles");

    struct Case { std::uint32_t w, h, spp; };
    const Case cases[] = {{640, 400, 8}, {640, 400, 32}, {1280, 800, 16}, {1920, 1200, 16}};
    for (const Case& c : cases) {
        pathtrace::TraceSettings t;
        t.width = c.w;
        t.height = c.h;
        t.samplesPerPixel = c.spp;
        t.threads = 0; // all cores
        double bestBuild = 1e9;
        double bestRender = 1e9;
        double meanLum = 0.0;
        for (int i = 0; i < 3; ++i) {
            pathtrace::PathTracer tr;
            pathtrace::Framebuffer fb;
            REQUIRE(tr.render(snap, t, fb).has_value());
            bestBuild = std::min(bestBuild, tr.stats().buildSeconds * 1000.0);
            bestRender = std::min(bestRender, tr.stats().renderSeconds * 1000.0);
            meanLum = fb.meanLuminance();
            REQUIRE_FALSE(fb.isBlack()); // a fast black frame is not a fast render
        }
        WARN(c.w << "x" << c.h << " @" << c.spp << "spp  bvh " << bestBuild << " ms  render "
                 << bestRender << " ms  (min of 3, " << std::thread::hardware_concurrency()
                 << " threads, mean luminance " << meanLum << ")");
    }
}

TEST_CASE("a base-colour texture reaches the shading, and its colour space is respected",
          "[unit][pathtrace][render][texture]") {
    // Two arms over the same geometry and lighting. Arm A puts a high-contrast checker on the
    // ground; arm B puts a SOLID texture whose linear value is the checker's linear mean. If the
    // texture never reached the shading, both would render identically and the variance test below
    // would fail -- which is the point of pairing them.
    const auto makeGround = [](bool checker) {
        scene::Scene s = buildTargetScene();
        scene::TextureData t;
        t.width = 8;
        t.height = 8;
        t.format = scene::TextureFormat::Rgba8Srgb;
        t.data.assign(8 * 8 * 4, 0);
        for (int y = 0; y < 8; ++y) {
            for (int x = 0; x < 8; ++x) {
                std::uint8_t v = 0;
                if (checker) {
                    v = ((x + y) % 2 == 0) ? 240 : 30;
                } else {
                    // The sRGB byte whose LINEAR value is the mean of the two checker linear values.
                    const float lin = 0.5f * (std::pow(240.0f / 255.0f, 2.2f) + std::pow(30.0f / 255.0f, 2.2f));
                    v = static_cast<std::uint8_t>(std::pow(lin, 1.0f / 2.2f) * 255.0f + 0.5f);
                }
                auto* p = &t.data[(static_cast<std::size_t>(y) * 8 + x) * 4];
                p[0] = v; p[1] = v; p[2] = v; p[3] = 255;
            }
        }
        const auto id = static_cast<scene::TextureId>(s.textures.size());
        s.textures.push_back(std::move(t));
        s.entities[0].material.baseColorTexture.texture = id;
        s.entities[0].material.baseColorTexture.linearFilter = false; // keep the edges hard
        return s;
    };

    // 64 spp, not 16. At 16 the solid arm's row variance is 0.56 -- pure firefly noise from the
    // emissive sphere's bounce -- against a checker signal of 1.7, which is only 3x and would have
    // meant lowering the threshold to fit the data. Quadrupling the samples quarters the noise
    // floor instead, so a strict threshold keeps real headroom. Phase 3's MIS is the actual fix.
    pathtrace::TraceSettings settings = fastSettings();
    settings.samplesPerPixel = 64;

    const auto render = [&](const scene::Scene& sc) {
        pathtrace::PathTracer tr;
        pathtrace::Framebuffer fb;
        REQUIRE(tr.render(pathtrace::buildSnapshot(sc), settings, fb).has_value());
        return fb;
    };

    const pathtrace::Framebuffer checkered = render(makeGround(true));
    const pathtrace::Framebuffer solid = render(makeGround(false));

    // Spatial variance along a scanline across the near ground, away from the spheres.
    const auto rowVariance = [](const pathtrace::Framebuffer& fb, std::uint32_t y) {
        double sum = 0.0;
        double sum2 = 0.0;
        int n = 0;
        for (std::uint32_t x = 2; x < fb.width - 2; ++x) {
            const glm::vec3 c = fb.pixel(x, y);
            const double l = 0.2126 * c.x + 0.7152 * c.y + 0.0722 * c.z;
            sum += l;
            sum2 += l * l;
            ++n;
        }
        const double mean = sum / n;
        return (sum2 / n) - mean * mean;
    };

    const double vChecker = rowVariance(checkered, 92);
    const double vSolid = rowVariance(solid, 92);
    INFO("checker variance " << vChecker << " solid variance " << vSolid);
    // The checker must be visibly patterned and the solid must not be. Both carry the same
    // path-tracing noise, so the difference is the texture and nothing else.
    REQUIRE(vChecker > vSolid * 4.0);
    REQUIRE(vSolid >= 0.0);

    // And the two must agree on overall brightness: same linear mean albedo, same illumination.
    // This is the arm that catches a missing sRGB decode -- without it the checker's mean would
    // land somewhere else entirely.
    const auto rowMean = [](const pathtrace::Framebuffer& fb, std::uint32_t y) {
        double sum = 0.0;
        int n = 0;
        for (std::uint32_t x = 2; x < fb.width - 2; ++x) {
            const glm::vec3 c = fb.pixel(x, y);
            sum += 0.2126 * c.x + 0.7152 * c.y + 0.0722 * c.z;
            ++n;
        }
        return sum / n;
    };
    REQUIRE(rowMean(checkered, 92) == Catch::Approx(rowMean(solid, 92)).epsilon(0.15));
}

TEST_CASE("a metal reflects its surroundings and a dielectric does not",
          "[unit][pathtrace][render][bsdf]") {
    // The middle sphere is metallic 1.0, roughness 0.15, so at depth >= 1 it must pick up the cyan
    // emissive sphere beside it. The diffuse blue sphere, at the same distance on the other side,
    // must not. That asymmetry is the thing that says "metal" rather than "bright grey".
    scene::Scene s = buildTargetScene();
    pathtrace::TraceSettings settings = fastSettings();
    settings.maxDepth = 2;
    settings.samplesPerPixel = 96;

    pathtrace::PathTracer tr;
    pathtrace::Framebuffer fb;
    REQUIRE(tr.render(pathtrace::buildSnapshot(s), settings, fb).has_value());

    // The right-hand limb of the metal sphere faces the cyan emitter.
    const glm::vec3 metalRight = regionMean(fb, 86, 44, 92, 56);
    // The left-hand limb of the diffuse blue sphere, the mirror-image position, faces nothing.
    const glm::vec3 blueLeft = regionMean(fb, 36, 44, 42, 56);

    // "Reflects cyan" means the green and blue channels beat red on the metal's lit limb.
    INFO("metal right " << metalRight.x << "," << metalRight.y << "," << metalRight.z);
    REQUIRE(metalRight.z > metalRight.x * 1.5f);
    REQUIRE(metalRight.y > metalRight.x * 1.3f);
    // CONTROL: the blue sphere is blue because its ALBEDO is blue, so this alone would not
    // distinguish them. The discriminating claim is that the metal's cyan is much greener than the
    // blue sphere's, whose albedo has very little green.
    REQUIRE(metalRight.y / metalRight.x > blueLeft.y / blueLeft.x);
}

TEST_CASE("the capability report warns about the BRDF's grazing energy gain on every render",
          "[unit][pathtrace][capability]") {
    // ADR-345. The owner chose to keep the glTF model faithful, gain and all, so the gain is
    // permanent and the warning must be too. It goes in the startup report because an ADR is no use
    // to somebody who does not already suspect the BRDF.
    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(buildTargetScene());
    REQUIRE_FALSE(snap.capabilities.caveats.empty());

    const bool found = std::any_of(
        snap.capabilities.caveats.begin(), snap.capabilities.caveats.end(),
        [](const std::string& c) { return c.find("ADR-345") != std::string::npos; });
    REQUIRE(found);
    // It must say the number and the direction, not merely that a caveat exists.
    const std::string& c = snap.capabilities.caveats.front();
    REQUIRE(c.find("1.68") != std::string::npos);
    REQUIRE(c.find("grazing") != std::string::npos);
    REQUIRE(c.find("bright") != std::string::npos);

    // It is printed. `format()` is what reaches the log.
    const std::string report = snap.capabilities.format();
    REQUIRE(report.find("caveat") != std::string::npos);
    REQUIRE(report.find("ADR-345") != std::string::npos);

    // CONTROL: a caveat is NOT a scene-content problem. An unremarkable scene must still report
    // nothing degraded or unsupported, or callers branching on those lose the distinction.
    REQUIRE_FALSE(snap.capabilities.anyUnsupported());
    REQUIRE_FALSE(snap.capabilities.anyDegraded());
}
