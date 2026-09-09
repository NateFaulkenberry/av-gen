// GPU point processing (ADR-025/029): the effector pass against spatial::applyEffectorsToRecords,
// Point billboards, the Field deformer and emissive field paths, particle field forces, and
// the [.perf] probes behind docs/performance/procedural-geometry.md ("Fields and effectors").
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"
#include "spatial/effector.hpp"
#include "spatial/field.hpp"

#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
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

gpu::ShaderLibrary makeShaders(gpu::Context& ctx) {
    return gpu::ShaderLibrary(ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
}

float testRandom(std::uint32_t index, std::uint32_t channel) {
    std::uint32_t h = index * 0x9E3779B1u ^ (channel + 1u) * 0x85EBCA77u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return static_cast<float>(h >> 8) / 16777216.0f;
}

// `count` records scattered deterministically in a box of half extent `extent` (unit scale,
// identity rotation, white multipliers, density 1).
std::vector<scene::InstanceRecord> scatteredInstances(std::uint32_t count, float extent) {
    std::vector<scene::InstanceRecord> out;
    out.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        scene::InstanceRecord r{};
        r.position = {(testRandom(i, 10) * 2.0f - 1.0f) * extent, (testRandom(i, 11) * 2.0f - 1.0f) * extent,
                      (testRandom(i, 12) * 2.0f - 1.0f) * extent, 1.0f};
        r.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
        r.scale = {1.0f, 1.0f, 1.0f, count > 1 ? static_cast<float>(i) / static_cast<float>(count - 1) : 0.0f};
        r.random = {testRandom(i, 0), testRandom(i, 1), testRandom(i, 2), testRandom(i, 3)};
        r.color = {1.0f, 1.0f, 1.0f, static_cast<float>(i)};
        r.emissive = {1.0f, 1.0f, 1.0f, 0.0f};
        out.push_back(r);
    }
    return out;
}

std::uint64_t specHash(const scene::SourceSpec& s) {
    std::uint64_t h = 0xC0FFEE5678ull;
    auto mix = [&](std::uint64_t v) { h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2); };
    mix(static_cast<std::uint64_t>(s.kind));
    mix(static_cast<std::uint64_t>(s.pointSize * 100000.0f));
    mix(static_cast<std::uint64_t>(s.size.x * 1000.0f));
    mix(static_cast<std::uint64_t>(s.subdivisions));
    return h == 0 ? 1 : h;
}

scene::ProceduralGeometry pointObject(const std::string& name, std::uint32_t count, float extent, float pointSize) {
    scene::ProceduralGeometry g;
    g.name = name;
    g.source.kind = scene::PrimitiveKind::Point;
    g.source.pointSize = pointSize;
    g.instances = scatteredInstances(count, extent);
    g.structureVersion = 1;
    g.meshHash = specHash(g.source);
    g.material.baseColor = {0.9f, 0.8f, 0.6f};
    g.material.emissiveColor = {1.0f, 0.7f, 0.3f};
    g.material.emissiveIntensity = 2.0f;
    g.material.unlit = true;
    return g;
}

scene::ProceduralGeometry boxObject(const std::string& name, std::uint32_t count, float extent) {
    scene::ProceduralGeometry g;
    g.name = name;
    g.source.kind = scene::PrimitiveKind::Box;
    g.source.size = {0.3f, 0.3f, 0.3f};
    g.source.subdivisions = 1;
    g.instances = scatteredInstances(count, extent);
    g.structureVersion = 1;
    g.meshHash = specHash(g.source);
    g.material.baseColor = {0.8f, 0.7f, 0.6f};
    g.material.roughness = 0.6f;
    return g;
}

spatial::FieldSpec vortexField() {
    spatial::FieldSpec f;
    f.name = "swirl";
    f.kind = spatial::FieldKind::Vortex;
    f.axis = {0.2f, 1.0f, 0.1f};
    f.point = {0.5f, 0.0f, -0.3f};
    f.strength = 1.2f;
    f.falloff.kind = spatial::FalloffKind::Smooth;
    f.falloff.inner = 1.0f;
    f.falloff.outer = 12.0f;
    f.rotationDegrees = {10.0f, 30.0f, -5.0f};
    return f;
}

