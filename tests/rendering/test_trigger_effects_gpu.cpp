// TRIGGER's effects on pixels (Effect Library Wave 2): Shockwave, Ripple and Velocity Distortion
// through DF's passes, and the remaining Particle Emitter looks through the particle renderer.
//
// The DF fixture is test_distortion_gpu.cpp's: an unlit black-and-white checker wall 30 m down the
// view axis, so a bend is unambiguous, bloom off unless a case is about the edge glow. Each case
// asks what only a frame can answer -- the front is a RING that moves outward frame by frame, a
// dormant trigger-activated instance leaves the frame byte-identical, the ripple bends inside its
// disc and nowhere else, the wake lies behind a moving owner -- and, with AVGEN_EFFECT_DUMP=<dir>,
// writes every arm as a PNG for a person to look at.

#include "app/engine.hpp"
#include "assets/image.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"
#include "world/effects/distortion_frame.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"
#include "world/effects/effect_trigger.hpp"

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

constexpr std::uint32_t kWidth = 320;
constexpr std::uint32_t kHeight = 200;

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

void dump(const gpu::Image8& image, const std::string& name) {
    const char* dir = std::getenv("AVGEN_EFFECT_DUMP");
    if (dir == nullptr || dir[0] == '\0') {
        return;
    }
    static_cast<void>(assets::writePng(fs::path(dir) / ("w2-" + name + ".png"), image.width, image.height, image.rgba));
}

scene::TextureData checker() {
    scene::TextureData t;
    t.name = "w2-checker";
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

// Camera at the origin looking down -Z; the checker wall at z = -30 facing it.
scene::Scene wallScene() {
    scene::Scene s;
    const scene::MeshId plane = s.addMesh(scene::makePlane(30.0f, 1));
    scene::Entity& wall = s.addEntity("wall", plane);
    wall.transform.position = glm::vec3(0.0f, 0.0f, -30.0f);
    wall.transform.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    scene::Material m;
    m.baseColor = glm::vec3(1.0f);
    m.unlit = true;
    m.emissiveIntensity = 0.0f;
    wall.material = m;
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

std::size_t changedPixels(const gpu::Image8& a, const gpu::Image8& b) {
    std::size_t n = 0;
    for (std::size_t i = 0; i + 3 < a.rgba.size(); i += 4) {
        int sum = 0;
        for (int c = 0; c < 3; ++c) {
            sum += std::abs(static_cast<int>(a.rgba[i + c]) - static_cast<int>(b.rgba[i + c]));
        }
        n += sum > 24 ? 1u : 0u;
    }
    return n;
}

// The mean distance, in pixels, of the changed pixels from `c`: where the ring is.
float ringRadiusPx(const gpu::Image8& a, const gpu::Image8& b, glm::vec2 c) {
    double sum = 0.0;
    std::size_t n = 0;
    for (std::uint32_t y = 0; y < a.height; ++y) {
        for (std::uint32_t x = 0; x < a.width; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * a.width + x) * 4;
            int d = 0;
            for (int k = 0; k < 3; ++k) {
                d += std::abs(static_cast<int>(a.rgba[i + k]) - static_cast<int>(b.rgba[i + k]));
            }
            if (d > 24) {
                sum += glm::length(glm::vec2(x + 0.5f, y + 0.5f) - c);
                ++n;
            }
        }
    }
    return n > 0 ? static_cast<float>(sum / static_cast<double>(n)) : 0.0f;
}

glm::vec2 project(const scene::Scene& s, glm::vec3 p) {
    const glm::mat4 vp = s.camera.projection(static_cast<float>(kWidth) / static_cast<float>(kHeight)) * s.camera.view();
    const glm::vec4 c = vp * glm::vec4(p, 1.0f);
    const glm::vec2 ndc = glm::vec2(c) / c.w;
    return {(ndc.x * 0.5f + 0.5f) * kWidth, (0.5f - ndc.y * 0.5f) * kHeight};
}

world::DistortionFrame frameOf(const world::EffectInstance& e, double seconds, const world::TriggerClock& clock,
                               const world::EffectSceneQuery* query = nullptr,
                               world::EffectStatus* statusOut = nullptr) {
    world::EffectContext ctx;
    ctx.seconds = seconds;
    ctx.scene = query;
    ctx.triggers = &clock;
    ctx.cameraPosition = glm::vec3(0.0f);
    ctx.cameraForward = glm::vec3(0.0f, 0.0f, -1.0f);
    const std::vector<world::EffectInstance> list{e};
    std::vector<world::EffectStatus> status(1);
    std::vector<std::string> reasons(1);
    world::DistortionFrame frame;
    world::buildDistortionFrame(list, ctx, frame, {}, status, reasons);
    if (statusOut != nullptr) {
        *statusOut = status[0];
    }
    return frame;
}

world::Trigger repeatAt(double phase, double period = 100.0) {
    world::Trigger t;
    t.source = world::TriggerSource::Repeat;
    t.phase = phase;
    t.period = period;
    return t;
}

world::EffectInstance placed(world::EffectKind kind, const char* key, glm::vec3 at, world::Trigger trig) {
    world::EffectInstance e = world::makeEffect(kind, key);
    e.id = key;
    e.activation = world::Activation::Trigger;
    e.timing.trigger = std::move(trig);
    e.values.setFloat(std::string(key) + "/offsetX", at.x);
    e.values.setFloat(std::string(key) + "/offsetY", at.y);
    e.values.setFloat(std::string(key) + "/offsetZ", at.z);
    return e;
}

// An owner flying across the view, left to right, 20 m out: what HIST would answer for it.
class FlyingOwner final : public world::EffectSceneQuery {
public:
    double now = 2.0;
    [[nodiscard]] glm::vec3 at(double t) const { return {-18.0f + 12.0f * static_cast<float>(t), 0.0f, -20.0f}; }
    [[nodiscard]] bool nodePosition(std::string_view, glm::vec3& out) const override {
        out = at(now);
        return true;
    }
    [[nodiscard]] bool nodeView(std::string_view, world::NodeView& out) const override {
        out = world::NodeView{};
        out.world = glm::mat4(1.0f);
        out.world[3] = glm::vec4(at(now), 1.0f);
        out.boundsMin = at(now) - glm::vec3(1.2f, 0.4f, 1.2f);
        out.boundsMax = at(now) + glm::vec3(1.2f, 0.4f, 1.2f);
        out.hasBounds = true;
        out.entityCount = 1;
        return true;
    }
    [[nodiscard]] bool nodeVelocity(std::string_view, glm::vec3& out) const override {
        out = glm::vec3(12.0f, 0.0f, 0.0f);
        return true;
    }
    [[nodiscard]] bool nodeDrawnPosition(std::string_view, double t, glm::vec3& out) const override {
        if (t < 0.0 || t > now) {
            return false;
        }
        out = at(t);
        return true;
    }
};

} // namespace

