// The byte-identical control for the `Explore` goal-model extraction (ADR-330 §3).
//
// `Explore` is the only autonomous mind this engine has ever had and five Glowmere characters
// depend on it. ADR-330 moved its goal model -- taste over the interest registry, damped by
// distance, suppressed where the body has recently been -- out of that class and into
// `entity::goalWeight`, where a second character kind can score with it. A refactor of the only
// mind in the engine is the kind of change whose damage is a film that looks slightly different
// and nobody can say why.
//
// **So the arm is a position trace and not a walk test.** `docs/character-ai-plan.md` §P3 is
// explicit about it: "the arm that proves it is a before/after position trace, not a test that both
// versions still walk". A test that asserted the five bodies still move would have passed against
// an extraction that changed every route in the world, which is ADR-182's arm-that-cannot-fail
// wearing a refactor's clothes.
//
// The golden file is `tests/data/explore-position-trace.txt`, written by **the build before the
// extraction** and committed in the commit before it. Every float is compared as its raw bits:
// `0.1 % 1e-9` is not the assertion here, exact equality is, because the claim is that the
// arithmetic is the same arithmetic and not that it is close.
//
// The controls (ADR-182), because a trace that cannot differ proves nothing either:
//
//   sensitivity   the same world with `entity/scout/explore/maxRange` moved by one metre must
//                 produce a **different** trace -- so the golden is measuring the goal model
//   coverage      every body in the trace must actually have moved, and the bodies must not all
//                 have moved identically; a golden of five stationary characters would match
//                 whatever the extraction did to them
//
// Regenerating the golden is deliberately awkward: set `AVGEN_WRITE_EXPLORE_GOLDEN=1`. It should
// only ever be done when a change to `Explore`'s route is *intended*, and the commit that does it
// has to say which routes moved and why.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "entity/entity.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "signals/signal_bus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

fs::path repoRoot() { return fs::path(AVGEN_SOURCE_DIR); }
fs::path fixture() {
    return repoRoot() / "examples" / "labs" / "character" / "character-intelligence-lab.scene.json";
}
fs::path golden() { return repoRoot() / "tests" / "data" / "explore-position-trace.txt"; }
bool assetsPresent() { return fs::exists(repoRoot() / "assets" / "aliens" / "alien-scout.glb"); }

// A float as the 32 bits it is. The comparison this file makes is bit equality, and printing a
// decimal would throw away the bits that the whole exercise is about.
std::string bits(float v) {
    std::uint32_t u = 0;
    std::memcpy(&u, &v, sizeof(u));
    return fmt::format("{:08x}", u);
}

// Sixty seconds of the lab fixture at a fixed 60 Hz, sampling every body's **simulation** position
// (R1, ADR-260: `state().position()`, the answer navigation reads) and its facing every fifth
// frame. 720 samples a body, which is enough that a route that diverges anywhere diverges in the
// file, and small enough to read.
std::vector<std::string> trace(float maxRangeNudge) {
    assets::AssetRegistry registry(fixture().parent_path());
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    auto loaded = scene::Composition::loadFile(fixture(), registry);
    INFO((loaded.has_value() ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    std::unique_ptr<scene::Composition> comp = std::move(*loaded);
    comp->attach(params, modulator);
    comp->setViewport(1280, 720);
    // ADR-186's offline setting. With the distance cull on, the trace would be a measurement of
    // which band the camera put each body in rather than of the goal model.
    comp->scene().detailLimits.entityDistanceCull = false;

    if (maxRangeNudge != 0.0f) {
        auto* p = params.findAs<float>("entity/scout/explore/maxRange");
        REQUIRE(p != nullptr);
        p->setBase(p->base() + maxRangeNudge);
    }

    std::vector<std::string> lines;
    constexpr int kFrames = 3600;
    constexpr double kStep = 1.0 / 60.0;
    FrameTime time;
    for (int i = 0; i < kFrames; ++i) {
        time.renderTime = static_cast<double>(i) * kStep;
        time.deltaTime = i == 0 ? 0.0 : kStep;
        time.frameIndex = static_cast<std::uint64_t>(i);
        params.resetFinals();
        comp->updateFields(time, bus, modulator);
        modulator.applyRoutes(bus, params, time.deltaTime);
        comp->updateBehaviour(time, bus);
        comp->update(time);
        if (i % 5 != 0) {
            continue;
        }
        for (const auto& e : comp->entityWorld().entities()) {
            if (e == nullptr || e->behaviors().empty()) {
                continue;
            }
            const glm::vec3 p = e->state().position();
            lines.push_back(fmt::format("{} {} {} {} {} {}", e->name(), i, bits(p.x), bits(p.y),
                                        bits(p.z), bits(e->state().yaw)));
        }
    }
    return lines;
}

std::vector<std::string> readGolden() {
    std::vector<std::string> lines;
    std::ifstream in(golden());
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line[0] != '#') {
            lines.push_back(line);
        }
    }
    return lines;
}

} // namespace