spatial::FieldSpec radialField() {
    spatial::FieldSpec f;
    f.name = "bulge";
    f.kind = spatial::FieldKind::Radial;
    f.radius = 6.0f;
    f.strength = 0.8f;
    f.position = {1.0f, 0.5f, 0.0f};
    f.falloff.kind = spatial::FalloffKind::EaseInOut;
    f.falloff.inner = 0.0f;
    f.falloff.outer = 8.0f;
    return f;
}

spatial::FieldSpec curlField() {
    spatial::FieldSpec f;
    f.name = "curl";
    f.kind = spatial::FieldKind::CurlNoise;
    f.frequency = 0.3f;
    f.strength = 1.0f;
    f.speed = 0.5f;
    f.seed = 5;
    f.falloff.kind = spatial::FalloffKind::None;
    return f;
}

int brightness(const gpu::Image8& img) {
    long sum = 0;
    for (std::size_t i = 0; i < img.rgba.size(); i += 4) {
        sum += img.rgba[i] + img.rgba[i + 1] + img.rgba[i + 2];
    }
    return static_cast<int>(sum / static_cast<long>(img.width * img.height));
}

int litPixels(const gpu::Image8& img) {
    int n = 0;
    for (std::size_t i = 0; i < img.rgba.size(); i += 4) {
        if (img.rgba[i] + img.rgba[i + 1] + img.rgba[i + 2] > 15) {
            ++n;
        }
    }
    return n;
}

scene::Scene darkScene() {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.environment.showSkybox = false;
    s.camera.position = {0.0f, 4.0f, 14.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    scene::PunctualLight key;
    key.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.6f));
    key.intensity = 3.0f;
    s.addLight(key);
    return s;
}

float maxAbs(const glm::vec4& v) {
    return std::max(std::max(std::abs(v.x), std::abs(v.y)), std::max(std::abs(v.z), std::abs(v.w)));
}

} // namespace

