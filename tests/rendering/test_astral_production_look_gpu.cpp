// THE ASTRAL FORGE iteration 3, the production look (ADR-1150..1153): the GPU half. Every "it works" has a
// control that must come out the other way (ADR-182):
//   ADR-1150: the coarse grid is the block maxima of the volume; skipping empty blocks takes fewer steps AND
//             draws the same surface; the sharpened density surface shades like the tree's own surface.
//   ADR-1151: bands light a mirror AND not the background; gain 0 is byte-identical to no bands; the phase
//             moves the dashes.
//   ADR-1152: an engraving adds fine detail AND a zero-weight engraving adds none; the grating adds colour.
//   ADR-1153: flakes are dark without bands AND glint with them; a bound flake stores the latent normal.
// The CPU half is tests/unit/test_astral_production_look.cpp. `[.perf][astral3]` measures the costs.

#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/frame_timeline.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/particle_renderer.hpp"
#include "rendering/scene_renderer.hpp"
#include "rendering/sdf_renderer.hpp"
#include "scene/reflection_bands.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <vector>

using namespace avgen;

namespace {

std::unique_ptr<gpu::Context> makeContext() {
    static bool logInit = false;
    if (!logInit) {
        log::init(log::Level::Warn);
        logInit = true;
    }
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

constexpr double kDt = 1.0 / 60.0;

FrameTime frameAt(std::uint64_t i) {
    FrameTime t{};
    t.renderTime = static_cast<double>(i) * kDt;
    t.deltaTime = kDt;
    t.frameIndex = i;
    return t;
}

scene::Scene blackScene() {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.environment.showSkybox = false;
    s.environment.gridIntensity = 0.0f;
    s.post.bloomEnabled = false;
    s.post.tonemap = scene::TonemapOperator::Clamp;
    s.camera.position = {0.0f, 0.0f, 8.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    return s;
}

spatial::SdfNode sphereNode(float radius, glm::vec3 at = glm::vec3(0.0f)) {
    spatial::SdfNode n;
    n.kind = spatial::SdfNodeKind::Sphere;
    n.radius = radius;
    if (at == glm::vec3(0.0f)) {
        return n;
    }
    spatial::SdfNode t;
    t.kind = spatial::SdfNodeKind::Translate;
    t.translation = at;
    t.children.push_back(n);
    return t;
}

// A mask sphere the matter binds to, 4096 still particles bursting in a box round it, and a density volume.
void addMatter(scene::Scene& s, glm::vec3 centre, float radius, int count, int resolution) {
    scene::SdfObject mask;
    mask.name = "mask";
    mask.visible = false;
    mask.tree.root = sphereNode(radius);
    mask.transform.position = centre;
    mask.boundsMin = glm::vec3(-2.0f);
    mask.boundsMax = glm::vec3(2.0f);
    s.sdfs.push_back(mask);
    scene::ParticleSystem p;
    p.name = "matter";
    p.capacity = static_cast<std::uint32_t>(count);
    p.seed = 3;
    p.shape = scene::EmitterShape::Box;
    p.position = centre;
    p.extent = glm::vec3(radius * 1.1f);
    p.spawnRate = 0.0f;
    p.burst = static_cast<float>(count);
    p.lifetimeMin = 1000.0f;
    p.lifetimeMax = 1000.0f;
    p.speedMin = 0.0f;
    p.speedMax = 0.0f;
    p.gravity = glm::vec3(0.0f);
    p.drag = 0.0f;
    p.turbulence = 0.0f;
    p.sizeStart = 0.0f;
    p.sizeEnd = 0.0f;
    p.colorStart = glm::vec4(0.0f);
    p.colorEnd = glm::vec4(0.0f);
    p.emissive = 0.0f;
    p.latent.sdf = "mask";
    p.latent.coherence = 1.0f;
    p.density.enabled = true;
    p.density.boundsMin = glm::vec3(-3.0f, -2.0f, -2.0f);
    p.density.boundsMax = glm::vec3(3.0f, 2.0f, 2.0f);
    p.density.resolution = resolution;
    p.density.weight = 1.0f;
    s.particles.push_back(p);
}

gpu::Image8 settle(rendering::SceneRenderer& renderer, scene::Scene& s, int frames, std::uint32_t size = 192) {
    gpu::Image8 last;
    for (int i = 0; i < frames; ++i) {
        auto img = renderer.renderToImage(s, frameAt(static_cast<std::uint64_t>(i)), size, size);
        REQUIRE(img.has_value());
        last = std::move(*img);
        for (auto& p : s.particles) {
            p.burst = 0.0f;
        }
    }
    return last;
}

double luminance(const std::uint8_t* px) { return 0.2126 * px[0] + 0.7152 * px[1] + 0.0722 * px[2]; }

struct Diff {
    double meanAbs = 0.0; // over the pixels either image lit
    long lit = 0;
    long differing = 0;   // channels
};

Diff diff(const gpu::Image8& a, const gpu::Image8& b) {
    Diff d;
    double sum = 0.0;
    for (std::uint32_t y = 0; y < a.height; ++y) {
        for (std::uint32_t x = 0; x < a.width; ++x) {
            const auto* pa = a.pixel(x, y);
            const auto* pb = b.pixel(x, y);
            for (int c = 0; c < 3; ++c) {
                d.differing += pa[c] != pb[c] ? 1 : 0;
            }
            if (luminance(pa) > 8.0 || luminance(pb) > 8.0) {
                ++d.lit;
                sum += std::abs(luminance(pa) - luminance(pb));
            }
        }
    }
    d.meanAbs = d.lit > 0 ? sum / static_cast<double>(d.lit) : 0.0;
    return d;
}

// The mean squared Laplacian of the luminance over the lit pixels: the fine-detail energy.
double detailEnergy(const gpu::Image8& img) {
    double sum = 0.0;
    long n = 0;
    for (std::uint32_t y = 1; y + 1 < img.height; ++y) {
        for (std::uint32_t x = 1; x + 1 < img.width; ++x) {
            const double c = luminance(img.pixel(x, y));
            if (c < 8.0) {
                continue;
            }
            const double l = 4.0 * c - luminance(img.pixel(x - 1, y)) - luminance(img.pixel(x + 1, y)) -
                             luminance(img.pixel(x, y - 1)) - luminance(img.pixel(x, y + 1));
            sum += l * l;
            ++n;
        }
    }
    return n > 0 ? sum / static_cast<double>(n) : 0.0;
}

// Mean chroma (max - min channel) over the lit pixels.
double chroma(const gpu::Image8& img) {
    double sum = 0.0;
    long n = 0;
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            const auto* px = img.pixel(x, y);
            if (luminance(px) < 8.0) {
                continue;
            }
            sum += std::max({px[0], px[1], px[2]}) - std::min({px[0], px[1], px[2]});
            ++n;
        }
    }
    return n > 0 ? sum / static_cast<double>(n) : 0.0;
}

long litPixels(const gpu::Image8& img, double above = 30.0) {
    long n = 0;
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            n += luminance(img.pixel(x, y)) > above ? 1 : 0;
        }
    }
    return n;
}

