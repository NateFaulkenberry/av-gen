// FXL Wave 2 on pixels: the clip, the displacement and the pattern sub-blocks (the surface slice).
//
// The CPU suite (tests/unit/test_entity_fx_surface.cpp) proves what the builder writes. What only a
// GPU can answer is whether EVERY pass does what the record says:
//
//   * a half-dissolved owner's SHADOW has holes where the owner does -- the clip runs in `fs_depth`
//     (shadow maps) and not only in the lit pass -- for a mesh entity and for a procedural node;
//   * a contracting (Breathing) owner leaves no hole where the depth prepass and the lit pass would
//     disagree: the displacement runs in the prepass's vertex stage too;
//   * the displacement is in the velocity target at the previous frame's time: at the crest of a
//     breath (no motion) the owner writes no velocity, mid-breath it does;
//   * Worley F2 on the GPU is the CPU's, F2 >= F1 everywhere;
//   * a neutral clip (Dissolve at 0, Growth at 1) changes no pixel though its record is live;
//   * every Wave 2 surface type is visible on, and gone off (with AVGEN_EFFECT_DUMP=<dir> every arm
//     is written there as a PNG for a person to look at, including a Dissolve sequence).
//
// Scenes are built directly and the frame block comes from the real builder through a small scene
// query, so the path under test is builder -> scene block -> renderer -> shader, with no engine.

#include "assets/image.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/procedural.hpp"
#include "scene/scene.hpp"
#include "support/image_diff.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/entity_fx.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;
using world::EffectKind;
using world::EffectStatus;

namespace {

constexpr std::uint32_t kWidth = 192;
constexpr std::uint32_t kHeight = 108;

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
};

// Fresh temporal history and drawn twice, so an arm never inherits the previous arm's history.
gpu::Image8 render(rendering::SceneRenderer& renderer, const scene::Scene& s, double seconds = 1.0,
                   std::uint32_t w = kWidth, std::uint32_t h = kHeight) {
    FrameTime t{};
    t.renderTime = seconds;
    renderer.resetTemporalHistory();
    auto first = renderer.renderToImage(s, t, w, h);
    REQUIRE(first.has_value());
    auto img = renderer.renderToImage(s, t, w, h);
    REQUIRE(img.has_value());
    return std::move(*img);
}

void dump(const gpu::Image8& image, const std::string& name) {
    const char* dir = std::getenv("AVGEN_EFFECT_DUMP");
    if (dir == nullptr || dir[0] == '\0') {
        return;
    }
    static_cast<void>(assets::writePng(fs::path(dir) / (name + ".png"), image.width, image.height, image.rgba));
}

float lum(const gpu::Image8& img, std::size_t pixel) {
    const std::size_t i = pixel * 4;
    return (0.2126f * img.rgba[i] + 0.7152f * img.rgba[i + 1] + 0.0722f * img.rgba[i + 2]) / 255.0f;
}

std::size_t differing(const gpu::Image8& a, const gpu::Image8& b, int threshold = 24) {
    REQUIRE(a.rgba.size() == b.rgba.size());
    std::size_t n = 0;
    for (std::size_t i = 0; i + 3 < a.rgba.size(); i += 4) {
        int sum = 0;
        for (int c = 0; c < 3; ++c) {
            sum += std::abs(static_cast<int>(a.rgba[i + c]) - static_cast<int>(b.rgba[i + c]));
        }
        n += sum > threshold ? 1u : 0u;
    }
    return n;
}

// Every named entity is its own node, with a box from its transform; a node may be given a velocity
// (what HIST would report) for Motion Smear.
class EntityScene final : public world::EffectSceneQuery {
public:
    explicit EntityScene(const scene::Scene& s) : scene_(s) {}
    std::map<std::string, glm::vec3, std::less<>> velocities;
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
        // A procedural node: its placement and bounds are whatever the test registered.
        if (const auto it = procedurals_.find(name); it != procedurals_.end()) {
            out = it->second;
            return true;
        }
        return false;
    }
    [[nodiscard]] bool nodeVelocity(std::string_view name, glm::vec3& out) const override {
        const auto it = velocities.find(name);
        if (it == velocities.end()) {
            return false;
        }
        out = it->second;
        return true;
    }
    void addProcedural(const std::string& name, world::NodeView view) { procedurals_[name] = view; }

private:
    const scene::Scene& scene_;
    std::map<std::string, world::NodeView, std::less<>> procedurals_;
};

struct Evaluated {
    std::vector<EffectStatus> status;
    std::vector<std::string> reasons;
};