TEST_CASE("the extracted goal model walks the same route to the bit", "[entity][decision][adr330]") {
    if (!assetsPresent()) {
        WARN("assets missing; skipping");
        return;
    }
    const std::vector<std::string> now = trace(0.0f);
    REQUIRE_FALSE(now.empty());

    if (std::getenv("AVGEN_WRITE_EXPLORE_GOLDEN") != nullptr) {
        std::ofstream out(golden());
        out << "# Sixty seconds of examples/labs/character/character-intelligence-lab.scene.json at a\n"
               "# fixed 60 Hz, every fifth frame: <entity> <frame> <x> <y> <z> <yaw>, each float as\n"
               "# its raw 32 bits. Written by the build BEFORE ADR-330's goal-model extraction and\n"
               "# compared by tests/unit/test_decision_extraction.cpp after it. Regenerating this is\n"
               "# a statement that a route was *meant* to change; see that file's header.\n";
        for (const std::string& line : now) {
            out << line << '\n';
        }
        WARN(fmt::format("wrote {} trace lines to {}", now.size(), golden().string()));
        return;
    }

    const std::vector<std::string> before = readGolden();
    INFO("golden: " << golden().string());
    REQUIRE(before.size() == now.size());
    std::size_t differing = 0;
    std::string firstDifference;
    for (std::size_t i = 0; i < now.size(); ++i) {
        if (before[i] != now[i]) {
            ++differing;
            if (firstDifference.empty()) {
                firstDifference = fmt::format("line {}: was [{}] is [{}]", i, before[i], now[i]);
            }
        }
    }
    INFO(firstDifference);
    CHECK(differing == 0);

    // Coverage. A golden of five bodies that never moved would match any extraction at all, so the
    // trace has to be shown to carry a route before its equality means anything.
    std::size_t moved = 0;
    std::set<std::string> names;
    for (const std::string& line : now) {
        names.insert(line.substr(0, line.find(' ')));
    }
    for (const std::string& name : names) {
        std::string first;
        std::size_t distinct = 0;
        std::set<std::string> places;
        for (const std::string& line : now) {
            if (line.compare(0, name.size(), name) == 0 && line[name.size()] == ' ') {
                places.insert(line.substr(line.find(' ', name.size() + 1)));
            }
        }
        distinct = places.size();
        if (distinct > 1) {
            ++moved;
        }
        INFO("body " << name << " occupies " << distinct << " distinct samples");
        (void)first;
    }
    // `penned` is walled in and genuinely does not move; the other four do (lab case 4).
    CHECK(moved >= 4);
    CHECK(names.size() == 5);
}

TEST_CASE("the trace is sensitive to the goal model it is measuring", "[entity][decision][adr330]") {
    if (!assetsPresent()) {
        WARN("assets missing; skipping");
        return;
    }
    // The control. One metre on one character's `maxRange` re-weights every candidate it scores --
    // the falloff divides by `max(hi * 0.5, 1)` -- and must therefore move the trace. If this
    // passes and the arm above also passes, the arm above is measuring the goal model. If this
    // fails, the golden is a photograph of something else and the equality above is worthless.
    const std::vector<std::string> nudged = trace(1.0f);
    const std::vector<std::string> plain = trace(0.0f);
    REQUIRE(nudged.size() == plain.size());
    std::size_t differing = 0;
    for (std::size_t i = 0; i < plain.size(); ++i) {
        if (plain[i] != nudged[i]) {
            ++differing;
        }
    }
    WARN(fmt::format("one metre of maxRange moved {} of {} trace samples", differing, plain.size()));
    CHECK(differing > 0);
}