TEST_CASE("Effector pass matches applyEffectorsToRecords (vortex offset + radial scale)", "[gpu][points][fields]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    scene::Scene s = darkScene();
    s.fields.fields.push_back(vortexField());
    s.fields.fields.push_back(radialField());
    auto object = boxObject("swirled", 3000, 5.0f);
    spatial::Effector offset;
    offset.field = "swirl";
    offset.op = spatial::EffectorOp::PositionOffset;
    offset.strength = 0.75f;
    spatial::Effector scale;
    scale.field = "bulge";
    scale.op = spatial::EffectorOp::Scale;
    scale.blend = spatial::EffectorBlend::Replace;
    scale.strength = 1.5f;
    scale.scaleAxis = {1.0f, 0.5f, 1.0f};
    spatial::Effector disabled;
    disabled.field = "swirl";
    disabled.op = spatial::EffectorOp::Emission;
    disabled.enabled = false;
    spatial::Effector unknown;
    unknown.field = "nope";
    object.effectors = {offset, scale, disabled, unknown};
    s.procedurals.push_back(object);

    const double time = 0.8;
    FrameTime t{};
    t.renderTime = time;
    auto img = renderer.renderToImage(s, t, 128, 128);
    REQUIRE(img.has_value());
    CHECK(ctx->errorCount() == 0);
    CHECK(renderer.stats().procedural.effectorObjects == 1);
    CHECK(renderer.stats().procedural.effectors == 2);
    CHECK(renderer.stats().procedural.effectorInstances == 3000);

    auto live = renderer.procedurals().readInstanceRecords("swirled");
    REQUIRE(live.has_value());
    REQUIRE(live->size() == object.instances.size());
    std::vector<scene::InstanceRecord> reference = object.instances;
    const int applied = spatial::applyEffectorsToRecords(reference, object.effectors, s.fields, time, glm::mat4(1.0f));
    CHECK(applied == 2);
    float worst = 0.0f;
    bool moved = false;
    for (std::size_t i = 0; i < reference.size(); ++i) {
        const auto& a = reference[i];
        const auto& b = (*live)[i];
        const float d = std::max({maxAbs(a.position - b.position), maxAbs(a.rotation - b.rotation),
                                  maxAbs(a.scale - b.scale), maxAbs(a.color - b.color), maxAbs(a.emissive - b.emissive)});
        worst = std::max(worst, d);
        INFO("record " << i << " cpu position (" << a.position.x << "," << a.position.y << "," << a.position.z
                       << ") gpu (" << b.position.x << "," << b.position.y << "," << b.position.z << ") cpu scale ("
                       << a.scale.x << "," << a.scale.y << "," << a.scale.z << ") gpu (" << b.scale.x << "," << b.scale.y
                       << "," << b.scale.z << ")");
        REQUIRE(d <= 1e-4f);
        if (maxAbs(a.position - object.instances[i].position) > 1e-3f) {
            moved = true;
        }
    }
    CHECK(moved); // the effectors actually did something
    INFO("worst deviation " << worst);
    CHECK(worst <= 1e-4f);

    // A rotated, translated object matrix: sampling happens in world space and the offset is
    // rotated back into object space. The scene renderer uses identity matrices, so this goes
    // through the CPU reference only for the record maths while the GPU pass is exercised
    // above; the two agree on the identity case, and the rotation path is unit-checked here.
    const glm::mat4 world = glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, -1.0f)), 0.7f,
                                        glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f)));
    std::vector<scene::InstanceRecord> rotated = object.instances;
    spatial::applyEffectorsToRecords(rotated, object.effectors, s.fields, time, world);
    bool differs = false;
    for (std::size_t i = 0; i < rotated.size() && !differs; ++i) {
        differs = maxAbs(rotated[i].position - reference[i].position) > 1e-4f;
    }
    CHECK(differs);

    // Without effectors the draw reads the base buffer unchanged.
    s.procedurals[0].effectors.clear();
    (void)renderer.renderToImage(s, t, 64, 64);
    CHECK(renderer.stats().procedural.effectorObjects == 0);
    auto base = renderer.procedurals().readInstanceRecords("swirled");
    REQUIRE(base.has_value());
    for (std::size_t i = 0; i < base->size(); ++i) {
        REQUIRE(maxAbs((*base)[i].position - object.instances[i].position) == 0.0f);
    }
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Every effector op runs on the GPU with the CPU reference semantics", "[gpu][points][fields]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    scene::Scene s = darkScene();
    s.fields.fields.push_back(vortexField());
    s.fields.fields.push_back(radialField());
    spatial::FieldSpec colour;
    colour.name = "tint";
    colour.kind = spatial::FieldKind::RadialGradient;
    colour.radius = 5.0f;
    colour.colorA = {1.0f, 0.2f, 0.1f, 1.0f};
    colour.colorB = {0.1f, 0.3f, 1.0f, 1.0f};
    colour.falloff.kind = spatial::FalloffKind::Linear;
    colour.falloff.outer = 9.0f;
    s.fields.fields.push_back(colour);
    auto object = boxObject("ops", 512, 4.0f);
    for (const auto op : {spatial::EffectorOp::PositionOffset, spatial::EffectorOp::Scale, spatial::EffectorOp::Rotation,
                          spatial::EffectorOp::Color, spatial::EffectorOp::Emission, spatial::EffectorOp::Density}) {
        spatial::Effector e;
        e.op = op;
        e.field = op == spatial::EffectorOp::Color ? "tint" : op == spatial::EffectorOp::Rotation ? "swirl" : "bulge";
        e.strength = 0.6f;
        e.blend = op == spatial::EffectorOp::Scale ? spatial::EffectorBlend::Mix : spatial::EffectorBlend::Add;
        e.weight = 0.4f;
        e.axis = {0.0f, 0.0f, 1.0f};
        object.effectors.push_back(e);
    }
    // A scalar-field position offset along the effector axis, and a rotation about the axis.
    spatial::Effector scalarOffset;
    scalarOffset.op = spatial::EffectorOp::PositionOffset;
    scalarOffset.field = "bulge";
    scalarOffset.axis = {1.0f, 0.0f, 0.0f};
    scalarOffset.strength = 0.3f;
    object.effectors.push_back(scalarOffset);
    spatial::Effector scalarRotation;
    scalarRotation.op = spatial::EffectorOp::Rotation;
    scalarRotation.field = "bulge";
    scalarRotation.axis = {0.0f, 1.0f, 0.0f};
    scalarRotation.strength = 1.1f;
    object.effectors.push_back(scalarRotation);
    REQUIRE(object.effectors.size() <= static_cast<std::size_t>(spatial::kMaxEffectors));
    s.procedurals.push_back(object);
    const double time = 1.3;
    FrameTime t{};
    t.renderTime = time;
    auto img = renderer.renderToImage(s, t, 96, 96);
    REQUIRE(img.has_value());
    CHECK(ctx->errorCount() == 0);
    CHECK(renderer.stats().procedural.effectors == 8);
    auto live = renderer.procedurals().readInstanceRecords("ops");
    REQUIRE(live.has_value());
    std::vector<scene::InstanceRecord> reference = object.instances;
    CHECK(spatial::applyEffectorsToRecords(reference, object.effectors, s.fields, time) == 8);
    for (std::size_t i = 0; i < reference.size(); ++i) {
        const auto& a = reference[i];
        const auto& b = (*live)[i];
        INFO("record " << i);
        REQUIRE(maxAbs(a.position - b.position) <= 1e-4f);
        REQUIRE(maxAbs(a.rotation - b.rotation) <= 1e-4f);
        REQUIRE(maxAbs(a.scale - b.scale) <= 1e-4f);
        REQUIRE(maxAbs(a.color - b.color) <= 1e-4f);
        REQUIRE(maxAbs(a.emissive - b.emissive) <= 1e-4f);
    }
}

