// ADR-903..906 on pixels. Every number these parameters move, measured where the frame is made:
//
//   * a node's `emissiveBoost` multiplies the final emission of a program-lit procedural node, a
//     plain one, a mesh and an SDF by the factor asked -- after the program, where the old material
//     lane cannot reach (the control arm shows that lane is dead on the program-lit node);
//   * a route onto it does the same, and at rest it changes no byte;
//   * a procedural instance's emission variation is applied ONCE (the brightness spread is g, not g
//     squared) and as a ROTATION of the displayed hue, where it used to be a ratio made against a
//     colour the program never shows;
//   * a scatter layer's gain and hue act after its program, on it alone;
//   * a material layer's `emissionIntensity` parameter moves a glow that lives in a layer;
//   * `scene/ecologyLight` lights the ground under a glowing layer;
//   * a wave field whose clock starts at a marker sends a ring of light out through the instances
//     after that marker and not before, and lands on the same frame however the second was reached.
//
// The emission target is read through the aux debug view (linear, `e.rgb * scale`), which is what
// the FXL tests read: it holds exactly what a surface emits, with no lighting in it.
// With AVGEN_EMISSION_DUMP=<dir> every arm is written as a PNG for a person to look at.

#include "assets/asset_registry.hpp"
#include "assets/image.hpp"
#include "core/color.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/composition.hpp"
#include "scene/field_params.hpp"
#include "scene/material_program.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/procedural.hpp"
#include "scene/scatter_anchors.hpp"
#include "scene/scene.hpp"
#include "signals/signal_bus.hpp"
#include "support/gltf_fixture.hpp"
#include "support/image_diff.hpp"
#include "support/temp_dir.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_trigger.hpp"
#include "world/effects/entity_fx.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kWidth = 384;
constexpr std::uint32_t kHeight = 216;

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

struct Harness {
    std::unique_ptr<gpu::Context> ctx = makeContext();
    gpu::ShaderLibrary shaders{*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)}};
    rendering::SceneRenderer renderer{*ctx, shaders};
    Harness() { REQUIRE(renderer.init().has_value()); }

    // Fresh temporal history and drawn twice, so an arm never inherits the previous arm's history.
    gpu::Image8 render(const scene::Scene& s, double seconds = 1.0) {
        FrameTime t{};
        t.renderTime = seconds;
        renderer.resetTemporalHistory();
        auto first = renderer.renderToImage(s, t, kWidth, kHeight);
        REQUIRE(first.has_value());
        auto img = renderer.renderToImage(s, t, kWidth, kHeight);
        REQUIRE(img.has_value());
        return std::move(*img);
    }
    // The emission target, linear, `e.rgb * scale` per channel.
    gpu::Image8 emission(const scene::Scene& s, float scale, double seconds = 1.0) {
        renderer.setAuxDebugView(rendering::AuxDebugView::Emission);
        renderer.setAuxDebugScale(scale);
        gpu::Image8 img = render(s, seconds);
        renderer.setAuxDebugView(rendering::AuxDebugView::None);
        return img;
    }
};

void dump(const gpu::Image8& image, const std::string& name) {
    const char* dir = std::getenv("AVGEN_EMISSION_DUMP");
    if (dir == nullptr || dir[0] == '\0') {
        return;
    }
    static_cast<void>(assets::writePng(fs::path(dir) / (name + ".png"), image.width, image.height, image.rgba));
}

// Where a world point lands in the frame (pixels, top-left origin).
glm::vec2 project(const scene::Camera& camera, const glm::vec3& p) {
    const glm::mat4 vp =
        camera.projection(static_cast<float>(kWidth) / static_cast<float>(kHeight)) * camera.view();
    const glm::vec4 c = vp * glm::vec4(p, 1.0f);
    const glm::vec2 ndc = glm::vec2(c) / c.w;
    return {(ndc.x * 0.5f + 0.5f) * static_cast<float>(kWidth), (0.5f - ndc.y * 0.5f) * static_cast<float>(kHeight)};
}

// Mean rgb (0..255) of a (2 half + 1)^2 window.
glm::dvec3 windowMean(const gpu::Image8& img, glm::vec2 centre, int half) {
    glm::dvec3 sum(0.0);
    int n = 0;
    for (int y = static_cast<int>(centre.y) - half; y <= static_cast<int>(centre.y) + half; ++y) {
        for (int x = static_cast<int>(centre.x) - half; x <= static_cast<int>(centre.x) + half; ++x) {
            if (x < 0 || y < 0 || x >= static_cast<int>(img.width) || y >= static_cast<int>(img.height)) {
                continue;
            }
            const std::uint8_t* p = img.pixel(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
            sum += glm::dvec3(p[0], p[1], p[2]);
            ++n;
        }
    }
    REQUIRE(n > 0);
    return sum / static_cast<double>(n);
}

// Mean rgb over the pixels of columns [x0, x1) that `mask` shows emitting.
glm::dvec3 maskedMean(const gpu::Image8& img, const gpu::Image8& mask, std::uint32_t x0, std::uint32_t x1) {
    glm::dvec3 sum(0.0);
    std::size_t n = 0;
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = x0; x < x1 && x < img.width; ++x) {
            const std::uint8_t* m = mask.pixel(x, y);
            if (m[0] + m[1] + m[2] < 24) {
                continue;
            }
            const std::uint8_t* p = img.pixel(x, y);
            sum += glm::dvec3(p[0], p[1], p[2]);
            ++n;
        }
    }
    INFO("emitting pixels in columns " << x0 << ".." << x1 << ": " << n);
    REQUIRE(n > 40);
    return sum / static_cast<double>(n);
}

double total(const glm::dvec3& v) {
    return v.x + v.y + v.z;
}

glm::dvec3 oklch(const glm::dvec3& rgb255) {
    return glm::dvec3(color::oklabToOklch(color::rgbToOklab(glm::vec3(rgb255 / 255.0))));
}

// Signed hue difference a - b in turns, the short way round.
double hueDelta(double a, double b) {
    double d = a - b;
    return d - std::round(d);
}

