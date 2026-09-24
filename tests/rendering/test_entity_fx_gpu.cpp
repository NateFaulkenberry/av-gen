// FXL and LIGHTMOD on pixels (Effect Library Wave 1, packages 1.4, 1.5 and the 1.9 light types).
//
// The CPU suite (tests/unit/test_entity_fx.cpp) proves what the builder writes. What only a GPU can
// answer is whether the lit shader does what the record says, on the draws the record names, and on
// nothing else:
//
//   * a Glow of gain 4 puts four times the light in the emission target that the control does --
//     and exactly what a material with four times the emission would;
//   * with no lane effect live the frame is byte-identical to the frame without FXL, and a neutral
//     Glow (gain 1, nothing added) is byte-identical too: the gate and the arithmetic are both inert;
//   * a Glow and a Pulse on one owner both reach the frame; two owners' Glows do not touch each other;
//   * Bloom Source changes what the bloom sees without changing the surface's own colour;
//   * a travelling band moves along its owner between two seconds;
//   * a Glow's spill light lights the ground under it, and the 17th spill is Partial with a reason.
//
// Scenes are built directly and the frame block comes from the real builder through a small scene
// query, so the path under test is builder -> scene block -> renderer -> shader, with no engine.
// With AVGEN_EFFECT_DUMP=<dir> every arm is written there as a PNG for a person to look at.

#include "assets/image.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"
#include "support/image_diff.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/entity_fx.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
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

// Fresh temporal history and drawn twice, so an arm never inherits the previous arm's history.
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
    static_cast<void>(assets::writePng(fs::path(dir) / (name + ".png"), image.width, image.height, image.rgba));
}

// Every entity is its own node, named as the entity, with a box from its transform.
class EntityScene final : public world::EffectSceneQuery {
public:
    explicit EntityScene(const scene::Scene& s) : scene_(s) {}
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

struct Evaluated {
    std::vector<EffectStatus> status;
    std::vector<std::string> reasons;
};

// The real builder, writing the frame block onto the scene the way `Engine::updateEffects` does.
Evaluated evaluate(scene::Scene& s, const std::vector<world::EffectInstance>& effects, double seconds = 1.0) {
    const EntityScene query(s);
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

// A glow that only multiplies: no added light, no rim, no spill.
world::EffectInstance pureGain(const std::string& owner, float gain) {
    world::EffectInstance e = effectOn(EffectKind::Glow, owner, owner + "-gain");
    e.values.setFloat("glow/gain", gain);
    e.values.setFloat("glow/glow", 0.0f);
    e.values.setFloat("glow/rim", 0.0f);
    e.values.setBool("glow/spill", false);
    return e;
}

// Two orbs over a ground plane, left and right of centre, dim key light, dark sky. The orbs' own
// material emits a little (so a gain has something to multiply), well below saturation.
scene::Scene twoOrbs(float emissive = 0.08f) {
    scene::Scene s;
    const scene::MeshId plane = s.addMesh(scene::makePlane(30.0f, 1));
    const scene::MeshId sphere = s.addMesh(scene::makeIcosphere(1.0f, 3));
    scene::Entity& ground = s.addEntity("ground", plane);
    ground.material.baseColor = glm::vec3(0.4f, 0.4f, 0.42f);
    ground.material.roughness = 0.9f;
    ground.material.emissiveIntensity = 0.0f;
    for (const auto& [name, x] : {std::pair<const char*, float>{"orbA", -2.4f}, {"orbB", 2.4f}}) {
        scene::Entity& orb = s.addEntity(name, sphere);
        orb.transform.position = glm::vec3(x, 1.4f, 0.0f);
        orb.material.baseColor = glm::vec3(0.3f, 0.3f, 0.32f);
        orb.material.roughness = 0.5f;
        orb.material.metallic = 0.0f;
        orb.material.emissiveColor = glm::vec3(1.0f, 0.6f, 0.3f);
        orb.material.emissiveIntensity = emissive;
    }
    scene::PunctualLight key;
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(-0.3f, -1.0f, -0.4f));
    key.intensity = 0.6f;
    s.addLight(key);
    s.camera.position = glm::vec3(0.0f, 3.0f, 10.0f);
    s.camera.target = glm::vec3(0.0f, 1.2f, 0.0f);
    s.camera.fovYRadians = glm::radians(45.0f);
    s.environment.backgroundColor = glm::vec3(0.01f, 0.012f, 0.02f);
    return s;
}

// Mean of the red channel over pixels of one half of the frame where `mask` is lit.
double meanOver(const gpu::Image8& img, const gpu::Image8& mask, bool leftHalf, int channel = 0) {
    double sum = 0.0;
    std::size_t n = 0;
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            if ((x < img.width / 2) != leftHalf) {
                continue;
            }
            const std::size_t i = (static_cast<std::size_t>(y) * img.width + x) * 4;
            if (mask.rgba[i + static_cast<std::size_t>(channel)] < 8) {
                continue;
            }
            sum += img.rgba[i + static_cast<std::size_t>(channel)];
            ++n;
        }
    }
    REQUIRE(n > 50);
    return sum / static_cast<double>(n);
}

