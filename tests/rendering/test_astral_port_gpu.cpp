// THE ASTRAL FORGE's iteration-4 port, the GPU half. Every "it works" has a control that must come out the
// other way (ADR-182):
//   ADR-1146 tendons: bound matter gathers on the authored curves AND streams along them; coherence 0 does not.
//   ADR-1148 heat:    a coherence drop heats the matter the front passes AND not the matter it does not reach;
//                     the heat decays; without a drop there is none.
//   ADR-1147 shards:  near bound plates become lit geometry AND a zero fraction draws exactly the flakes.
//   ADR-1149 regions: the film thickens and colours near a region point AND not far from it.
//   ADR-1154 domain:  the line field follows the tree's domain chain (a turned tree engraves like a turned
//                     axis) AND would not without it.
// The CPU half is tests/unit/test_astral_port.cpp. `[.perf][astral4]` measures the costs.

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
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

using namespace avgen;

namespace {

std::vector<std::filesystem::path> shaderDirs() {
    std::vector<std::filesystem::path> dirs;
    if (const char* env = std::getenv("AVGEN_SHADER_DIR")) {
        dirs.emplace_back(env);
    }
    dirs.emplace_back(AVGEN_SHADER_SOURCE_DIR);
    return dirs;
}

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

spatial::SdfNode sphereNode(float radius) {
    spatial::SdfNode n;
    n.kind = spatial::SdfNodeKind::Sphere;
    n.radius = radius;
    return n;
}

scene::SdfObject maskSphere(float radius = 1.0f) {
    scene::SdfObject o;
    o.name = "mask";
    o.visible = false;
    o.tree.root = sphereNode(radius);
    o.boundsMin = glm::vec3(-2.0f);
    o.boundsMax = glm::vec3(2.0f);
    return o;
}

scene::ParticleSystem matter(int count, float coherence, glm::vec3 extent = glm::vec3(1.6f)) {
    scene::ParticleSystem p;
    p.name = "matter";
    p.capacity = static_cast<std::uint32_t>(count);
    p.seed = 3;
    p.shape = scene::EmitterShape::Box;
    p.extent = extent;
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
    p.latent.coherence = coherence;
    p.latent.release = 12.0f;
    return p;
}

struct Stepper {
    gpu::Context& ctx;
    gpu::ShaderLibrary shaders;
    rendering::ParticleRenderer particles;
    std::uint64_t frame = 0;
    explicit Stepper(gpu::Context& c) : ctx(c), shaders(c, shaderDirs()), particles(c, shaders) {
        REQUIRE(particles.init().has_value());
    }
    void step(scene::Scene& s, int frames) {
        const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 8.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 proj = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
        for (int i = 0; i < frames; ++i) {
            wgpu::CommandEncoder encoder = ctx.device().CreateCommandEncoder();
            particles.update(encoder, s, frameAt(frame++), view, proj);
            wgpu::CommandBuffer commands = encoder.Finish();
            ctx.queue().Submit(1, &commands);
            for (auto& p : s.particles) {
                p.burst = 0.0f;
            }
        }
    }
    std::vector<rendering::ParticleSnapshot> alive() {
        auto all = particles.readParticles(0);
        REQUIRE(all.has_value());
        std::vector<rendering::ParticleSnapshot> out;
        for (const auto& p : *all) {
            if (p.life > 0.0f) {
                out.push_back(p);
            }
        }
        return out;
    }
};

// Distance from p to a polyline, and the polyline's unit direction at the nearest point.
std::pair<float, glm::vec3> nearestOnCurve(const std::vector<glm::vec3>& c, glm::vec3 p) {
    float best = 1e9f;
    glm::vec3 dir(0.0f);
    for (std::size_t k = 1; k < c.size(); ++k) {
        const glm::vec3 a = c[k - 1];
        const glm::vec3 ba = c[k] - a;
        const float h = std::clamp(glm::dot(p - a, ba) / glm::dot(ba, ba), 0.0f, 1.0f);
        const float d = glm::length(p - a - ba * h);
        if (d < best) {
            best = d;
            dir = glm::normalize(ba);
        }
    }
    return {best, dir};
}

std::vector<std::vector<glm::vec3>> testCurves() {
    std::vector<std::vector<glm::vec3>> curves;
    std::vector<glm::vec3> arc;
    for (int k = 0; k <= 12; ++k) {
        const float a = 3.14159265f * static_cast<float>(k) / 12.0f;
        arc.emplace_back(1.2f * std::cos(a), 1.2f * std::sin(a) - 0.4f, 0.3f);
    }
    curves.push_back(arc);
    curves.push_back({glm::vec3(-1.0f, -0.8f, -0.2f), glm::vec3(0.0f, -1.1f, 0.0f), glm::vec3(1.0f, -0.8f, -0.2f)});
    return curves;
}

} // namespace