scene::ReflectionBands studioBands() {
    scene::ReflectionBands b;
    b.enabled = true;
    scene::ReflectionBand top;
    top.axis = glm::vec3(0.0f, 1.0f, 0.0f);
    top.offset = 0.35f;
    top.width = 0.04f;
    top.intensity = 3.0f;
    top.warmth = 0.8f;
    scene::ReflectionBand side;
    side.axis = glm::vec3(1.0f, 0.0f, 0.0f);
    side.offset = -0.2f;
    side.width = 0.03f;
    side.intensity = 2.0f;
    side.segments = 5;
    side.warmth = 0.1f;
    b.strips = {top, side};
    b.softbox.intensity = 0.3f;
    return b;
}

// A polished metal sphere drawn by the raymarch, lit by nothing but what the test adds.
scene::SdfObject mirrorSphere(float radius = 1.6f) {
    scene::SdfObject o;
    o.name = "mirror";
    o.tree.root = sphereNode(radius);
    o.boundsMin = glm::vec3(-2.0f);
    o.boundsMax = glm::vec3(2.0f);
    o.material.baseColor = glm::vec3(0.6f, 0.6f, 0.62f);
    o.material.metallic = 1.0f;
    o.material.roughness = 0.12f;
    return o;
}

} // namespace

// ---- ADR-1150 ---------------------------------------------------------------------------------------------

