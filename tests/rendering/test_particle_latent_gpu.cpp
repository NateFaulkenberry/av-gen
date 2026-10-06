// ADR-1140 (a latent SDF force on GPU particles), ADR-1141 (a render-transient particle density
// volume) and ADR-1142 (an SDF object drawing the density's iso-surface sharpened toward its own tree):
// the GPU half. Every "it works" assertion has a control that must come out the other way (ADR-182):
// coherence 1 converges onto the sphere AND coherence 0 does not; the density matches a CPU splat AND
// is non-trivial; the surface appears where matter is AND not where it is not, and not at all when
// there is no matter. The CPU references are scene/particle_latent.cpp (tests/unit/test_particle_latent.cpp).

#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/frame_timeline.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/particle_renderer.hpp"
#include "rendering/scene_renderer.hpp"
#include "rendering/sdf_renderer.hpp"
#include "scene/particle_latent.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
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

// An invisible sphere: the latent, never drawn.
scene::SdfObject maskSphere(const std::string& name, glm::vec3 position, float radius) {
    scene::SdfObject o;
    o.name = name;
    o.visible = false;
    spatial::SdfNode n;
    n.kind = spatial::SdfNodeKind::Sphere;
    n.radius = radius;
    o.tree.root = n;
    o.transform.position = position;
    o.boundsMin = glm::vec3(-2.0f);
    o.boundsMax = glm::vec3(2.0f);
    return o;
}

// 4096 still particles burst once into a box round `centre`, living for ever, under no force but the
// latent: no gravity, no drag, no turbulence. Whatever moves them is the latent.
scene::ParticleSystem matter(glm::vec3 centre, float coherence) {
    scene::ParticleSystem p;
    p.name = "matter";
    p.capacity = 4096;
    p.seed = 3;
    p.shape = scene::EmitterShape::Box;
    p.position = centre;
    p.extent = glm::vec3(1.6f);
    p.spawnRate = 0.0f;
    p.burst = 4096.0f;
    p.lifetimeMin = 1000.0f;
    p.lifetimeMax = 1000.0f;
    p.speedMin = 0.0f;
    p.speedMax = 0.0f;
    p.gravity = glm::vec3(0.0f);
    p.drag = 0.0f;
    p.turbulence = 0.0f;
    p.sizeStart = 0.0f; // nothing of the billboards reaches a frame: only the surface is tested there
    p.sizeEnd = 0.0f;
    p.colorStart = glm::vec4(0.0f);
    p.colorEnd = glm::vec4(0.0f);
    p.emissive = 0.0f;
    p.latent.sdf = "mask";
    p.latent.coherence = coherence;
    p.latent.release = 12.0f;
    return p;
}

scene::Scene latentScene(float coherence, glm::vec3 centre = glm::vec3(0.0f)) {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.environment.showSkybox = false;
    s.environment.gridIntensity = 0.0f;
    s.post.bloomEnabled = false;
    s.post.tonemap = scene::TonemapOperator::Clamp;
    s.camera.position = {0.0f, 0.0f, 8.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    s.sdfs.push_back(maskSphere("mask", centre, 1.0f));
    s.particles.push_back(matter(centre, coherence));
    return s;
}

FrameTime frameAt(std::uint64_t i) {
    FrameTime t{};
    t.renderTime = static_cast<double>(i) * kDt;
    t.deltaTime = kDt;
    t.frameIndex = i;
    return t;
}

// The particle renderer on its own, stepped frame by frame (the burst is spent on the first frame).
struct Stepper {
    gpu::Context& ctx;
    gpu::ShaderLibrary shaders;
    rendering::ParticleRenderer particles;
    std::uint64_t frame = 0;
    explicit Stepper(gpu::Context& c)
        : ctx(c), shaders(c, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)}), particles(c, shaders) {
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

double shareOnShell(const std::vector<rendering::ParticleSnapshot>& ps, glm::vec3 centre, float radius, float tol) {
    std::size_t n = 0;
    for (const auto& p : ps) {
        n += std::abs(glm::length(p.position - centre) - radius) < tol ? 1 : 0;
    }
    return ps.empty() ? 0.0 : static_cast<double>(n) / static_cast<double>(ps.size());
}

} // namespace