std::size_t differing(const gpu::Image8& a, const gpu::Image8& b, int threshold = 12) {
    std::size_t n = 0;
    for (std::size_t i = 0; i + 3 < a.rgba.size(); i += 4) {
        int sum = 0;
        for (int c = 0; c < 3; ++c) {
            sum += std::abs(static_cast<int>(a.rgba[i + static_cast<std::size_t>(c)]) -
                            static_cast<int>(b.rgba[i + static_cast<std::size_t>(c)]));
        }
        n += sum > threshold ? 1u : 0u;
    }
    return n;
}

// A composition loaded from text and attached, with the modulation chain a route runs through.
// Declaration order is destruction order in reverse: the composition goes first, while the
// parameter set it registered into still exists.
struct Loaded {
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    std::unique_ptr<scene::Composition> comp;

    void frame(double seconds = 1.0) {
        params.resetFinals();
        modulator.applyRoutes(bus, params, 1.0 / 60.0);
        FrameTime t{};
        t.renderTime = seconds;
        comp->update(t);
    }
    params::Parameter<float>& param(const std::string& path) {
        auto* p = params.findAs<float>(path);
        INFO(path);
        REQUIRE(p != nullptr);
        return *p;
    }
};

std::unique_ptr<Loaded> load(const std::string& text, assets::AssetRegistry& registry) {
    auto out = std::make_unique<Loaded>();
    auto comp = scene::Composition::fromJson(nlohmann::json::parse(text), registry);
    if (!comp) {
        FAIL(comp.error().message);
    }
    out->comp = std::move(*comp);
    out->comp->attach(out->params, out->modulator);
    out->frame();
    return out;
}

// ---- the four drawable kinds -----------------------------------------------------------------------

// Four emitters in a row, dark ground-free space, so each owns a band of columns: a procedural box
// whose PROGRAM asserts its emission (the kind emissiveBoost never reached), a plain procedural box,
// a mesh (the GLB fixture's emissive triangle) and an SDF sphere. Emission is kept low enough that
// three times it still fits the 8-bit view at the scale the tests read. Each procedural is ONE box:
// the default distribution is a ring of 32 at 6 m, which seen edge-on is a bar across two bands.
std::string fourKinds(const std::string& glb) {
    return R"({
      "format": "avgen-scene", "version": 1, "name": "four",
      "camera": { "mode": 1, "position": [0, 0.5, 14], "target": [0, 0.5, 0], "fov": 50.0 },
      "environment": { "background": [0, 0, 0], "intensity": 0.0 },
      "materialPrograms": [
        { "name": "glow", "ops": [ { "kind": "constant", "dst": 4, "constant": [0.1, 0.9, 0.5, 1.0] } ],
          "emission": 4, "emissionIntensity": 1.0 } ],
      "nodes": [
        { "name": "lit", "kind": "procedural", "position": [-6, 0.5, 0], "procedural": {
            "source": { "kind": "box", "size": [1.8, 1.8, 1.8] }, "distribution": { "kind": "single" },
            "material": { "program": "glow", "baseColor": [0, 0, 0] } } },
        { "name": "plain", "kind": "procedural", "position": [-2, 0.5, 0], "procedural": {
            "source": { "kind": "box", "size": [1.8, 1.8, 1.8] }, "distribution": { "kind": "single" },
            "material": { "baseColor": [0, 0, 0], "emissiveColor": [1.0, 0.5, 0.2], "emissiveIntensity": 1.0 } } },
        { "name": "mesh", "kind": "gltf", "asset": ")" +
           glb + R"(", "position": [-3.0, -0.5, 0], "scale": [2, 2, 2] },
        { "name": "orb", "kind": "sdf", "position": [6, 0.5, 0], "sdf": {
            "tree": { "root": { "kind": "sphere", "radius": 1.0 } },
            "boundsMin": [-1.5, -1.5, -1.5], "boundsMax": [1.5, 1.5, 1.5],
            "material": { "baseColor": [0, 0, 0], "emissiveColor": [0.2, 0.4, 1.0], "emissiveIntensity": 1.0 } } }
      ]
    })";
}

// The columns each emitter owns in the frame above.
struct Band {
    const char* node;
    std::uint32_t x0;
    std::uint32_t x1;
};
constexpr std::array<Band, 4> kBands{{{"lit", 40, 130}, {"plain", 130, 188}, {"mesh", 188, 250}, {"orb", 250, 350}}};

} // namespace

TEST_CASE("emissiveBoost multiplies a node's final emission after its program, for every drawable kind",
          "[gpu][emission][boost]") {
    Harness h;
    const fs::path glb = testsupport::writeTriangleGlb("emission_lanes_gpu");
    assets::AssetRegistry registry{testsupport::processTempDir()};
    auto scene = load(fourKinds(glb.filename().string()), registry);
    constexpr float kScale = 0.2f;

    const gpu::Image8 control = h.emission(scene->comp->scene(), kScale);
    dump(control, "boost-control");
    for (const Band& b : kBands) {
        scene->param(std::string("nodes/") + b.node + "/emissiveBoost").setBase(3.0f);
    }
    scene->frame();
    const gpu::Image8 boosted = h.emission(scene->comp->scene(), kScale);
    dump(boosted, "boost-x3");
    for (const Band& b : kBands) {
        const double before = total(maskedMean(control, control, b.x0, b.x1));
        const double after = total(maskedMean(boosted, control, b.x0, b.x1));
        INFO(b.node << ": emission " << before << " -> " << after << ", ratio " << after / before);
        // Before ADR-903 this ratio was exactly 1 for "lit", "plain" and "orb": the boost wrote
        // only the entities' material lane, and a procedural or SDF node has no entities.
        CHECK(after / before > 2.8);
        CHECK(after / before < 3.2);
    }

    SECTION("the control: the old lane -- the material's intensity -- cannot reach a program-lit surface") {
        // This is the mechanism the boost used to be, and why it could never have been the fix: a
        // program that writes emission discards the material's intensity (ADR-179). The same x3 on
        // the material moves the plain box and leaves the program-lit one exactly where it was.
        for (const Band& b : kBands) {
            scene->param(std::string("nodes/") + b.node + "/emissiveBoost").setBase(1.0f);
        }
        const float litRest = scene->param("procedural/lit/material/emissive").base();
        const float plainRest = scene->param("procedural/plain/material/emissive").base();
        scene->param("procedural/lit/material/emissive").setBase(litRest * 3.0f);
        scene->param("procedural/plain/material/emissive").setBase(plainRest * 3.0f);
        scene->frame();
        const gpu::Image8 materialLane = h.emission(scene->comp->scene(), kScale);
        dump(materialLane, "boost-material-lane-x3");
        const double lit = total(maskedMean(materialLane, control, kBands[0].x0, kBands[0].x1)) /
                           total(maskedMean(control, control, kBands[0].x0, kBands[0].x1));
        const double plain = total(maskedMean(materialLane, control, kBands[1].x0, kBands[1].x1)) /
                             total(maskedMean(control, control, kBands[1].x0, kBands[1].x1));
        INFO("material lane x3: program-lit ratio " << lit << ", plain ratio " << plain);
        CHECK(lit > 0.98);
        CHECK(lit < 1.02);
        CHECK(plain > 2.8);
    }
    fs::remove(glb);
}