// Pixels in one half that differ visibly (summed channels > 24 of 765).
std::size_t differingIn(const gpu::Image8& a, const gpu::Image8& b, int half /* -1 left, 1 right, 0 all */) {
    REQUIRE(a.rgba.size() == b.rgba.size());
    std::size_t differ = 0;
    for (std::uint32_t y = 0; y < a.height; ++y) {
        for (std::uint32_t x = 0; x < a.width; ++x) {
            if ((half < 0 && x >= a.width / 2) || (half > 0 && x < a.width / 2)) {
                continue;
            }
            const std::size_t i = (static_cast<std::size_t>(y) * a.width + x) * 4;
            int sum = 0;
            for (int c = 0; c < 3; ++c) {
                sum += std::abs(static_cast<int>(a.rgba[i + c]) - static_cast<int>(b.rgba[i + c]));
            }
            differ += sum > 24 ? 1u : 0u;
        }
    }
    return differ;
}

constexpr std::size_t kVisible = 40;

struct Harness {
    std::unique_ptr<gpu::Context> ctx = makeContext();
    gpu::ShaderLibrary shaders{*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)}};
    rendering::SceneRenderer renderer{*ctx, shaders};
    Harness() { REQUIRE(renderer.init().has_value()); }
};

} // namespace

TEST_CASE("FXL: a Glow of gain 4 puts four times the light in the emission target", "[gpu][effects][fxl]") {
    Harness h;
    // The emission target itself, drawn linearly at scale 1 (aux view 4 is `e.rgb * scale`).
    h.renderer.setAuxDebugView(rendering::AuxDebugView::Emission);
    h.renderer.setAuxDebugScale(1.0f);

    scene::Scene control = twoOrbs(0.05f);
    const gpu::Image8 before = render(h.renderer, control);

    scene::Scene glowing = twoOrbs(0.05f);
    const Evaluated ev = evaluate(glowing, {pureGain("orbA", 4.0f)});
    REQUIRE(ev.status[0] == EffectStatus::Drawn);
    const gpu::Image8 after = render(h.renderer, glowing);

    // What "four times the emission" means, rendered by the material path the renderer already had.
    scene::Scene reference = twoOrbs(0.05f);
    reference.entities[1].material.emissiveIntensity = 0.2f;
    const gpu::Image8 fourfold = render(h.renderer, reference);
    h.renderer.setAuxDebugView(rendering::AuxDebugView::None);

    dump(before, "fxl-gain-emission-control");
    dump(after, "fxl-gain-emission-glow4");
    dump(fourfold, "fxl-gain-emission-material4");

    const double a = meanOver(before, before, true);
    const double b = meanOver(after, before, true);
    INFO("emission-target red over orb A: control " << a << ", Glow x4 " << b << ", ratio " << b / a);
    CHECK(b / a > 3.6);
    CHECK(b / a < 4.4);
    // The right orb has no effect and must not move at all.
    const auto right = testing::byteDiff(before.rgba, after.rgba);
    CHECK(differingIn(before, after, 1) == 0);
    // And the glow is the same light a four-times-brighter material makes, pixel for pixel within
    // one 8-bit step of rounding.
    std::size_t off = 0;
    for (std::size_t i = 0; i < after.rgba.size(); ++i) {
        off += std::abs(static_cast<int>(after.rgba[i]) - static_cast<int>(fourfold.rgba[i])) > 1 ? 1u : 0u;
    }
    INFO(right.describe());
    CHECK(off == 0);
}