// The real builder, writing the frame block onto the scene the way `Engine::updateEffects` does.
Evaluated evaluate(scene::Scene& s, const std::vector<world::EffectInstance>& effects, double seconds = 1.0,
                   const EntityScene* custom = nullptr) {
    const EntityScene plain(s);
    const EntityScene& query = custom != nullptr ? *custom : plain;
    world::EffectContext ctx;
    ctx.seconds = seconds;
    ctx.scene = &query;
    ctx.cameraPosition = s.camera.position;
    Evaluated out;
    out.status.assign(effects.size(), EffectStatus::Dormant);
    out.reasons.assign(effects.size(), std::string());
    world::buildEntityFxFrame(effects, ctx, s.entityFx, {}, out.status, out.reasons);
    return out;
}

world::EffectInstance effectOn(EffectKind kind, const std::string& owner, const std::string& id) {
    world::EffectInstance e = world::makeEffect(kind, id);
    e.id = id;
    e.owner = world::EffectOwner::entity(owner);
    e.activation = world::Activation::Always;
    e.timing = world::Timing{};
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    return e;
}

world::EffectInstance dissolve(const std::string& owner, float progress, float edgeEmission = 0.0f) {
    world::EffectInstance e = effectOn(EffectKind::Dissolve, owner, owner + "-dissolve");
    e.values.setFloat("dissolve/progress", progress);
    e.values.setFloat("dissolve/scale", 3.0f);
    e.values.setFloat("dissolve/edgeEmission", edgeEmission);
    return e;
}

// ---- a caster above a floor, with its shadow and its body in separate parts of the frame --------
//
// The layout test_abduction_fade_gpu.cpp uses for the same question: the camera nearly level with the
// caster, so the body is seen against the black background and its shadow on the floor below it,
// and each can be read as a set of pixels of its own.