TEST_CASE("A route onto emissiveBoost reaches the pixels, and at rest changes none", "[gpu][emission][boost][modulation]") {
    Harness h;
    const fs::path glb = testsupport::writeTriangleGlb("emission_route_gpu");
    assets::AssetRegistry registry{testsupport::processTempDir()};
    auto scene = load(fourKinds(glb.filename().string()), registry);
    const gpu::Image8 unrouted = h.emission(scene->comp->scene(), 0.2f);

    const signals::SignalId kick = scene->bus.declare("test.kick");
    scene->modulator.clearRoutes(); // the composition's default audio routes: this bus has no audio
    params::ModRoute route;
    route.source = "test.kick";
    route.target = "nodes/lit/emissiveBoost";
    route.amount = 2.0f;
    scene->modulator.addRoute(route);
    REQUIRE(scene->modulator.bind(scene->bus, scene->params).has_value());

    scene->bus.set(kick, 0.0f);
    scene->frame();
    const gpu::Image8 rest = h.emission(scene->comp->scene(), 0.2f);
    const auto same = testing::byteDiff(unrouted.rgba, rest.rgba);
    INFO("a route at rest: " << same.describe());
    CHECK(same.identical());

    scene->bus.set(kick, 1.0f);
    scene->frame();
    const gpu::Image8 hit = h.emission(scene->comp->scene(), 0.2f);
    dump(hit, "boost-route-hit");
    const double ratio = total(maskedMean(hit, unrouted, kBands[0].x0, kBands[0].x1)) /
                         total(maskedMean(unrouted, unrouted, kBands[0].x0, kBands[0].x1));
    INFO("the routed node, 1 + 2 x kick: ratio " << ratio);
    CHECK(ratio > 2.8);
    CHECK(ratio < 3.2);
    // The route names one node: every other band is the frame it was.
    for (std::size_t i = 1; i < kBands.size(); ++i) {
        const double other = total(maskedMean(hit, unrouted, kBands[i].x0, kBands[i].x1)) /
                             total(maskedMean(unrouted, unrouted, kBands[i].x0, kBands[i].x1));
        INFO(kBands[i].node << " ratio " << other);
        CHECK(std::abs(other - 1.0) < 0.01);
    }
    fs::remove(glb);
}

// ---- ADR-903 with FXL: the lane composes with an effect's gain -------------------------------------

namespace {

// Every entity is its own node, named as the entity -- the query the FXL builder asks (as in
// test_entity_fx_gpu.cpp).
class OrbQuery final : public world::EffectSceneQuery {
public:
    explicit OrbQuery(const scene::Scene& s) : scene_(s) {}
    [[nodiscard]] bool nodePosition(std::string_view name, glm::vec3& out) const override {
        for (const scene::Entity& e : scene_.entities) {
            if (e.name == name) {
                out = e.transform.position;
                return true;
            }
        }
        return false;
    }
    [[nodiscard]] bool nodeView(std::string_view name, world::NodeView& out) const override {
        for (std::size_t i = 0; i < scene_.entities.size(); ++i) {
            const scene::Entity& e = scene_.entities[i];
            if (e.name != name) {
                continue;
            }
            out = world::NodeView{};
            out.world = e.transform.matrix();
            const auto& [lo, hi] = scene_.meshBounds(e.mesh);
            out.boundsMin = e.transform.position + lo * e.transform.scale;
            out.boundsMax = e.transform.position + hi * e.transform.scale;
            out.hasBounds = true;
            out.firstEntity = static_cast<std::uint32_t>(i);
            out.entityCount = 1;
            return true;
        }
        return false;
    }

private:
    const scene::Scene& scene_;
};

scene::Scene twoOrbs(float laneA) {
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.showSkybox = false;
    s.environment.environmentIntensity = 0.0f;
    s.camera.position = glm::vec3(0.0f, 0.0f, 10.0f);
    s.camera.target = glm::vec3(0.0f);
    s.camera.fovYRadians = glm::radians(45.0f);
    const scene::MeshId sphere = s.addMesh(scene::makeIcosphere(1.0f, 3));
    for (const auto& [name, x] : {std::pair<const char*, float>{"orbA", -2.4f}, {"orbB", 2.4f}}) {
        scene::Entity& orb = s.addEntity(name, sphere);
        orb.transform.position = glm::vec3(x, 0.0f, 0.0f);
        orb.material.baseColor = glm::vec3(0.0f);
        orb.material.emissiveColor = glm::vec3(1.0f, 0.6f, 0.3f);
        orb.material.emissiveIntensity = 0.05f;
    }
    s.entities[0].emissionGain = laneA;
    return s;
}

void glowOn(scene::Scene& s, float gain) {
    world::EffectInstance e = world::makeEffect(world::EffectKind::Glow, "orbA-gain");
    e.id = "orbA-gain";
    e.owner = world::EffectOwner::entity("orbA");
    e.activation = world::Activation::Always;
    e.timing = world::Timing{};
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    e.values.setFloat("glow/gain", gain);
    e.values.setFloat("glow/glow", 0.0f);
    e.values.setFloat("glow/rim", 0.0f);
    e.values.setBool("glow/spill", false);
    const std::vector<world::EffectInstance> effects{e};
    const OrbQuery query(s);
    world::EffectContext ctx;
    ctx.seconds = 1.0;
    ctx.scene = &query;
    ctx.cameraPosition = s.camera.position;
    std::vector<world::EffectStatus> status(1, world::EffectStatus::Dormant);
    std::vector<std::string> reasons(1);
    world::buildEntityFxFrame(effects, ctx, s.entityFx, {}, status, reasons);
    REQUIRE(status[0] == world::EffectStatus::Drawn);
}

} // namespace