// ---- ADR-1146 ---------------------------------------------------------------------------------------------

TEST_CASE("tendons: bound matter rides the authored curves and streams along them; coherence 0 does not",
          "[gpu][particles][latent][tendons]") {
    auto ctx = makeContext();
    const auto curves = testCurves();
    const auto run = [&](float coherence) {
        scene::Scene s;
        s.sdfs.push_back(maskSphere());
        scene::ParticleSystem p = matter(8192, coherence);
        p.latent.tendons.curves = curves;
        p.latent.tendons.speed = 0.6f;
        s.particles.push_back(p);
        Stepper st(*ctx);
        st.step(s, 240);
        return st.alive();
    };
    const auto bound = run(1.0f);
    const auto loose = run(0.0f);
    REQUIRE(bound.size() == 8192);
    const auto share = [&](const std::vector<rendering::ParticleSnapshot>& ps, double& streaming) {
        std::size_t near = 0;
        double along = 0.0;
        std::size_t moving = 0;
        for (const auto& p : ps) {
            float best = 1e9f;
            glm::vec3 dir(0.0f);
            for (const auto& c : curves) {
                const auto [d, t] = nearestOnCurve(c, p.position);
                if (d < best) {
                    best = d;
                    dir = t;
                }
            }
            if (best < 0.06f) {
                ++near;
                const float speed = glm::length(p.velocity);
                if (speed > 1e-3f) {
                    along += std::abs(glm::dot(p.velocity / speed, dir));
                    ++moving;
                }
            }
        }
        streaming = moving > 0 ? along / static_cast<double>(moving) : 0.0;
        return static_cast<double>(near) / static_cast<double>(std::max<std::size_t>(ps.size(), 1));
    };
    double streamBound = 0.0;
    double streamLoose = 0.0;
    const double onBound = share(bound, streamBound);
    const double onLoose = share(loose, streamLoose);
    INFO("within 0.06 of a curve: coherence 1 " << onBound << ", coherence 0 " << onLoose
                                                << "; |cos(velocity, curve)| on the curves " << streamBound);
    CHECK(onBound > 0.6);        // most of the matter is on a curve (the rest is ramping in or spraying off)
    CHECK(onLoose < 0.1);        // control: nothing binds at coherence 0
    CHECK(streamBound > 0.8);    // and it moves ALONG the curves, not across them
    CHECK(ctx->errorCount() == 0);
}

// ---- ADR-1155 ---------------------------------------------------------------------------------------------

TEST_CASE("stagger: a projection refreshed every third step binds the matter as every step does, and stores it",
          "[gpu][particles][latent][stagger]") {
    auto ctx = makeContext();
    const auto run = [&](int stagger, float coherence) {
        scene::Scene s;
        s.sdfs.push_back(maskSphere(1.0f));
        scene::ParticleSystem p = matter(8192, coherence);
        p.shape2d = scene::ParticleShape::Flake; // so the normal is stored and read back
        p.latent.stagger = stagger;
        s.particles.push_back(p);
        Stepper st(*ctx);
        st.step(s, 180);
        return st.alive();
    };
    const auto shell = [](const std::vector<rendering::ParticleSnapshot>& ps) {
        std::size_t n = 0;
        for (const auto& p : ps) {
            n += std::abs(glm::length(p.position) - 1.0f) < 0.02f ? 1 : 0;
        }
        return static_cast<double>(n) / static_cast<double>(std::max<std::size_t>(ps.size(), 1));
    };
    const auto every = run(1, 1.0f);
    const auto third = run(3, 1.0f);
    const auto loose = run(3, 0.0f);
    std::size_t stored = 0;
    double worstNormal = 0.0;
    for (const auto& p : third) {
        if (p.home.w <= -0.5) { // a packed normal: the stored point is on the sphere, the normal radial
            ++stored;
            worstNormal = std::max(worstNormal, static_cast<double>(std::abs(glm::length(glm::vec3(p.home)) - 1.0f)));
        }
    }
    INFO("on the sphere: every step " << shell(every) << ", every third " << shell(third) << ", coherence 0 "
                                     << shell(loose) << "; stored " << stored << ", worst stored point off the sphere "
                                     << worstNormal);
    CHECK(shell(every) > 0.99);
    CHECK(shell(third) > 0.99);
    CHECK(shell(loose) < 0.1);
    CHECK(stored == third.size());
    CHECK(worstNormal < 0.01);
    CHECK(ctx->errorCount() == 0);
}