scene::MeshData boxMesh(glm::vec3 half) {
    scene::MeshData m;
    const glm::vec3 n[6] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    for (const glm::vec3 normal : n) {
        const glm::vec3 u = std::abs(normal.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::cross(glm::vec3(0, 1, 0), normal);
        const glm::vec3 v = glm::cross(normal, u);
        const glm::vec3 c = normal * half;
        const glm::vec3 du = u * half;
        const glm::vec3 dv = v * half;
        const auto base = static_cast<std::uint32_t>(m.vertices.size());
        m.vertices.push_back({c - du - dv, normal, {0, 0}});
        m.vertices.push_back({c + du - dv, normal, {1, 0}});
        m.vertices.push_back({c + du + dv, normal, {1, 1}});
        m.vertices.push_back({c - du + dv, normal, {0, 1}});
        m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return m;
}

// `caster`: 0 none, 1 an entity box, 2 a procedural sphere node.
scene::Scene casterScene(int caster) {
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.showSkybox = false;
    s.environment.environmentIntensity = 0.0f;
    s.camera.position = {0.0f, 6.0f, 18.0f};
    s.camera.target = {0.0f, 4.0f, 0.0f};
    s.camera.fovYRadians = 0.9f;
    s.camera.nearPlane = 0.5f;
    s.camera.farPlane = 120.0f;
    const auto floor = s.addMesh(boxMesh({20.0f, 0.25f, 20.0f}));
    {
        auto& e = s.addEntity("floor", floor);
        e.transform.position = {0.0f, -0.25f, 0.0f};
        e.material.baseColor = glm::vec3(0.8f);
        e.material.roughness = 0.9f;
    }
    if (caster == 1) {
        const auto box = s.addMesh(boxMesh({2.0f, 2.0f, 2.0f}));
        auto& e = s.addEntity("caster", box);
        e.transform.position = {0.0f, 6.0f, 0.0f};
        e.material.baseColor = glm::vec3(0.8f);
        e.material.roughness = 0.9f;
    } else if (caster == 2) {
        scene::ProceduralGeometry g;
        g.name = "caster";
        g.source.kind = scene::PrimitiveKind::Sphere;
        g.source.radius = 2.2f;
        g.source.radialSegments = 48;
        g.source.heightSegments = 24;
        scene::InstanceRecord r{};
        r.position = {0.0f, 6.0f, 0.0f, 1.0f};
        r.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
        r.scale = {1.0f, 1.0f, 1.0f, 0.0f};
        r.color = {1.0f, 1.0f, 1.0f, 0.0f};
        r.emissive = {1.0f, 1.0f, 1.0f, 0.0f};
        g.instances.push_back(r);
        g.structureVersion = 1;
        g.meshHash = 0xD1550ull;
        g.material.baseColor = {0.8f, 0.8f, 0.8f};
        g.material.roughness = 0.9f;
        s.procedurals.push_back(g);
    }
    scene::PunctualLight key;
    key.name = "key";
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::vec3(0.0f, -1.0f, 0.0f);
    key.intensity = 4.0f;
    key.castsShadow = true;
    key.contactShadow = false;
    key.softness = 0.05f;
    s.addLight(key);
    scene::PunctualLight fill;
    fill.name = "fill";
    fill.type = scene::PunctualLight::Type::Directional;
    fill.direction = glm::normalize(glm::vec3(0.0f, -0.15f, -1.0f));
    fill.intensity = 3.0f;
    fill.castsShadow = false;
    fill.contactShadow = false;
    s.addLight(fill);
    return s;
}

world::NodeView proceduralCasterView() {
    world::NodeView v;
    v.world[3] = glm::vec4(0.0f, 6.0f, 0.0f, 1.0f);
    v.boundsMin = glm::vec3(-2.2f, 3.8f, -2.2f);
    v.boundsMax = glm::vec3(2.2f, 8.2f, 2.2f);
    v.hasBounds = true;
    v.firstProcedural = 0;
    v.proceduralCount = 1;
    return v;
}

struct ShadowReading {
    std::size_t shadowPixels = 0; // floor pixels the whole caster shadows
    std::size_t shadowLit = 0;    // ...of which the dissolved caster no longer shadows
    std::size_t bodyPixels = 0;   // pixels the whole caster covers against the background
    std::size_t bodyGone = 0;     // ...of which the dissolved caster no longer covers
};

// Reads the three arms: without the caster, with it whole, with it dissolved.
ShadowReading readShadow(const gpu::Image8& none, const gpu::Image8& whole, const gpu::Image8& dissolved) {
    ShadowReading r;
    for (std::size_t p = 0; p < static_cast<std::size_t>(none.width) * none.height; ++p) {
        const float n = lum(none, p);
        const float w = lum(whole, p);
        const float d = lum(dissolved, p);
        if (n < 0.02f && w > 0.05f) { // the body, against the black background
            ++r.bodyPixels;
            r.bodyGone += d < 0.5f * w ? 1u : 0u;
        } else if (n - w > 0.15f) { // the floor, in the whole caster's shadow
            ++r.shadowPixels;
            r.shadowLit += d > w + 0.5f * (n - w) ? 1u : 0u;
        }
    }
    return r;
}

} // namespace

TEST_CASE("FXL clip: a half-dissolved owner's shadow has holes where the owner does",
          "[gpu][effects][fxl][clip]") {
    Harness h;
    for (const int caster : {1, 2}) {
        const std::string what = caster == 1 ? "entity" : "procedural";
        INFO("caster: " << what);
        constexpr std::uint32_t kW = 384;
        constexpr std::uint32_t kH = 216;
        const gpu::Image8 none = render(h.renderer, casterScene(0), 1.0, kW, kH);
        const gpu::Image8 whole = render(h.renderer, casterScene(caster), 1.0, kW, kH);
        scene::Scene s = casterScene(caster);
        EntityScene q(s);
        q.addProcedural("caster", proceduralCasterView());
        const Evaluated ev = evaluate(s, {dissolve("caster", 0.5f)}, 1.0, &q);
        REQUIRE(ev.status[0] == EffectStatus::Drawn);
        const gpu::Image8 half = render(h.renderer, s, 1.0, kW, kH);
        dump(none, "surface-shadow-" + what + "-none");
        dump(whole, "surface-shadow-" + what + "-whole");
        dump(half, "surface-shadow-" + what + "-dissolved");
        const ShadowReading r = readShadow(none, whole, half);
        const double bodyGone = static_cast<double>(r.bodyGone) / static_cast<double>(std::max<std::size_t>(r.bodyPixels, 1));
        const double shadowLit =
            static_cast<double>(r.shadowLit) / static_cast<double>(std::max<std::size_t>(r.shadowPixels, 1));
        INFO("body " << r.bodyPixels << " px, " << bodyGone * 100.0 << "% gone; shadow " << r.shadowPixels << " px, "
                     << shadowLit * 100.0 << "% lit through");
        REQUIRE(r.bodyPixels > 600);
        REQUIRE(r.shadowPixels > 400);
        // The body is about half gone...
        CHECK(bodyGone > 0.2);
        CHECK(bodyGone < 0.8);
        // ...and so is its shadow: light reaches the floor through the holes. With `fs_depth`'s clip
        // removed this is ~0 (the shadow map still holds the whole caster).
        CHECK(shadowLit > 0.15);
        CHECK(shadowLit < 0.85);
    }
}

namespace {

// A smooth sphere in front of a bright emissive backdrop, black clear colour. A draw the depth test
// wrongly refuses shows as black -- the clear -- which neither the sphere nor the backdrop is.
scene::Scene breathingScene() {
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.showSkybox = false;
    s.environment.environmentIntensity = 0.0f;
    s.camera.position = {0.0f, 3.0f, 10.0f};
    s.camera.target = {0.0f, 3.0f, 0.0f};
    s.camera.fovYRadians = 0.7f;
    const auto wall = s.addMesh(boxMesh({12.0f, 12.0f, 0.1f}));
    {
        auto& e = s.addEntity("wall", wall);
        e.transform.position = {0.0f, 3.0f, -6.0f};
        e.material.baseColor = glm::vec3(0.0f);
        e.material.emissiveColor = glm::vec3(0.1f, 0.9f, 0.2f);
        e.material.emissiveIntensity = 1.0f;
        e.material.unlit = true;
    }
    const auto sphere = s.addMesh(scene::makeIcosphere(2.0f, 4));
    {
        auto& e = s.addEntity("pod", sphere);
        e.transform.position = {0.0f, 3.0f, 0.0f};
        e.material.baseColor = glm::vec3(0.9f, 0.25f, 0.2f);
        e.material.roughness = 0.6f;
    }
    scene::PunctualLight fill;
    fill.type = scene::PunctualLight::Type::Directional;
    fill.direction = glm::normalize(glm::vec3(-0.2f, -0.3f, -1.0f));
    fill.intensity = 3.0f;
    fill.castsShadow = false;
    s.addLight(fill);
    return s;
}

world::EffectInstance breathing(float amplitude) {
    world::EffectInstance e = effectOn(EffectKind::Breathing, "pod", "pod-breath");
    e.values.setFloat("breathing/amplitude", amplitude);
    e.values.setFloat("breathing/rate", 0.25f);
    e.values.setFloat("breathing/asymmetry", 0.0f);
    e.values.setFloat("breathing/regionWidth", 0.0f);
    return e;
}

struct Coverage {
    std::size_t sphere = 0;   // reddish: the sphere
    std::size_t backdrop = 0; // green: the wall
    std::size_t black = 0;    // neither: nothing was drawn there
};

Coverage coverage(const gpu::Image8& img) {
    Coverage c;
    for (std::size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
        const int r = img.rgba[i];
        const int g = img.rgba[i + 1];
        if (r + g + img.rgba[i + 2] < 12) {
            ++c.black;
        } else if (g > r) {
            ++c.backdrop;
        } else {
            ++c.sphere;
        }
    }
    return c;
}

} // namespace

TEST_CASE("FXL displacement runs in the depth prepass: a contracting owner leaves no depth hole",
          "[gpu][effects][fxl][displace]") {
    Harness h;
    const scene::Scene still = breathingScene();
    // t = 2 s at 0.25 Hz is the crest of a breath: the full amplitude.
    const gpu::Image8 whole = render(h.renderer, still, 2.0);
    scene::Scene s = breathingScene();
    const Evaluated ev = evaluate(s, {breathing(-0.6f)}, 2.0);
    REQUIRE(ev.status[0] == EffectStatus::Drawn);
    const gpu::Image8 shrunk = render(h.renderer, s, 2.0);
    dump(whole, "surface-breathing-rest");
    dump(shrunk, "surface-breathing-contracted");
    const Coverage a = coverage(whole);
    const Coverage b = coverage(shrunk);
    INFO("rest: sphere " << a.sphere << ", backdrop " << a.backdrop << ", black " << a.black
                         << "; contracted: sphere " << b.sphere << ", backdrop " << b.backdrop << ", black " << b.black);
    REQUIRE(a.black < 20);
    // The sphere really contracted: radius 2 -> 1.4 is about half the area.
    const double ratio = static_cast<double>(b.sphere) / static_cast<double>(a.sphere);
    CHECK(ratio > 0.35);
    CHECK(ratio < 0.65);
    // And nowhere did the prepass (a bigger sphere) refuse the lit pass: with the displacement missing
    // from the depth-only vertex stage the whole disc goes black.
    CHECK(b.black < 20);
}

namespace {

// `breathingScene`'s layout -- a reddish sphere before a green emissive wall, black clear colour --
// with the sphere either a mesh entity or a procedural node. Every pixel is the sphere's outside,
// its inside, or the wall; black is none of them.
scene::Scene hollowScene(bool procedural) {
    scene::Scene s = breathingScene();
    if (!procedural) {
        return s;
    }
    s.entities.pop_back(); // the entity pod; the same sphere comes back as a procedural node
    scene::ProceduralGeometry g;
    g.name = "pod";
    g.source.kind = scene::PrimitiveKind::Sphere;
    g.source.radius = 2.0f;
    g.source.radialSegments = 48;
    g.source.heightSegments = 24;
    scene::InstanceRecord r{};
    r.position = {0.0f, 3.0f, 0.0f, 1.0f};
    r.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    r.scale = {1.0f, 1.0f, 1.0f, 0.0f};
    r.color = {1.0f, 1.0f, 1.0f, 0.0f};
    r.emissive = {1.0f, 1.0f, 1.0f, 0.0f};
    g.instances.push_back(r);
    g.structureVersion = 1;
    g.meshHash = 0x401107ull;
    g.material.baseColor = {0.9f, 0.25f, 0.2f};
    g.material.roughness = 0.6f;
    s.procedurals.push_back(g);
    return s;
}

world::NodeView hollowProceduralView() {
    world::NodeView v;
    v.world[3] = glm::vec4(0.0f, 3.0f, 0.0f, 1.0f);
    v.boundsMin = glm::vec3(-2.0f, 1.0f, -2.0f);
    v.boundsMax = glm::vec3(2.0f, 5.0f, 2.0f);
    v.hasBounds = true;
    v.firstProcedural = 0;
    v.proceduralCount = 1;
    return v;
}

} // namespace

TEST_CASE("FXL clip: through a dissolving hollow owner's holes its inside is shaded, not black",
          "[gpu][effects][fxl][clip][interior]") {
    // The depth prepass never culls, so where the clip removes a front face the prepass keeps the
    // back face behind it. A lit pass that culls back faces then draws nothing there, the depth test
    // refuses everything behind, and the pixel keeps the clear colour: the black inside of the
    // dissolving saucer. A live clip draws its owner two-sided -- mesh entities and procedural nodes
    // alike -- so the inside is shaded, with the flipped normal `shadeSurface` gives a back face.
    Harness h;
    for (const bool procedural : {false, true}) {
        const std::string what = procedural ? "procedural" : "entity";
        INFO("owner: " << what);
        const gpu::Image8 whole = render(h.renderer, hollowScene(procedural));
        scene::Scene s = hollowScene(procedural);
        EntityScene q(s);
        q.addProcedural("pod", hollowProceduralView());
        // No edge glow: the inside must be visible by the surface's own shading, not painted by it.
        world::EffectInstance e = effectOn(EffectKind::Dissolve, "pod", "pod-dissolve");
        e.values.setFloat("dissolve/progress", 0.45f);
        e.values.setFloat("dissolve/scale", 2.0f);
        e.values.setFloat("dissolve/edgeEmission", 0.0f);
        const Evaluated ev = evaluate(s, {e}, 1.0, &q);
        INFO(ev.reasons[0]);
        REQUIRE(ev.status[0] == EffectStatus::Drawn);
        const gpu::Image8 open = render(h.renderer, s);
        dump(whole, "surface-hollow-" + what + "-whole");
        dump(open, "surface-hollow-" + what + "-dissolved");
        const Coverage a = coverage(whole);
        const Coverage b = coverage(open);
        INFO("whole: sphere " << a.sphere << ", wall " << a.backdrop << ", black " << a.black
                              << "; dissolved: sphere " << b.sphere << ", wall " << b.backdrop << ", black "
                              << b.black);
        REQUIRE(a.black < 20);
        REQUIRE(a.sphere > 1500);
        // The clip really opened holes: the wall shows through where both walls are gone.
        CHECK(b.backdrop > a.backdrop + 100);
        // And where only the front wall is gone, the inside is drawn. With the back faces culled
        // (the pre-fix procedural path) the inside shows the clear colour.
        CHECK(b.black < 20);
    }
}

TEST_CASE("FXL displacement is in the velocity target at the previous frame's time",
          "[gpu][effects][fxl][displace]") {
    Harness h;
    // Two consecutive frames, one Scene object (a different address is a scene swap and drops the
    // renderer's history), through the velocity view.
    const auto velocityAt = [&](double t0, bool breathe) {
        scene::Scene s = breathingScene();
        const std::vector<world::EffectInstance> fx = breathe ? std::vector{breathing(0.5f)}
                                                              : std::vector<world::EffectInstance>{};
        h.renderer.setAuxDebugView(rendering::AuxDebugView::None);
        h.renderer.resetTemporalHistory();
        static_cast<void>(evaluate(s, fx, t0));
        FrameTime t{};
        t.renderTime = t0;
        REQUIRE(h.renderer.renderToImage(s, t, kWidth, kHeight).has_value());
        static_cast<void>(evaluate(s, fx, t0 + 1.0 / 30.0));
        h.renderer.setAuxDebugView(rendering::AuxDebugView::Velocity);
        h.renderer.setAuxDebugScale(400.0f);
        t.renderTime = t0 + 1.0 / 30.0;
        auto img = h.renderer.renderToImage(s, t, kWidth, kHeight);
        REQUIRE(img.has_value());
        h.renderer.setAuxDebugView(rendering::AuxDebugView::None);
        h.renderer.setAuxDebugScale(0.0f);
        return std::move(*img);
    };
    const auto moving = [](const gpu::Image8& img) {
        std::size_t n = 0;
        for (std::size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
            n += std::abs(img.rgba[i] - 128) + std::abs(img.rgba[i + 1] - 128) > 16 ? 1u : 0u;
        }
        return n;
    };
    // Mid-inhale (t = 1 s: a quarter of the cycle, the steepest part of the swell).
    const gpu::Image8 still = velocityAt(1.0, false);
    const gpu::Image8 swelling = velocityAt(1.0 - 1.0 / 60.0, true);
    // Across the crest (t = 2 s): the surface is where it was a frame ago, so no velocity -- unless
    // the previous-frame position were undisplaced, which would read as the whole amplitude of motion.
    const gpu::Image8 crest = velocityAt(2.0 - 1.0 / 60.0, true);
    dump(still, "surface-velocity-still");
    dump(swelling, "surface-velocity-swelling");
    dump(crest, "surface-velocity-crest");
    INFO("pixels with visible velocity: still " << moving(still) << ", swelling " << moving(swelling) << ", crest "
                                                << moving(crest));
    CHECK(moving(still) < 20);
    CHECK(moving(swelling) > 300);
    CHECK(moving(crest) < 60);
}

TEST_CASE("FXL clip: a neutral clip -- Dissolve at 0, Growth at 1 -- changes no pixel",
          "[gpu][effects][fxl][gate]") {
    Harness h;
    const gpu::Image8 base = render(h.renderer, casterScene(1));
    SECTION("Dissolve at progress 0") {
        scene::Scene s = casterScene(1);
        REQUIRE(evaluate(s, {dissolve("caster", 0.0f, 20.0f)}).status[0] == EffectStatus::Drawn);
        REQUIRE(s.entityFx.recordFor(1) != 0);
        const auto d = testing::byteDiff(base.rgba, render(h.renderer, s).rgba);
        INFO(d.describe());
        CHECK(d.identical());
    }
    SECTION("Growth at progress 1") {
        scene::Scene s = casterScene(1);
        world::EffectInstance g = effectOn(EffectKind::Growth, "caster", "grow");
        g.values.setFloat("growth/progress", 1.0f);
        REQUIRE(evaluate(s, {g}).status[0] == EffectStatus::Drawn);
        const auto d = testing::byteDiff(base.rgba, render(h.renderer, s).rgba);
        INFO(d.describe());
        CHECK(d.identical());
    }
    SECTION("the control: Dissolve at 0.5 does change the frame") {
        scene::Scene s = casterScene(1);
        static_cast<void>(evaluate(s, {dissolve("caster", 0.5f)}));
        CHECK(differing(base, render(h.renderer, s)) > 200);
    }
}

namespace {

// Two orbs over a ground plane at night-ish light: every surface type goes on orb A, orb B is the
// untouched reference in the same frame.
scene::Scene orbs() {
    scene::Scene s;
    const scene::MeshId plane = s.addMesh(scene::makePlane(30.0f, 1));
    const scene::MeshId sphere = s.addMesh(scene::makeIcosphere(1.0f, 4));
    scene::Entity& ground = s.addEntity("ground", plane);
    ground.material.baseColor = glm::vec3(0.35f, 0.36f, 0.4f);
    ground.material.roughness = 0.9f;
    for (const auto& [name, x] : {std::pair<const char*, float>{"orbA", -1.6f}, {"orbB", 1.6f}}) {
        scene::Entity& orb = s.addEntity(name, sphere);
        orb.transform.position = glm::vec3(x, 1.3f, 0.0f);
        orb.material.baseColor = glm::vec3(0.42f, 0.3f, 0.55f);
        orb.material.roughness = 0.55f;
        orb.material.emissiveColor = glm::vec3(0.3f, 0.5f, 1.0f);
        orb.material.emissiveIntensity = 0.05f;
    }
    scene::PunctualLight key;
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.5f));
    key.intensity = 0.8f;
    key.castsShadow = true;
    s.addLight(key);
    s.camera.position = glm::vec3(0.0f, 2.2f, 6.5f);
    s.camera.target = glm::vec3(0.0f, 1.2f, 0.0f);
    s.camera.fovYRadians = glm::radians(45.0f);
    s.environment.backgroundColor = glm::vec3(0.01f, 0.012f, 0.03f);
    return s;
}

} // namespace