TEST_CASE("latent: coherence 1 pulls the matter onto the sphere, coherence 0 leaves it where it was",
          "[gpu][particles][latent]") {
    auto ctx = makeContext();
    const glm::vec3 centre(0.3f, -0.2f, 0.1f);
    scene::Scene bound = latentScene(1.0f, centre);
    Stepper a(*ctx);
    a.step(bound, 180);
    const auto onSphere = a.alive();
    REQUIRE(onSphere.size() == 4096);
    CHECK(a.particles.stats().latentSystems == 1);

    scene::Scene free = latentScene(0.0f, centre);
    Stepper b(*ctx);
    b.step(free, 180);
    const auto loose = b.alive();
    REQUIRE(loose.size() == 4096);

    const double boundShare = shareOnShell(onSphere, centre, 1.0f, 0.02f);
    const double freeShare = shareOnShell(loose, centre, 1.0f, 0.02f);
    double worst = 0.0;
    for (const auto& p : onSphere) {
        worst = std::max(worst, static_cast<double>(std::abs(glm::length(p.position - centre) - 1.0f)));
    }
    INFO("within 0.02 of the sphere: coherence 1 " << boundShare << ", coherence 0 " << freeShare << "; worst " << worst);
    CHECK(boundShare > 0.99);
    CHECK(worst < 0.05);
    CHECK(freeShare < 0.1);
    // Coherence 0 binds nothing, so nothing moved: the burst positions are still a box.
    double maxSpeed = 0.0;
    for (const auto& p : loose) {
        maxSpeed = std::max(maxSpeed, static_cast<double>(glm::length(p.velocity)));
    }
    CHECK(maxSpeed == 0.0);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("latent: one GPU step matches the CPU reference, binding and release alike", "[gpu][particles][latent]") {
    auto ctx = makeContext();
    scene::Scene s = latentScene(0.6f);
    Stepper st(*ctx);
    st.step(s, 20); // a partly bound field in motion
    const auto before = st.particles.readParticles(0);
    REQUIRE(before.has_value());

    std::vector<spatial::SdfNodeGpu> program;
    REQUIRE(spatial::packSdfTree(s.sdfs[0].tree, program) > 0);
    const auto compare = [&](float coherence, float previous, const std::vector<rendering::ParticleSnapshot>& from) {
        s.particles[0].latent.coherence = coherence;
        st.step(s, 1);
        const auto after = st.particles.readParticles(0);
        REQUIRE(after.has_value());
        scene::LatentStep step;
        step.model = s.sdfs[0].transform.matrix();
        step.inverse = glm::inverse(step.model);
        step.normal = glm::transpose(step.inverse);
        step.coherence = coherence;
        step.prevCoherence = previous;
        step.width = s.particles[0].latent.width;
        step.strength = s.particles[0].latent.strength;
        step.release = s.particles[0].latent.release;
        step.epsilon = scene::latentGradientEpsilon(s.sdfs[0].boundsMin, s.sdfs[0].boundsMax);
        step.time = static_cast<double>(st.frame - 1) * kDt;
        step.dt = static_cast<float>(kDt);
        double worst = 0.0;
        std::size_t moved = 0;
        for (std::size_t i = 0; i < from.size(); ++i) {
            if (from[i].life <= 0.0f) {
                continue;
            }
            const glm::vec3 expected = scene::latentVelocityStep(step, program, from[i].position, from[i].velocity, from[i].seed);
            const glm::vec3 got = (*after)[i].velocity;
            const double err = glm::length(got - expected) / (1.0 + glm::length(expected));
            worst = std::max(worst, err);
            moved += glm::length(expected - from[i].velocity) > 1e-3f ? 1 : 0;
            // and cs_simulate integrated exactly that velocity (no other force is on)
            const glm::vec3 p = from[i].position + got * static_cast<float>(kDt);
            CHECK(glm::length((*after)[i].position - p) < 1e-5f);
        }
        INFO("coherence " << previous << " -> " << coherence << ": worst relative velocity error " << worst << ", "
                          << moved << " particles moved by the latent");
        CHECK(worst < 1e-3);
        CHECK(moved > 500); // the comparison compared something
        return *after;
    };
    const auto held = compare(0.6f, 0.6f, *before);
    // The collapse: a drop from 0.6 to 0.2 releases the particles it unbinds with an impulse.
    const auto released = compare(0.2f, 0.6f, held);
    double fastest = 0.0;
    for (const auto& p : released) {
        fastest = std::max(fastest, static_cast<double>(glm::length(p.velocity)));
    }
    CHECK(fastest > 3.0);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("latent: deterministic across fresh renderers, and a zero-strength latent changes no byte",
          "[gpu][particles][latent]") {
    auto ctx = makeContext();
    const auto run = [&](float strength, float release, float flow, bool latent) {
        scene::Scene s = latentScene(0.7f);
        s.particles[0].latent.strength = strength;
        s.particles[0].latent.release = release;
        s.particles[0].latent.flow = flow;
        s.particles[0].turbulence = 0.6f; // something else moving, so "no change" is not "nothing moves"
        if (!latent) {
            s.particles[0].latent = scene::ParticleLatent{};
        }
        Stepper st(*ctx);
        st.step(s, 45);
        auto all = st.particles.readParticles(0);
        REQUIRE(all.has_value());
        return *all;
    };
    const auto same = [](const std::vector<rendering::ParticleSnapshot>& a, const std::vector<rendering::ParticleSnapshot>& b) {
        return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(a[0])) == 0;
    };
    const auto first = run(1.0f, 12.0f, 0.5f, true);
    const auto second = run(1.0f, 12.0f, 0.5f, true);
    CHECK(same(first, second));
    const auto none = run(1.0f, 12.0f, 0.5f, false);
    CHECK_FALSE(same(first, none)); // control: the latent did move the matter
    const auto zero = run(0.0f, 0.0f, 0.0f, true);
    CHECK(same(zero, none)); // dispatched, and changed nothing
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("density: the GPU volume of a known particle placement matches the CPU splat and blur",
          "[gpu][particles][density]") {
    auto ctx = makeContext();
    scene::Scene s = latentScene(1.0f);
    s.particles[0].density.enabled = true;
    s.particles[0].density.boundsMin = glm::vec3(-2.0f);
    s.particles[0].density.boundsMax = glm::vec3(2.0f);
    s.particles[0].density.resolution = 48;
    s.particles[0].density.weight = 1.5f;
    Stepper st(*ctx);
    st.step(s, 90);
    CHECK(st.particles.stats().densityVolumes == 1);
    const auto ps = st.alive();
    REQUIRE(ps.size() == 4096);
    auto gpuRho = st.particles.readDensity(0);
    REQUIRE(gpuRho.has_value());
    std::vector<glm::vec3> positions;
    for (const auto& p : ps) {
        positions.push_back(p.position);
    }
    std::vector<std::uint32_t> grid;
    scene::splatDensity(positions, glm::vec3(-2.0f), glm::vec3(2.0f), 48, grid);
    const std::vector<float> cpuRho = scene::resolveDensity(grid, 48, 1.5f);
    REQUIRE(gpuRho->size() == cpuRho.size());
    double worst = 0.0, total = 0.0, peak = 0.0;
    for (std::size_t i = 0; i < cpuRho.size(); ++i) {
        // rgba16float keeps 11 significant bits (2^-11 relative), and a corner weight's u32(w * 1024 + 0.5)
        // can round the other way on the GPU (Metal may fuse the multiply-add): one quantum, 1/1024 of a
        // particle, times the weight. The error is measured in those units.
        const double allowed = std::abs(cpuRho[i]) / 2048.0 + 2.0 * 1.5 / 1024.0;
        const double err = std::abs((*gpuRho)[i] - cpuRho[i]) / allowed;
        worst = std::max(worst, err);
        total += cpuRho[i];
        peak = std::max(peak, static_cast<double>(cpuRho[i]));
    }
    INFO("worst error over the allowance " << worst << ", total " << total << ", peak " << peak);
    CHECK(worst <= 1.0);
    CHECK(total > 1000.0); // non-trivial: ~4096 particles x weight 1.5, all inside the bounds
    // The volume names its bounds, and a system with no density block hands out the placeholder.
    const rendering::ParticleDensityVolume v = st.particles.densityVolume(s, "matter");
    CHECK(v.valid);
    CHECK(v.resolution == 48);
    CHECK_FALSE(st.particles.densityVolume(s, "nobody").valid);
    CHECK(ctx->errorCount() == 0);
}

namespace {

// The density-mode scene: matter bound to a sphere on the LEFT; the drawn object's own tree is a pair
// of spheres, left and right. Only the left one has matter, so only the left one may appear.
scene::Scene densityScene(float sharpness, float burst) {
    const glm::vec3 left(-1.4f, 0.0f, 0.0f);
    scene::Scene s = latentScene(1.0f, left);
    s.particles[0].extent = glm::vec3(0.9f);
    s.particles[0].burst = burst;
    s.particles[0].density.enabled = true;
    s.particles[0].density.boundsMin = glm::vec3(-3.0f, -2.0f, -2.0f);
    s.particles[0].density.boundsMax = glm::vec3(3.0f, 2.0f, 2.0f);
    s.particles[0].density.resolution = 64;
    s.particles[0].density.weight = 1.0f;
    s.sdfs[0].tree.root.radius = 0.8f;
    scene::SdfObject body;
    body.name = "body";
    spatial::SdfNode a;
    a.kind = spatial::SdfNodeKind::Sphere;
    a.radius = 0.8f;
    spatial::SdfNode ta;
    ta.kind = spatial::SdfNodeKind::Translate;
    ta.translation = left;
    ta.children.push_back(a);
    spatial::SdfNode tb = ta;
    tb.translation = -left;
    spatial::SdfNode u;
    u.kind = spatial::SdfNodeKind::Union;
    u.children = {ta, tb};
    body.tree.root = u;
    body.boundsMin = glm::vec3(-3.0f, -2.0f, -2.0f);
    body.boundsMax = glm::vec3(3.0f, 2.0f, 2.0f);
    body.material.baseColor = glm::vec3(0.9f, 0.6f, 0.2f);
    body.material.unlit = true;
    body.maxSteps = 512;
    body.stepScale = 0.7f;
    body.density.particles = "matter";
    body.density.iso = 0.6f;
    body.density.sharpness = sharpness;
    s.sdfs.push_back(body);
    return s;
}

struct Coverage {
    long left = 0;
    long right = 0;
};

Coverage coverage(const gpu::Image8& img) {
    Coverage c;
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            const auto* px = img.pixel(x, y);
            if (std::max({px[0], px[1], px[2]}) > 30) {
                (x < img.width / 2 ? c.left : c.right) += 1;
            }
        }
    }
    return c;
}

gpu::Image8 settle(rendering::SceneRenderer& renderer, scene::Scene& s, int frames, std::uint32_t size = 160) {
    gpu::Image8 last;
    for (int i = 0; i < frames; ++i) {
        auto img = renderer.renderToImage(s, frameAt(static_cast<std::uint64_t>(i)), size, size);
        REQUIRE(img.has_value());
        last = std::move(*img);
        s.particles[0].burst = 0.0f;
    }
    return last;
}

} // namespace