// ---- ADR-1148 ---------------------------------------------------------------------------------------------

TEST_CASE("heat: a coherence drop heats what the front passes, not what it does not reach, and the heat decays",
          "[gpu][particles][latent][heat]") {
    auto ctx = makeContext();
    scene::Scene s;
    s.sdfs.push_back(maskSphere(1.0f));
    scene::ParticleSystem p = matter(8192, 1.0f);
    p.latent.heat.enabled = true;
    p.latent.heat.origin = glm::vec3(0.0f, 1.0f, 0.0f); // the sphere's top
    p.latent.heat.speed = 6.0f;                         // 0.1 units per step at 60 Hz
    p.latent.heat.width = 0.25f;
    p.latent.heat.inject = 1.0f;
    p.latent.heat.decay = 3.0f;
    s.particles.push_back(p);
    Stepper st(*ctx);
    st.step(s, 120); // bound onto the sphere
    double heatBefore = 0.0;
    for (const auto& q : st.alive()) {
        heatBefore = std::max(heatBefore, static_cast<double>(q.trail));
    }
    // The collapse: one step from 1 to 0.1. The front has just set out (radius 0 this step), so only matter
    // within ~2 widths of the top is heated.
    s.particles[0].latent.coherence = 0.1f;
    st.step(s, 1);
    const auto hot = st.alive();
    double nearSum = 0.0;
    double hottest = 0.0;
    double farMax = 0.0;
    std::size_t nearCount = 0;
    for (const auto& q : hot) {
        hottest = std::max(hottest, static_cast<double>(q.trail));
        const float d = glm::length(q.position - glm::vec3(0.0f, 1.0f, 0.0f));
        if (d < 0.3f) {
            nearSum += q.trail;
            ++nearCount;
        } else if (d > 0.8f) {
            farMax = std::max(farMax, static_cast<double>(q.trail));
        }
    }
    const double nearMean = nearCount > 0 ? nearSum / static_cast<double>(nearCount) : 0.0;
    st.step(s, 60); // a second later
    double later = 0.0;
    for (const auto& q : st.alive()) {
        later = std::max(later, static_cast<double>(q.trail));
    }
    INFO("heat before the drop " << heatBefore << "; after it, mean within 0.3 of the origin " << nearMean << " over "
                                 << nearCount << ", hottest " << hottest << ", max beyond 0.8 " << farMax
                                 << "; max a second later " << later);
    CHECK(heatBefore == 0.0);    // no drop, no heat
    CHECK(nearCount > 20);
    CHECK(nearMean > 0.1);       // the front heats what it passes
    CHECK(farMax < 0.01 * hottest); // and not what it has not reached
    CHECK(later < 0.06 * hottest);  // exp(-3) after a second (no particle is heated again)
    CHECK(ctx->errorCount() == 0);
}

// ---- ADR-1147, ADR-1149, ADR-1154: rendered -------------------------------------------------------------