TEST_CASE("200k Point instances render non-black and deterministically across renderers", "[gpu][points]") {
    auto ctx = makeContext();
    scene::Scene s = darkScene();
    s.fields.fields.push_back(curlField());
    s.fields.fields.push_back(radialField());
    auto points = pointObject("dust", 200000, 6.0f, 0.03f);
    spatial::Effector drift;
    drift.field = "curl";
    drift.op = spatial::EffectorOp::PositionOffset;
    drift.strength = 0.5f;
    points.effectors.push_back(drift);
    points.emissiveField = "bulge";
    points.emissiveFieldAmount = 2.0f;
    s.procedurals.push_back(points);
    FrameTime t{};
    t.renderTime = 0.5;

    auto render = [&](gpu::Context& c) {
        auto shaders = makeShaders(c);
        rendering::SceneRenderer renderer(c, shaders);
        REQUIRE(renderer.init().has_value());
        auto img = renderer.renderToImage(s, t, 256, 256);
        REQUIRE(img.has_value());
        CHECK(renderer.stats().procedural.pointObjects == 1);
        CHECK(renderer.stats().procedural.instances == 200000);
        CHECK(renderer.stats().procedural.effectorObjects == 1);
        return *img;
    };
    const auto a = render(*ctx);
    CHECK(litPixels(a) > 2000);
    const auto b = render(*ctx);
    CHECK(gpu::hashImage(a) == gpu::hashImage(b));
    auto ctx2 = makeContext();
    const auto c = render(*ctx2);
    CHECK(gpu::hashImage(a) == gpu::hashImage(c));
    CHECK(ctx->errorCount() == 0);
    CHECK(ctx2->errorCount() == 0);
    // Later time: the curl field animates, the emissive field does not; the frame must change.
    t.renderTime = 2.5;
    const auto later = render(*ctx);
    CHECK(gpu::hashImage(later) != gpu::hashImage(a));
    if (const char* dumpDir = std::getenv("AVGEN_DUMP_DIR")) {
        REQUIRE(gpu::writePpm(a, std::filesystem::path(dumpDir) / "points_200k.ppm").has_value());
    }
}