TEST_CASE("The emission lane composes with an FXL Glow: the gains multiply", "[gpu][emission][boost][fxl]") {
    Harness h;
    const scene::Scene control = twoOrbs(1.0f);
    const gpu::Image8 before = h.emission(control, 2.0f);
    scene::Scene lane = twoOrbs(3.0f);
    const gpu::Image8 laneOnly = h.emission(lane, 2.0f);
    scene::Scene both = twoOrbs(3.0f);
    glowOn(both, 2.0f);
    const gpu::Image8 composed = h.emission(both, 2.0f);
    dump(before, "fxl-lane-control");
    dump(composed, "fxl-lane-x3-glow-x2");
    const double c = total(maskedMean(before, before, 0, kWidth / 2));
    const double l = total(maskedMean(laneOnly, before, 0, kWidth / 2));
    const double b = total(maskedMean(composed, before, 0, kWidth / 2));
    INFO("orb A emission: control " << c << ", lane x3 " << l << " (x" << l / c << "), lane x3 with Glow x2 " << b
                                    << " (x" << b / c << ")");
    CHECK(l / c > 2.85);
    CHECK(l / c < 3.15);
    CHECK(b / c > 5.7);
    CHECK(b / c < 6.3);
    // Orb B has neither, and is the frame it was.
    const double rb = total(maskedMean(composed, before, kWidth / 2, kWidth)) /
                      total(maskedMean(before, before, kWidth / 2, kWidth));
    CHECK(std::abs(rb - 1.0) < 0.01);
}

// ---- ADR-904: the instance variation, once, as a rotation ----------------------------------------

namespace {

// The multiplier procedural.cpp bakes (ADR-054): an OKLCH rotation of the material's emissive
// colour, as a per-channel ratio of it, times the instance's gain.
glm::vec3 bakedMultiplier(const glm::vec3& base, float turns, float gain) {
    const glm::vec3 safe = glm::max(base, glm::vec3(1e-3f));
    const glm::vec3 rotated = glm::max(color::hueShift(safe, turns), glm::vec3(0.0f));
    return glm::clamp(rotated / safe, glm::vec3(0.0f), glm::vec3(96.0f)) * gain;
}

struct Variation {
    float turns;
    float gain;
};
// Rotations chosen inside the sRGB gamut for both the purple the multipliers are made against and the
// cyan-green the program shows: a rotation that leaves the gamut is clamped (the CPU bake and the
// shader's hueShift both clip negative channels), and a clipped colour is no longer a pure rotation.
constexpr std::array<Variation, 5> kVariations{{{0.0f, 1.0f}, {0.2f, 1.0f}, {-0.1f, 1.0f}, {0.0f, 0.5f}, {0.0f, 1.6f}}};
const glm::vec3 kPurple{0.341f, 0.0821f, 1.0f}; // GV3's fungi: the colour the multipliers are made against
const glm::vec4 kCyanGreen{0.08f, 0.95f, 0.53f, 1.0f}; // glowmereTissue's own colour

scene::ProceduralGeometry boxRow(const std::string& name, float z, std::span<const Variation> variations,
                                 const glm::vec3& materialEmissive) {
    scene::ProceduralGeometry g;
    g.name = name;
    g.source.kind = scene::PrimitiveKind::Box;
    g.source.size = {1.4f, 1.4f, 1.4f};
    g.source.subdivisions = 1;
    g.structureVersion = 1;
    g.meshHash = 0x5eed0000ull + static_cast<std::uint64_t>(name.size());
    g.material.baseColor = glm::vec3(0.0f);
    g.material.emissiveColor = materialEmissive;
    g.material.emissiveIntensity = 1.0f;
    for (std::size_t i = 0; i < variations.size(); ++i) {
        scene::InstanceRecord r{};
        r.position = {(static_cast<float>(i) - static_cast<float>(variations.size() - 1) * 0.5f) * 2.6f, 0.0f, z, 1.0f};
        r.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
        r.scale = {1.0f, 1.0f, 1.0f, static_cast<float>(i) / 4.0f};
        r.random = {0.25f, 0.5f, 0.75f, 0.125f};
        r.color = {1.0f, 1.0f, 1.0f, static_cast<float>(i)};
        r.emissive = glm::vec4(bakedMultiplier(materialEmissive, variations[i].turns, variations[i].gain), 0.0f);
        g.instances.push_back(r);
    }
    g.boundsMin = glm::vec3(-8.0f, -1.0f, z - 1.0f);
    g.boundsMax = glm::vec3(8.0f, 1.0f, z + 1.0f);
    return g;
}

scene::Scene darkStage() {
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.showSkybox = false;
    s.environment.environmentIntensity = 0.0f;
    s.camera.position = glm::vec3(0.0f, 0.0f, 13.0f);
    s.camera.target = glm::vec3(0.0f, 0.0f, 0.0f);
    s.camera.fovYRadians = glm::radians(50.0f);
    return s;
}

// The scatter program in the two shapes this ADR is about: asserting its colour and leaving the
// variation to the engine (what glowmereTissue is now), or multiplying `instanceEmissive` in itself
// (what it was).
scene::MaterialProgram tissue(const std::string& name, bool readsInstance) {
    scene::MaterialProgram p;
    p.name = name;
    scene::MaterialOp colour;
    colour.kind = scene::MaterialOpKind::Constant;
    colour.dst = 4;
    colour.constant = kCyanGreen;
    p.ops.push_back(colour);
    if (readsInstance) {
        scene::MaterialOp in;
        in.kind = scene::MaterialOpKind::Input;
        in.dst = 5;
        in.input = scene::MaterialInput::InstanceEmissive;
        scene::MaterialOp mul;
        mul.kind = scene::MaterialOpKind::Multiply;
        mul.dst = 4;
        mul.srcA = 4;
        mul.srcB = 5;
        p.ops.push_back(in);
        p.ops.push_back(mul);
    }
    p.emissionRegister = 4;
    p.emissionIntensity = 0.28f;
    return p;
}

} // namespace