TEST_CASE("FXL Wave 2: every surface type is visible on and gone off", "[gpu][effects][fxl][surface]") {
    Harness h;
    constexpr std::uint32_t kW = 384;
    constexpr std::uint32_t kH = 216;
    const gpu::Image8 off = render(h.renderer, orbs(), 1.0, kW, kH);
    dump(off, "surface-off");
    struct Arm {
        EffectKind kind;
        const char* name;
        double at;
    };
    const Arm arms[] = {
        {EffectKind::Dissolve, "dissolve", 1.0},          {EffectKind::Growth, "growth", 1.0},
        {EffectKind::Breathing, "breathing", 2.0},        {EffectKind::OrganicPulsation, "organic-pulsation", 1.3},
        {EffectKind::Bioluminescence, "bioluminescence", 1.0}, {EffectKind::PulsingVeins, "pulsing-veins", 1.0},
        {EffectKind::Fresnel, "fresnel", 1.0},            {EffectKind::RimLight, "rim-light", 1.0},
        {EffectKind::ColorCycling, "color-cycling", 1.0}, {EffectKind::MotionSmear, "motion-smear", 1.0},
    };
    for (const Arm& arm : arms) {
        INFO(arm.name);
        scene::Scene s = orbs();
        EntityScene q(s);
        q.velocities["orbA"] = glm::vec3(18.0f, 0.0f, 0.0f); // Motion Smear's owner is dashing right
        world::EffectInstance e = effectOn(arm.kind, "orbA", arm.name);
        if (arm.kind == EffectKind::Breathing) {
            e.values.setFloat("breathing/amplitude", 0.25f); // exaggerated so the arm reads at 384 px
            e.values.setFloat("breathing/rate", 0.25f);
            e.values.setFloat("breathing/asymmetry", 0.0f);
        }
        if (arm.kind == EffectKind::OrganicPulsation) {
            e.values.setFloat("organicPulsation/amplitude", 0.25f);
        }
        const Evaluated ev = evaluate(s, {e}, arm.at, &q);
        INFO(ev.reasons[0]);
        REQUIRE(ev.status[0] == EffectStatus::Drawn);
        const gpu::Image8 on = render(h.renderer, s, arm.at, kW, kH);
        const gpu::Image8 reference = render(h.renderer, orbs(), arm.at, kW, kH);
        dump(on, std::string("surface-") + arm.name);
        CHECK(differing(reference, on) > 150);
        // Only orb A (the left half, and its shadow) may change: orb B is untouched.
        std::size_t rightChanged = 0;
        for (std::uint32_t y = 0; y < kH; ++y) {
            for (std::uint32_t x = kW / 2 + kW / 16; x < kW; ++x) {
                const std::size_t i = (static_cast<std::size_t>(y) * kW + x) * 4;
                int sum = 0;
                for (int c = 0; c < 3; ++c) {
                    sum += std::abs(static_cast<int>(on.rgba[i + c]) - static_cast<int>(reference.rgba[i + c]));
                }
                rightChanged += sum > 24 ? 1u : 0u;
            }
        }
        CHECK(rightChanged < 40);
    }
    // The Dissolve sequence, for a person to look at: whole, a quarter, half, three quarters, gone.
    for (const float p : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        scene::Scene s = orbs();
        world::EffectInstance e = effectOn(EffectKind::Dissolve, "orbA", "seq");
        e.values.setFloat("dissolve/progress", p);
        static_cast<void>(evaluate(s, {e}));
        dump(render(h.renderer, s, 1.0, kW, kH), "surface-dissolve-seq-" + std::to_string(static_cast<int>(p * 100.0f)));
    }
    // The Growth sequence likewise.
    for (const float p : {0.2f, 0.5f, 0.8f}) {
        scene::Scene s = orbs();
        world::EffectInstance e = effectOn(EffectKind::Growth, "orbA", "seq");
        e.values.setFloat("growth/progress", p);
        static_cast<void>(evaluate(s, {e}));
        dump(render(h.renderer, s, 1.0, kW, kH), "surface-growth-seq-" + std::to_string(static_cast<int>(p * 100.0f)));
    }
}

