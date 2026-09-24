// DF on pixels (Effect Library Wave 1, roadmap 1.7): the distortion framework's passes and Space
// Warp's producer, asked the questions only a frame can answer.
//
// The fixture is deliberately plain: an unlit black-and-white checker wall 30 m down the view axis,
// so a bend is unambiguous, and -- where the question needs one -- a saturated object in front of or
// at the warp, whose colour appears nowhere else, so "did it leak" is a colour count rather than a
// judgement. Bloom is off in the fixture unless a case is about bloom, so a change stays where it was
// made and "nothing outside the field moved" can be asked byte for byte.
//
// With AVGEN_EFFECT_DUMP=<dir> each case writes its arms as PNGs for a person to look at.

#include "app/engine.hpp"
#include "assets/image.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/distortion_renderer.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"
#include "world/effects/distortion_frame.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"

#include <glm/gtc/quaternion.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kWidth = 256;
constexpr std::uint32_t kHeight = 160;

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

void initialise(rendering::SceneRenderer& renderer) {
    auto r = renderer.init();
    INFO((r ? std::string("ok") : r.error().message));
    REQUIRE(r.has_value());
}

// ---- the fixture -----------------------------------------------------------------------------------

scene::TextureData checker() {
    scene::TextureData t;
    t.name = "df-checker";
    t.width = t.height = 256;
    t.data.resize(static_cast<std::size_t>(t.width) * t.height * 4);
    for (std::uint32_t y = 0; y < t.height; ++y) {
        for (std::uint32_t x = 0; x < t.width; ++x) {
            const bool on = ((x / 16) + (y / 16)) % 2 == 0;
            const std::uint8_t v = on ? 230 : 20;
            const std::size_t i = (static_cast<std::size_t>(y) * t.width + x) * 4;
            t.data[i] = v;
            t.data[i + 1] = v;
            t.data[i + 2] = v;
            t.data[i + 3] = 255;
        }
    }
    return t;
}

scene::Material unlit(glm::vec3 colour) {
    scene::Material m;
    m.baseColor = colour;
    m.unlit = true;
    m.emissiveIntensity = 0.0f;
    return m;
}

// Camera at the origin looking down -Z; the checker wall at z = -30 facing it.
scene::Scene wallScene() {
    scene::Scene s;
    const scene::MeshId plane = s.addMesh(scene::makePlane(30.0f, 1));
    scene::Entity& wall = s.addEntity("wall", plane);
    wall.transform.position = glm::vec3(0.0f, 0.0f, -30.0f);
    wall.transform.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    wall.material = unlit(glm::vec3(1.0f));
    const scene::TextureId tex = s.addTexture(checker());
    s.entities[0].material.baseColorTexture.texture = tex;
    s.entities[0].material.baseColorTexture.linearFilter = false;
    s.camera.position = glm::vec3(0.0f);
    s.camera.target = glm::vec3(0.0f, 0.0f, -1.0f);
    s.camera.fovYRadians = glm::radians(50.0f);
    s.camera.lens.useExplicitFov = true;
    s.camera.nearPlane = 0.1f;
    s.camera.farPlane = 200.0f;
    s.environment.backgroundColor = glm::vec3(0.02f, 0.03f, 0.05f);
    s.post.bloomEnabled = false;
    return s;
}

// A saturated, unlit sphere or cube: its colour occurs nowhere else in the frame.
void addBody(scene::Scene& s, const char* name, scene::MeshData mesh, glm::vec3 at, glm::vec3 colour) {
    const scene::MeshId id = s.addMesh(std::move(mesh));
    scene::Entity& e = s.addEntity(name, id);
    e.transform.position = at;
    e.material = unlit(colour);
}

// A World-owned warp, resolved through the real producer and builder.
world::EffectInstance warpInstance(const std::string& id, glm::vec3 at, float radius, float strength) {
    world::EffectInstance e = world::makeEffect(world::EffectKind::SpaceWarp, id);
    e.id = id;
    e.activation = world::Activation::Always;
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    const auto set = [&](const char* leaf, float v) { e.values.setFloat(std::string("spaceWarp/") + leaf, v); };
    set("offsetX", at.x);
    set("offsetY", at.y);
    set("offsetZ", at.z);
    set("radius", radius);
    set("strength", strength);
    set("radialWeight", 1.0f);
    set("bowWeight", 0.0f);
    set("swirl", 0.0f);
    set("turbulence", 0.0f);
    set("chroma", 0.0f);
    set("rimIntensity", 0.0f);
    return e;
}

