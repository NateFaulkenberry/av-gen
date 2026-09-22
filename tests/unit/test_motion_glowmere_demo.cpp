// Phase C §65: the Glowmere demonstration. A deterministic scene in which the alien goes idle, walk,
// accelerate, curve left, curve right, run, slow, turn, stop, strafe, walk again, **driven through
// motion requests** (the `motionScript` behaviour), with the matcher choosing what to play.
//
// The corpus is the scout's own 26 clips plus the §21 variants the coverage gate kept (the four
// turns), built by `avgen-motion augment` into assets/aliens-scout-augmented-pack. It is gitignored
// like every asset, so the test skips where the pack has not been built. The command is in the phase
// log.
//
// The weights are the scene's own (§45's versioned block): joint position 0.3, trajectory position
// 3, root velocity 3. They were chosen from the sweep below: at the engine defaults the pose half
// outweighs the request, and the alien stays in whatever it is playing (walk requests got `Idle`).
//
// **What is asserted is what the spec asks the demonstration to show**: that the matcher selects an
// appropriate continuation for each kind of request. For each segment, the family of motion it plays
// is checked, never exact samples. Where the corpus cannot serve a request (the scout has no strafe
// clip), the test records what it chose rather than pretending.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "signals/signal_bus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <filesystem>
#include <map>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

fs::path demo() {
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / "labs" / "motionmatch" / "glowmere-motion-matching-demo.scene.json";
}

struct Segment {
    const char* name;
    float from;
    float to;
};

// The script's segments, as authored in the scene.
const Segment kSegments[] = {
    {"idle", 0.0f, 1.5f},        {"walk", 1.5f, 3.5f},       {"accelerate", 3.5f, 5.0f}, {"curve left", 5.0f, 7.0f},
    {"curve right", 7.0f, 9.0f}, {"run", 9.0f, 11.0f},       {"slow", 11.0f, 12.5f},     {"turn", 12.5f, 14.0f},
    {"stop", 14.0f, 15.5f},      {"strafe", 15.5f, 17.5f},   {"walk again", 17.5f, 19.5f},
};

bool contains(const std::string& s, const char* part) {
    return s.find(part) != std::string::npos;
}

} // namespace

