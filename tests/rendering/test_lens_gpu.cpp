// Effect Library Wave 3, the lens slice, on pixels: Heat Shimmer (DF's Cylinder shape and Shimmer
// field) and Gravitational Lens (the Facing shape, the Lens remap and the horizon's cover target).
//
// The fixture is DF's own (test_distortion_gpu.cpp): an unlit checker wall 30 m down the view axis,
// bloom off unless a case is about it, and -- where the question needs one -- a saturated object whose
// colour appears nowhere else, so "did it leak" is a colour count.
//
// With AVGEN_EFFECT_DUMP=<dir> each case writes its arms as PNGs for a person to look at. The
// `[.visual]` cases render the owner's own scenes (Glowmere, the Tree of Life island) for a person and
// assert only that the effect was drawn.

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
#include "scene/composition.hpp"
#include "scene/scene.hpp"
#include "world/atmospherics.hpp"
#include "world/terrain_query.hpp"
#include "world/effects/distortion_frame.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"

#include <glm/gtc/quaternion.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
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

// A 256x256 texture: the checker, or (split) a red left half and a blue right half.
scene::TextureData wallTexture(bool split) {
    scene::TextureData t;
    t.name = split ? "lens-split" : "lens-checker";
    t.width = t.height = 256;
    t.data.resize(static_cast<std::size_t>(t.width) * t.height * 4);
    for (std::uint32_t y = 0; y < t.height; ++y) {
        for (std::uint32_t x = 0; x < t.width; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * t.width + x) * 4;
            if (split) {
                const bool left = x < t.width / 2;
                t.data[i] = left ? 230 : 10;
                t.data[i + 1] = 10;
                t.data[i + 2] = left ? 10 : 230;
            } else {
                const std::uint8_t v = ((x / 16) + (y / 16)) % 2 == 0 ? 230 : 20;
                t.data[i] = t.data[i + 1] = t.data[i + 2] = v;
            }
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

// Camera at the origin looking down -Z; the wall at z = -30 facing it.
scene::Scene wallScene(bool split = false) {
    scene::Scene s;
    const scene::MeshId plane = s.addMesh(scene::makePlane(30.0f, 1));
    scene::Entity& wall = s.addEntity("wall", plane);
    wall.transform.position = glm::vec3(0.0f, 0.0f, -30.0f);
    wall.transform.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    wall.material = unlit(glm::vec3(1.0f));
    const scene::TextureId tex = s.addTexture(wallTexture(split));
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

void addBody(scene::Scene& s, const char* name, scene::MeshData mesh, glm::vec3 at, glm::vec3 colour) {
    const scene::MeshId id = s.addMesh(std::move(mesh));
    scene::Entity& e = s.addEntity(name, id);
    e.transform.position = at;
    e.material = unlit(colour);
}

world::EffectInstance live(world::EffectKind kind, const std::string& id) {
    world::EffectInstance e = world::makeEffect(kind, id);
    e.id = id;
    e.activation = world::Activation::Always;
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    return e;
}

void put(world::EffectInstance& e, const char* leaf, float v) {
    const char* key = e.kind == world::EffectKind::HeatShimmer ? "heatShimmer/" : "gravLens/";
    e.values.setFloat(std::string(key) + leaf, v);
}

// A World lens at `at`: Lens mode, no ring, no chroma unless asked.
world::EffectInstance lensAt(glm::vec3 at, float thetaE, bool hole = false) {
    world::EffectInstance e = live(world::EffectKind::GravitationalLens, "lens");
    put(e, "offsetX", at.x);
    put(e, "offsetY", at.y);
    put(e, "offsetZ", at.z);
    put(e, "einsteinRadius", thetaE);
    put(e, "falloffRadius", 3.0f);
    put(e, "mode", hole ? 1.0f : 0.0f);
    put(e, "photonRing", 0.0f);
    put(e, "chroma", 0.0f);
    return e;
}

// A World column whose base is at `base`.
world::EffectInstance columnAt(glm::vec3 base, float radius, float height, float strength) {
    world::EffectInstance e = live(world::EffectKind::HeatShimmer, "heat");
    put(e, "offsetX", base.x);
    put(e, "offsetY", base.y);
    put(e, "offsetZ", base.z);
    put(e, "radius", radius);
    put(e, "height", height);
    put(e, "strength", strength);
    put(e, "chroma", 0.0f);
    return e;
}

world::DistortionFrame frameOf(const std::vector<world::EffectInstance>& effects, double seconds = 1.0) {
    world::EffectContext ctx;
    ctx.seconds = seconds;
    std::vector<std::uint32_t> order;
    world::effectEvaluationOrder(effects, order);
    std::vector<world::EffectStatus> status(effects.size());
    std::vector<std::string> reasons(effects.size());
    world::DistortionFrame frame;
    world::buildDistortionFrame(effects, ctx, frame, order, status, reasons);
    return frame;
}

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
    static_cast<void>(assets::writePng(fs::path(dir) / ("lens-" + name + ".png"), image.width, image.height, image.rgba));
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
bool differs(const gpu::Image8& a, const gpu::Image8& b, int x, int y) {
    const Px p = at(a, x, y);
    const Px q = at(b, x, y);
    return std::abs(p.r - q.r) + std::abs(p.g - q.g) + std::abs(p.b - q.b) > 24;
}
bool isGreen(Px p) { return p.g > 120 && p.g > p.r + 70 && p.g > p.b + 70; }
bool isRed(Px p) { return p.r > 120 && p.r > p.g + 70 && p.r > p.b + 70; }
// Within two pixels of the saturated body: the post chain's anti-aliasing blends its edge into its
// neighbours, so those are neither "the body" nor "background" for a count.
bool nearGreen(const gpu::Image8& img, int x, int y) {
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            const int xx = std::clamp(x + dx, 0, static_cast<int>(img.width) - 1);
            const int yy = std::clamp(y + dy, 0, static_cast<int>(img.height) - 1);
            const Px p = at(img, xx, yy);
            if (p.g > 60 && p.g > p.r + 30 && p.g > p.b + 30) {
                return true;
            }
        }
    }
    return false;
}
bool isBlue(Px p) { return p.b > 120 && p.b > p.r + 70 && p.b > p.g + 70; }

glm::vec2 project(const scene::Scene& s, glm::vec3 p, std::uint32_t w = kWidth, std::uint32_t h = kHeight) {
    const glm::mat4 vp = s.camera.projection(static_cast<float>(w) / static_cast<float>(h)) * s.camera.view();
    const glm::vec4 c = vp * glm::vec4(p, 1.0f);
    const glm::vec2 ndc = glm::vec2(c) / c.w;
    return {(ndc.x * 0.5f + 0.5f) * static_cast<float>(w), (0.5f - ndc.y * 0.5f) * static_cast<float>(h)};
}

double meanDiff(const gpu::Image8& x, const gpu::Image8& y) {
    double sum = 0.0;
    for (std::size_t i = 0; i + 3 < x.rgba.size(); i += 4) {
        for (int ch = 0; ch < 3; ++ch) {
            sum += std::abs(static_cast<int>(x.rgba[i + ch]) - static_cast<int>(y.rgba[i + ch]));
        }
    }
    return sum / static_cast<double>(x.rgba.size());
}

} // namespace