TEST_CASE("density mode: a surface where the matter is, none where it is not, none without matter",
          "[gpu][sdf][density]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const auto render = [&](float sharpness, float burst) {
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        scene::Scene s = densityScene(sharpness, burst);
        auto img = settle(renderer, s, 90);
        CHECK(renderer.sdfs().stats().densityObjects == 1);
        return img;
    };
    const Coverage blob = coverage(render(0.0f, 4096.0f));
    const Coverage sharp = coverage(render(1.0f, 4096.0f));
    const Coverage empty = coverage(render(1.0f, 0.0f));
    INFO("lit pixels left/right: sharpness 0 " << blob.left << "/" << blob.right << ", sharpness 1 " << sharp.left << "/"
                                               << sharp.right << ", no matter " << empty.left << "/" << empty.right);
    CHECK(blob.left > 800);
    CHECK(blob.right == 0);
    CHECK(sharp.left > 800);
    CHECK(sharp.right == 0); // the tree's right sphere has no matter, so it is not drawn
    CHECK(empty.left == 0);
    CHECK(empty.right == 0);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("density mode: sharpening pulls the surface onto the tree's zero set", "[gpu][sdf][density]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    // Projected radius of the surface on the left: at sharpness 1 it is the tree's sphere (0.8 at a
    // distance of 8, about 0.1 of the vertical field), whatever the iso level of the matter is.
    const auto widthAt = [&](float sharpness, float iso) {
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        scene::Scene s = densityScene(sharpness, 4096.0f);
        s.sdfs[1].density.iso = iso;
        const gpu::Image8 img = settle(renderer, s, 90);
        const std::uint32_t y = img.height / 2;
        long n = 0;
        for (std::uint32_t x = 0; x < img.width / 2; ++x) {
            const auto* px = img.pixel(x, y);
            n += std::max({px[0], px[1], px[2]}) > 30 ? 1 : 0;
        }
        return n;
    };
    const long blobLow = widthAt(0.0f, 0.3f), blobHigh = widthAt(0.0f, 1.2f);
    const long sharpLow = widthAt(1.0f, 0.3f), sharpHigh = widthAt(1.0f, 1.2f);
    INFO("row width: sharpness 0 at iso 0.3 / 1.2 = " << blobLow << " / " << blobHigh << "; sharpness 1 = " << sharpLow
                                                      << " / " << sharpHigh);
    CHECK(blobLow != blobHigh);                 // the iso level moves a pure density surface
    CHECK(std::abs(sharpLow - sharpHigh) <= 2); // and does not move a fully sharpened one
    CHECK(sharpLow > 10);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("density: a volume with no consumer changes no pixel of the frame", "[gpu][particles][density]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const auto render = [&](bool withVolume) {
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        scene::Scene s = densityScene(0.5f, 4096.0f);
        s.sdfs[1].density = scene::SdfDensitySource{}; // the body draws its own tree, as any object does
        s.particles[0].sizeStart = 0.05f;              // and the matter draws itself as billboards
        s.particles[0].sizeEnd = 0.05f;
        s.particles[0].colorStart = glm::vec4(1.0f);
        s.particles[0].colorEnd = glm::vec4(1.0f);
        s.particles[0].emissive = 1.0f;
        s.particles[0].density.enabled = withVolume;
        return settle(renderer, s, 30);
    };
    const gpu::Image8 with = render(true);
    const gpu::Image8 without = render(false);
    const Coverage c = coverage(with);
    INFO("lit left/right " << c.left << "/" << c.right);
    CHECK(c.left > 100); // the frame has something in it
    CHECK(c.right > 100);
    const bool identical = with.rgba == without.rgba;
    CHECK(identical);
    CHECK(ctx->errorCount() == 0);
}