TEST_CASE("FXL Wave 2 stacks: Breathing, Organic Pulsation and Bioluminescence on one owner all act",
          "[gpu][effects][fxl][surface]") {
    Harness h;
    scene::Scene s = orbs();
    world::EffectInstance b = effectOn(EffectKind::Breathing, "orbA", "b");
    b.values.setFloat("breathing/amplitude", 0.2f);
    world::EffectInstance o = effectOn(EffectKind::OrganicPulsation, "orbA", "o");
    world::EffectInstance l = effectOn(EffectKind::Bioluminescence, "orbA", "l");
    const Evaluated ev = evaluate(s, {b, o, l}, 2.0);
    CHECK(ev.status[0] == EffectStatus::Drawn);
    CHECK(ev.status[1] == EffectStatus::Drawn);
    CHECK(ev.status[2] == EffectStatus::Drawn);
    const std::uint32_t r = s.entityFx.recordFor(1);
    REQUIRE(r != 0);
    const auto flags = static_cast<std::uint32_t>(s.entityFx.records[r].lanes[world::kFxLaneA].z + 0.5f);
    CHECK((flags & world::kFxInflate) != 0);
    CHECK((flags & world::kFxTravel) != 0);
    CHECK((flags & world::kFxBio) != 0);
    const gpu::Image8 on = render(h.renderer, s, 2.0);
    dump(on, "surface-stack");
    CHECK(differing(render(h.renderer, orbs(), 2.0), on) > 100);
}