TEST_CASE("Shockwave on pixels: a ring that moves outward frame by frame, and a dormant one draws nothing",
          "[gpu][effects][distortion][shockwave]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    const world::TriggerClock clock;

    scene::Scene s = wallScene();
    const gpu::Image8 plain = render(renderer, s);
    dump(plain, "shockwave-off");
    const glm::vec3 centre(0.0f, 0.0f, -18.0f);
    world::EffectInstance e = placed(world::EffectKind::Shockwave, "shockwave", centre, repeatAt(1.0));
    e.values.setFloat("shockwave/maxRadius", 12.0f);
    e.values.setFloat("shockwave/duration", 1.2f);
    e.values.setFloat("shockwave/thickness", 1.2f);
    e.values.setFloat("shockwave/strength", 1.5f);

    // The gate: before its trigger the instance is Dormant, the frame block is empty, and the frame is
    // byte-identical to one with no instance at all.
    world::EffectStatus status{};
    s.distortion = frameOf(e, 0.9, clock, nullptr, &status);
    CHECK(status == world::EffectStatus::Dormant);
    REQUIRE(s.distortion.count == 0);
    const gpu::Image8 dormant = render(renderer, s);
    CHECK(gpu::hashImage(dormant) == gpu::hashImage(plain));
    CHECK_FALSE(renderer.distortionStats().encoded);

    const glm::vec2 c = project(s, centre);
    float lastRing = 0.0f;
    int k = 0;
    for (const double t : {1.08, 1.2, 1.36, 1.56, 1.8}) {
        INFO("t = " << t);
        s.distortion = frameOf(e, t, clock);
        REQUIRE(s.distortion.count == 1);
        const gpu::Image8 on = render(renderer, s, t);
        dump(on, "shockwave-frame" + std::to_string(k++));
        const std::size_t changed = changedPixels(plain, on);
        INFO("changed " << changed);
        CHECK(changed > 150);
        const float ring = ringRadiusPx(plain, on, c);
        INFO("ring at " << ring << " px, previous " << lastRing);
        CHECK(ring > lastRing);
        lastRing = ring;
    }
    CHECK(ctx->errorCount() == 0);

    // The Energy Blast look: a bright leading edge, with bloom on.
    world::EffectInstance blast = e;
    REQUIRE(world::applyEffectStyle(blast, world::EffectKind::Shockwave, "Energy Blast"));
    blast.timing.trigger = repeatAt(1.0);
    blast.values.setFloat("shockwave/maxRadius", 12.0f);
    s.post.bloomEnabled = true;
    const gpu::Image8 plainBloom = render(renderer, [&] {
        scene::Scene q = s;
        q.distortion = world::DistortionFrame{};
        return q;
    }());
    s.distortion = frameOf(blast, 1.3, clock);
    const gpu::Image8 glowing = render(renderer, s, 1.3);
    dump(glowing, "shockwave-energy-blast");
    CHECK(changedPixels(plainBloom, glowing) > 300);
    // Its expansion, for a person to look at: the front born, at its brightest, and spreading.
    for (const double t : {1.04, 1.1, 1.2}) {
        s.distortion = frameOf(blast, t, clock);
        dump(render(renderer, s, t), "shockwave-energy-blast-" + std::to_string(static_cast<int>(t * 100.0 + 0.5)));
    }

    // After a drawn frame, a dormant one is byte-identical again: nothing of DF lingers.
    s.post.bloomEnabled = false;
    s.distortion = frameOf(e, 0.5, clock);
    CHECK(gpu::hashImage(render(renderer, s)) == gpu::hashImage(plain));
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Ripple on pixels: rings inside the membrane's disc, nothing outside it",
          "[gpu][effects][distortion][shockwave]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    const world::TriggerClock clock;
    scene::Scene s = wallScene();
    const gpu::Image8 plain = render(renderer, s);
    const glm::vec3 centre(0.0f, 0.0f, -16.0f);
    world::EffectInstance e = placed(world::EffectKind::Ripple, "ripple", centre, repeatAt(1.0));
    REQUIRE(world::applyEffectStyle(e, world::EffectKind::Ripple, "Membrane Touch"));
    e.timing.trigger = repeatAt(1.0);
    e.values.setFloat("ripple/radius", 6.0f);
    e.values.setFloat("ripple/amplitude", 0.4f);
    int k = 0;
    for (const double t : {1.5, 2.1}) {
        s.distortion = frameOf(e, t, clock);
        REQUIRE(s.distortion.count == 1);
        const gpu::Image8 on = render(renderer, s, t);
        dump(on, "ripple-frame" + std::to_string(k++));
        const std::size_t changed = changedPixels(plain, on);
        INFO("changed " << changed);
        CHECK(changed > 200);
        // Nothing outside the disc (plus the resolve's small neighbourhood) moved.
        const glm::vec2 c = project(s, centre);
        const float rPx = glm::length(project(s, centre + glm::vec3(6.0f, 0.0f, 0.0f)) - c) + 6.0f;
        std::size_t outside = 0;
        for (std::uint32_t y = 0; y < kHeight; ++y) {
            for (std::uint32_t x = 0; x < kWidth; ++x) {
                const std::size_t i = (static_cast<std::size_t>(y) * kWidth + x) * 4;
                if (glm::length(glm::vec2(x + 0.5f, y + 0.5f) - c) > rPx &&
                    (plain.rgba[i] != on.rgba[i] || plain.rgba[i + 1] != on.rgba[i + 1])) {
                    ++outside;
                }
            }
        }
        CHECK(outside == 0);
    }
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("Velocity Distortion on pixels: a wake behind a moving owner, nothing ahead of it",
          "[gpu][effects][distortion][shockwave]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    const world::TriggerClock clock;
    scene::Scene s = wallScene();
    const gpu::Image8 plain = render(renderer, s);
    FlyingOwner owner;
    world::EffectInstance e = world::makeEffect(world::EffectKind::VelocityDistortion, "wake");
    e.id = "wake";
    e.owner = world::EffectOwner::entity("craft");
    REQUIRE(world::applyEffectStyle(e, world::EffectKind::VelocityDistortion, "Warp Contrail"));
    e.values.setFloat("velocityDistortion/strength", 2.0f);
    for (const double t : {2.0, 2.05}) {
        owner.now = t;
        s.distortion = frameOf(e, t, clock, &owner);
        REQUIRE(s.distortion.count >= 4);
        const gpu::Image8 on = render(renderer, s, t);
        dump(on, t < 2.01 ? "wake-a" : "wake-b");
        const std::size_t changed = changedPixels(plain, on);
        INFO("changed " << changed);
        CHECK(changed > 150);
        // Ahead of the owner (to its right on screen, beyond its bounds) nothing moved.
        const float aheadX = project(s, owner.at(t) + glm::vec3(3.0f, 0.0f, 0.0f)).x;
        std::size_t ahead = 0;
        for (std::uint32_t y = 0; y < kHeight; ++y) {
            for (std::uint32_t x = static_cast<std::uint32_t>(std::max(aheadX, 0.0f)); x < kWidth; ++x) {
                const std::size_t i = (static_cast<std::size_t>(y) * kWidth + x) * 4;
                ahead += plain.rgba[i] != on.rgba[i] ? 1u : 0u;
            }
        }
        CHECK(ahead == 0);
    }
    CHECK(ctx->errorCount() == 0);
}

namespace {

// test_particle_emitter_gpu.cpp's runner, for the Wave 2 looks: a fresh engine with one World-owned
// emitter in `style`, placed where a new composition's camera looks (a carried look's box around the
// camera itself), the preset's own rows otherwise untouched.
gpu::Image8 runEmitter(gpu::Context& ctx, const char* style, int frames = 150) {
    gpu::ShaderLibrary shaders(ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(ctx, shaders);
    REQUIRE(renderer.init().has_value());
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    engine.setViewport(kWidth, kHeight);
    if (style != nullptr) {
        world::EffectInstance e = world::makeEffect(world::EffectKind::ParticleEmitter, "Particles");
        e.id.clear();
        REQUIRE(world::applyEffectStyle(e, world::EffectKind::ParticleEmitter, style));
        if (e.activation == world::Activation::Trigger) {
            e.timing.trigger = repeatAt(0.2, 0.8); // no audio here: a schedule stands in for the beat
        }
        std::vector<world::EffectInstance> list;
        REQUIRE(world::insertEffect(list, e).has_value());
        REQUIRE(engine.setEffects(list).has_value());
        const std::string id = engine.effects()[0].id;
        engine.update(FrameTime{0.0, 1.0 / 60.0, 0});
        const scene::Camera cam = engine.scene().camera;
        const float d = glm::length(cam.target - cam.position);
        const auto set = [&](const char* leaf, float v) {
            auto* p = engine.params().find(world::effectParameterPrefix(id) + leaf);
            REQUIRE(p != nullptr);
            p->setBaseComponent(0, v);
        };
        // A carried look's World X/Y/Z is already an offset from the camera, and its box the preset's
        // own: left alone, so the frame is the preset at its authored scale. A local look is moved to
        // where the camera looks.
        const std::string st(style);
        const bool carried = e.values.getFloat("particleEmitter/look", 0.0f) >= 2.5f &&
                             e.values.getFloat("particleEmitter/look", 0.0f) <= 5.5f;
        if (!carried) {
            set("centerX", cam.target.x);
            set("centerY", cam.target.y);
            set("centerZ", cam.target.z);
            set("radius", std::min(e.values.getFloat("particleEmitter/radius", 1.0f), d * 0.5f));
        }
    }
    gpu::Image8 last;
    for (int i = 0; i <= frames; ++i) {
        const double t = i / 60.0;
        const FrameTime ft{t, 1.0 / 60.0, static_cast<std::uint64_t>(i)};
        engine.update(ft);
        auto img = renderer.renderToImage(engine.scene(), ft, kWidth, kHeight);
        REQUIRE(img.has_value());
        last = std::move(*img);
    }
    CHECK(ctx.errorCount() == 0);
    return last;
}

} // namespace

TEST_CASE("every Wave 2 Particle Emitter preset reaches the frame", "[gpu][effects][emit][shockwave]") {
    auto ctx = makeContext();
    const gpu::Image8 off = runEmitter(*ctx, nullptr);
    dump(off, "emitter-off");
    for (const char* style :
         {"Light Snow", "Blizzard", "Magical Snow", "Drizzle", "Storm", "Neon Rain", "Volcanic Ashfall", "Burned Forest",
          "Autumn Leaves", "Cherry Petals", "Glowmere Drift", "Mushroom Puff", "Glowmere Spores", "Spore Burst",
          "Nebula Drift", "Hyperspace Dust", "Vortex Motes", "Glowmere Canopy", "Burning Debris", "Healing Motes"}) {
        INFO(style);
        const gpu::Image8 on = runEmitter(*ctx, style);
        dump(on, std::string("emitter-") + style);
        const std::size_t changed = changedPixels(off, on);
        INFO("visibly changed pixels: " << changed);
        CHECK(changed > 40);
    }
}