namespace {

// The example's crude mask (examples/astral-forge/latent-entity.scene.json): a rounded plate, two
// eyes and a mouth, smooth-unioned.
spatial::SdfTree crudeMask() {
    const auto prim = [](spatial::SdfNodeKind kind) {
        spatial::SdfNode n;
        n.kind = kind;
        return n;
    };
    const auto moved = [](glm::vec3 t, spatial::SdfNode child) {
        spatial::SdfNode n;
        n.kind = spatial::SdfNodeKind::Translate;
        n.translation = t;
        n.children.push_back(std::move(child));
        return n;
    };
    spatial::SdfNode plate = prim(spatial::SdfNodeKind::RoundedBox);
    plate.size = glm::vec3(1.05f, 1.45f, 0.22f);
    plate.rounding = 0.2f;
    spatial::SdfNode eye = prim(spatial::SdfNodeKind::Sphere);
    eye.radius = 0.36f;
    spatial::SdfNode mouth = prim(spatial::SdfNodeKind::Capsule);
    mouth.radius = 0.12f;
    mouth.height = 0.38f;
    spatial::SdfNode turned = prim(spatial::SdfNodeKind::Rotate);
    turned.rotationDegrees = glm::vec3(0.0f, 0.0f, 90.0f);
    turned.children.push_back(mouth);
    spatial::SdfNode root = prim(spatial::SdfNodeKind::SmoothUnion);
    root.smooth = 0.18f;
    root.children = {plate, moved({-0.5f, 0.38f, 0.2f}, eye), moved({0.5f, 0.38f, 0.2f}, eye),
                     moved({0.0f, -0.62f, 0.18f}, turned)};
    spatial::SdfTree tree;
    tree.root = root;
    return tree;
}

} // namespace