// ---- Worley F2 on the GPU is the CPU's -----------------------------------------------------------

namespace {

constexpr const char* kWorleyKernel = R"(
@group(0) @binding(0) var<storage, read> samples: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> results: array<vec4<f32>>;

@compute @workgroup_size(64)
fn cs_worley(@builtin(global_invocation_id) gid: vec3<u32>) {
    let i = gid.x;
    if (i >= arrayLength(&samples)) { return; }
    let w = worleyF1F2(samples[i].xyz, 23u);
    results[i] = vec4<f32>(w, voronoiF1(samples[i].xyz, 23u));
}
)";

} // namespace

TEST_CASE("Worley F2: the GPU's F1, F2 and cell hash are the CPU's, and F2 >= F1", "[gpu][effects][fxl][worley]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    const auto noise = shaders.loadSource("noise.wgsl");
    REQUIRE(noise.has_value());
    auto module = shaders.compile(*noise + "\n" + kWorleyKernel, "worley-parity");
    INFO((module ? std::string() : module.error().message));
    REQUIRE(module.has_value());
    const auto& device = ctx->device();
    std::array<wgpu::BindGroupLayoutEntry, 2> layoutEntries{};
    layoutEntries[0].binding = 0;
    layoutEntries[0].visibility = wgpu::ShaderStage::Compute;
    layoutEntries[0].buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
    layoutEntries[1].binding = 1;
    layoutEntries[1].visibility = wgpu::ShaderStage::Compute;
    layoutEntries[1].buffer.type = wgpu::BufferBindingType::Storage;
    wgpu::BindGroupLayoutDescriptor ldesc{};
    ldesc.entryCount = layoutEntries.size();
    ldesc.entries = layoutEntries.data();
    const wgpu::BindGroupLayout layout = device.CreateBindGroupLayout(&ldesc);
    wgpu::PipelineLayoutDescriptor pldesc{};
    pldesc.bindGroupLayoutCount = 1;
    pldesc.bindGroupLayouts = &layout;

    std::vector<glm::vec4> samples;
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(-20.0f, 20.0f);
    for (int i = 0; i < 4096; ++i) {
        samples.emplace_back(u(rng), u(rng), u(rng), 0.0f);
    }
    const std::size_t bytes = samples.size() * sizeof(glm::vec4);
    wgpu::BufferDescriptor in{};
    in.size = bytes;
    in.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;
    const wgpu::Buffer inBuf = device.CreateBuffer(&in);
    ctx->queue().WriteBuffer(inBuf, 0, samples.data(), bytes);
    wgpu::BufferDescriptor out{};
    out.size = bytes;
    out.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc;
    const wgpu::Buffer outBuf = device.CreateBuffer(&out);

    wgpu::ComputePipelineDescriptor pd{};
    pd.layout = device.CreatePipelineLayout(&pldesc);
    pd.compute.module = *module;
    pd.compute.entryPoint = "cs_worley";
    const wgpu::ComputePipeline pipeline = device.CreateComputePipeline(&pd);
    REQUIRE(pipeline);
    std::array<wgpu::BindGroupEntry, 2> entries{};
    entries[0].binding = 0;
    entries[0].buffer = inBuf;
    entries[0].size = bytes;
    entries[1].binding = 1;
    entries[1].buffer = outBuf;
    entries[1].size = bytes;
    wgpu::BindGroupDescriptor bg{};
    bg.layout = layout;
    bg.entryCount = entries.size();
    bg.entries = entries.data();
    const wgpu::BindGroup group = device.CreateBindGroup(&bg);
    const wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
    {
        const wgpu::ComputePassEncoder pass = encoder.BeginComputePass();
        pass.SetPipeline(pipeline);
        pass.SetBindGroup(0, group);
        pass.DispatchWorkgroups(static_cast<std::uint32_t>((samples.size() + 63) / 64));
        pass.End();
    }
    const wgpu::CommandBuffer commands = encoder.Finish();
    ctx->queue().Submit(1, &commands);
    auto read = gpu::readBuffer(*ctx, outBuf, 0, bytes);
    REQUIRE(read.has_value());
    std::vector<glm::vec4> gpuOut(samples.size());
    std::memcpy(gpuOut.data(), read->data(), bytes);

    float worstF1 = 0.0f;
    float worstF2 = 0.0f;
    std::size_t hashDisagree = 0;
    std::size_t orderViolations = 0;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const glm::vec3 cpu = world::worleyF1F2(glm::vec3(samples[i]), 23u);
        worstF1 = std::max(worstF1, std::abs(cpu.x - gpuOut[i].x));
        worstF2 = std::max(worstF2, std::abs(cpu.y - gpuOut[i].y));
        // The nearest cell can only disagree where two points are within rounding of each other.
        hashDisagree += (std::abs(cpu.z - gpuOut[i].z) > 1e-6f && std::abs(cpu.y - cpu.x) > 1e-4f) ? 1u : 0u;
        orderViolations += gpuOut[i].y + 1e-6f < gpuOut[i].x ? 1u : 0u;
        // F1 is voronoiF1's, the function this extends.
        CHECK(std::abs(gpuOut[i].x - gpuOut[i].w) < 1e-6f);
    }
    INFO("worst |F1 cpu - gpu| " << worstF1 << ", worst |F2 cpu - gpu| " << worstF2 << ", hash disagreements "
                                 << hashDisagree);
    CHECK(worstF1 < 1e-4f);
    CHECK(worstF2 < 1e-4f);
    CHECK(hashDisagree == 0);
    CHECK(orderViolations == 0);
}