// ---- Gravitational Lens ---------------------------------------------------------------------------------

TEST_CASE("Gravitational Lens bends the checker behind it, not a cube in front, and nothing outside its reach",
          "[gpu][effects][distortion][lens]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    initialise(renderer);

    scene::Scene s = wallScene();
    // A green cube 8 m away -- in front of the mass at 15 m -- across the lens's strongest band.
    addBody(s, "cube", scene::makeCube(0.8f), glm::vec3(1.6f, 0.0f, -8.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const gpu::Image8 plain = render(renderer, s);
    const glm::vec3 centre(0.0f, 0.0f, -15.0f);
    s.distortion = frameOf({lensAt(centre, 1.5f)});
    REQUIRE(s.distortion.count == 1);
    const gpu::Image8 lensed = render(renderer, s);
    CHECK(renderer.distortionStats().encoded);
    CHECK(ctx->errorCount() == 0);
    dump(plain, "cube-off");
    dump(lensed, "cube-on");

    const glm::vec2 c = project(s, centre);
    const float reachPx = glm::length(project(s, centre + glm::vec3(4.5f, 0.0f, 0.0f)) - c) + 3.0f;
    std::size_t cubePixels = 0;
    std::size_t cubeChanged = 0;
    std::size_t leaked = 0;
    std::size_t bent = 0;
    std::size_t outside = 0;
    for (int y = 0; y < static_cast<int>(kHeight); ++y) {
        for (int x = 0; x < static_cast<int>(kWidth); ++x) {
            if (glm::length(glm::vec2(x + 0.5f, y + 0.5f) - c) > reachPx) {
                outside += same(plain, lensed, x, y) ? 0u : 1u;
                continue;
            }
            if (isGreen(at(plain, x, y))) {
                ++cubePixels;
                cubeChanged += same(plain, lensed, x, y) ? 0u : 1u;
            } else {
                leaked += isGreen(at(lensed, x, y)) ? 1u : 0u;
                bent += differs(plain, lensed, x, y) ? 1u : 0u;
            }
        }
    }
    INFO("cube pixels " << cubePixels << ", changed " << cubeChanged << "; green outside the cube "
         << leaked << "; bent background " << bent << "; changed beyond the reach " << outside);
    REQUIRE(cubePixels > 150);
    REQUIRE(bent > 800);
    CHECK(cubeChanged == 0);
    CHECK(leaked == 0);
    CHECK(outside == 0);
}

TEST_CASE("Gravitational Lens: inside the Einstein ring the image is of the far side (mirrored)",
          "[gpu][effects][distortion][lens]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    initialise(renderer);

    // A wall red on the left and blue on the right, the mass straight ahead in front of the seam.
    scene::Scene s = wallScene(true);
    const gpu::Image8 plain = render(renderer, s);
    const glm::vec3 centre(0.0f, 0.0f, -15.0f);
    const float tE = 2.0f;
    s.distortion = frameOf({lensAt(centre, tE)});
    const gpu::Image8 lensed = render(renderer, s);
    dump(plain, "mirror-off");
    dump(lensed, "mirror-on");

    // Sample small patches at theta = +-0.5 tE (inside the ring) and +-2.2 tE (outside it).
    const auto patch = [&](const gpu::Image8& img, float thetaOverE, bool (*pred)(Px)) {
        const glm::vec2 p = project(s, centre + glm::vec3(thetaOverE * tE, 0.0f, 0.0f));
        std::size_t hits = 0;
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                hits += pred(at(img, static_cast<int>(p.x) + dx, static_cast<int>(p.y) + dy)) ? 1u : 0u;
            }
        }
        return hits;
    };
    // The control: without the lens each side is its own colour.
    REQUIRE(patch(plain, 0.5f, isBlue) == 25);
    REQUIRE(patch(plain, -0.5f, isRed) == 25);
    // Inside the ring, each side shows the other side's colour.
    CHECK(patch(lensed, 0.5f, isRed) == 25);
    CHECK(patch(lensed, -0.5f, isBlue) == 25);
    // Outside it, each side keeps its own (magnified toward the mass, not flipped).
    CHECK(patch(lensed, 2.2f, isBlue) == 25);
    CHECK(patch(lensed, -2.2f, isRed) == 25);
}