TEST_CASE("occupancy: the coarse grid holds each 8^3 block's maximum, apron included", "[gpu][particles][density][adr1150]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::ParticleRenderer particles(*ctx, shaders);
    REQUIRE(particles.init().has_value());
    scene::Scene s = blackScene();
    addMatter(s, glm::vec3(-1.0f, 0.2f, 0.0f), 0.8f, 4096, 40); // 40 = five blocks of 8
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 8.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 proj = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
    for (std::uint64_t i = 0; i < 60; ++i) {
        wgpu::CommandEncoder encoder = ctx->device().CreateCommandEncoder();
        particles.update(encoder, s, frameAt(i), view, proj);
        wgpu::CommandBuffer commands = encoder.Finish();
        ctx->queue().Submit(1, &commands);
        s.particles[0].burst = 0.0f;
    }
    auto rho = particles.readDensity(0);
    auto coarse = particles.readDensityCoarse(0);
    REQUIRE(rho.has_value());
    REQUIRE(coarse.has_value());
    const int res = 40;
    const int cr = 5;
    REQUIRE(coarse->size() == static_cast<std::size_t>(cr * cr * cr));
    int mismatches = 0;
    int empty = 0;
    for (int cz = 0; cz < cr; ++cz) {
        for (int cy = 0; cy < cr; ++cy) {
            for (int cx = 0; cx < cr; ++cx) {
                float m = 0.0f;
                for (int z = cz * 8 - 1; z <= cz * 8 + 8; ++z) {
                    for (int y = cy * 8 - 1; y <= cy * 8 + 8; ++y) {
                        for (int x = cx * 8 - 1; x <= cx * 8 + 8; ++x) {
                            const int qx = std::clamp(x, 0, res - 1), qy = std::clamp(y, 0, res - 1),
                                      qz = std::clamp(z, 0, res - 1);
                            m = std::max(m, (*rho)[static_cast<std::size_t>(qx + res * (qy + res * qz))]);
                        }
                    }
                }
                const float got = (*coarse)[static_cast<std::size_t>(cx + cr * (cy + cr * cz))];
                mismatches += got != m ? 1 : 0;
                empty += m == 0.0f ? 1 : 0;
            }
        }
    }
    INFO(mismatches << " of 125 blocks differ from the CPU maximum; " << empty << " blocks are empty");
    CHECK(mismatches == 0);   // max of halves is a half: exact
    CHECK(empty > 20);        // and there is empty space to skip (the matter is on one side)
    CHECK(empty < 120);
    CHECK(ctx->errorCount() == 0);
}