class OwnerScene final : public world::EffectSceneQuery {
public:
    glm::vec3 centre{0.0f};
    glm::vec3 half{1.0f};
    [[nodiscard]] bool nodePosition(std::string_view, glm::vec3& out) const override {
        out = centre;
        return true;
    }
    [[nodiscard]] bool nodeView(std::string_view, world::NodeView& out) const override {
        out = world::NodeView{};
        out.boundsMin = centre - half;
        out.boundsMax = centre + half;
        out.hasBounds = true;
        out.entityCount = 1;
        return true;
    }
};

world::DistortionFrame frameOf(const std::vector<world::EffectInstance>& effects, double seconds = 1.0,
                               const world::EffectSceneQuery* query = nullptr) {
    world::EffectContext ctx;
    ctx.seconds = seconds;
    ctx.scene = query;
    std::vector<std::uint32_t> order;
    world::effectEvaluationOrder(effects, order);
    std::vector<world::EffectStatus> status(effects.size());
    std::vector<std::string> reasons(effects.size());
    world::DistortionFrame frame;
    world::buildDistortionFrame(effects, ctx, frame, order, status, reasons);
    return frame;
}

gpu::Image8 render(rendering::SceneRenderer& renderer, const scene::Scene& s, double seconds = 1.0) {
    FrameTime t{};
    t.renderTime = seconds;
    renderer.resetTemporalHistory();
    auto first = renderer.renderToImage(s, t, kWidth, kHeight);
    REQUIRE(first.has_value());
    auto img = renderer.renderToImage(s, t, kWidth, kHeight);
    REQUIRE(img.has_value());
    return std::move(*img);
}

void dump(const gpu::Image8& image, const std::string& name) {
    const char* dir = std::getenv("AVGEN_EFFECT_DUMP");
    if (dir == nullptr || dir[0] == '\0') {
        return;
    }
    static_cast<void>(assets::writePng(fs::path(dir) / ("df-" + name + ".png"), image.width, image.height, image.rgba));
}

struct Px {
    int r, g, b;
};
Px at(const gpu::Image8& img, int x, int y) {
    const std::size_t i = (static_cast<std::size_t>(y) * img.width + static_cast<std::size_t>(x)) * 4;
    return {img.rgba[i], img.rgba[i + 1], img.rgba[i + 2]};
}
bool same(const gpu::Image8& a, const gpu::Image8& b, int x, int y) {
    const Px p = at(a, x, y);
    const Px q = at(b, x, y);
    return p.r == q.r && p.g == q.g && p.b == q.b;
}
// Visibly different: summed over three channels by more than 24 of 765 (as the effect stack test).
bool differs(const gpu::Image8& a, const gpu::Image8& b, int x, int y) {
    const Px p = at(a, x, y);
    const Px q = at(b, x, y);
    return std::abs(p.r - q.r) + std::abs(p.g - q.g) + std::abs(p.b - q.b) > 24;
}
// Saturated enough that no blend of the grey checker can produce it (the tone map lifts the dark
// channels of a pure primary, so "the others are low" is measured against the strong one).
bool isRed(Px p) { return p.r > 120 && p.r > p.g + 70 && p.r > p.b + 70; }
bool isGreen(Px p) { return p.g > 120 && p.g > p.r + 70 && p.g > p.b + 70; }

// Where a world point lands, in pixels, and how many pixels a world length at it spans.
glm::vec2 project(const scene::Scene& s, glm::vec3 p) {
    const glm::mat4 vp = s.camera.projection(static_cast<float>(kWidth) / static_cast<float>(kHeight)) * s.camera.view();
    const glm::vec4 c = vp * glm::vec4(p, 1.0f);
    const glm::vec2 ndc = glm::vec2(c) / c.w;
    return {(ndc.x * 0.5f + 0.5f) * kWidth, (0.5f - ndc.y * 0.5f) * kHeight};
}

} // namespace