TEST_CASE("FXL off is byte-identical: no live lane effect, and a neutral Glow, change no pixel",
          "[gpu][effects][fxl][gate]") {
    Harness h;
    const scene::Scene plain = twoOrbs();
    const gpu::Image8 base = render(h.renderer, plain);

    SECTION("disabled and dormant instances write no record, and the frame is the frame without FXL") {
        scene::Scene s = twoOrbs();
        std::vector<world::EffectInstance> effects{pureGain("orbA", 4.0f), effectOn(EffectKind::Pulse, "orbB", "p")};
        effects[0].enabled = false;
        effects[1].activation = world::Activation::Window;
        effects[1].timing.windowStart = 50.0;
        const Evaluated ev = evaluate(s, effects);
        CHECK(ev.status[0] == EffectStatus::Disabled);
        CHECK(ev.status[1] == EffectStatus::Dormant);
        REQUIRE(s.entityFx.empty());
        const auto d = testing::byteDiff(base.rgba, render(h.renderer, s).rgba);
        INFO(d.describe());
        REQUIRE(d.identical());
    }
    SECTION("a record uploaded for an entity that is not drawn changes no other draw") {
        scene::Scene s = twoOrbs();
        s.entities[1].visible = false; // orb A still owns a record; the renderer never draws it
        scene::Scene hidden = twoOrbs();
        hidden.entities[1].visible = false;
        const gpu::Image8 hiddenBase = render(h.renderer, hidden);
        const Evaluated ev = evaluate(s, {pureGain("orbA", 4.0f)});
        REQUIRE_FALSE(s.entityFx.empty());
        CHECK(s.entityFx.recordFor(1) != 0);
        const auto d = testing::byteDiff(hiddenBase.rgba, render(h.renderer, s).rgba);
        INFO(d.describe());
        REQUIRE(d.identical());
    }
    SECTION("a neutral Glow -- gain 1, nothing added -- is invisible though its gate is open") {
        scene::Scene s = twoOrbs();
        const Evaluated ev = evaluate(s, {pureGain("orbA", 1.0f)});
        REQUIRE(s.entityFx.recordFor(1) != 0);
        const auto d = testing::byteDiff(base.rgba, render(h.renderer, s).rgba);
        INFO(d.describe());
        CHECK(d.identical());
    }
    SECTION("the control: a real Glow does change the frame") {
        scene::Scene s = twoOrbs();
        static_cast<void>(evaluate(s, {pureGain("orbA", 4.0f)}));
        CHECK_FALSE(testing::byteDiff(base.rgba, render(h.renderer, s).rgba).identical());
    }
}