namespace {

// A density-mode sphere: the matter on a sphere on the left, the drawn object's tree the same sphere.
scene::Scene densitySphere(float sharpness, int resolution = 64) {
    scene::Scene s = blackScene();
    const glm::vec3 left(-1.2f, 0.0f, 0.0f);
    addMatter(s, left, 0.9f, 16384, resolution);
    scene::SdfObject body;
    body.name = "body";
    body.tree.root = sphereNode(0.9f, left);
    body.boundsMin = glm::vec3(-3.0f, -2.0f, -2.0f);
    body.boundsMax = glm::vec3(3.0f, 2.0f, 2.0f);
    body.material.baseColor = glm::vec3(0.9f);
    body.material.metallic = 0.0f;
    body.material.roughness = 0.6f;
    body.maxSteps = 512;
    body.stepScale = 0.7f;
    body.density.particles = "matter";
    body.density.iso = 0.5f;
    body.density.sharpness = sharpness;
    s.sdfs.push_back(body);
    scene::PunctualLight key;
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(-0.5f, -0.4f, -0.8f));
    key.intensity = 3.0f;
    key.castsShadow = false;
    s.addLight(key);
    return s;
}

} // namespace

TEST_CASE("occupancy: skipping empty blocks takes fewer steps and draws the same surface",
          "[gpu][sdf][density][adr1150]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const auto render = [&](bool skipping, double& steps) {
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        renderer.sdfs().setDensityOccupancySkipping(skipping);
        scene::Scene s = densitySphere(1.0f);
        auto img = settle(renderer, s, 70);
        steps = renderer.sdfs().stats().avgSteps;
        CHECK(renderer.sdfs().stats().sampledRays > 0);
        return img;
    };
    double stepsOn = 0.0, stepsOff = 0.0;
    const gpu::Image8 on = render(true, stepsOn);
    const gpu::Image8 off = render(false, stepsOff);
    const Diff d = diff(on, off);
    INFO("mean steps per sampled ray: skipping " << stepsOn << ", stepping " << stepsOff << "; lit " << d.lit
                                                 << ", mean |dL| " << d.meanAbs);
    CHECK(stepsOn < 0.6 * stepsOff); // most of the box is empty
    CHECK(d.lit > 1500);             // the surface is there
    CHECK(d.meanAbs < 1.5);          // and it is the same surface (the hit is bisected either way)
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("density normals: a fully sharpened density surface shades like the tree's own surface",
          "[gpu][sdf][density][adr1150]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const auto render = [&](bool density, float sharpness) {
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        scene::Scene s = densitySphere(sharpness);
        if (!density) {
            s.sdfs[1].density = scene::SdfDensitySource{};
        }
        return settle(renderer, s, 70, 256);
    };
    const gpu::Image8 tree = render(false, 1.0f);
    const gpu::Image8 sharp = render(true, 1.0f);
    const gpu::Image8 blob = render(true, 0.0f);
    const Diff dSharp = diff(sharp, tree);
    const Diff dBlob = diff(blob, tree);
    const double eTree = detailEnergy(tree), eSharp = detailEnergy(sharp), eBlob = detailEnergy(blob);
    INFO("mean |dL| against the tree: sharpness 1 " << dSharp.meanAbs << ", sharpness 0 " << dBlob.meanAbs
                                                    << "; detail energy tree " << eTree << ", sharp " << eSharp
                                                    << ", blob " << eBlob);
    CHECK(dSharp.meanAbs < 4.0);           // the sharpened surface IS the sphere, smoothly shaded
    CHECK(dBlob.meanAbs > dSharp.meanAbs); // control: the unsharpened matter is not
    CHECK(eSharp < 4.0 * eTree + 4.0);     // no facets or contour bands on it
    CHECK(ctx->errorCount() == 0);
}

// ---- ADR-1151 ---------------------------------------------------------------------------------------------