TEST_CASE("A procedural instance's variation is applied once, as a rotation of the hue it shows",
          "[gpu][emission][variation]") {
    Harness h;
    const auto measure = [&h](bool readsInstance, const std::string& tag) {
        scene::Scene s = darkStage();
        s.materialPrograms.push_back(tissue("tissue", readsInstance));
        scene::ProceduralGeometry row = boxRow("fungi", 0.0f, kVariations, kPurple);
        row.material.program = "tissue";
        s.procedurals.push_back(std::move(row));
        const gpu::Image8 img = h.emission(s, 2.0f);
        dump(img, "variation-" + tag);
        std::vector<glm::dvec3> out;
        for (const scene::InstanceRecord& r : s.procedurals[0].instances) {
            out.push_back(windowMean(img, project(s.camera, glm::vec3(r.position) + glm::vec3(0, 0, 0.7f)), 3));
        }
        return out;
    };

    // ---- after: the program asserts cyan-green, the engine applies the variation once ----
    const std::vector<glm::dvec3> engine = measure(false, "engine");
    const glm::dvec3 base = oklch(engine[0]);
    INFO("the unvaried instance: rgb " << engine[0].x << "," << engine[0].y << "," << engine[0].z << " hue "
                                       << base.z);
    REQUIRE(total(engine[0]) > 60.0);
    // A gain is a gain: half is half, not a quarter (the m-squared defect gave 0.25).
    const double half = total(engine[3]) / total(engine[0]);
    const double more = total(engine[4]) / total(engine[0]);
    INFO("brightness ratios: gain 0.5 -> " << half << ", gain 1.6 -> " << more);
    CHECK(half > 0.46);
    CHECK(half < 0.54);
    CHECK(more > 1.5);
    CHECK(more < 1.7);
    // A hue offset rotates the colour the program shows, at its lightness.
    for (std::size_t i : {1u, 2u}) {
        const glm::dvec3 c = oklch(engine[i]);
        const double turned = hueDelta(c.z, base.z);
        INFO("instance " << i << " asked " << kVariations[i].turns << " turns, displayed " << turned << " (L "
                         << c.x << " vs " << base.x << ")");
        CHECK(std::abs(turned - static_cast<double>(kVariations[i].turns)) < 0.02);
        CHECK(std::abs(c.x - base.x) < 0.06);
    }

    // ---- a program that multiplies instanceEmissive in itself owns the variation: once, not twice ----
    const std::vector<glm::dvec3> owned = measure(true, "program-owned");
    const double ownedHalf = total(owned[3]) / total(owned[0]);
    INFO("a program that reads instanceEmissive: gain 0.5 -> " << ownedHalf << " (0.25 before ADR-904)");
    CHECK(ownedHalf > 0.46);
    CHECK(ownedHalf < 0.54);
    // ...and its hue is the ratio's, not a rotation: what it used to be (and why the library
    // programs stopped doing it). Recorded, not held: this is the shape nobody should author.
    for (std::size_t i : {1u, 2u}) {
        const double turned = hueDelta(oklch(owned[i]).z, oklch(owned[0]).z);
        INFO("program-owned instance " << i << ": asked " << kVariations[i].turns << ", displayed " << turned);
        CHECK(std::isfinite(turned));
    }
}

TEST_CASE("A scatter layer's gain and hue act after its program, on that layer alone", "[gpu][emission][scatter]") {
    Harness h;
    const auto frame = [&h](float gain, float hue) {
        scene::Scene s = darkStage();
        s.camera.position = glm::vec3(0.0f, 0.0f, 16.0f);
        s.materialPrograms.push_back(tissue("tissue", false));
        const std::array<Variation, 3> flat{{{0.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 1.0f}}};
        scene::ProceduralGeometry fungi = boxRow("fungi", 0.0f, flat, kPurple);
        fungi.material.program = "tissue";
        for (scene::InstanceRecord& r : fungi.instances) {
            r.position.y = 1.4f;
        }
        fungi.emissionGain = gain;
        fungi.emissionHue = hue;
        scene::ProceduralGeometry beacons = boxRow("beacons", 0.0f, flat, glm::vec3(0.06f, 0.82f, 1.0f));
        beacons.material.program = "tissue";
        for (scene::InstanceRecord& r : beacons.instances) {
            r.position.y = -1.4f;
        }
        s.procedurals.push_back(std::move(fungi));
        s.procedurals.push_back(std::move(beacons));
        return std::pair{h.emission(s, 2.0f), s.camera};
    };
    const auto [control, camera] = frame(1.0f, 0.0f);
    const auto [gained, cam2] = frame(2.0f, 0.0f);
    const auto [turned, cam3] = frame(1.0f, 0.25f);
    dump(control, "layer-control");
    dump(gained, "layer-gain2");
    dump(turned, "layer-hue025");
    const glm::vec2 fungusAt = project(camera, {0.0f, 1.4f, 0.7f});
    const glm::vec2 beaconAt = project(camera, {0.0f, -1.4f, 0.7f});
    const glm::dvec3 f0 = windowMean(control, fungusAt, 3);
    const glm::dvec3 b0 = windowMean(control, beaconAt, 3);
    REQUIRE(total(f0) > 60.0);
    const double gainRatio = total(windowMean(gained, fungusAt, 3)) / total(f0);
    INFO("layer gain 2: fungi x" << gainRatio);
    CHECK(gainRatio > 1.9);
    CHECK(gainRatio < 2.1);
    const glm::dvec3 c0 = oklch(f0);
    const glm::dvec3 c1 = oklch(windowMean(turned, fungusAt, 3));
    INFO("layer hue +0.25 turns: displayed " << hueDelta(c1.z, c0.z) << ", L " << c0.x << " -> " << c1.x);
    CHECK(std::abs(hueDelta(c1.z, c0.z) - 0.25) < 0.02);
    CHECK(std::abs(c1.x - c0.x) < 0.08);
    // The other layer is the frame it was, in both arms.
    for (const gpu::Image8* img : {&gained, &turned}) {
        const glm::dvec3 b = windowMean(*img, beaconAt, 3);
        CHECK(std::abs(total(b) - total(b0)) < 1.0);
    }
}