TEST_CASE("§65 the Glowmere demonstration: the matcher picks a continuation for each request",
          "[glowmeredemo][motionmatching][aliens][phaseC]") {
    if (!fs::exists(fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens-scout-augmented-pack" / "pack.json")) {
        SKIP("the augmented scout pack is not present (see the phase log, §65)");
    }
    assets::AssetRegistry registry(demo().parent_path());
    auto loaded = scene::Composition::loadFile(demo(), registry);
    REQUIRE(loaded.has_value());
    scene::Composition& comp = **loaded;
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    comp.attach(params, modulator);
    comp.setViewport(320, 180);
    comp.scene().detailLimits.entityDistanceCull = false;

    std::vector<std::map<std::string, int>> played(std::size(kSegments));
    int fromMatcher = 0;
    int frames = 0;
    for (int f = 0; f <= 19 * 60 + 29; ++f) {
        FrameTime time;
        time.renderTime = static_cast<double>(f) / 60.0;
        time.deltaTime = f == 0 ? 0.0 : 1.0 / 60.0;
        time.frameIndex = static_cast<std::uint64_t>(f);
        params.resetFinals();
        comp.updateFields(time, bus, modulator);
        modulator.applyRoutes(bus, params, time.deltaTime);
        comp.updateBehaviour(time, bus);
        comp.update(time);
        const entity::Entity* e = comp.entityWorld().find("alien-match");
        REQUIRE(e != nullptr);
        ++frames;
        if (e->motionChainResult().provider != 0) {
            continue;
        }
        ++fromMatcher;
        const float t = static_cast<float>(time.renderTime);
        for (std::size_t s = 0; s < std::size(kSegments); ++s) {
            // The second half of each segment: the part after the matcher has had time to respond.
            const float mid = 0.5f * (kSegments[s].from + kSegments[s].to);
            if (t >= mid && t < kSegments[s].to) {
                ++played[s][std::string(e->motionChainResult().result.content)];
            }
        }
    }
    std::string report = fmt::format("{} of {} frames from the matcher\n", fromMatcher, frames);
    for (std::size_t s = 0; s < std::size(kSegments); ++s) {
        report += fmt::format("  {:<12}", kSegments[s].name);
        for (const auto& [clip, n] : played[s]) {
            report += fmt::format(" {} {}", clip, n);
        }
        report += "\n";
    }
    WARN(report);
    CHECK(fromMatcher == frames);

    const auto share = [&](std::size_t s, std::initializer_list<const char*> families) {
        int hit = 0;
        int all = 0;
        for (const auto& [clip, n] : played[s]) {
            all += n;
            for (const char* fam : families) {
                if (contains(clip, fam)) {
                    hit += n;
                    break;
                }
            }
        }
        return all > 0 ? static_cast<float>(hit) / static_cast<float>(all) : 0.0f;
    };
    // Walking requests get walking motion, and the curves get the turn variants in the right
    // direction: that is the matcher choosing continuations, not a clip table.
    CHECK(share(1, {"Walking"}) > 0.5f);
    CHECK(share(3, {"turn+1.40"}) > 0.3f);
    CHECK(share(4, {"turn-1.40"}) > 0.3f);
    CHECK(share(5, {"Running"}) > 0.5f);
    // A turn on the spot gets the scout's turn on the spot, which §7/§24's future-facing feature is
    // what makes visible to the search at all.
    CHECK(share(7, {"Idle_turn"}) > 0.5f);
}

#include <fstream>
#include <nlohmann/json.hpp>

TEST_CASE("§65 the scripted demonstration scrubs to the played frame", "[glowmeredemo][scrub][aliens][phaseC]") {
    // The script's clock is the timeline, and the provider memory replays on every seek step (ADR-623),
    // so a scrub into the middle of the curve lands on the played sample.
    if (!fs::exists(fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens-scout-augmented-pack" / "pack.json")) {
        SKIP("the augmented scout pack is not present");
    }
    assets::AssetRegistry registry(demo().parent_path());
    auto loaded = scene::Composition::loadFile(demo(), registry);
    REQUIRE(loaded.has_value());
    scene::Composition& comp = **loaded;
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    comp.attach(params, modulator);
    comp.setViewport(320, 180);
    comp.scene().detailLimits.entityDistanceCull = false;
    const auto frame = [&](int f) {
        FrameTime time;
        time.renderTime = static_cast<double>(f) / 60.0;
        time.deltaTime = f == 0 ? 0.0 : 1.0 / 60.0;
        time.frameIndex = static_cast<std::uint64_t>(f);
        params.resetFinals();
        comp.updateFields(time, bus, modulator);
        modulator.applyRoutes(bus, params, time.deltaTime);
        comp.updateBehaviour(time, bus);
        comp.update(time);
    };
    constexpr int kAt = 6 * 60; // one second into "curve left"
    for (int f = 0; f <= kAt; ++f) {
        frame(f);
    }
    const entity::Entity* e = comp.entityWorld().find("alien-match");
    const entity::MotionMemory played = e->motionMemory();
    const glm::vec3 where = e->state().position();
    for (int f = kAt + 1; f <= kAt + 180; ++f) {
        frame(f);
    }
    comp.entityWorld().seek(static_cast<double>(kAt) / 60.0, &params);
    const entity::MotionMemory sought = e->motionMemory();
    CHECK(sought.selection == played.selection);
    CHECK(std::abs(sought.localTime - played.localTime) < 1e-4f);
    CHECK(glm::length(e->state().position() - where) < 1e-3f);
}

TEST_CASE("§45/§65 the demonstration under different weights", "[.measure][glowmeredemo][aliens][phaseC]") {
    // The sweep the demonstration's weights were chosen from. Hidden: it prints, it does not assert.
    if (!fs::exists(fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens-scout-augmented-pack" / "pack.json")) {
        SKIP("the augmented scout pack is not present");
    }
    std::ifstream in(demo());
    const nlohmann::json base = nlohmann::json::parse(in);
    std::string report = "jointPos trajPos rootVel | walk curveL curveR run turn stop strafe\n";
    for (const float joint : {1.0f, 0.3f}) {
        for (const float traj : {1.0f, 3.0f, 6.0f}) {
            for (const float root : {1.0f, 3.0f}) {
                nlohmann::json scene = base;
                auto& m = scene["entities"][0]["motionMatching"];
                m["weights"] = {{"version", 1}, {"jointPosition", joint}, {"trajectoryPosition", traj},
                                {"rootVelocity", root}, {"trajectoryFacing", 0.5}, {"jointVelocity", 0.4}};
                const fs::path file = demo().parent_path() / ".sweep.scene.json";
                {
                    std::ofstream out(file);
                    out << scene.dump();
                }
                assets::AssetRegistry registry(file.parent_path());
                auto loaded = scene::Composition::loadFile(file, registry);
                REQUIRE(loaded.has_value());
                scene::Composition& comp = **loaded;
                params::ParameterSet params;
                params::Modulator modulator;
                signals::SignalBus bus;
                comp.attach(params, modulator);
                comp.setViewport(320, 180);
                comp.scene().detailLimits.entityDistanceCull = false;
                std::vector<std::map<std::string, int>> played(std::size(kSegments));
                for (int f = 0; f <= 19 * 60 + 29; ++f) {
                    FrameTime time;
                    time.renderTime = static_cast<double>(f) / 60.0;
                    time.deltaTime = f == 0 ? 0.0 : 1.0 / 60.0;
                    time.frameIndex = static_cast<std::uint64_t>(f);
                    params.resetFinals();
                    comp.updateFields(time, bus, modulator);
                    modulator.applyRoutes(bus, params, time.deltaTime);
                    comp.updateBehaviour(time, bus);
                    comp.update(time);
                    const entity::Entity* e = comp.entityWorld().find("alien-match");
                    const float t = static_cast<float>(time.renderTime);
                    for (std::size_t s = 0; s < std::size(kSegments); ++s) {
                        const float mid = 0.5f * (kSegments[s].from + kSegments[s].to);
                        if (t >= mid && t < kSegments[s].to) {
                            ++played[s][std::string(e->motionChainResult().result.content)];
                        }
                    }
                }
                std::error_code ec;
                fs::remove(file, ec);
                const auto top = [&](std::size_t s) {
                    std::string best;
                    int n = -1;
                    for (const auto& [clip, c] : played[s]) {
                        if (c > n) {
                            n = c;
                            best = clip;
                        }
                    }
                    return best;
                };
                report += fmt::format("{:>8} {:>7} {:>7} | {} | {} | {} | {} | {} | {} | {}\n", joint, traj, root, top(1), top(3),
                                      top(4), top(5), top(7), top(8), top(9));
            }
        }
    }
    WARN(report);
}