TEST_CASE("Gravitational Lens in Black hole mode: a dark horizon, a glowing ring, and neither over a "
          "cube in front",
          "[gpu][effects][distortion][lens][bloom]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    initialise(renderer);

    scene::Scene s = wallScene();
    const glm::vec3 centre(0.0f, 0.0f, -15.0f);
    const float tE = 2.5f;
    // A small green cube in front of the horizon's left edge.
    addBody(s, "cube", scene::makeCube(0.35f), glm::vec3(-0.45f, 0.0f, -8.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const gpu::Image8 plain = render(renderer, s);
    world::EffectInstance hole = lensAt(centre, tE, true);
    put(hole, "horizonScale", 0.45f);
    put(hole, "ringWidth", 0.15f);
    s.distortion = frameOf({hole});
    const gpu::Image8 dark = render(renderer, s);
    put(hole, "photonRing", 8.0f);
    s.distortion = frameOf({hole});
    s.post.bloomEnabled = true;
    const gpu::Image8 ringed = render(renderer, s);
    dump(dark, "hole-dark");
    dump(ringed, "hole-ring");

    const glm::vec2 c = project(s, centre);
    const float pxPerM = glm::length(project(s, centre + glm::vec3(1.0f, 0.0f, 0.0f)) - c);
    const float rh = 0.45f * tE * pxPerM;
    std::size_t inside = 0;
    std::size_t black = 0;
    std::size_t cube = 0;
    std::size_t cubeKept = 0;
    for (int y = 0; y < static_cast<int>(kHeight); ++y) {
        for (int x = 0; x < static_cast<int>(kWidth); ++x) {
            const float d = glm::length(glm::vec2(x + 0.5f, y + 0.5f) - c);
            if (isGreen(at(plain, x, y))) {
                ++cube;
                cubeKept += same(plain, dark, x, y) ? 1u : 0u;
                continue;
            }
            if (d < rh - 1.5f && !nearGreen(plain, x, y)) {
                ++inside;
                const Px p = at(dark, x, y);
                black += (p.r + p.g + p.b) < 12 ? 1u : 0u;
            }
        }
    }
    // The ring: brighter than the dark arm just outside the horizon.
    double ringGain = 0.0;
    std::size_t ringPx = 0;
    for (int y = 0; y < static_cast<int>(kHeight); ++y) {
        for (int x = 0; x < static_cast<int>(kWidth); ++x) {
            const float d = glm::length(glm::vec2(x + 0.5f, y + 0.5f) - c);
            if (d > rh + 0.5f && d < rh + 0.15f * tE * pxPerM + 1.0f && !nearGreen(plain, x, y)) {
                const Px a = at(dark, x, y);
                const Px b = at(ringed, x, y);
                ringGain += (b.r + b.g + b.b) - (a.r + a.g + a.b);
                ++ringPx;
            }
        }
    }
    INFO("horizon pixels " << inside << ", black " << black << "; cube " << cube << ", kept " << cubeKept
         << "; ring pixels " << ringPx << ", mean gain " << (ringPx ? ringGain / ringPx : 0.0));
    REQUIRE(inside > 40);
    REQUIRE(cube > 10);
    CHECK(black == inside);
    CHECK(cubeKept == cube);
    REQUIRE(ringPx > 20);
    CHECK(ringGain / static_cast<double>(ringPx) > 60.0);
    CHECK(ctx->errorCount() == 0);
}

// ---- Heat Shimmer ------------------------------------------------------------------------------------

TEST_CASE("Heat Shimmer bends what is behind the column, not a cube in front of it, and nothing beside it",
          "[gpu][effects][distortion][shimmer]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    initialise(renderer);

    scene::Scene s = wallScene();
    // The column: 2 m radius, 8 m tall, base 4 m below the axis, 15 m away. A green cube 8 m away in
    // front of it.
    addBody(s, "cube", scene::makeCube(0.7f), glm::vec3(0.6f, 0.0f, -8.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const gpu::Image8 plain = render(renderer, s);
    world::EffectInstance heat = columnAt(glm::vec3(0.0f, -4.0f, -15.0f), 2.0f, 8.0f, 0.3f);
    put(heat, "heightFalloff", 0.3f);
    s.distortion = frameOf({heat}, 2.0);
    REQUIRE(s.distortion.count == 1);
    const gpu::Image8 hot = render(renderer, s, 2.0);
    CHECK(renderer.distortionStats().encoded);
    CHECK(ctx->errorCount() == 0);
    dump(plain, "shimmer-off");
    dump(hot, "shimmer-on");

    // The column's screen footprint, conservatively: its silhouette's x extent, grown for AA.
    const float x0 = project(s, glm::vec3(-2.0f, 0.0f, -13.0f)).x - 3.0f;
    const float x1 = project(s, glm::vec3(2.0f, 0.0f, -13.0f)).x + 3.0f;
    const float y0 = project(s, glm::vec3(0.0f, 4.0f, -13.0f)).y - 3.0f;
    const float y1 = project(s, glm::vec3(0.0f, -4.0f, -13.0f)).y + 3.0f;
    std::size_t cubePixels = 0;
    std::size_t cubeChanged = 0;
    std::size_t leaked = 0;
    std::size_t bent = 0;
    std::size_t outside = 0;
    for (int y = 0; y < static_cast<int>(kHeight); ++y) {
        for (int x = 0; x < static_cast<int>(kWidth); ++x) {
            const float fx = x + 0.5f;
            const float fy = y + 0.5f;
            if (fx < x0 || fx > x1 || fy < y0 || fy > y1) {
                outside += same(plain, hot, x, y) ? 0u : 1u;
                continue;
            }
            if (isGreen(at(plain, x, y))) {
                ++cubePixels;
                cubeChanged += same(plain, hot, x, y) ? 0u : 1u;
            } else {
                leaked += isGreen(at(hot, x, y)) ? 1u : 0u;
                bent += differs(plain, hot, x, y) ? 1u : 0u;
            }
        }
    }
    INFO("cube pixels " << cubePixels << ", changed " << cubeChanged << "; green outside the cube " << leaked
         << "; bent background " << bent << "; changed beside the column " << outside);
    REQUIRE(cubePixels > 100);
    REQUIRE(bent > 120); // the control: the column bends the checker edges behind it
    CHECK(cubeChanged == 0);
    CHECK(leaked == 0);
    CHECK(outside == 0);
}

TEST_CASE("Heat Shimmer is temporally coherent: a frame apart it moves far less than a second apart",
          "[gpu][effects][distortion][shimmer][temporal]") {
    // ADR-703's measure for Space Warp's turbulence: the mean absolute difference over the frame, one
    // frame (1/60 s) apart -- here held against WHITE NOISE directly: the same second with the field
    // reseeded (another instance id), which is what a per-frame noise would change between two frames.
    // Heat haze is a fast wobble, so a frame apart it moves a real fraction of that, but a coherent
    // flow moves far less than an uncorrelated one. One second apart is reported alongside.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    initialise(renderer);

    scene::Scene s = wallScene();
    world::EffectInstance heat = columnAt(glm::vec3(0.0f, -5.0f, -15.0f), 3.0f, 10.0f, 0.35f);
    put(heat, "riseSpeed", 2.5f);
    put(heat, "scale", 0.35f);
    put(heat, "heightFalloff", 0.3f);
    const auto frameAt = [&](double t) {
        s.distortion = frameOf({heat}, t);
        return render(renderer, s, t);
    };
    const gpu::Image8 a = frameAt(3.0);
    const gpu::Image8 b = frameAt(3.0 + 1.0 / 60.0);
    const gpu::Image8 c = frameAt(4.0);
    // Across a layer's rebirth (the cycle is 4 s: layer A is reborn at t = 4, B at t = 2 and 6): a
    // frame either side of it is as coherent as anywhere else.
    const gpu::Image8 d = frameAt(4.0 - 1.0 / 120.0);
    const gpu::Image8 e = frameAt(4.0 + 1.0 / 120.0);
    // White noise: the field at 3 s with a different seed.
    heat.id = "heat-reseeded";
    const gpu::Image8 w = frameAt(3.0);
    dump(a, "shimmer-t3");
    dump(b, "shimmer-t3-plus-frame");
    dump(c, "shimmer-t4");
    const double frame = meanDiff(a, b);
    const double second = meanDiff(a, c);
    const double rebirth = meanDiff(d, e);
    const double noise = meanDiff(a, w);
    INFO("mean |diff| one frame apart " << frame << ", across a layer's rebirth " << rebirth
                                        << ", one second apart " << second << ", reseeded (white noise) "
                                        << noise);
    REQUIRE(noise > 0.4); // the control: the shimmer does move the image
    CHECK(second > 0.6 * noise); // and a second decorrelates it about as much as a reseed
    CHECK(frame < 0.4 * noise);
    CHECK(rebirth < 0.4 * noise);
}

// ---- both --------------------------------------------------------------------------------------------

TEST_CASE("with no lens or shimmer the frame is byte-identical, before and after frames that had both",
          "[gpu][effects][distortion][lens][shimmer][gate]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});

    scene::Scene s = wallScene();
    addBody(s, "cube", scene::makeCube(1.5f), glm::vec3(-2.0f, 1.0f, -12.0f), glm::vec3(0.2f, 0.4f, 1.0f));
    s.post.bloomEnabled = true;

    rendering::SceneRenderer fresh(*ctx, shaders);
    initialise(fresh);
    const gpu::Image8 reference = render(fresh, s);
    CHECK_FALSE(fresh.distortionStats().encoded);

    rendering::SceneRenderer used(*ctx, shaders);
    initialise(used);
    const gpu::Image8 before = render(used, s);
    scene::Scene effected = s;
    world::EffectInstance hole = lensAt(glm::vec3(3.0f, 0.0f, -15.0f), 1.5f, true);
    put(hole, "photonRing", 6.0f);
    effected.distortion = frameOf({hole, columnAt(glm::vec3(-3.0f, -4.0f, -15.0f), 1.5f, 6.0f, 0.1f)});
    REQUIRE(effected.distortion.count == 2);
    const gpu::Image8 during = render(used, effected);
    REQUIRE(used.distortionStats().encoded);
    const gpu::Image8 after = render(used, s);
    CHECK_FALSE(used.distortionStats().encoded);

    REQUIRE(reference.rgba.size() == before.rgba.size());
    const bool beforeIdentical = before.rgba == reference.rgba;
    const bool afterIdentical = after.rgba == reference.rgba;
    const bool duringDiffers = during.rgba != reference.rgba;
    CHECK(beforeIdentical);
    CHECK(afterIdentical);
    CHECK(duringDiffers);
}

TEST_CASE("Heat Shimmer and Gravitational Lens through the engine: evaluated, drawn and reported",
          "[gpu][effects][distortion][lens][shimmer][engine]") {
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
    const glm::vec3 fwd = glm::normalize(camera.target - camera.position);
    const glm::vec3 ahead = camera.position + fwd * 14.0f;

    world::EffectInstance lens = world::makeEffect(world::EffectKind::GravitationalLens, "Gravitational Lens");
    lens.values.setFloat("gravLens/offsetX", ahead.x + 2.0f);
    lens.values.setFloat("gravLens/offsetY", ahead.y);
    lens.values.setFloat("gravLens/offsetZ", ahead.z);
    world::EffectInstance heat = world::makeEffect(world::EffectKind::HeatShimmer, "Heat Shimmer");
    heat.values.setFloat("heatShimmer/offsetX", ahead.x - 2.0f);
    heat.values.setFloat("heatShimmer/offsetY", ahead.y - 2.0f);
    heat.values.setFloat("heatShimmer/offsetZ", ahead.z);
    std::vector<world::EffectInstance> effects;
    REQUIRE(world::insertEffect(effects, lens).has_value());
    REQUIRE(world::insertEffect(effects, heat).has_value());
    REQUIRE(engine.setEffects(effects).has_value());
    const std::string lensId = engine.effects()[0].id;
    const std::string heatId = engine.effects()[1].id;

    engine.update(FrameTime{kSecond, 1.0 / 60.0, 120});
    CHECK(engine.effectStatus(lensId) == world::EffectStatus::Drawn);
    CHECK(engine.effectStatus(heatId) == world::EffectStatus::Drawn);
    CHECK(engine.scene().distortion.count == 2);
    const gpu::Image8 on = render(renderer, engine.scene(), kSecond);
    CHECK(renderer.distortionStats().encoded);
    CHECK(renderer.distortionStats().proxies == 2);
    dump(on, "engine-on");
}

// ---- the owner's scenes, for a person to look at ---------------------------------------------------
//
// Hidden: they load whole projects and assert only that the effect was drawn. Run with
// AVGEN_EFFECT_DUMP=<dir>; AVGEN_LENS_SECOND overrides the second rendered.

namespace {

constexpr std::uint32_t kShowW = 960;
constexpr std::uint32_t kShowH = 540;

double envOr(const char* name, double fallback) {
    const char* v = std::getenv(name);
    return v != nullptr && v[0] != '\0' ? std::atof(v) : fallback;
}

const scene::Scene& showAt(app::Engine& engine, double seconds) {
    engine.setViewport(kShowW, kShowH);
    engine.update(FrameTime{seconds, 1.0 / 60.0, static_cast<std::uint64_t>(seconds * 60.0)});
    return engine.scene();
}

void enable(app::Engine& engine, const std::string& id, bool on) {
    auto* p = engine.params().find(world::effectParameterPrefix(id) + "enabled");
    REQUIRE(p != nullptr);
    p->setBaseComponent(0, on ? 1.0f : 0.0f);
}

std::string addEffect(app::Engine& engine, world::EffectInstance e) {
    std::string id;
    REQUIRE(engine
                .editEffects([&](std::vector<world::EffectInstance>& list) -> Result<void> {
                    auto added = world::insertEffect(list, std::move(e));
                    if (!added) {
                        return std::unexpected(added.error());
                    }
                    id = *added;
                    return {};
                })
                .has_value());
    return id;
}

// A lens in the sky ahead of the camera: `lift` of the way from the view axis to straight up, `dist`
// metres out, sized so its Einstein radius subtends a fixed share of the frame.
world::EffectInstance skyLens(const scene::Camera& cam, const char* style, float dist, float lift, float side,
                              float size) {
    const glm::vec3 fwd = glm::normalize(cam.target - cam.position);
    const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 dir = glm::normalize(fwd + glm::vec3(0.0f, lift, 0.0f) + right * side);
    const glm::vec3 at = cam.position + dir * dist;
    world::EffectInstance e = world::makeEffect(world::EffectKind::GravitationalLens, style);
    REQUIRE(world::applyEffectStyle(e, world::EffectKind::GravitationalLens, style));
    e.values.setFloat("gravLens/offsetX", at.x);
    e.values.setFloat("gravLens/offsetY", at.y);
    e.values.setFloat("gravLens/offsetZ", at.z);
    e.values.setFloat("gravLens/einsteinRadius", dist * size);
    return e;
}

struct Framing {
    double second;
    float dist, lift, side, size;
};

void showLens(const fs::path& project, Framing f, const std::string& stem) {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    initialise(renderer);
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(project).has_value());
    const double t = envOr("AVGEN_LENS_SECOND", f.second);
    const scene::Camera cam = showAt(engine, t).camera;
    const float dist = static_cast<float>(envOr("AVGEN_LENS_DIST", f.dist));
    const float lift = static_cast<float>(envOr("AVGEN_LENS_LIFT", f.lift));
    const float side = static_cast<float>(envOr("AVGEN_LENS_SIDE", f.side));
    const float size = static_cast<float>(envOr("AVGEN_LENS_SIZE", f.size));
    const std::string hole = addEffect(engine, skyLens(cam, "Black Hole", dist, lift, side, size));
    const std::string ring = addEffect(engine, skyLens(cam, "Einstein Ring", dist, lift, side, size));
    enable(engine, hole, false);
    enable(engine, ring, false);
    dump(render(renderer, showAt(engine, t), t, kShowW, kShowH), stem + "-off");
    enable(engine, hole, true);
    const gpu::Image8 on = render(renderer, showAt(engine, t), t, kShowW, kShowH);
    CHECK(engine.effectStatus(hole) == world::EffectStatus::Drawn);
    CHECK(renderer.distortionStats().encoded);
    dump(on, stem + "-black-hole");
    enable(engine, hole, false);
    enable(engine, ring, true);
    dump(render(renderer, showAt(engine, t), t, kShowW, kShowH), stem + "-einstein-ring");
    CHECK(engine.effectStatus(ring) == world::EffectStatus::Drawn);
    CHECK(ctx->errorCount() == 0);
}

} // namespace

TEST_CASE("VISUAL Glowmere: a gravitational lens in the night sky", "[.visual][lens]") {
    // In the night sky over the valley, 400 m out.
    showLens(fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2-multicam.json",
             {90.0, 400.0f, 0.28f, 0.15f, 0.07f}, "glowmere-sky");
}

TEST_CASE("VISUAL Glowmere: a gravitational lens in front of the hillside", "[.visual][lens]") {
    // 90 m out, in front of a forested ridge: the trees behind it are drawn into rings.
    showLens(fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2-multicam.json",
             {90.0, 90.0f, 0.1f, 0.0f, 0.08f}, "glowmere-hillside");
}

TEST_CASE("VISUAL Tree of Life island: a gravitational lens in its sky", "[.visual][lens]") {
    showLens(fs::path(AVGEN_SOURCE_DIR) / "examples" / "treeisland" / "tree-of-life-floating-island-night.json",
             {10.0, 400.0f, 0.14f, 0.32f, 0.08f}, "treeisland-sky");
}

TEST_CASE("VISUAL Glowmere: heat shimmer over a campfire", "[.visual][shimmer]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    initialise(renderer);
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2-multicam.json")
                .has_value());
    const double t = envOr("AVGEN_LENS_SECOND", 75.0);
    const scene::Camera cam = showAt(engine, t).camera;
    REQUIRE(engine.composition() != nullptr);
    const glm::vec3 fwd = glm::normalize(cam.target - cam.position);
    const glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0.0f, 1.0f, 0.0f)));
    // Where the view (lowered by AVGEN_FIRE_DROP of the way to straight down) meets the ground: the
    // fire stands in the middle of the picture whatever the shot is.
    const float drop = static_cast<float>(envOr("AVGEN_FIRE_DROP", 0.0));
    const float side = static_cast<float>(envOr("AVGEN_FIRE_SIDE", 0.2));
    const glm::vec3 ray = glm::normalize(fwd + glm::vec3(0.0f, -drop, 0.0f) + right * side);
    const world::TerrainQuery ground = engine.composition()->terrainQuery();
    glm::vec3 base = cam.position;
    for (float s = 1.0f; s < 600.0f; s += 0.25f) {
        const glm::vec3 q = cam.position + ray * s;
        const glm::vec3 g = ground.groundPoint(glm::vec2(q.x, q.z));
        if (q.y <= g.y) {
            base = g;
            break;
        }
    }
    INFO("camera " << cam.position.x << "," << cam.position.y << "," << cam.position.z << " fire at " << base.x << ","
                   << base.y << "," << base.z);

    world::EffectInstance heat = world::makeEffect(world::EffectKind::HeatShimmer, "Campfire");
    REQUIRE(world::applyEffectStyle(heat, world::EffectKind::HeatShimmer, "Campfire"));
    heat.values.setFloat("heatShimmer/offsetX", base.x);
    heat.values.setFloat("heatShimmer/offsetY", base.y);
    heat.values.setFloat("heatShimmer/offsetZ", base.z);
    const float boost = static_cast<float>(envOr("AVGEN_FIRE_STRENGTH", 0.0));
    if (boost > 0.0f) {
        heat.values.setFloat("heatShimmer/strength", boost);
    }
    const std::string heatId = addEffect(engine, heat);

    // A person's-eye view of the fire: the film's camera looks down on the valley from 20-odd metres,
    // where a column of hot air is seen end-on. The same evaluated scene, re-aimed from 1.7 m up and
    // AVGEN_FIRE_VIEW metres back along the film camera's bearing (the proxies are world-space, so
    // they need no re-evaluation), and the off arm is that scene with its distortion block emptied --
    // an exact pair, nothing else differs.
    const float back = static_cast<float>(envOr("AVGEN_FIRE_VIEW", 14.0));
    const auto eyeLevel = [&](double at) {
        scene::Scene s = showAt(engine, at);
        // From whichever of 16 bearings has the lowest ground `back` metres out (a fire in a dip seen
        // from the uphill side is seen from above, against the ground), at eye height over the higher
        // of that ground and the fire's, looking level at the fire: the hot air stands against the
        // valley beyond it.
        glm::vec3 eye = base;
        float lowest = 1e30f;
        for (int k = 0; k < 16; ++k) {
            const float a = 6.2831853f * static_cast<float>(k) / 16.0f;
            const glm::vec3 e = base + glm::vec3(std::cos(a), 0.0f, std::sin(a)) * back;
            const float g = ground.groundPoint(glm::vec2(e.x, e.z)).y;
            if (g < lowest) {
                lowest = g;
                eye = e;
            }
        }
        const float eyeY = std::max(lowest, base.y) + 1.7f;
        s.camera.position = glm::vec3(eye.x, eyeY, eye.z);
        s.camera.target = glm::vec3(base.x, eyeY - 0.3f, base.z);
        // The fire itself, so there is something to stand the hot air over: a squat emissive flame
        // and the warm light it throws.
        const scene::MeshId flame = s.addMesh(scene::makeIcosphere(0.3f, 2));
        scene::Entity& f = s.addEntity("fire", flame);
        f.transform.position = base + glm::vec3(0.0f, 0.35f, 0.0f);
        f.transform.scale = glm::vec3(1.0f, 1.7f, 1.0f);
        f.material.baseColor = glm::vec3(1.0f, 0.45f, 0.1f);
        f.material.emissiveColor = glm::vec3(1.0f, 0.42f, 0.08f);
        f.material.emissiveIntensity = 12.0f;
        scene::PunctualLight glow;
        glow.type = scene::PunctualLight::Type::Point;
        glow.position = base + glm::vec3(0.0f, 0.8f, 0.0f);
        glow.color = glm::vec3(1.0f, 0.55f, 0.2f);
        glow.intensity = 120.0f;
        glow.range = 10.0f;
        s.addLight(glow);
        return s;
    };
    scene::Scene hot = eyeLevel(t);
    CHECK(engine.effectStatus(heatId) == world::EffectStatus::Drawn);
    REQUIRE(hot.distortion.count == 1);
    const gpu::Image8 on = render(renderer, hot, t, kShowW, kShowH);
    CHECK(renderer.distortionStats().encoded);
    scene::Scene cold = hot;
    cold.distortion.count = 0;
    dump(render(renderer, cold, t, kShowW, kShowH), "glowmere-campfire-off");
    dump(on, "glowmere-campfire-on");
    for (int k = 1; k <= 3; ++k) {
        const double tk = t + k / 30.0;
        dump(render(renderer, eyeLevel(tk), tk, kShowW, kShowH), "glowmere-campfire-on-f" + std::to_string(k));
    }
    CHECK(ctx->errorCount() == 0);
}