TEST_CASE("FXL: Glow and Pulse on one owner both act, and two owners' Glows are independent",
          "[gpu][effects][fxl]") {
    Harness h;
    const auto glow = [](const std::string& owner, glm::vec3 tint) {
        world::EffectInstance e = effectOn(EffectKind::Glow, owner, owner + "-glow");
        e.values.setColor("glow/tint", tint);
        e.values.setFloat("glow/gain", 1.5f);
        e.values.setFloat("glow/glow", 1.2f);
        e.values.setFloat("glow/rim", 3.0f);
        e.values.setColor("glow/rimColor", tint);
        return e;
    };
    world::EffectInstance pulse = effectOn(EffectKind::Pulse, "orbA", "orbA-pulse");
    pulse.values.setFloat("pulse/rate", 0.5f);
    pulse.values.setFloat("pulse/peak", 3.0f);
    pulse.values.setFloat("pulse/depth", 0.8f);
    constexpr double kCrest = 1.0; // sine at 0.5 Hz crests at t = 1 s

    const auto frameOf = [&](std::vector<world::EffectInstance> list, double t = kCrest) {
        scene::Scene s = twoOrbs();
        const Evaluated ev = evaluate(s, list, t);
        for (EffectStatus st : ev.status) {
            CHECK(st == EffectStatus::Drawn);
        }
        return render(h.renderer, s, t);
    };

    SECTION("Glow + Pulse on orb A") {
        const gpu::Image8 none = frameOf({});
        const gpu::Image8 g = frameOf({glow("orbA", {0.3f, 0.9f, 1.0f})});
        const gpu::Image8 p = frameOf({pulse});
        const gpu::Image8 both = frameOf({glow("orbA", {0.3f, 0.9f, 1.0f}), pulse});
        dump(none, "fxl-glowpulse-none");
        dump(g, "fxl-glowpulse-glow");
        dump(p, "fxl-glowpulse-pulse");
        dump(both, "fxl-glowpulse-both");
        const std::size_t glowAlone = differingIn(none, g, -1);
        const std::size_t pulseAlone = differingIn(none, p, -1);
        const std::size_t pulseOverGlow = differingIn(g, both, -1);
        const std::size_t glowOverPulse = differingIn(p, both, -1);
        INFO("glow alone " << glowAlone << " px, pulse alone " << pulseAlone << " px, pulse with glow "
                           << pulseOverGlow << " px, glow with pulse " << glowOverPulse << " px");
        REQUIRE(glowAlone > kVisible);
        REQUIRE(pulseAlone > kVisible);
        CHECK(pulseOverGlow > kVisible);
        CHECK(glowOverPulse > kVisible);
        // (Orb B's half is not held to zero here: at the crest the bloom's widest level carries a
        // few pixels of orb A's halo over the midline, which is the bloom working. Independence of
        // owners is the next section's question, asked at a gain whose halo stays on its side.)

        // The pulse is a waveform in time: its trough is a different frame from its crest.
        const gpu::Image8 trough = frameOf({glow("orbA", {0.3f, 0.9f, 1.0f}), pulse}, 0.0);
        dump(trough, "fxl-glowpulse-both-trough");
        CHECK(differingIn(trough, both, -1) > kVisible);
        for (int i = 0; i < 5; ++i) { // the waveform, sampled across one 2 s cycle, for the eye
            dump(frameOf({glow("orbA", {0.3f, 0.9f, 1.0f}), pulse}, 0.4 * i), "fxl-pulse-t" + std::to_string(i));
        }
    }

    SECTION("a Glow each on orb A and orb B") {
        const gpu::Image8 none = frameOf({});
        const gpu::Image8 a = frameOf({glow("orbA", {1.0f, 0.3f, 0.2f})});
        const gpu::Image8 b = frameOf({glow("orbB", {0.2f, 1.0f, 0.4f})});
        const gpu::Image8 both = frameOf({glow("orbA", {1.0f, 0.3f, 0.2f}), glow("orbB", {0.2f, 1.0f, 0.4f})});
        dump(both, "fxl-two-owners-both");
        CHECK(differingIn(none, a, -1) > kVisible);
        CHECK(differingIn(none, a, 1) == 0);  // A's glow leaves B's half alone...
        CHECK(differingIn(none, b, 1) > kVisible);
        CHECK(differingIn(none, b, -1) == 0); // ...and B's leaves A's
        // Together, each half is exactly what its own glow made it.
        CHECK(differingIn(a, both, -1) == 0);
        CHECK(differingIn(b, both, 1) == 0);
    }
}