// ---- ADR-905: a material layer's intensity ----------------------------------------------------------

TEST_CASE("A material layer's emissionIntensity parameter moves a glow that lives in a layer",
          "[gpu][emission][material]") {
    Harness h;
    assets::AssetRegistry registry{testsupport::processTempDir()};
    auto scene = load(R"({
      "format": "avgen-scene", "version": 1, "name": "layered",
      "camera": { "mode": 1, "position": [0, 0, 8], "target": [0, 0, 0], "fov": 50.0 },
      "environment": { "background": [0, 0, 0], "intensity": 0.0 },
      "materialPrograms": [
        { "name": "crown", "emissionIntensity": 1.0,
          "layers": [ { "name": "fireflies",
                        "ops": [ { "kind": "constant", "dst": 3, "constant": [0.2, 0.6, 1.0, 1.0] } ],
                        "emission": 3, "emissionIntensity": 0.3 } ] } ],
      "nodes": [ { "name": "tree", "kind": "procedural", "procedural": {
          "source": { "kind": "box", "size": [2, 2, 2] }, "material": { "program": "crown", "baseColor": [0, 0, 0] } } } ]
    })",
                      registry);
    const gpu::Image8 control = h.emission(scene->comp->scene(), 1.0f);
    // The program-level intensity multiplies the BASE's emission, and this base has none: the knob
    // GV3's fireflies arcs were keyed on moved nothing, so it is no longer registered -- a key on it
    // is now a target nobody has, reported at load, instead of a silent one.
    CHECK(scene->params.find("material/crown/emissionIntensity") == nullptr);
    // The layer's own, which is live.
    scene->param("material/crown/layer/1/fireflies/emissionIntensity").setBase(0.9f);
    scene->frame();
    const gpu::Image8 layer = h.emission(scene->comp->scene(), 1.0f);
    dump(control, "layer-intensity-control");
    dump(layer, "layer-intensity-x3");
    const double ratio = total(maskedMean(layer, control, 0, kWidth)) / total(maskedMean(control, control, 0, kWidth));
    INFO("layer emissionIntensity 0.3 -> 0.9: ratio " << ratio);
    CHECK(ratio > 2.8);
    CHECK(ratio < 3.2);
}

// ---- ADR-906: a ring of light through the instances, on a marker ------------------------------------