TEST_CASE("Field deformer and emissive field change the image and keep determinism", "[gpu][points][fields]") {
    auto ctx = makeContext();
    scene::Scene s = darkScene();
    s.fields.fields.push_back(curlField());
    s.fields.fields.push_back(radialField());
    s.procedurals.push_back(boxObject("boxes", 2000, 5.0f));
    FrameTime t{};
    t.renderTime = 1.0;
    auto render = [&](const scene::Scene& sc) {
        auto shaders = makeShaders(*ctx);
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        auto img = renderer.renderToImage(sc, t, 160, 160);
        REQUIRE(img.has_value());
        return std::make_pair(*img, renderer.stats().procedural);
    };
    const auto [base, baseStats] = render(s);
    CHECK(litPixels(base) > 500);
    CHECK(baseStats.fieldDeformers == 0);

    scene::Scene deformed = s;
    scene::Deformer field;
    field.kind = scene::DeformerKind::Field;
    field.space = scene::DeformSpace::World;
    field.field = "curl";
    field.amount = 0.8f;
    deformed.procedurals[0].deformers.push_back(field);
    scene::Deformer localScalar;
    localScalar.kind = scene::DeformerKind::Field;
    localScalar.space = scene::DeformSpace::Local;
    localScalar.field = "bulge";
    localScalar.amount = 0.2f;
    localScalar.alongNormal = true;
    deformed.procedurals[0].deformers.push_back(localScalar);
    const auto [img, stats] = render(deformed);
    CHECK(stats.fieldDeformers == 2);
    CHECK(gpu::hashImage(img) != gpu::hashImage(base));
    CHECK(litPixels(img) > 500);
    const auto [again, againStats] = render(deformed);
    CHECK(gpu::hashImage(again) == gpu::hashImage(img));
    // An unbound field name is inert.
    scene::Scene unbound = s;
    scene::Deformer missing = field;
    missing.field = "missing";
    unbound.procedurals[0].deformers.push_back(missing);
    const auto [same, sameStats] = render(unbound);
    CHECK(sameStats.fieldDeformers == 0);
    CHECK(gpu::hashImage(same) == gpu::hashImage(base));

    scene::Scene glowing = s;
    glowing.procedurals[0].material.emissiveColor = {1.0f, 0.5f, 0.2f};
    glowing.procedurals[0].material.emissiveIntensity = 1.0f;
    const auto [glow0, glow0Stats] = render(glowing);
    glowing.procedurals[0].emissiveField = "bulge";
    glowing.procedurals[0].emissiveFieldAmount = 3.0f;
    const auto [glow1, glow1Stats] = render(glowing);
    CHECK(brightness(glow1) > brightness(glow0));
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Particle field forces are deterministic and change the simulation", "[gpu][particles][fields]") {
    auto ctx = makeContext();
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.camera.position = {0.0f, 0.0f, 8.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    scene::ParticleSystem sys;
    sys.name = "wind";
    sys.capacity = 4096;
    sys.seed = 3;
    sys.shape = scene::EmitterShape::Sphere;
    sys.position = {0.0f, 0.0f, 0.0f};
    sys.extent = {1.0f, 1.0f, 1.0f};
    sys.spawnRate = 3000.0f;
    sys.lifetimeMin = sys.lifetimeMax = 1.5f;
    sys.speedMin = 0.2f;
    sys.speedMax = 0.5f;
    sys.spread = 1.0f;
    sys.gravity = {0.0f, 0.0f, 0.0f};
    sys.drag = 0.0f;
    sys.turbulence = 0.0f;
    sys.sizeStart = sys.sizeEnd = 0.08f;
    sys.emissive = 2.0f;
    s.particles.push_back(sys);
    s.fields.fields.push_back(vortexField());
    s.fields.fields.push_back(radialField());
    spatial::FieldSpec killer;
    killer.name = "wall";
    killer.kind = spatial::FieldKind::Plane;
    killer.axis = {0.0f, 1.0f, 0.0f};
    killer.position = {0.0f, 2.0f, 0.0f};
    killer.softness = 0.01f;
    s.fields.fields.push_back(killer);

    auto run = [&](const scene::Scene& sc, gpu::Context& c, int frames) {
        auto shaders = makeShaders(c);
        rendering::SceneRenderer renderer(c, shaders);
        REQUIRE(renderer.init().has_value());
        FixedStepClock clock(60.0);
        std::vector<std::uint64_t> hashes;
        for (int i = 0; i < frames; ++i) {
            auto img = renderer.renderToImage(sc, clock.tick(), 96, 96);
            REQUIRE(img.has_value());
            hashes.push_back(gpu::hashImage(*img));
        }
        return hashes;
    };
    constexpr int kFrames = 60;
    const auto plain = run(s, *ctx, kFrames);

    scene::Scene forced = s;
    scene::FieldForce force;
    force.field = "swirl";
    force.mode = scene::FieldForceMode::Force;
    force.strength = 4.0f;
    scene::FieldForce velocity;
    velocity.field = "bulge";
    velocity.mode = scene::FieldForceMode::Velocity;
    velocity.strength = 0.5f;
    velocity.mix = 0.2f;
    velocity.axis = {1.0f, 0.0f, 0.0f};
    forced.particles[0].fieldForces = {force, velocity};
    const auto a = run(forced, *ctx, kFrames);
    const auto b = run(forced, *ctx, kFrames);
    auto ctx2 = makeContext();
    const auto c = run(forced, *ctx2, kFrames);
    REQUIRE(a.size() == b.size());
    REQUIRE(a.size() == c.size());
    int differentFromPlain = 0;
    for (int i = 0; i < kFrames; ++i) {
        INFO("frame " << i);
        REQUIRE(a[static_cast<std::size_t>(i)] == b[static_cast<std::size_t>(i)]);
        REQUIRE(a[static_cast<std::size_t>(i)] == c[static_cast<std::size_t>(i)]);
        if (a[static_cast<std::size_t>(i)] != plain[static_cast<std::size_t>(i)]) {
            ++differentFromPlain;
        }
    }
    CHECK(differentFromPlain > kFrames / 2);

    // Kill mode: a plane field above the emitter removes particles that cross it, so fewer stay alive.
    auto countAlive = [&](const scene::Scene& sc) {
        auto shaders = makeShaders(*ctx);
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        FixedStepClock clock(60.0);
        for (int i = 0; i < 70; ++i) {
            auto img = renderer.renderToImage(sc, clock.tick(), 32, 32);
            REQUIRE(img.has_value());
        }
        auto counts = renderer.particles().readCounts(0);
        REQUIRE(counts.has_value());
        return counts->alive;
    };
    scene::Scene killing = s;
    killing.particles[0].direction = {0.0f, 1.0f, 0.0f};
    killing.particles[0].spread = 0.0f;
    killing.particles[0].speedMin = killing.particles[0].speedMax = 3.0f;
    const std::uint32_t aliveWithout = countAlive(killing);
    scene::FieldForce kill;
    kill.field = "wall";
    kill.mode = scene::FieldForceMode::Kill;
    killing.particles[0].fieldForces = {kill};
    const std::uint32_t aliveWith = countAlive(killing);
    INFO("alive without kill " << aliveWithout << " with " << aliveWith);
    CHECK(aliveWithout > 0);
    CHECK(aliveWith < aliveWithout);
    CHECK(ctx->errorCount() == 0);
    CHECK(ctx2->errorCount() == 0);
}

// Hidden performance probes: `avgen_render_tests "[.perf][fields]"` (Release). Reports the
// effector pass and frame GPU times at 1920x1080 for 1M Point instances with a CurlNoise
// PositionOffset + Radial Scale effector, 100k boxes with a Field deformer, and 256k particles
// with two field forces. See docs/performance/procedural-geometry.md.
TEST_CASE("Fields and effectors throughput", "[.perf][fields]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    auto measure = [&](const scene::Scene& s, const char* label) {
        FixedStepClock clock(60.0);
        double gpuSum = 0.0, effSum = 0.0, simSum = 0.0, cpuSum = 0.0;
        int counted = 0, effCounted = 0, simCounted = 0;
        for (int i = 0; i < 90; ++i) {
            auto img = renderer.renderToImage(s, clock.tick(), 1920, 1080);
            REQUIRE(img.has_value());
            if (i >= 30 && renderer.stats().gpuFrameMs >= 0.0) {
                gpuSum += renderer.stats().gpuFrameMs;
                cpuSum += renderer.stats().procedural.cpuUpdateMs;
                ++counted;
                if (renderer.stats().procedural.effectorPassMs >= 0.0) {
                    effSum += renderer.stats().procedural.effectorPassMs;
                    ++effCounted;
                }
                if (renderer.stats().particles.simulateMs >= 0.0) {
                    simSum += renderer.stats().particles.simulateMs;
                    ++simCounted;
                }
            }
        }
        CHECK(ctx->errorCount() == 0);
        const double gpu = counted ? gpuSum / counted : -1.0;
        const double eff = effCounted ? effSum / effCounted : -1.0;
        const double sim = simCounted ? simSum / simCounted : -1.0;
        WARN(label << ": scene+post GPU " << gpu << " ms, effector pass " << eff << " ms, particle sim " << sim
                   << " ms, total GPU " << (gpu + std::max(eff, 0.0) + std::max(sim, 0.0)) << " ms, CPU update "
                   << (counted ? cpuSum / counted : -1.0) << " ms, instances " << renderer.stats().procedural.instances
                   << ", particles capacity " << renderer.stats().particles.capacity);
    };
    spatial::Effector drift;
    drift.field = "curl";
    drift.op = spatial::EffectorOp::PositionOffset;
    drift.strength = 0.6f;
    spatial::Effector swirlOffset;
    swirlOffset.field = "swirl";
    swirlOffset.op = spatial::EffectorOp::PositionOffset;
    swirlOffset.strength = 0.6f;
    spatial::Effector bulge;
    bulge.field = "bulge";
    bulge.op = spatial::EffectorOp::Scale;
    bulge.strength = 1.0f;
    struct PointConfig {
        const char* name;
        std::vector<spatial::Effector> effectors;
    };
    for (const PointConfig& cfg : {PointConfig{"1M points, no effectors", {}},
                                   PointConfig{"1M points, radial scale effector", {bulge}},
                                   PointConfig{"1M points, vortex offset + radial scale effectors", {swirlOffset, bulge}},
                                   PointConfig{"1M points, curl offset + radial scale effectors", {drift, bulge}}}) {
        scene::Scene s = darkScene();
        s.camera.position = {0.0f, 6.0f, 22.0f};
        s.fields.fields.push_back(curlField());
        s.fields.fields.push_back(radialField());
        s.fields.fields.push_back(vortexField());
        auto points = pointObject("million", 1u << 20, 8.0f, 0.02f);
        points.effectors = cfg.effectors;
        s.procedurals.push_back(points);
        measure(s, cfg.name);
    }
    for (const char* fieldName : {"swirl", "curl"}) {
        scene::Scene s = darkScene();
        s.camera.position = {0.0f, 10.0f, 40.0f};
        s.fields.fields.push_back(curlField());
        s.fields.fields.push_back(vortexField());
        auto boxes = boxObject("boxes100k", 100000, 14.0f);
        scene::Deformer field;
        field.kind = scene::DeformerKind::Field;
        field.space = scene::DeformSpace::World;
        field.field = fieldName;
        field.amount = 0.5f;
        boxes.deformers.push_back(field);
        s.procedurals.push_back(boxes);
        measure(s, (std::string("100k boxes, world Field deformer (") + fieldName + ")").c_str());
    }
    {
        scene::Scene s = darkScene();
        s.camera.position = {0.0f, 10.0f, 40.0f};
        s.procedurals.push_back(boxObject("boxes100k", 100000, 14.0f));
        measure(s, "100k boxes, no deformers");
    }
    for (int variant = 0; variant < 2; ++variant) {
        scene::Scene s;
        s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
        s.camera.position = {0.0f, 0.0f, 10.0f};
        s.camera.target = {0.0f, 0.0f, 0.0f};
        s.fields.fields.push_back(vortexField());
        s.fields.fields.push_back(curlField());
        scene::ParticleSystem sys;
        sys.capacity = 1u << 18;
        sys.shape = scene::EmitterShape::Sphere;
        sys.extent = {2.0f, 2.0f, 2.0f};
        sys.spawnRate = 150000.0f;
        sys.lifetimeMin = 1.5f;
        sys.lifetimeMax = 2.0f;
        sys.turbulence = 0.0f;
        sys.sizeStart = 0.01f;
        sys.sizeEnd = 0.0f;
        if (variant == 1) {
            scene::FieldForce swirl;
            swirl.field = "swirl";
            swirl.strength = 2.0f;
            scene::FieldForce curl;
            curl.field = "curl";
            curl.mode = scene::FieldForceMode::Turbulence;
            curl.strength = 1.0f;
            sys.fieldForces = {swirl, curl};
        }
        s.particles.push_back(sys);
        measure(s, variant == 0 ? "256k particles, no field forces" : "256k particles, vortex force + curl turbulence");
    }
}