TEST_CASE("bands: a mirror reflects them, the background stays black, and at gain 0 nothing is lit",
          "[gpu][bands][adr1151]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const auto render = [&](bool bands, float gain, float phase) {
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        scene::Scene s = blackScene();
        s.environment.environmentIntensity = 0.0f; // whatever environment there is lights nothing
        s.environment.sky.intensity = 0.0f;
        s.sdfs.push_back(mirrorSphere());
        if (bands) {
            s.environment.bands = studioBands();
            s.environment.bands.gain = gain;
            s.environment.bands.phase = phase;
        }
        return settle(renderer, s, 2);
    };
    const gpu::Image8 none = render(false, 1.0f, 0.0f);
    const gpu::Image8 lit = render(true, 1.0f, 0.0f);
    const gpu::Image8 zero = render(true, 0.0f, 0.0f);
    const gpu::Image8 moved = render(true, 1.0f, 2.0f);
    const long bright = litPixels(lit, 120.0);
    const long brightNone = litPixels(none, 120.0);
    const long anyNone = litPixels(none, 8.0);
    const long anyZero = litPixels(zero, 1.0);
    INFO("pixels above 120: with bands " << bright << ", without " << brightNone << "; lit at all: no bands "
                                         << anyNone << ", bands at gain 0 " << anyZero);
    CHECK(bright > 200);           // the strips, in the mirror
    CHECK(bright > 4 * brightNone);
    CHECK(anyZero == 0);           // bands replace the no-map hemisphere: at gain 0 nothing at all is lit
    // the background (the corners) is black: the bands are seen only in reflection
    for (const auto [x, y] : {std::pair{2u, 2u}, std::pair{lit.width - 3, 2u}, std::pair{2u, lit.height - 3},
                              std::pair{lit.width - 3, lit.height - 3}}) {
        const auto* px = lit.pixel(x, y);
        CHECK(std::max({px[0], px[1], px[2]}) == 0);
    }
    CHECK(diff(lit, moved).differing > 50); // the phase turns the dashed strip
    CHECK(ctx->errorCount() == 0);
}

// ---- ADR-1152 ---------------------------------------------------------------------------------------------

namespace {

scene::Engraving guilloche(float weight, float grating) {
    scene::Engraving e;
    e.depth = 0.3f;
    e.grating = grating;
    e.panels = 0.0f;
    scene::EngravingLayer rosette;
    rosette.family = scene::EngravingFamily::Rosette;
    rosette.center = glm::vec3(0.5f, 0.4f, 1.5f);
    rosette.petals = 12.0f;
    rosette.frequency = 10.0f;
    rosette.inner = 0.1f;
    rosette.outer = 0.9f;
    rosette.weight = weight;
    scene::EngravingLayer engine;
    engine.family = scene::EngravingFamily::Engine;
    engine.frequency = 6.0f;
    engine.weight = weight;
    e.layers = {rosette, engine};
    return e;
}

} // namespace

TEST_CASE("engraving: grooves add fine detail and colour, a zero-weight engraving adds none",
          "[gpu][sdf][engraving][adr1152]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const auto render = [&](bool engraved, float weight, float grating) {
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        scene::Scene s = blackScene();
        scene::SdfObject o = mirrorSphere();
        o.material.roughness = 0.2f;
        o.material.anisotropy.strength = 0.6f;
        if (engraved) {
            o.material.engraving = guilloche(weight, grating);
        }
        s.sdfs.push_back(o);
        s.environment.bands = studioBands();
        s.environment.bands.softbox.intensity = 0.6f;
        return settle(renderer, s, 2, 256);
    };
    const gpu::Image8 plain = render(false, 1.0f, 0.0f);
    const gpu::Image8 cut = render(true, 1.0f, 0.0f);
    const gpu::Image8 blank = render(true, 0.0f, 0.0f);
    const gpu::Image8 grating = render(true, 1.0f, 2.0f);
    const double ePlain = detailEnergy(plain), eCut = detailEnergy(cut), eBlank = detailEnergy(blank);
    const Diff dBlank = diff(blank, plain);
    // what the grating added: the pixels it brightened, and how coloured the added light is
    long added = 0;
    double addedChroma = 0.0;
    for (std::uint32_t y = 0; y < cut.height; ++y) {
        for (std::uint32_t x = 0; x < cut.width; ++x) {
            const auto* a0 = cut.pixel(x, y);
            const auto* b0 = grating.pixel(x, y);
            const int d[3] = {b0[0] - a0[0], b0[1] - a0[1], b0[2] - a0[2]};
            const int mx = std::max({d[0], d[1], d[2]});
            if (mx < 6) {
                continue;
            }
            ++added;
            addedChroma += static_cast<double>(mx - std::min({d[0], d[1], d[2]})) / mx;
        }
    }
    addedChroma = added > 0 ? addedChroma / static_cast<double>(added) : 0.0;
    INFO("detail energy: plain " << ePlain << ", engraved " << eCut << ", zero weight " << eBlank
                                 << "; zero weight vs plain mean |dL| " << dBlank.meanAbs << "; the grating brightened "
                                 << added << " pixels, their added light's mean chroma " << addedChroma);
    CHECK(eCut > 2.0 * ePlain + 10.0);
    CHECK(dBlank.meanAbs < 0.5); // the engraved variant with nothing cut draws the plain sphere
    CHECK(added > 20);           // the grooves diffract the bands
    CHECK(addedChroma > 0.35);   // into spectral colour, not white
    CHECK(ctx->errorCount() == 0);
}