TEST_CASE("A wave field timed from a marker sends a ring of light through the instances after it",
          "[gpu][emission][fields][trigger]") {
    Harness h;
    world::TriggerClock clock;
    const std::array markers{world::TriggerMarker{2.0, "drop"}};
    clock.setMarkers(markers);
    // The emission view's scale. The crest multiplies the rest by up to 1 + 4 = 5, and the rest is
    // (0.04, 0.18, 0.12) linear, so 5x of it (0.9 in green) must still fit 8 bits with room to spare:
    // at 3.0 the green and blue channels clipped at 255 and a crest could read at most 2.5x the rest,
    // and at 1.0 the brightest crest pixel measured 253.
    constexpr float kWaveScale = 0.8f;

    // A row of 41 small emitters along x, the ring's centre at the origin, 10 m/s outward.
    const auto stage = [](bool withField) {
        scene::Scene s = darkStage();
        s.camera.position = glm::vec3(0.0f, 6.0f, 18.0f);
        s.camera.target = glm::vec3(0.0f, 0.0f, 0.0f);
        scene::ProceduralGeometry row;
        row.name = "fungi";
        row.source.kind = scene::PrimitiveKind::Box;
        row.source.size = {0.36f, 0.36f, 0.36f};
        row.structureVersion = 1;
        row.meshHash = 0x70dd;
        row.material.baseColor = glm::vec3(0.0f);
        row.material.emissiveColor = glm::vec3(0.2f, 0.9f, 0.6f);
        row.material.emissiveIntensity = 0.2f;
        for (int i = 0; i <= 40; ++i) {
            scene::InstanceRecord r{};
            r.position = {-10.0f + 0.5f * static_cast<float>(i), 0.0f, 0.0f, 1.0f};
            r.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
            r.scale = {1.0f, 1.0f, 1.0f, static_cast<float>(i) / 40.0f};
            r.color = {1.0f, 1.0f, 1.0f, static_cast<float>(i)};
            r.emissive = {1.0f, 1.0f, 1.0f, 0.0f};
            row.instances.push_back(r);
        }
        row.boundsMin = glm::vec3(-11.0f, -1.0f, -1.0f);
        row.boundsMax = glm::vec3(11.0f, 1.0f, 1.0f);
        if (withField) {
            row.emissiveField = "ripple";
            row.emissiveFieldAmount = 4.0f;
            spatial::FieldSpec wave;
            wave.name = "ripple";
            wave.kind = spatial::FieldKind::Wave;
            wave.waveGeometry = spatial::WaveGeometry::Radial;
            wave.waveShape = spatial::WaveShape::Pulse;
            wave.wavelength = 3.0f;
            wave.waveSpeed = 10.0f;
            wave.waveWidth = 0.0f;
            world::Trigger trigger;
            trigger.source = world::TriggerSource::TimelineMarker;
            trigger.name = "drop";
            wave.trigger = trigger;
            s.fields.fields.push_back(wave);
        }
        s.procedurals.push_back(std::move(row));
        return s;
    };
    const auto at = [&](double seconds) {
        scene::Scene s = stage(true);
        scene::resolveFieldTriggers(s.fields, &clock, seconds);
        return std::pair{h.emission(s, kWaveScale, seconds), s};
    };
    const scene::Scene plain = stage(false);
    const gpu::Image8 noField = h.emission(plain, kWaveScale, 1.9);
    const auto [before, sBefore] = at(1.9);
    const auto [early, sEarly] = at(2.5); // 0.5 s after the drop: the ring is 5 m out
    const auto [later, sLater] = at(2.9); // 0.9 s: 9 m
    dump(noField, "wave-no-field");
    dump(before, "wave-before-drop");
    dump(early, "wave-drop+0.5s");
    dump(later, "wave-drop+0.9s");

    // Before its marker the field is silent: the frame is the one with no field at all.
    const auto silent = testing::byteDiff(noField.rgba, before.rgba);
    INFO("before the drop, against no field: " << silent.describe());
    CHECK(silent.identical());

    const auto brightnessAt = [&plain](const gpu::Image8& img, float x) {
        return total(windowMean(img, project(plain.camera, {x, 0.0f, 0.0f}), 2));
    };
    const double rest = brightnessAt(noField, 5.0f);
    REQUIRE(rest > 10.0);
    // 0.5 s after the drop: the instances 5 m out, on both sides, carry the crest; those at 1 m and 9 m
    // do not.
    INFO("0.5 s: x=5 " << brightnessAt(early, 5.0f) << ", x=-5 " << brightnessAt(early, -5.0f) << ", x=1 "
                       << brightnessAt(early, 1.0f) << ", x=9 " << brightnessAt(early, 9.0f) << " (rest " << rest
                       << ")");
    CHECK(brightnessAt(early, 5.0f) > rest * 3.0);
    CHECK(brightnessAt(early, -5.0f) > rest * 3.0);
    CHECK(brightnessAt(early, 1.0f) < rest * 1.3);
    CHECK(brightnessAt(early, 9.0f) < rest * 1.3);
    // The measurement itself: a channel at 255 is a clipped crest, and the ratios above would then be
    // measuring the view's ceiling rather than the field.
    {
        const glm::vec2 crest = project(plain.camera, {5.0f, 0.0f, 0.0f});
        int peak = 0;
        for (int y = static_cast<int>(crest.y) - 2; y <= static_cast<int>(crest.y) + 2; ++y) {
            for (int x = static_cast<int>(crest.x) - 2; x <= static_cast<int>(crest.x) + 2; ++x) {
                const std::uint8_t* p = early.pixel(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
                peak = std::max({peak, static_cast<int>(p[0]), static_cast<int>(p[1]), static_cast<int>(p[2])});
            }
        }
        INFO("the crest's brightest channel: " << peak);
        CHECK(peak < 255);
    }
    // 0.4 s later it has moved out to 9 m and left 5 m behind.
    INFO("0.9 s: x=9 " << brightnessAt(later, 9.0f) << ", x=5 " << brightnessAt(later, 5.0f));
    CHECK(brightnessAt(later, 9.0f) > rest * 3.0);
    CHECK(brightnessAt(later, 5.0f) < rest * 1.3);

    // The control: the same field WITHOUT its trigger runs on the transport clock, so at 1.9 s -- before
    // the drop -- its front is already 19 m out and nothing holds it back. What makes the ring wait for
    // the drop is the trigger and nothing else.
    {
        scene::Scene untriggered = stage(true);
        untriggered.fields.fields[0].trigger.reset();
        untriggered.fields.fields[0].waveSpeed = 4.0f; // 7.6 m out at 1.9 s: inside the row
        const gpu::Image8 free = h.emission(untriggered, kWaveScale, 1.9);
        dump(free, "wave-untriggered-1.9s");
        INFO("untriggered at 1.9 s: x=7.5 " << brightnessAt(free, 7.5f) << " (rest " << rest << ")");
        CHECK(brightnessAt(free, 7.5f) > rest * 3.0);
        CHECK_FALSE(testing::byteDiff(noField.rgba, free.rgba).identical());
    }

    // Seek-exact: the same second reached by stepping from zero at 60 fps is the same frame as the
    // second asked cold -- the clock is a function of the second, not of how the playhead got there.
    scene::Scene stepped = stage(true);
    for (int frame = 0; frame <= 150; ++frame) {
        scene::resolveFieldTriggers(stepped.fields, &clock, static_cast<double>(frame) / 60.0);
    }
    const gpu::Image8 played = h.emission(stepped, kWaveScale, 2.5);
    const auto seek = testing::byteDiff(played.rgba, early.rgba);
    INFO("stepped to 2.5 s against asked at 2.5 s: " << seek.describe());
    CHECK(seek.identical());
}

// ---- ADR-905/906 through a composition: the real scatter path, the real mushrooms ------------------

namespace {

std::string valley(const fs::path& asset, bool ring) {
    std::string text = R"({
      "format": "avgen-scene", "version": 1, "name": "valley",
      "camera": { "mode": 1, "position": [0, 16, 34], "target": [0, 0, 0], "fov": 55.0 },
      "environment": { "background": [0.005, 0.006, 0.012], "intensity": 0.0,
                       "ecologyLight": 1.4, "ecologyLightRange": 400.0 },
      "materialPrograms": [
        { "name": "tissue", "ops": [ { "kind": "constant", "dst": 4, "constant": [0.08, 0.95, 0.53, 1.0] } ],
          "emission": 4, "emissionIntensity": 2.0 } ],
      "nodes": [
        { "name": "ground", "kind": "terrain", "world": { "name": "small", "size": [80, 80], "features": [] },
          "terrain": { "chunkSize": 40.0, "resolution": 16, "lodLevels": 1, "viewDistance": 400.0 },
          "material": { "baseColor": [0.35, 0.35, 0.35], "roughness": 0.9 },
          "scatter": [
            { "name": "fungi", "asset": "@ASSET@", "densities": { "meadow": 0.2, "forest": 0.2 },
              "height": 0.6, "emissiveIntensity": 6.0, "emissiveColor": [0.34, 0.08, 1.0],
              "materialProgram": "tissue", "castsShadow": false @RING@ } ] }
        @FIELD@
      ]
    })";
    text.replace(text.find("@ASSET@"), 7, asset.string());
    text.replace(text.find("@RING@"), 6, ring ? R"(, "emissiveField": "ripple", "emissiveFieldAmount": 6.0)" : "");
    text.replace(text.find("@FIELD@"), 7,
                 ring ? R"(, { "name": "ripple", "kind": "field", "field": {
                         "name": "ripple", "kind": "wave", "waveGeometry": "radial", "waveShape": "pulse",
                         "wavelength": 10.0, "waveSpeed": 12.0, "waveWidth": 0.0,
                         "trigger": { "source": "marker", "name": "drop" } } })"
                      : "");
    return text;
}