namespace {

scene::Scene blackScene() {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.environment.showSkybox = false;
    s.environment.gridIntensity = 0.0f;
    s.environment.environmentIntensity = 0.0f;
    s.environment.sky.intensity = 0.0f;
    s.post.bloomEnabled = false;
    s.post.tonemap = scene::TonemapOperator::Clamp;
    s.camera.position = {0.0f, 0.0f, 8.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    return s;
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

scene::SdfObject metalSphere(float radius = 1.6f) {
    scene::SdfObject o;
    o.name = "metal";
    o.tree.root = sphereNode(radius);
    o.boundsMin = glm::vec3(-2.0f);
    o.boundsMax = glm::vec3(2.0f);
    o.material.baseColor = glm::vec3(0.6f, 0.6f, 0.62f);
    o.material.metallic = 1.0f;
    o.material.roughness = 0.12f;
    return o;
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

long differing(const gpu::Image8& a, const gpu::Image8& b) {
    long n = 0;
    for (std::uint32_t y = 0; y < a.height; ++y) {
        for (std::uint32_t x = 0; x < a.width; ++x) {
            for (int c = 0; c < 3; ++c) {
                n += a.pixel(x, y)[c] != b.pixel(x, y)[c] ? 1 : 0;
            }
        }
    }
    return n;
}

double meanAbs(const gpu::Image8& a, const gpu::Image8& b) {
    double sum = 0.0;
    long n = 0;
    for (std::uint32_t y = 0; y < a.height; ++y) {
        for (std::uint32_t x = 0; x < a.width; ++x) {
            if (luminance(a.pixel(x, y)) > 8.0 || luminance(b.pixel(x, y)) > 8.0) {
                sum += std::abs(luminance(a.pixel(x, y)) - luminance(b.pixel(x, y)));
                ++n;
            }
        }
    }
    return n > 0 ? sum / static_cast<double>(n) : 0.0;
}

// Mean chroma (max - min channel) over the lit pixels of a rectangle.
double chroma(const gpu::Image8& img, std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1) {
    double sum = 0.0;
    long n = 0;
    for (std::uint32_t y = y0; y < y1; ++y) {
        for (std::uint32_t x = x0; x < x1; ++x) {
            const auto* px = img.pixel(x, y);
            if (luminance(px) < 6.0) {
                continue;
            }
            sum += std::max({px[0], px[1], px[2]}) - std::min({px[0], px[1], px[2]});
            ++n;
        }
    }
    return n > 0 ? sum / static_cast<double>(n) : 0.0;
}

} // namespace

TEST_CASE("shards: near bound plates become lit geometry; a zero fraction draws exactly the flakes",
          "[gpu][particles][shards]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, shaderDirs());
    const auto render = [&](bool shards, float fraction) {
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        scene::Scene s = blackScene();
        s.camera.position = {0.0f, 0.0f, 3.2f};
        s.environment.bands = studioBands();
        scene::SdfObject body = metalSphere(1.0f);
        s.sdfs.push_back(body);
        s.sdfs.push_back(maskSphere(1.02f));
        scene::ParticleSystem p = matter(16384, 1.0f);
        p.shape2d = scene::ParticleShape::Flake;
        p.sizeStart = p.sizeEnd = 0.03f; // large enough to resolve this close
        p.colorStart = p.colorEnd = glm::vec4(1.0f);
        p.emissive = 1.0f;
        p.flake.temper = 40.0f;
        p.shards.enabled = shards;
        p.shards.fraction = fraction;
        p.shards.pixels = 1.0f;
        s.particles.push_back(p);
        return settle(renderer, s, 90, 256);
    };
    const gpu::Image8 flakes = render(false, 0.25f);
    const gpu::Image8 none = render(true, 0.0f);
    const gpu::Image8 shards = render(true, 1.0f);
    const long zeroDiff = differing(none, flakes);
    const long shardDiff = differing(shards, flakes);
    INFO("channels differing from the flakes alone: shards at fraction 0 " << zeroDiff << ", at fraction 1 "
                                                                         << shardDiff);
    CHECK(zeroDiff == 0);    // nothing resolves: exactly the flakes
    CHECK(shardDiff > 300);  // the near plates are drawn as shards
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("regions: the film thickens and colours near a region point and not far from it",
          "[gpu][sdf][regions]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, shaderDirs());
    const auto render = [&](float film) {
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        scene::Scene s = blackScene();
        s.environment.bands = studioBands();
        scene::SdfObject o = metalSphere();
        o.material.thinFilm.thickness = 20.0f;
        o.material.thinFilm.ior = 2.4f;
        o.material.regions.film = film;
        scene::SurfaceRegion point;
        point.center = glm::vec3(-1.0f, 0.0f, 1.2f); // the left of the sphere, facing the camera
        point.sharpness = 4.0f;
        o.material.regions.points.push_back(point);
        s.sdfs.push_back(o);
        return settle(renderer, s, 2, 256);
    };
    const gpu::Image8 off = render(0.0f);
    const gpu::Image8 on = render(260.0f);
    // left third (the region) against the right third (far from it)
    const double leftOff = chroma(off, 40, 64, 100, 192), leftOn = chroma(on, 40, 64, 100, 192);
    const double rightOff = chroma(off, 156, 64, 216, 192), rightOn = chroma(on, 156, 64, 216, 192);
    INFO("chroma, left (the region): film 0 " << leftOff << ", film 260 " << leftOn << "; right: " << rightOff << ", "
                                             << rightOn);
    CHECK(leftOn > leftOff + 5.0);              // the region's film colours the metal
    CHECK(std::abs(rightOn - rightOff) < 2.0);  // and far from it nothing changes
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("engraving domain: a turned tree engraves like a turned axis, and would not without the chain",
          "[gpu][sdf][engraving][adr1154]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, shaderDirs());
    const auto render = [&](bool turnTree, glm::vec3 axis) {
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        scene::Scene s = blackScene();
        s.environment.bands = studioBands();
        scene::SdfObject o = metalSphere();
        if (turnTree) {
            spatial::SdfNode rot;
            rot.kind = spatial::SdfNodeKind::Rotate;
            rot.rotationDegrees = glm::vec3(0.0f, 180.0f, 0.0f);
            rot.children.push_back(o.tree.root);
            o.tree.root = rot;
        }
        scene::EngravingLayer engine;
        engine.family = scene::EngravingFamily::Engine;
        engine.axis = axis;
        engine.frequency = 5.0f;
        engine.weight = 1.0f;
        o.material.engraving.depth = 0.4f;
        o.material.engraving.layers = {engine};
        s.sdfs.push_back(o);
        return settle(renderer, s, 2, 256);
    };
    // The tree turned 180 degrees about y: its domain point is (-x, y, -z), so the engine-turned lines across +x
    // in the domain (v = -x, u = 3y) are exactly what an unturned sphere cuts with the axis -x.
    const gpu::Image8 turned = render(true, glm::vec3(1.0f, 0.0f, 0.0f));
    const gpu::Image8 axisTurned = render(false, glm::vec3(-1.0f, 0.0f, 0.0f));
    const gpu::Image8 unturned = render(false, glm::vec3(1.0f, 0.0f, 0.0f));
    const double same = meanAbs(turned, axisTurned);
    const double other = meanAbs(turned, unturned);
    INFO("mean |dL|: turned tree vs turned axis " << same << "; vs the unturned lines (what object space cut) "
                                                  << other);
    CHECK(same < 1.0);
    CHECK(other > 3.0 * same + 1.0);
    CHECK(ctx->errorCount() == 0);
}

// ---- measured cost (not an assertion: run `avgen_render_tests "[.perf][astral4]"` under the GPU lock) -------
//
// The particle renderer alone with its own timeline (the only marked passes are the particle compute pass and
// the density pass), 1M particles bound at coherence 0.93 on a 2.6-unit cloud: the latent sphere interpreted
// (the control), ADR-1140's crude 7-record mask interpreted, the authored Astral Forge face compiled (ADR-1144,
// ADR-1145), the face with the heat front (ADR-1148) and the 18 face tendon curves (ADR-1146).
TEST_CASE("astral port: GPU cost of the compiled face latent, tendons and heat", "[.perf][astral4]") {
    auto ctx = makeContext();
    spatial::SdfTree face;
    {
        std::ifstream in(std::string(AVGEN_SOURCE_DIR) + "/examples/astral-forge/compare-t01-face.scene.json");
        REQUIRE(in.good());
        const nlohmann::json scene = nlohmann::json::parse(in);
        for (const auto& n : scene.at("nodes")) {
            if (n.at("name") == "latent") {
                auto t = spatial::SdfTree::fromJson(n.at("sdf").at("tree"));
                REQUIRE(t);
                face = *t;
            }
        }
    }
    std::vector<std::vector<glm::vec3>> faceCurves;
    {
        std::ifstream in(std::string(AVGEN_SOURCE_DIR) + "/examples/astral-forge/compare-t01-face.scene.json");
        const nlohmann::json scene = nlohmann::json::parse(in);
        for (const auto& n : scene.at("nodes")) {
            if (n.at("name") == "tendons") {
                for (const auto& c : n.at("particles").at("latent").at("tendons").at("curves")) {
                    std::vector<glm::vec3> pts;
                    for (const auto& q : c) {
                        pts.emplace_back(q[0].get<float>(), q[1].get<float>(), q[2].get<float>());
                    }
                    faceCurves.push_back(pts);
                }
            }
        }
    }
    REQUIRE(faceCurves.size() == 18);
    spatial::SdfTree crude;
    {
        spatial::SdfNode plate;
        plate.kind = spatial::SdfNodeKind::RoundedBox;
        plate.size = glm::vec3(1.05f, 1.45f, 0.22f);
        plate.rounding = 0.2f;
        spatial::SdfNode eye = sphereNode(0.36f);
        const auto moved = [](glm::vec3 t, spatial::SdfNode child) {
            spatial::SdfNode n;
            n.kind = spatial::SdfNodeKind::Translate;
            n.translation = t;
            n.children.push_back(std::move(child));
            return n;
        };
        spatial::SdfNode root;
        root.kind = spatial::SdfNodeKind::SmoothUnion;
        root.smooth = 0.18f;
        root.children = {plate, moved({-0.5f, 0.38f, 0.2f}, eye), moved({0.5f, 0.38f, 0.2f}, eye),
                         moved({0.0f, -0.62f, 0.18f}, sphereNode(0.2f))};
        crude.root = root;
    }
    enum class Kind { Sphere, Crude, Face, FaceStagger, FaceHeat, Tendons };
    struct Arm {
        const char* name;
        Kind kind;
    };
    for (const Arm arm : {Arm{"latent sphere, interpreted (control)", Kind::Sphere},
                          Arm{"ADR-1140's crude mask, interpreted (7 records)", Kind::Crude},
                          Arm{"the authored face, compiled (ADR-1144/1145)", Kind::Face},
                          Arm{"the face, compiled, staggered 3 (ADR-1155)", Kind::FaceStagger},
                          Arm{"the face + the heat front (ADR-1148)", Kind::FaceHeat},
                          Arm{"18 face tendons (ADR-1146)", Kind::Tendons}}) {
        scene::Scene s;
        scene::SdfObject mask = maskSphere(1.0f);
        mask.boundsMin = glm::vec3(-6.0f);
        mask.boundsMax = glm::vec3(6.0f);
        if (arm.kind == Kind::Crude) {
            mask.tree = crude;
        } else if (arm.kind == Kind::Face || arm.kind == Kind::FaceHeat || arm.kind == Kind::FaceStagger) {
            mask.tree = face;
            mask.compile = true;
        }
        s.sdfs.push_back(mask);
        scene::ParticleSystem p = matter(1 << 20, 0.93f, glm::vec3(4.0f));
        p.turbulence = 0.5f;
        if (arm.kind == Kind::FaceStagger) {
            p.latent.stagger = 3;
        }
        if (arm.kind == Kind::FaceHeat) {
            p.latent.heat.enabled = true;
            p.latent.heat.origin = glm::vec3(0.0f, -1.45f, 0.62f);
        }
        if (arm.kind == Kind::Tendons) {
            p.latent.tendons.curves = faceCurves;
            p.latent.tendons.speed = 3.9f;
        }
        s.particles.push_back(p);
        Stepper st(*ctx);
        gpu::FrameTimeline timeline(*ctx);
        if (!timeline.available()) {
            SKIP("no timestamp queries on this adapter");
        }
        st.particles.setTimeline(&timeline);
        const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 13.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 proj = glm::perspective(glm::radians(26.0f), 16.0f / 9.0f, 0.1f, 100.0f);
        std::vector<double> sim;
        for (int i = 0; i < 180; ++i) {
            if (arm.kind == Kind::FaceHeat) {
                // a collapse every 30 frames, so the heat pass injects as well as decays
                s.particles[0].latent.coherence = (i % 30) < 2 ? 0.2f : 0.93f;
            }
            timeline.beginFrame();
            wgpu::CommandEncoder encoder = ctx->device().CreateCommandEncoder();
            st.particles.update(encoder, s, frameAt(st.frame++), view, proj);
            timeline.resolve(encoder);
            wgpu::CommandBuffer commands = encoder.Finish();
            ctx->queue().Submit(1, &commands);
            ctx->waitForQueue();
            timeline.collect();
            s.particles[0].burst = 0.0f;
            if (i >= 90) {
                const double a = timeline.msFor("particles");
                if (a >= 0.0) sim.push_back(a);
            }
        }
        std::sort(sim.begin(), sim.end());
        CHECK(ctx->errorCount() == 0);
        WARN(arm.name << ": particle compute pass p50 " << (sim.empty() ? -1.0 : sim[sim.size() / 2]) << " ms, p90 "
                      << (sim.empty() ? -1.0 : sim[sim.size() * 9 / 10]) << " ms (" << sim.size() << " samples)");
    }
}