TEST_CASE("Pulse: a travelling band moves along its owner", "[gpu][effects][fxl]") {
    Harness h;
    scene::Scene base = twoOrbs(0.0f);
    // A tall column for orb A, so "along the owner" is a visible distance.
    base.entities[1].transform.scale = glm::vec3(0.8f, 2.6f, 0.8f);
    base.entities[1].transform.position.y = 2.6f;
    world::EffectInstance glow = effectOn(EffectKind::Glow, "orbA", "col-glow");
    glow.values.setFloat("glow/gain", 1.0f);
    glow.values.setFloat("glow/glow", 3.0f);
    glow.values.setFloat("glow/rim", 0.0f);
    world::EffectInstance band = effectOn(EffectKind::Pulse, "orbA", "col-band");
    band.values.setFloat("pulse/mode", 1.0f);
    band.values.setFloat("pulse/rate", 0.5f);
    band.values.setFloat("pulse/peak", 2.0f);
    band.values.setFloat("pulse/depth", 1.0f);
    band.values.setFloat("pulse/bandWidth", 0.15f);

    const auto at = [&](double t) {
        scene::Scene s = base;
        const Evaluated ev = evaluate(s, {glow, band}, t);
        CHECK(ev.status[1] == EffectStatus::Drawn);
        return render(h.renderer, s, t);
    };
    // Rows of the left half weighted by how much light each holds: the band's height on screen.
    const auto bandRow = [](const gpu::Image8& img) {
        double num = 0.0;
        double den = 0.0;
        for (std::uint32_t y = 0; y < img.height; ++y) {
            for (std::uint32_t x = 0; x < img.width / 2; ++x) {
                const std::size_t i = (static_cast<std::size_t>(y) * img.width + x) * 4;
                // The band is cyan and the ground is grey: weight by how much greener than red a
                // pixel is, so the lit ground does not pull the centroid down.
                const double v = static_cast<double>(img.rgba[i + 1]) - static_cast<double>(img.rgba[i]) - 20.0;
                if (v > 0.0) {
                    num += v * y;
                    den += v;
                }
            }
        }
        return den > 0.0 ? num / den : -1.0;
    };
    const gpu::Image8 low = at(0.6);   // the band a third of the way up...
    const gpu::Image8 high = at(1.4);  // ...and two thirds
    dump(low, "fxl-band-low");
    dump(high, "fxl-band-high");
    for (int i = 0; i < 5; ++i) {
        dump(at(0.4 * i), "fxl-band-t" + std::to_string(i));
    }
    const double rowLow = bandRow(low);
    const double rowHigh = bandRow(high);
    INFO("band centroid row at 0.6 s: " << rowLow << ", at 1.4 s: " << rowHigh);
    REQUIRE(rowLow > 0.0);
    REQUIRE(rowHigh > 0.0);
    CHECK(rowHigh < rowLow - 5.0); // image rows grow downward: higher on the column is a smaller row
}

TEST_CASE("Bloom Source: the bloom sees more of the owner, and its colour is unchanged",
          "[gpu][effects][fxl]") {
    Harness h;
    const auto brightOrbs = [] {
        scene::Scene s = twoOrbs(0.0f);
        s.entities[1].material.baseColor = glm::vec3(0.95f, 0.95f, 0.9f);
        s.lights[0].intensity = 6.0f; // a lit surface well over the bloom threshold, emitting nothing
        s.post.bloomEnabled = true;
        s.post.bloomThreshold = 0.8f;
        s.post.bloomIntensity = 0.6f;
        s.post.bloomEmissionWeight = 1.0f; // selective bloom on: the case Bloom Source exists for
        return s;
    };
    scene::Scene none = brightOrbs();
    const gpu::Image8 plain = render(h.renderer, none);
    h.renderer.setAuxDebugView(rendering::AuxDebugView::Emission);
    const gpu::Image8 plainEmission = render(h.renderer, none);
    h.renderer.setAuxDebugView(rendering::AuxDebugView::None);

    scene::Scene source = brightOrbs();
    world::EffectInstance bloom = effectOn(EffectKind::BloomSource, "orbA", "bloom");
    bloom.values.setFloat("bloomSource/weight", 1.0f);
    const Evaluated ev = evaluate(source, {bloom});
    REQUIRE(ev.status[0] == EffectStatus::Drawn);
    const gpu::Image8 halo = render(h.renderer, source);
    h.renderer.setAuxDebugView(rendering::AuxDebugView::Emission);
    const gpu::Image8 haloEmission = render(h.renderer, source);
    h.renderer.setAuxDebugView(rendering::AuxDebugView::None);
    dump(plain, "fxl-bloomsource-off");
    dump(halo, "fxl-bloomsource-on");
    dump(plainEmission, "fxl-bloomsource-emission-off");
    dump(haloEmission, "fxl-bloomsource-emission-on");

    // The emission target over orb A rose from nothing...
    CHECK(differingIn(plainEmission, haloEmission, -1) > kVisible);
    // ...and the picture gained a halo AROUND orb A: pixels off the orb, near it, got brighter.
    std::size_t brighterAround = 0;
    for (std::uint32_t y = 0; y < kHeight; ++y) {
        for (std::uint32_t x = 0; x < kWidth / 2; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * kWidth + x) * 4;
            const bool onOrb = haloEmission.rgba[i] > 8;
            if (!onOrb && halo.rgba[i] > plain.rgba[i] + 3) {
                ++brighterAround;
            }
        }
    }
    INFO("pixels off orb A brightened by the halo: " << brighterAround);
    CHECK(brighterAround > kVisible);
    // Orb B, with no effect, is untouched in the emission target.
    CHECK(differingIn(plainEmission, haloEmission, 1) == 0);
}