// Where the lit pixels of an emission image sit, as their mean distance from a screen point.
double meanDistanceOfChange(const gpu::Image8& a, const gpu::Image8& b, glm::vec2 from, std::size_t& count) {
    double sum = 0.0;
    count = 0;
    for (std::uint32_t y = 0; y < a.height; ++y) {
        for (std::uint32_t x = 0; x < a.width; ++x) {
            const std::uint8_t* p = a.pixel(x, y);
            const std::uint8_t* q = b.pixel(x, y);
            const int d = std::abs(p[0] - q[0]) + std::abs(p[1] - q[1]) + std::abs(p[2] - q[2]);
            if (d > 24) {
                sum += static_cast<double>(glm::length(glm::vec2(static_cast<float>(x), static_cast<float>(y)) - from));
                ++count;
            }
        }
    }
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

} // namespace

TEST_CASE("Through a composition: a layer's lanes, the ecology light and a ring on a marker reach the mushrooms",
          "[gpu][emission][scatter][composition]") {
    const fs::path asset = fs::path(AVGEN_SOURCE_DIR) / "assets/quaternius/glTF/Mushroom_Common.gltf";
    if (!fs::exists(asset)) {
        SKIP("the Quaternius library is not present in this checkout");
    }
    Harness h;
    assets::AssetRegistry registry{testsupport::processTempDir()};

    SECTION("the layer's gain and hue parameters, after glowmereTissue's kind of program") {
        auto scene = load(valley(asset, false), registry);
        const gpu::Image8 control = h.emission(scene->comp->scene(), 0.25f);
        scene->param("nodes/ground/scatter/fungi/emissionGain").setBase(2.0f);
        scene->frame();
        const gpu::Image8 gained = h.emission(scene->comp->scene(), 0.25f);
        scene->param("nodes/ground/scatter/fungi/emissionGain").setBase(1.0f);
        scene->param("nodes/ground/scatter/fungi/hueOffset").setBase(0.3f);
        scene->frame();
        const gpu::Image8 turned = h.emission(scene->comp->scene(), 0.25f);
        dump(control, "valley-control");
        dump(gained, "valley-fungi-gain2");
        dump(turned, "valley-fungi-hue03");
        const glm::dvec3 c = maskedMean(control, control, 0, kWidth);
        const glm::dvec3 g = maskedMean(gained, control, 0, kWidth);
        const glm::dvec3 t = maskedMean(turned, control, 0, kWidth);
        INFO("fungi emission: control " << total(c) << ", gain 2 " << total(g) << "; hue " << oklch(c).z << " -> "
                                        << oklch(t).z);
        CHECK(total(g) / total(c) > 1.8);
        CHECK(total(g) / total(c) < 2.1);
        CHECK(std::abs(hueDelta(oklch(t).z, oklch(c).z) - 0.3) < 0.04);
    }

    SECTION("scene/ecologyLight lights the ground under the glowing layer") {
        auto scene = load(valley(asset, false), registry);
        scene->param("scene/ecologyLight").setBase(0.0f);
        scene->frame();
        const gpu::Image8 dark = h.render(scene->comp->scene());
        scene->param("scene/ecologyLight").setBase(4.0f);
        scene->frame();
        const gpu::Image8 lit = h.render(scene->comp->scene());
        dump(dark, "valley-ecology-0");
        dump(lit, "valley-ecology-4");
        const std::size_t changed = differing(dark, lit);
        double darkSum = 0.0;
        double litSum = 0.0;
        for (std::size_t i = 0; i + 3 < dark.rgba.size(); i += 4) {
            darkSum += dark.rgba[i] + dark.rgba[i + 1] + dark.rgba[i + 2];
            litSum += lit.rgba[i] + lit.rgba[i + 1] + lit.rgba[i + 2];
        }
        INFO("ecology light 0 -> 4: " << changed << " pixels changed, frame sum " << darkSum << " -> " << litSum);
        CHECK(changed > kWidth * kHeight / 50);
        CHECK(litSum > darkSum * 1.05);
    }

    SECTION("a ring of light passes out through the mushrooms after a marker, and not before") {
        auto scene = load(valley(asset, true), registry);
        world::TriggerClock clock;
        const std::array markers{world::TriggerMarker{2.0, "drop"}};
        clock.setMarkers(markers);
        scene->comp->setTriggerClock(&clock);
        const auto at = [&](double seconds) {
            clock.setFrame(seconds);
            scene->frame(seconds);
            return h.emission(scene->comp->scene(), 0.25f, seconds);
        };
        const gpu::Image8 quiet = at(1.5);
        const gpu::Image8 stillQuiet = at(1.95);
        const gpu::Image8 early = at(2.6); // the front 7.2 m out
        const gpu::Image8 later = at(3.4); // 16.8 m
        dump(quiet, "valley-ring-before");
        dump(early, "valley-ring-0.6s");
        dump(later, "valley-ring-1.4s");
        // Nothing moves before the drop: the field is silent and nothing else animates here.
        CHECK(testing::byteDiff(quiet.rgba, stillQuiet.rgba).identical());
        const glm::vec2 centre = project(scene->comp->scene().camera, glm::vec3(0.0f));
        std::size_t earlyCount = 0;
        std::size_t laterCount = 0;
        const double earlyDistance = meanDistanceOfChange(quiet, early, centre, earlyCount);
        const double laterDistance = meanDistanceOfChange(quiet, later, centre, laterCount);
        INFO("changed pixels 0.6 s after the drop: " << earlyCount << " at " << earlyDistance
                                                      << " px from the centre; 1.4 s after: " << laterCount << " at "
                                                      << laterDistance << " px");
        CHECK(earlyCount > 20);
        CHECK(laterCount > 20);
        CHECK(laterDistance > earlyDistance * 1.4); // the ring has travelled outward
    }
}
