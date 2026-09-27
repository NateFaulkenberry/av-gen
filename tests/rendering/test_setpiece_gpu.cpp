// A set piece's beam colour reaches the pixels (ADR-928).
//
// A coloured set piece writes its colour into the beam's particle ramp -- three channels of
// `colorStart` and three of `colorEnd`, each a one-component `set` step, while the craft is unseen --
// and puts the scene's own colours back as it departs. The CPU film (`test_setpiece_film.cpp`) proves
// the parameters take those values in a play and after a seek; this proves the renderer draws them:
// the same frame of the lab film, rendered twice -- the plan with its "field" abduction's beam red, and
// the same plan with no colour -- must differ in the beam, towards red.
//
// The control is the same pair of films at a moment no set piece is running (the craft hidden, the
// beam out): the two must render identically, so the difference above is the beam's colour and
// nothing else a colour could have changed.
//
// With AVGEN_SETPIECE_DUMP=<dir> each frame is written as a PNG for a person to look at.

#include "app/directing_plan_file.hpp"
#include "app/engine.hpp"
#include "assets/image.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "params/parameter_set.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/composition.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

using namespace avgen;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kWidth = 480;
constexpr std::uint32_t kHeight = 270;

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
        renderer.setParticleWarmUpFrames(120); // the column filled, as two seconds of beam fill it
    }
    gpu::Image8 render(const scene::Scene& s, double seconds) {
        FrameTime t{};
        t.renderTime = seconds;
        renderer.resetTemporalHistory();
        auto first = renderer.renderToImage(s, t, kWidth, kHeight);
        REQUIRE(first.has_value());
        auto img = renderer.renderToImage(s, t, kWidth, kHeight);
        REQUIRE(img.has_value());
        return std::move(*img);
    }
};

void dump(const gpu::Image8& image, const std::string& name) {
    const char* dir = std::getenv("AVGEN_SETPIECE_DUMP");
    if (dir == nullptr || dir[0] == '\0') {
        return;
    }
    static_cast<void>(assets::writePng(fs::path(dir) / (name + ".png"), image.width, image.height, image.rgba));
}

struct Diff {
    std::size_t changed = 0;
    glm::dvec3 meanA{0.0};
    glm::dvec3 meanB{0.0};
};
Diff compare(const gpu::Image8& a, const gpu::Image8& b) {
    Diff d;
    for (std::size_t i = 0; i + 3 < a.rgba.size(); i += 4) {
        int sum = 0;
        for (int c = 0; c < 3; ++c) {
            sum += std::abs(static_cast<int>(a.rgba[i + static_cast<std::size_t>(c)]) -
                            static_cast<int>(b.rgba[i + static_cast<std::size_t>(c)]));
        }
        if (sum > 6) {
            ++d.changed;
            d.meanA += glm::dvec3(a.rgba[i], a.rgba[i + 1], a.rgba[i + 2]);
            d.meanB += glm::dvec3(b.rgba[i], b.rgba[i + 1], b.rgba[i + 2]);
        }
    }
    if (d.changed > 0) {
        d.meanA /= static_cast<double>(d.changed);
        d.meanB /= static_cast<double>(d.changed);
    }
    return d;
}

json labPlan(bool red) {
    json field = {{"key", "field"},
                  {"template", "abduction"},
                  {"craft", "saucer"},
                  {"at", {{"seconds", 60}}},
                  {"place", {{"point", {62, 22}}}},
                  {"set", {{"animals", 2}, {"approachSeconds", 6}, {"hoverHeight", 30}}}};
    if (red) {
        field["beamColor"] = {1.0, 0.25, 0.15};
    }
    return json{{"schemaVersion", 1}, {"id", "ufo"}, {"title", "UFO activity"}, {"tier", "baked"},
                {"setPieces", json::array({field})}};
}

// The lab film with the plan installed, the camera 70 m from the field station looking at the beam.
std::unique_ptr<app::Engine> film(bool red) {
    auto engine = std::make_unique<app::Engine>(app::EngineMode::Offline);
    REQUIRE(engine->loadProject(fs::path(AVGEN_SOURCE_DIR) / "tests/data/setpieces/setpiece-lab.json").has_value());
    auto applied = app::applyPlanDocument(*engine, labPlan(red));
    INFO((applied ? std::string() : applied.error().message));
    REQUIRE(applied.has_value());
    REQUIRE(applied->blocked.empty());
    params::IParameter* position = engine->params().find("camera/position");
    params::IParameter* target = engine->params().find("camera/target");
    REQUIRE(position != nullptr);
    REQUIRE(target != nullptr);
    const float eye[3] = {62.0f, 22.0f, 92.0f};
    const float look[3] = {62.0f, 16.0f, 22.0f};
    for (int c = 0; c < 3; ++c) {
        position->setBaseComponent(c, eye[c]);
        target->setBaseComponent(c, look[c]);
    }
    engine->composition()->scene().detailLimits.entityDistanceCull = false;
    return engine;
}

// The frame at `seconds`, landed by a seek as a render that starts there lands it.
const scene::Scene& land(app::Engine& engine, double seconds) {
    engine.seekSeconds(seconds);
    engine.update(FrameTime{seconds, 0.0, 0});
    return engine.composition()->scene();
}

} // namespace

TEST_CASE("A set piece's beam colour reaches the pixels, and nothing else changes", "[gpu][setpiece][adr928]") {
    Harness h;
    auto red = film(true);
    auto plain = film(false);

    // Mid-lift of the field abduction (beam at 60 s, lift from ~61.1 s): the beam is lit in full.
    const double lift = 62.5;
    const gpu::Image8 redFrame = h.render(land(*red, lift), lift);
    CHECK(red->params().find("particles/visitor-beam/colorStart")->finalComponent(0) == 1.0f);
    const gpu::Image8 plainFrame = h.render(land(*plain, lift), lift);
    CHECK(plain->params().find("particles/visitor-beam/colorStart")->finalComponent(0) < 0.5f);
    dump(redFrame, "setpiece-lift-red");
    dump(plainFrame, "setpiece-lift-plain");
    const Diff beam = compare(redFrame, plainFrame);
    INFO("changed " << beam.changed << " px; red film mean (" << beam.meanA.x << ", " << beam.meanA.y << ", "
                    << beam.meanA.z << "), plain (" << beam.meanB.x << ", " << beam.meanB.y << ", " << beam.meanB.z << ")");
    CHECK(beam.changed > 300);
    // Towards red, away from the scene's cyan.
    CHECK(beam.meanA.x > beam.meanB.x);
    CHECK(beam.meanA.z < beam.meanB.z);
    CHECK(beam.meanA.x > beam.meanA.z);

    // The control: between set pieces (the craft taken at ~52 s) nothing is lit and nothing differs.
    const double before = 40.0;
    const gpu::Image8 redBefore = h.render(land(*red, before), before);
    const gpu::Image8 plainBefore = h.render(land(*plain, before), before);
    dump(redBefore, "setpiece-before-red");
    dump(plainBefore, "setpiece-before-plain");
    const Diff none = compare(redBefore, plainBefore);
    INFO("before the set piece: changed " << none.changed << " px");
    CHECK(none.changed == 0);
}