TEST_CASE("DF: a checker behind a Space Warp is bent, and nothing outside the field moves",
          "[gpu][effects][distortion]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    initialise(renderer);

    scene::Scene s = wallScene();
    const gpu::Image8 plain = render(renderer, s);
    CHECK_FALSE(renderer.distortionStats().encoded);
    const glm::vec3 centre(0.0f, 0.0f, -15.0f);
    s.distortion = frameOf({warpInstance("w", centre, 6.0f, 1.0f)});
    REQUIRE(s.distortion.count == 1);
    const gpu::Image8 warped = render(renderer, s);
    CHECK(renderer.distortionStats().encoded);
    CHECK(renderer.distortionStats().proxies == 1);
    CHECK(ctx->errorCount() == 0);
    dump(plain, "checker-off");
    dump(warped, "checker-on");

    const glm::vec2 c = project(s, centre);
    // The silhouette of a 6 m sphere 15 m away subtends asin(6/15), a little more than its projected
    // radius; the margin covers that and the resolve's AA neighbourhood.
    const float rPx = glm::length(project(s, centre + glm::vec3(6.0f, 0.0f, 0.0f)) - c) * 1.15f + 4.0f;
    std::size_t inside = 0;
    std::size_t outside = 0;
    for (int y = 0; y < static_cast<int>(kHeight); ++y) {
        for (int x = 0; x < static_cast<int>(kWidth); ++x) {
            const float d = glm::length(glm::vec2(x + 0.5f, y + 0.5f) - c);
            if (d > rPx) {
                outside += same(plain, warped, x, y) ? 0u : 1u;
            } else if (differs(plain, warped, x, y)) {
                ++inside;
            }
        }
    }
    INFO("bent pixels inside the field " << inside << ", changed pixels outside it " << outside);
    CHECK(inside > 800);
    CHECK(outside == 0);
}

