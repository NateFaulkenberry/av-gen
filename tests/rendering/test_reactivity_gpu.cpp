// ADR-927 on pixels: a route the default proposer proposes reaches the picture, for every kind of
// target it proposes.
//
// The glade (tests/support/reactivity_fixture.hpp) is loaded into an offline engine with its groove
// and its sections; the default proposal is compiled and installed exactly as the Director installs a
// plan. For each kind of target, one proposed route is taken, a moment is found at which its source is
// active -- a detected kick, clap or hat from the engine's own analysis, a downbeat, a moment inside
// the drop -- and the frame there is drawn twice: with the route, and with only that route switched
// off. The pixels must differ where the target is, and in the direction the route pushes. That is the
// control arm: the same frame without the route. A kick route is also drawn in the kickless break,
// where it must change nothing.
//
// Kinds: a hero's glow, a scatter layer's glow, its hue (a rotation of the displayed hue by the
// section's key, measured), its light wave, a hero part's own light wave, particles, a practical light,
// the ecology light, the fog, the wind, the water, a world effect (the aurora), and a lamp layer.
//
// With AVGEN_REACTIVITY_DUMP=<dir> every arm is written as a PNG for a person to look at.

#include "app/directing_context.hpp"
#include "app/engine.hpp"
#include "assets/image.hpp"
#include "core/color.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "directing/compiler.hpp"
#include "directing/reactivity_proposer.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "support/gltf_fixture.hpp"
#include "support/reactivity_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

using namespace avgen;
using namespace avgen::directing;
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
    Harness() {
        REQUIRE(renderer.init().has_value());
        renderer.setParticleWarmUpFrames(120); // a swarm in the frame, not the bloom-in
    }
    // Fresh temporal history and drawn twice, so an arm never inherits the previous arm's history.
    gpu::Image8 render(const scene::Scene& s, double seconds, bool emission) {
        FrameTime t{};
        t.renderTime = seconds;
        renderer.setAuxDebugView(emission ? rendering::AuxDebugView::Emission : rendering::AuxDebugView::None);
        renderer.setAuxDebugScale(0.25f);
        renderer.resetTemporalHistory();
        auto first = renderer.renderToImage(s, t, kWidth, kHeight);
        REQUIRE(first.has_value());
        auto img = renderer.renderToImage(s, t, kWidth, kHeight);
        REQUIRE(img.has_value());
        renderer.setAuxDebugView(rendering::AuxDebugView::None);
        return std::move(*img);
    }
};

void dump(const gpu::Image8& image, const std::string& name) {
    const char* dir = std::getenv("AVGEN_REACTIVITY_DUMP");
    if (dir == nullptr || dir[0] == '\0') {
        return;
    }
    static_cast<void>(assets::writePng(fs::path(dir) / (name + ".png"), image.width, image.height, image.rgba));
}

// The pixels whose summed |difference| is over 6 of 765, and their mean rgb in each image.
struct Diff {
    std::size_t changed = 0;
    glm::dvec3 meanOn{0.0};
    glm::dvec3 meanOff{0.0};
    [[nodiscard]] double lumaOn() const { return meanOn.x + meanOn.y + meanOn.z; }
    [[nodiscard]] double lumaOff() const { return meanOff.x + meanOff.y + meanOff.z; }
};
Diff compare(const gpu::Image8& on, const gpu::Image8& off) {
    Diff d;
    for (std::size_t i = 0; i + 3 < on.rgba.size(); i += 4) {
        int sum = 0;
        for (int c = 0; c < 3; ++c) {
            sum += std::abs(static_cast<int>(on.rgba[i + static_cast<std::size_t>(c)]) -
                            static_cast<int>(off.rgba[i + static_cast<std::size_t>(c)]));
        }
        if (sum > 6) {
            ++d.changed;
            d.meanOn += glm::dvec3(on.rgba[i], on.rgba[i + 1], on.rgba[i + 2]);
            d.meanOff += glm::dvec3(off.rgba[i], off.rgba[i + 1], off.rgba[i + 2]);
        }
    }
    if (d.changed > 0) {
        d.meanOn /= static_cast<double>(d.changed);
        d.meanOff /= static_cast<double>(d.changed);
    }
    return d;
}