// Measured cost of the three features. Not an assertion: run it by name
// (`avgen_render_tests "[.perf][latent]"`) under the GPU lock and read the WARN lines.
//
// Part 1 times the particle renderer ALONE, with its own timeline: the only marked passes are the
// particle compute pass and the density pass, so neither interval can absorb another pass's work.
// Part 2 compares a density-mode object with the same tree raymarched directly, whole frame, 1080p.
TEST_CASE("latent, density and density mode: GPU cost", "[.perf][latent]") {
    auto ctx = makeContext();
    struct Arm {
        const char* name;
        bool latent;
        int resolution; // 0 = no density volume
    };
    for (const Arm arm : {Arm{"1M particles, no latent", false, 0}, Arm{"1M particles + latent (crude mask)", true, 0},
                          Arm{"+ density 128^3", true, 128}, Arm{"+ density 192^3", true, 192},
                          Arm{"+ density 256^3", true, 256}}) {
        scene::Scene s = latentScene(0.93f);
        s.sdfs[0].tree = crudeMask();
        s.sdfs[0].boundsMin = glm::vec3(-2.4f);
        s.sdfs[0].boundsMax = glm::vec3(2.4f);
        scene::ParticleSystem& p = s.particles[0];
        p.capacity = 1u << 20;
        p.burst = static_cast<float>(p.capacity);
        p.extent = glm::vec3(2.6f);
        p.turbulence = 0.5f;
        if (!arm.latent) {
            p.latent = scene::ParticleLatent{};
        }
        p.density.enabled = arm.resolution > 0;
        p.density.boundsMin = glm::vec3(-2.4f);
        p.density.boundsMax = glm::vec3(2.4f);
        p.density.resolution = std::max(arm.resolution, 4);
        Stepper st(*ctx);
        gpu::FrameTimeline timeline(*ctx);
        if (!timeline.available()) {
            SKIP("no timestamp queries on this adapter");
        }
        st.particles.setTimeline(&timeline);
        const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 8.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 proj = glm::perspective(glm::radians(45.0f), 1.0f, 0.1f, 100.0f);
        std::vector<double> sim, dens;
        for (int i = 0; i < 180; ++i) {
            timeline.beginFrame();
            wgpu::CommandEncoder encoder = ctx->device().CreateCommandEncoder();
            st.particles.update(encoder, s, frameAt(st.frame++), view, proj);
            timeline.resolve(encoder);
            wgpu::CommandBuffer commands = encoder.Finish();
            ctx->queue().Submit(1, &commands);
            ctx->waitForQueue();
            timeline.collect();
            p.burst = 0.0f;
            if (i >= 90) {
                const double a = timeline.msFor("particles");
                const double b = timeline.msFor("particle-density");
                if (a >= 0.0) sim.push_back(a);
                if (b >= 0.0) dens.push_back(b);
            }
        }
        const auto median = [](std::vector<double> v) {
            if (v.empty()) return -1.0;
            std::sort(v.begin(), v.end());
            return v[v.size() / 2];
        };
        CHECK(ctx->errorCount() == 0);
        WARN(arm.name << ": particle compute pass p50 " << median(sim) << " ms (" << sim.size()
                      << " samples), density pass p50 " << median(dens) << " ms (" << dens.size() << " samples)");
    }

    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    for (const int mode : {1, 2}) {
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        scene::Scene s = latentScene(0.93f);
        s.camera.position = {0.0f, 0.25f, 7.2f};
        s.addLight(scene::PunctualLight{});
        s.sdfs[0].tree = crudeMask();
        s.sdfs[0].boundsMin = glm::vec3(-2.4f);
        s.sdfs[0].boundsMax = glm::vec3(2.4f);
        scene::ParticleSystem& p = s.particles[0];
        p.capacity = 1u << 20;
        p.burst = static_cast<float>(p.capacity);
        p.extent = glm::vec3(2.6f);
        p.density.enabled = true;
        p.density.boundsMin = glm::vec3(-2.4f);
        p.density.boundsMax = glm::vec3(2.4f);
        p.density.resolution = 192;
        scene::SdfObject body = s.sdfs[0];
        body.name = "body";
        body.visible = true;
        body.material.baseColor = glm::vec3(0.56f);
        body.material.metallic = 1.0f;
        body.material.roughness = 0.24f;
        body.maxSteps = 448;
        body.stepScale = mode == 2 ? 0.6f : 0.9f;
        if (mode == 2) {
            body.density.particles = "matter";
            body.density.iso = 1.5f;
            body.density.sharpness = 0.85f;
        }
        s.sdfs.push_back(body);
        std::vector<double> march, frame;
        long lit = 0;
        for (int i = 0; i < 150; ++i) {
            auto img = renderer.renderToImage(s, frameAt(static_cast<std::uint64_t>(i)), 1920, 1080);
            REQUIRE(img.has_value());
            p.burst = 0.0f;
            const auto& stats = renderer.stats();
            if (i >= 90) {
                if (stats.sdf.raymarchMs >= 0.0) march.push_back(stats.sdf.raymarchMs);
                frame.push_back(stats.gpuFrameMs);
            }
            if (i == 149) {
                lit = coverage(*img).left + coverage(*img).right;
            }
        }
        const auto median = [](std::vector<double> v) {
            if (v.empty()) return -1.0;
            std::sort(v.begin(), v.end());
            return v[v.size() / 2];
        };
        CHECK(ctx->errorCount() == 0);
        WARN((mode == 1 ? "the mask tree raymarched directly" : "density mode, sharpness 0.85, step scale 0.6")
             << ": sdf raymarch pass p50 " << median(march) << " ms (" << march.size() << " samples), GPU frame p50 "
             << median(frame) << " ms, lit pixels " << lit);
    }
}