TEST_CASE("DF: a cube in front of the warp is not bent, and never leaks into the bend (Sousa's mask)",
          "[gpu][effects][distortion]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    initialise(renderer);

    scene::Scene s = wallScene();
    // A green cube 8 m away, well in front of the lens plane at 15 m, sitting across the field's
    // strongest band. Both rules get a case: background pixels to its right pull toward the field's
    // centre -- onto the cube (the leak mask) -- and the cube's own left edge would pull toward the
    // centre onto the background (the pixel rule: nothing nearer than the lens is bent).
    addBody(s, "cube", scene::makeCube(0.8f), glm::vec3(1.8f, 0.0f, -8.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const gpu::Image8 plain = render(renderer, s);
    s.distortion = frameOf({warpInstance("w", glm::vec3(0.0f, 0.0f, -15.0f), 6.0f, 1.4f)});
    const gpu::Image8 warped = render(renderer, s);
    dump(plain, "cube-off");
    dump(warped, "cube-on");

    std::size_t cubePixels = 0;
    std::size_t cubeChanged = 0;
    std::size_t leaked = 0;
    std::size_t bent = 0;
    for (int y = 0; y < static_cast<int>(kHeight); ++y) {
        for (int x = 0; x < static_cast<int>(kWidth); ++x) {
            const bool cube = isGreen(at(plain, x, y));
            if (cube) {
                ++cubePixels;
                cubeChanged += same(plain, warped, x, y) ? 0u : 1u;
            } else {
                leaked += isGreen(at(warped, x, y)) ? 1u : 0u;
                bent += differs(plain, warped, x, y) ? 1u : 0u;
            }
        }
    }
    INFO("cube pixels " << cubePixels << ", of them changed " << cubeChanged << "; green pixels outside the "
         "cube after the warp " << leaked << "; bent background pixels " << bent);
    REQUIRE(cubePixels > 200);
    REQUIRE(bent > 500); // the control: the warp is doing something around the cube
    CHECK(cubeChanged == 0);
    CHECK(leaked == 0);
}

TEST_CASE("DF: the owner of an entity warp stays crisp while what is behind it bends (self-exclusion)",
          "[gpu][effects][distortion]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    initialise(renderer);

    // A red SAUCER -- wide and flat -- seen from above, the shape the rule exists for: the far half of
    // its top is BEHIND its centre, so a lens plane at the centre alone would bend the owner's own
    // back. The exclusion radius (the bounds' half-diagonal) puts the lens plane behind all of it.
    scene::Scene s = wallScene();
    s.camera.position = glm::vec3(0.0f, 7.0f, 0.0f);
    const glm::vec3 centre(0.0f, 0.0f, -15.0f);
    s.camera.target = centre;
    const glm::vec3 half(3.5f, 0.35f, 3.5f);
    addBody(s, "owner", scene::makeCube(1.0f), centre, glm::vec3(1.0f, 0.0f, 0.0f));
    s.entities.back().transform.scale = half;
    const gpu::Image8 plain = render(renderer, s);

    // Owned by the red saucer: fitted to its bounds, its exclusion radius the bounds' half-diagonal.
    OwnerScene owner;
    owner.centre = centre;
    owner.half = half;
    world::EffectInstance warp = warpInstance("owned", glm::vec3(0.0f), 1.0f, 1.4f);
    warp.owner = world::EffectOwner::entity("owner");
    warp.values.setFloat("spaceWarp/boundsScale", 2.4f);
    s.distortion = frameOf({warp}, 1.0, &owner);
    REQUIRE(s.distortion.count == 1);
    const gpu::Image8 warped = render(renderer, s);
    dump(plain, "owner-off");
    dump(warped, "owner-on");

    std::size_t ownerPixels = 0;
    std::size_t ownerChanged = 0;
    std::size_t smeared = 0;
    std::size_t bent = 0;
    for (int y = 0; y < static_cast<int>(kHeight); ++y) {
        for (int x = 0; x < static_cast<int>(kWidth); ++x) {
            if (isRed(at(plain, x, y))) {
                ++ownerPixels;
                ownerChanged += same(plain, warped, x, y) ? 0u : 1u;
            } else {
                smeared += isRed(at(warped, x, y)) ? 1u : 0u;
                bent += differs(plain, warped, x, y) ? 1u : 0u;
            }
        }
    }
    INFO("owner pixels " << ownerPixels << ", changed " << ownerChanged << "; red outside the owner "
         << smeared << "; bent background " << bent);
    REQUIRE(ownerPixels > 300);
    REQUIRE(bent > 500);
    CHECK(ownerChanged == 0);
    CHECK(smeared == 0);
}

TEST_CASE("DF: with no producer the frame is byte-identical, before and after a warped frame",
          "[gpu][effects][distortion][gate]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});

    scene::Scene s = wallScene();
    addBody(s, "cube", scene::makeCube(1.5f), glm::vec3(-2.0f, 1.0f, -12.0f), glm::vec3(0.2f, 0.4f, 1.0f));
    s.post.bloomEnabled = true; // the whole post chain, as a shipping frame has it

    // A renderer that never sees a distortion.
    rendering::SceneRenderer fresh(*ctx, shaders);
    initialise(fresh);
    const gpu::Image8 reference = render(fresh, s);
    CHECK_FALSE(fresh.distortionStats().encoded);

    // One that renders plain, then warped, then plain again.
    rendering::SceneRenderer used(*ctx, shaders);
    initialise(used);
    const gpu::Image8 before = render(used, s);
    scene::Scene warpedScene = s;
    warpedScene.distortion = frameOf({warpInstance("w", glm::vec3(0.0f, 0.0f, -15.0f), 6.0f, 1.0f)});
    const gpu::Image8 warped = render(used, warpedScene);
    REQUIRE(used.distortionStats().encoded);
    const gpu::Image8 after = render(used, s);
    CHECK_FALSE(used.distortionStats().encoded);

    // Compared as booleans: a failing `==` over two images prints every byte (ADR-362's killed run).
    REQUIRE(reference.rgba.size() == before.rgba.size());
    const bool beforeIdentical = before.rgba == reference.rgba;
    const bool afterIdentical = after.rgba == reference.rgba;
    const bool warpedDiffers = warped.rgba != reference.rgba;
    CHECK(beforeIdentical);
    CHECK(afterIdentical);
    CHECK(warpedDiffers); // the control: the warped frame really was different
}

TEST_CASE("DF: two overlapping warps superpose rather than one replacing the other",
          "[gpu][effects][distortion]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    initialise(renderer);

    scene::Scene s = wallScene();
    const glm::vec3 ca(-3.0f, 0.0f, -15.0f);
    const glm::vec3 cb(3.0f, 0.0f, -15.0f);
    const world::EffectInstance a = warpInstance("a", ca, 5.0f, 1.0f);
    world::EffectInstance b = warpInstance("b", cb, 5.0f, 1.0f);
    b.values.setFloat("spaceWarp/swirl", 1.0f); // a different field, so "B alone" is not "A mirrored"

    const gpu::Image8 none = render(renderer, s);
    s.distortion = frameOf({a});
    const gpu::Image8 onlyA = render(renderer, s);
    s.distortion = frameOf({b});
    const gpu::Image8 onlyB = render(renderer, s);
    s.distortion = frameOf({a, b});
    REQUIRE(s.distortion.count == 2);
    const gpu::Image8 both = render(renderer, s);
    dump(onlyA, "superpose-a");
    dump(onlyB, "superpose-b");
    dump(both, "superpose-both");

    // The overlap: pixels inside both fields' projected discs.
    const glm::vec2 pa = project(s, ca);
    const glm::vec2 pb = project(s, cb);
    const float r = glm::length(project(s, ca + glm::vec3(5.0f, 0.0f, 0.0f)) - pa) * 0.9f;
    std::size_t overlap = 0;
    std::size_t notA = 0;
    std::size_t notB = 0;
    std::size_t aVisible = 0;
    std::size_t bVisible = 0;
    for (int y = 0; y < static_cast<int>(kHeight); ++y) {
        for (int x = 0; x < static_cast<int>(kWidth); ++x) {
            const glm::vec2 p(x + 0.5f, y + 0.5f);
            if (glm::length(p - pa) > r || glm::length(p - pb) > r) {
                continue;
            }
            ++overlap;
            aVisible += differs(none, onlyA, x, y) ? 1u : 0u;
            bVisible += differs(none, onlyB, x, y) ? 1u : 0u;
            notA += differs(onlyA, both, x, y) ? 1u : 0u;
            notB += differs(onlyB, both, x, y) ? 1u : 0u;
        }
    }
    INFO("overlap " << overlap << " px; A alone bends " << aVisible << ", B alone " << bVisible
                    << "; both differs from A in " << notA << " and from B in " << notB);
    REQUIRE(overlap > 200);
    REQUIRE(aVisible > 50);
    REQUIRE(bVisible > 50);
    CHECK(notA > 50); // B still acts where A is
    CHECK(notB > 50); // and A where B is
}

TEST_CASE("DF: a full budget of 64 proxies renders in one pass", "[gpu][effects][distortion][capacity]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    initialise(renderer);

    scene::Scene s = wallScene();
    std::vector<world::EffectInstance> effects;
    for (int i = 0; i < 65; ++i) {
        const float x = static_cast<float>(i % 8) * 2.4f - 8.4f;
        const float y = static_cast<float>(i / 8) * 1.6f - 5.6f;
        world::EffectInstance e = warpInstance("w" + std::to_string(i), glm::vec3(x, y, -20.0f), 1.0f, 1.2f);
        e.order = i;
        effects.push_back(e);
    }
    s.distortion = frameOf(effects);
    REQUIRE(s.distortion.count == world::kMaxDistortionProxies);
    CHECK(s.distortion.dropped == 1); // the 65th
    const gpu::Image8 none = render(renderer, wallScene());
    const gpu::Image8 many = render(renderer, s);
    dump(many, "budget-64");
    CHECK(renderer.distortionStats().proxies == 64);
    CHECK(renderer.distortionStats().dropped == 1);
    CHECK(ctx->errorCount() == 0);
    const bool manyDiffers = many.rgba != none.rgba;
    CHECK(manyDiffers);
}

TEST_CASE("DF: the edge glow reaches HDR and the emission target, so bloom sees it",
          "[gpu][effects][distortion][bloom]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    initialise(renderer);

    scene::Scene s = wallScene();
    s.post.bloomEnabled = true;
    const glm::vec3 centre(0.0f, 0.0f, -15.0f);
    world::EffectInstance e = warpInstance("rim", centre, 5.0f, 0.0f); // no bend: the rim alone
    s.distortion = frameOf({e});
    const gpu::Image8 dark = render(renderer, s);
    e.values.setFloat("spaceWarp/rimIntensity", 8.0f);
    e.values.setColor("spaceWarp/rimColor", glm::vec3(0.3f, 0.8f, 1.0f));
    s.distortion = frameOf({e});
    const gpu::Image8 glowing = render(renderer, s);
    dump(glowing, "rim-bloom");

    // Outside the field's disc nothing DF writes can reach -- except the bloom of what it wrote.
    const glm::vec2 c = project(s, centre);
    const float rPx = glm::length(project(s, centre + glm::vec3(5.0f, 0.0f, 0.0f)) - c) * 1.15f + 4.0f;
    std::size_t ring = 0;
    std::size_t halo = 0;
    for (int y = 0; y < static_cast<int>(kHeight); ++y) {
        for (int x = 0; x < static_cast<int>(kWidth); ++x) {
            const float d = glm::length(glm::vec2(x + 0.5f, y + 0.5f) - c);
            if (d <= rPx) {
                ring += differs(dark, glowing, x, y) ? 1u : 0u;
            } else if (d <= rPx + 12.0f) {
                halo += same(dark, glowing, x, y) ? 0u : 1u;
            }
        }
    }
    INFO("rim pixels " << ring << ", bloom halo pixels beyond the field " << halo);
    CHECK(ring > 100);
    CHECK(halo > 50);
}

TEST_CASE("DF: the screen rects bound what a proxy can touch, and a proxy at the lens is full-screen",
          "[gpu][effects][distortion][rects]") {
    scene::Scene s = wallScene();
    const float aspect = static_cast<float>(kWidth) / static_cast<float>(kHeight);
    const glm::mat4 vp = s.camera.projection(aspect) * s.camera.view();

    world::DistortionFrame far = frameOf({warpInstance("far", glm::vec3(4.0f, 2.0f, -40.0f), 3.0f, 1.0f)});
    const rendering::DistortionRects r = rendering::distortionRects(far, vp, 0.1f, kWidth, kHeight, 1.1f);
    CHECK_FALSE(r.fullScreen);
    const glm::vec2 c = project(s, glm::vec3(4.0f, 2.0f, -40.0f));
    CHECK(c.x > static_cast<float>(r.scissor.x));
    CHECK(c.x < static_cast<float>(r.scissor.z));
    CHECK(c.y > static_cast<float>(r.scissor.y));
    CHECK(c.y < static_cast<float>(r.scissor.w));
    // The copy reaches at least as far as the resolve.
    CHECK(r.copy.x <= r.scissor.x);
    CHECK(r.copy.y <= r.scissor.y);
    CHECK(r.copy.z >= r.scissor.z);
    CHECK(r.copy.w >= r.scissor.w);
    // Well short of the frame: this is the saving the scissor exists for.
    CHECK((r.scissor.z - r.scissor.x) * (r.scissor.w - r.scissor.y) < static_cast<int>(kWidth * kHeight) / 4);

    world::DistortionFrame around = frameOf({warpInstance("around", glm::vec3(0.0f, 0.0f, -2.0f), 5.0f, 1.0f)});
    const rendering::DistortionRects full = rendering::distortionRects(around, vp, 0.1f, kWidth, kHeight, 1.1f);
    CHECK(full.fullScreen);
    CHECK(full.scissor == glm::ivec4(0, 0, static_cast<int>(kWidth), static_cast<int>(kHeight)));
}

TEST_CASE("DF through the engine: a Space Warp on the World is evaluated, drawn and reported",
          "[gpu][effects][distortion][engine]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    initialise(renderer);

    constexpr double kSecond = 2.0;
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    engine.setViewport(kWidth, kHeight);
    engine.update(FrameTime{kSecond, 1.0 / 60.0, 120});
    const scene::Camera camera = engine.scene().camera;
    const glm::vec3 ahead = camera.position + glm::normalize(camera.target - camera.position) * 12.0f;

    world::EffectInstance warp = world::makeEffect(world::EffectKind::SpaceWarp, "Space Warp");
    warp.values.setFloat("spaceWarp/offsetX", ahead.x);
    warp.values.setFloat("spaceWarp/offsetY", ahead.y);
    warp.values.setFloat("spaceWarp/offsetZ", ahead.z);
    warp.values.setFloat("spaceWarp/radius", 4.0f);
    std::vector<world::EffectInstance> effects;
    REQUIRE(world::insertEffect(effects, warp).has_value());
    REQUIRE(engine.setEffects(effects).has_value());
    const std::string id = engine.effects()[0].id;

    engine.update(FrameTime{kSecond, 1.0 / 60.0, 120});
    CHECK(engine.effectStatus(id) == world::EffectStatus::Drawn);
    CHECK(engine.scene().distortion.count == 1);
    const gpu::Image8 on = render(renderer, engine.scene(), kSecond);
    CHECK(renderer.distortionStats().encoded);

    auto* enabled = engine.params().find(world::effectParameterPrefix(id) + "enabled");
    REQUIRE(enabled != nullptr);
    enabled->setBaseComponent(0, 0.0f);
    engine.update(FrameTime{kSecond, 1.0 / 60.0, 120});
    CHECK(engine.effectStatus(id) == world::EffectStatus::Disabled);
    CHECK(engine.scene().distortion.count == 0);
    const gpu::Image8 off = render(renderer, engine.scene(), kSecond);
    CHECK_FALSE(renderer.distortionStats().encoded);
    dump(on, "engine-on");
    dump(off, "engine-off");
}