double hueOf(const glm::dvec3& rgb255) {
    return static_cast<double>(color::oklabToOklch(color::rgbToOklab(glm::vec3(rgb255 / 255.0))).z);
}

struct Proof {
    app::Engine engine{app::EngineMode::Offline};
    Compilation compiled;

    explicit Proof(const fs::path& mushroom) {
        const fs::path walker = testsupport::writeTriangleGlb("reactivity_gpu_walker");
        REQUIRE(testsupport::loadGlade(engine, mushroom, walker));
        const SceneFacts facts = app::sceneFactsFor(engine);
        compiled = compilePlan(proposeReactivity(facts.capabilities.reactive(), facts.music).plan, facts);
        REQUIRE_FALSE(compiled.validation.hasErrors());
        REQUIRE(app::installCompilation(engine, compiled).has_value());
    }

    [[nodiscard]] params::ModRoute* route(std::string_view target) {
        for (params::ModRoute& r : engine.modulator().routes()) {
            if (r.target == target && !r.planItem.empty()) {
                return &r;
            }
        }
        return nullptr;
    }
    // The first frame on the 60 Hz grid at or after `seconds`, landed by a seek as a render lands it.
    double land(double seconds) {
        const double t = std::ceil(seconds * 60.0 - 1e-6) / 60.0;
        engine.seekSeconds(t);
        engine.update(FrameTime{t, 0.0, 0});
        return t;
    }
    // The first detected event of a band after `after` seconds (the engine's own analysis).
    [[nodiscard]] double firstOnset(bool analysis::AnalysisFrame::*band, double after) const {
        for (const analysis::AnalysisFrame& f : engine.track()->frames()) {
            if (f.timeSeconds > after && f.*band) {
                return f.timeSeconds;
            }
        }
        FAIL("no onset after " << after);
        return 0.0;
    }
};

struct Arm {
    gpu::Image8 on;
    gpu::Image8 off;
    Diff diff;
};

// The frame at `seconds` with the proposed route on `target`, and with only that route switched off.
Arm armAt(Harness& h, Proof& p, std::string_view target, double seconds, bool emission, const std::string& name) {
    params::ModRoute* r = p.route(target);
    INFO(target);
    REQUIRE(r != nullptr);
    const double t = p.land(seconds);
    Arm arm;
    arm.on = h.render(p.engine.composition()->scene(), t, emission);
    r->enabled = false;
    p.engine.update(FrameTime{t, 0.0, 0});
    arm.off = h.render(p.engine.composition()->scene(), t, emission);
    r->enabled = true;
    p.engine.update(FrameTime{t, 0.0, 0});
    arm.diff = compare(arm.on, arm.off);
    dump(arm.on, name + "-on");
    dump(arm.off, name + "-off");
    return arm;
}

} // namespace