// ---- ADR-1153 ---------------------------------------------------------------------------------------------

TEST_CASE("flakes: dark without bands, glinting with them; a bound flake stores the latent normal",
          "[gpu][particles][flake][adr1153]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const auto scene = [](bool bands) {
        scene::Scene s = blackScene();
        addMatter(s, glm::vec3(0.0f), 1.2f, 16384, 32);
        scene::ParticleSystem& p = s.particles[0];
        p.density.enabled = false;
        p.shape2d = scene::ParticleShape::Flake;
        p.flake.sparkle = 0.0f;
        p.flake.glint = 0.03f;
        p.flake.free = 1.0f;
        p.flake.bound = 1.0f;
        p.sizeStart = 0.02f;
        p.sizeEnd = 0.02f;
        p.emissive = 1.0f;
        p.colorStart = glm::vec4(1.0f);
        p.colorEnd = glm::vec4(1.0f);
        if (bands) {
            s.environment.bands = studioBands();
        }
        return s;
    };
    const auto render = [&](bool bands) {
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        scene::Scene s = scene(bands);
        auto img = settle(renderer, s, 90);
        auto all = renderer.particles().readParticles(0);
        REQUIRE(all.has_value());
        double aligned = 0.0;
        long bound = 0;
        for (const auto& q : *all) {
            if (q.life <= 0.0f) {
                continue;
            }
            const glm::vec3 radial = glm::normalize(q.position);
            aligned += glm::dot(glm::vec3(q.home), radial) > 0.95f ? 1.0 : 0.0;
            ++bound;
        }
        return std::pair{img, bound > 0 ? aligned / static_cast<double>(bound) : 0.0};
    };
    const auto [dark, alignedDark] = render(false);
    const auto [glint, alignedGlint] = render(true);
    const long darkLit = litPixels(dark, 20.0);
    const long glintLit = litPixels(glint, 20.0);
    INFO("pixels above 20: no bands " << darkLit << ", bands " << glintLit << "; latent normals along the sphere's "
                                      << alignedGlint);
    CHECK(darkLit == 0);             // nothing to reflect, nothing seen
    CHECK(glintLit > 30);            // a band found: glints
    CHECK(alignedGlint > 0.95);      // every bound plate holds the sphere's normal
    CHECK(ctx->errorCount() == 0);
}

// ---- costs -----------------------------------------------------------------------------------------------