TEST_CASE("LIGHTMOD: a Glow's spill lights the ground, and the 17th spill is Partial with a reason",
          "[gpu][effects][fxl][lightmod]") {
    Harness h;
    // Orb A, not emissive, low over the ground; the spill should pool light beneath it.
    const auto scene0 = [] {
        scene::Scene s = twoOrbs(0.0f);
        s.entities[1].transform.position.y = 1.2f;
        s.lights[0].intensity = 0.15f; // a dark night, so a pool of light reads
        return s;
    };
    world::EffectInstance glow = effectOn(EffectKind::Glow, "orbA", "spill");
    glow.values.setColor("glow/tint", {1.0f, 0.55f, 0.2f});
    glow.values.setFloat("glow/gain", 1.0f);
    glow.values.setFloat("glow/glow", 1.0f);
    glow.values.setFloat("glow/rim", 0.0f);
    glow.values.setFloat("glow/spillIntensity", 30.0f);
    glow.values.setFloat("glow/spillRange", 4.0f);

    scene::Scene off = scene0();
    static_cast<void>(evaluate(off, {glow}));
    REQUIRE(off.entityFx.lights.count == 0);
    const gpu::Image8 noSpill = render(h.renderer, off);

    world::EffectInstance spilling = glow;
    spilling.values.setBool("glow/spill", true);
    scene::Scene on = scene0();
    const Evaluated ev = evaluate(on, {spilling});
    CHECK(ev.status[0] == EffectStatus::Drawn);
    REQUIRE(on.entityFx.lights.count == 1);
    const gpu::Image8 withSpill = render(h.renderer, on);
    const std::uint32_t shadedWithSpill = h.renderer.stats().shadedLights;
    dump(noSpill, "fxl-spill-off");
    dump(withSpill, "fxl-spill-on");

    // The ground below and around orb A brightened in the spill's colour: red rises more than blue.
    std::size_t warmer = 0;
    for (std::uint32_t y = kHeight / 2; y < kHeight; ++y) {
        for (std::uint32_t x = 0; x < kWidth / 2; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * kWidth + x) * 4;
            const int dr = static_cast<int>(withSpill.rgba[i]) - static_cast<int>(noSpill.rgba[i]);
            const int db = static_cast<int>(withSpill.rgba[i + 2]) - static_cast<int>(noSpill.rgba[i + 2]);
            warmer += (dr > 6 && dr > db) ? 1u : 0u;
        }
    }
    INFO("ground pixels warmed by the spill: " << warmer);
    CHECK(warmer > 200);
    // The spill is one more light in the shading, and it is unshadowed.
    render(h.renderer, off);
    CHECK(shadedWithSpill == h.renderer.stats().shadedLights + 1);

    SECTION("seventeen spills: sixteen lights, and the seventeenth glow says why it has none") {
        scene::Scene many = scene0();
        const scene::MeshId sphere = many.entities[1].mesh;
        std::vector<world::EffectInstance> effects;
        for (int i = 0; i < 17; ++i) {
            const std::string name = "lamp" + std::to_string(i);
            scene::Entity& lamp = many.addEntity(name, sphere);
            lamp.transform.position = glm::vec3(-8.0f + static_cast<float>(i), 0.4f, -6.0f - static_cast<float>(i % 3));
            lamp.transform.scale = glm::vec3(0.25f);
            world::EffectInstance e = spilling;
            e.id = name;
            e.name = name;
            e.owner = world::EffectOwner::entity(name);
            e.values.setFloat("glow/spillIntensity", 4.0f);
            effects.push_back(e);
        }
        const Evaluated crowd = evaluate(many, effects);
        CHECK(many.entityFx.lights.count == world::kEffectLightBudget);
        std::size_t partial = 0;
        for (std::size_t i = 0; i < effects.size(); ++i) {
            if (crowd.status[i] == EffectStatus::Partial) {
                ++partial;
                CHECK(crowd.reasons[i].find("budget (16)") != std::string::npos);
            } else {
                CHECK(crowd.status[i] == EffectStatus::Drawn);
            }
        }
        CHECK(partial == 1);
        const gpu::Image8 crowded = render(h.renderer, many);
        dump(crowded, "fxl-spill-seventeen");
        CHECK(h.renderer.stats().shadedLights >= world::kEffectLightBudget);
    }
}