TEST_CASE("A route the reactivity proposer proposes reaches the pixels, for every kind of target it proposes",
          "[gpu][reactivity][adr927]") {
    const fs::path mushroom = fs::path(AVGEN_SOURCE_DIR) / "assets/quaternius/glTF/Mushroom_Common.gltf";
    if (!fs::exists(mushroom)) {
        SKIP("the Quaternius library is not present in this checkout");
    }
    Harness h;
    Proof p(mushroom);
    const song::SectionTimeline sections = testsupport::gladeSections();
    const double dropStart = sections.sections[4].startSeconds;
    const double inTheDrop = dropStart + 7.0;           // past every arc's glide
    const double kick = p.firstOnset(&analysis::AnalysisFrame::lowOnset, 10.0);
    const double clap = p.firstOnset(&analysis::AnalysisFrame::midOnset, 10.0);
    const double hat = p.firstOnset(&analysis::AnalysisFrame::highOnset, 10.0);
    INFO("kick " << kick << " s, clap " << clap << " s, hat " << hat << " s, drop " << dropStart << " s");

    SECTION("a hero's glow on the kick, and nothing in the kickless break") {
        const params::ModRoute* r = p.route("nodes/elder-gills/emissiveBoost");
        REQUIRE(r != nullptr);
        CHECK(r->source == "audio.onsetLow");
        const Arm a = armAt(h, p, "nodes/elder-gills/emissiveBoost", kick + r->chain.delayMs / 1000.0 + 0.02, true, "hero-kick");
        INFO("changed " << a.diff.changed << ", luma " << a.diff.lumaOff() << " -> " << a.diff.lumaOn());
        CHECK(a.diff.changed > 40);
        CHECK(a.diff.lumaOn() > a.diff.lumaOff() * 1.1);
        // The break (bars 13-14) has no kick: 1.8 s after the last one the route has let go.
        const double breakStart = sections.sections[2].startSeconds;
        const Arm rest = armAt(h, p, "nodes/elder-gills/emissiveBoost", breakStart + 1.8, true, "hero-rest");
        INFO("in the break: changed " << rest.diff.changed);
        CHECK(rest.diff.changed == 0);
    }
    SECTION("a scatter layer's glow on the kick, 90 ms after the heroes") {
        const params::ModRoute* r = p.route("nodes/meadow/scatter/fungi/emissionGain");
        REQUIRE(r != nullptr);
        CHECK(r->chain.delayMs == 90.0f);
        const Arm a = armAt(h, p, "nodes/meadow/scatter/fungi/emissionGain", kick + 0.09 + 0.02, true, "fungi-kick");
        INFO("changed " << a.diff.changed << ", luma " << a.diff.lumaOff() << " -> " << a.diff.lumaOn());
        CHECK(a.diff.changed > 40);
        CHECK(a.diff.lumaOn() > a.diff.lumaOff() * 1.05);
    }
    SECTION("the layer's hue, keyed by section: warmer in the drop by the key's turn") {
        const Arm a = armAt(h, p, "nodes/meadow/scatter/fungi/hueOffset", dropStart + 3.0, true, "fungi-hue-drop");
        const double turn = hueOf(a.diff.meanOn) - hueOf(a.diff.meanOff);
        const double wrapped = turn - std::round(turn);
        INFO("changed " << a.diff.changed << ", hue " << hueOf(a.diff.meanOff) << " -> " << hueOf(a.diff.meanOn));
        CHECK(a.diff.changed > 40);
        // The drop's key is -0.08 of a turn on the fungi, applied in linear light (OKLCH); measured here
        // on the mean of the 8-bit pixels that changed, which overstates a rotation (measured -0.13 at
        // 384x216). The sign and the order are the claim: warmer, by about the key's turn.
        CHECK(wrapped < -0.04);
        CHECK(wrapped > -0.16);
    }
    SECTION("the layer's light wave, as deep as the section: faint in the quiet break") {
        // A downbeat in the break sends the ring out at 12 m/s; half a second on it crosses the fungi. The
        // section's depth holds the wave at 35% of its authored 6 there: dimmer with the route than without.
        const auto& bars = testsupport::gladeGroove().truth.downbeats;
        const double breakStart = sections.sections[2].startSeconds;
        const auto bar = std::find_if(bars.begin(), bars.end(), [&](double b) { return b >= breakStart - 1e-3; });
        REQUIRE(bar != bars.end());
        const Arm a = armAt(h, p, "nodes/meadow/scatter/fungi/emissiveFieldAmount", *bar + 0.55, true, "fungi-wave-break");
        INFO("changed " << a.diff.changed << ", luma " << a.diff.lumaOff() << " -> " << a.diff.lumaOn());
        CHECK(a.diff.changed > 10);
        CHECK(a.diff.lumaOn() < a.diff.lumaOff());
    }
    SECTION("a hero part's own light wave, as deep as the section: faint in the quiet break") {
        // The moss's gills name the same ring; it reaches them (11.7 m out, 12 m/s) about a second after
        // the downbeat. The break's depth holds its 3.0 at 35%: dimmer with the route than without.
        const auto& bars = testsupport::gladeGroove().truth.downbeats;
        const double breakStart = sections.sections[2].startSeconds;
        const auto bar = std::find_if(bars.begin(), bars.end(), [&](double b) { return b >= breakStart - 1e-3; });
        REQUIRE(bar != bars.end());
        const Arm a = armAt(h, p, "procedural/moss-gills/emissiveFieldAmount", *bar + 0.97, true, "moss-wave-break");
        INFO("changed " << a.diff.changed << ", luma " << a.diff.lumaOff() << " -> " << a.diff.lumaOn());
        CHECK(a.diff.changed > 10);
        CHECK(a.diff.lumaOn() < a.diff.lumaOff());
    }
    SECTION("particles: the elder's spores catch its kick") {
        const Arm a = armAt(h, p, "particles/elder-spores/emissive", kick + 0.09 + 0.03, false, "spores-kick");
        INFO("changed " << a.diff.changed << ", luma " << a.diff.lumaOff() << " -> " << a.diff.lumaOn());
        CHECK(a.diff.changed > 15); // small, fast, local: a swarm of 0.3 m motes (30 pixels at 384x216)
        CHECK(a.diff.lumaOn() > a.diff.lumaOff());
    }
    SECTION("a practical light echoes the elder's kick") {
        const Arm a = armAt(h, p, "lightrig/Glade/elder-practical/intensity", kick + 0.05 + 0.02, false, "practical-kick");
        INFO("changed " << a.diff.changed << ", luma " << a.diff.lumaOff() << " -> " << a.diff.lumaOn());
        CHECK(a.diff.changed > 15); // a small point light low under the elder: the ground around its foot (30 px)
        CHECK(a.diff.lumaOn() > a.diff.lumaOff());
    }
    SECTION("the ecology light follows the section") {
        const Arm a = armAt(h, p, "scene/ecologyLight", inTheDrop, false, "ecology-drop");
        INFO("changed " << a.diff.changed << ", luma " << a.diff.lumaOff() << " -> " << a.diff.lumaOn());
        CHECK(a.diff.changed > 40);
        CHECK(a.diff.lumaOn() > a.diff.lumaOff());
    }
    SECTION("the fog follows the section") {
        const Arm a = armAt(h, p, "scene/volumeDensity", inTheDrop, false, "fog-drop");
        INFO("changed " << a.diff.changed);
        CHECK(a.diff.changed > 40);
    }
    SECTION("the wind follows the section") {
        const Arm a = armAt(h, p, "scene/windSpeed", inTheDrop, false, "wind-drop");
        INFO("changed " << a.diff.changed);
        CHECK(a.diff.changed > 5);
    }
    SECTION("the water catches the hats") {
        const params::ModRoute* r = p.route("nodes/meadow/water/sparkle");
        REQUIRE(r != nullptr);
        const Arm sparkle = armAt(h, p, "nodes/meadow/water/sparkle", hat + r->chain.delayMs / 1000.0 + 0.02, false, "water-hat");
        const Arm glow = armAt(h, p, "nodes/meadow/water/glow", inTheDrop, false, "water-glow-drop");
        INFO("sparkle changed " << sparkle.diff.changed << ", glow changed " << glow.diff.changed);
        CHECK(sparkle.diff.changed + glow.diff.changed > 10);
    }
    SECTION("a world effect answers the lead") {
        const Arm a = armAt(h, p, "fx/aurora/intensity", inTheDrop, false, "aurora-lead");
        INFO("changed " << a.diff.changed);
        CHECK(a.diff.changed > 20);
    }
    SECTION("a lamp layer flares on the clap") {
        const params::ModRoute* r = p.route("nodes/meadow/scatter/lamps/emissionGain");
        REQUIRE(r != nullptr);
        CHECK(r->source == "audio.onsetMid");
        const Arm a = armAt(h, p, "nodes/meadow/scatter/lamps/emissionGain", clap + r->chain.delayMs / 1000.0 + 0.02, true, "lamps-clap");
        INFO("changed " << a.diff.changed << ", luma " << a.diff.lumaOff() << " -> " << a.diff.lumaOn());
        CHECK(a.diff.changed > 5);
        CHECK(a.diff.lumaOn() > a.diff.lumaOff());
    }
}