namespace {

double median(std::vector<double> v) {
    if (v.empty()) {
        return -1.0;
    }
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

} // namespace

// Not an assertion: run it by name (`avgen_render_tests "[.perf][astral3]"`) under the GPU lock and read
// the WARN lines. 1080p, p50 of the frames after warm-up.
TEST_CASE("astral production look: GPU cost of occupancy skipping, bands, engraving and flakes", "[.perf][astral3]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const auto measure = [&](const char* name, scene::Scene s, bool skipping) {
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        renderer.sdfs().setDensityOccupancySkipping(skipping);
        std::vector<double> march, frame, particles;
        for (int i = 0; i < 150; ++i) {
            auto img = renderer.renderToImage(s, frameAt(static_cast<std::uint64_t>(i)), 1920, 1080);
            REQUIRE(img.has_value());
            for (auto& p : s.particles) {
                p.burst = 0.0f;
            }
            const auto& stats = renderer.stats();
            if (i >= 90) {
                if (stats.sdf.raymarchMs >= 0.0) march.push_back(stats.sdf.raymarchMs);
                frame.push_back(stats.gpuFrameMs);
            }
        }
        CHECK(ctx->errorCount() == 0);
        WARN(name << ": sdf raymarch pass p50 " << median(march) << " ms (" << march.size() << " samples), GPU frame p50 "
                  << median(frame) << " ms");
    };
    // A frame-filling density sphere (the case ADR-1142's revisit trigger names), 200k particles.
    scene::Scene dens = densitySphere(1.0f, 192);
    dens.camera.position = {-1.2f, 0.0f, 2.6f};
    dens.camera.target = {-1.2f, 0.0f, 0.0f};
    dens.particles[0].capacity = 200000;
    dens.particles[0].burst = 200000.0f;
    dens.sdfs[1].maxSteps = 448;
    measure("density sphere, frame-filling, occupancy skipping OFF", dens, false);
    measure("density sphere, frame-filling, occupancy skipping ON", dens, true);
    // Bands and engraving on a sphere filling ~40% of the frame.
    scene::Scene metal = blackScene();
    metal.camera.position = {0.0f, 0.0f, 5.0f};
    scene::SdfObject sphere = mirrorSphere();
    sphere.material.anisotropy.strength = 0.6f;
    scene::PunctualLight key;
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(-0.5f, -0.4f, -0.8f));
    key.intensity = 2.0f;
    key.castsShadow = false;
    metal.addLight(key);
    metal.sdfs.push_back(sphere);
    measure("metal sphere, no bands", metal, true);
    metal.environment.bands = studioBands();
    measure("metal sphere + bands (2 strips + soft box)", metal, true);
    metal.sdfs[0].material.engraving = guilloche(1.0f, 0.0f);
    measure("metal sphere + bands + engraving (2 layers)", metal, true);
    metal.sdfs[0].material.engraving = guilloche(1.0f, 1.0f);
    measure("metal sphere + bands + engraving + grating", metal, true);
    // 1M particles: round billboards against flakes, with the bands on.
    for (const bool flakes : {false, true}) {
        scene::Scene f = blackScene();
        f.environment.bands = studioBands();
        addMatter(f, glm::vec3(0.0f), 1.5f, 1 << 20, 32);
        scene::ParticleSystem& p = f.particles[0];
        p.density.enabled = false;
        p.latent.coherence = 0.7f;
        p.sizeStart = 0.006f;
        p.sizeEnd = 0.006f;
        p.colorStart = glm::vec4(1.0f, 0.9f, 0.8f, 0.6f);
        p.colorEnd = p.colorStart;
        p.emissive = 1.0f;
        p.shape2d = flakes ? scene::ParticleShape::Flake : scene::ParticleShape::Round;
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        gpu::FrameTimeline* timeline = nullptr;
        (void)timeline;
        std::vector<double> frame;
        for (int i = 0; i < 150; ++i) {
            auto img = renderer.renderToImage(f, frameAt(static_cast<std::uint64_t>(i)), 1920, 1080);
            REQUIRE(img.has_value());
            p.burst = 0.0f;
            if (i >= 90) {
                frame.push_back(renderer.stats().gpuFrameMs);
            }
        }
        CHECK(ctx->errorCount() == 0);
        WARN((flakes ? "1M flakes (bands)" : "1M round billboards (bands)") << ": GPU frame p50 " << median(frame) << " ms");
    }
}
