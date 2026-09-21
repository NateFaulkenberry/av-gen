// ADR-623: with the `motionMatching` key absent, a scene behaves exactly as it did before the key
// existed.
//
// **How this is a proof and not a hope.** The trace below reduces every entity's simulated state,
// its provider memory and every rig's final pose over a stretch of Glowmere to one digest, and
// prints it. It was run on the commit before the wiring (f8fe1b4b), built in a separate worktree,
// and on the commit with it. The two digests are recorded in ADR-623 and asserted here. A digest
// that matched only because it saw nothing would be vacuous, so the second case is the control: the
// same scene with the key switched on for one alien must produce a *different* digest.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "entity/entity.hpp"
#include "organism/mushroom.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/tree_generated.hpp"
#include "signals/signal_bus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

// Recorded by running this trace on f8fe1b4b, the commit before the wiring; see ADR-623.
constexpr std::uint64_t kPreWiringDigest = 0x5599cff790bc8a34ull;

fs::path glowmere() {
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2.scene.json";
}

struct Fnv {
    std::uint64_t h = 1469598103934665603ull;
    void bytes(const void* p, std::size_t n) {
        const auto* b = static_cast<const unsigned char*>(p);
        for (std::size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= 1099511628211ull;
        }
    }
    void f(float v) { bytes(&v, sizeof v); }
    void u(std::uint64_t v) { bytes(&v, sizeof v); }
    void v3(const glm::vec3& v) { f(v.x); f(v.y); f(v.z); }
};

// Every entity's state, provider memory and every rig's final pose, over 3 s at 60 Hz, a seek back
// to 1.5 s, and 0.5 s more. The seek is in the trace because ADR-360's replay is the path the
// wiring changed (the chain is now built before the first advance).
std::uint64_t traceOf(const fs::path& file) {
    organism::registerMushroomGenerator();
    scene::registerTreeGenerator();
    assets::AssetRegistry registry(file.parent_path());
    auto loaded = scene::Composition::loadFile(file, registry);
    REQUIRE(loaded.has_value());
    scene::Composition& comp = **loaded;
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    comp.attach(params, modulator);
    comp.setViewport(640, 360);
    comp.scene().detailLimits.entityDistanceCull = false;

    Fnv fnv;
    const auto step = [&](int frame) {
        FrameTime time;
        time.renderTime = static_cast<double>(frame) / 60.0;
        time.deltaTime = frame == 0 ? 0.0 : 1.0 / 60.0;
        time.frameIndex = static_cast<std::uint64_t>(frame);
        params.resetFinals();
        comp.updateFields(time, bus, modulator);
        modulator.applyRoutes(bus, params, time.deltaTime);
        comp.updateBehaviour(time, bus);
        comp.update(time);
        for (const auto& e : comp.entityWorld().entities()) {
            const entity::EntityState& s = e->state();
            fnv.v3(s.position());
            fnv.f(s.yaw);
            fnv.f(s.speed);
            fnv.u(static_cast<std::uint64_t>(s.activity));
            const entity::MotionMemory& m = e->motionMemory();
            fnv.u(m.selection);
            fnv.u(m.generation);
            fnv.f(m.localTime);
            fnv.u(static_cast<std::uint64_t>(m.provider + 1));
        }
        for (const scene::SkinnedRig& rig : comp.scene().rigs) {
            for (const scene::Transform& t : rig.pose.local) {
                fnv.v3(t.position);
                fnv.f(t.rotation.x); fnv.f(t.rotation.y); fnv.f(t.rotation.z); fnv.f(t.rotation.w);
            }
        }
    };
    for (int frame = 0; frame <= 180; ++frame) {
        step(frame);
    }
    for (int frame = 90; frame <= 120; ++frame) {
        step(frame);
    }
    return fnv.h;
}

} // namespace

TEST_CASE("with the key absent, Glowmere behaves exactly as it did before ADR-623",
          "[motionmatching][defaultoff][aliens]") {
    if (!fs::exists(glowmere())) {
        SKIP("Glowmere is not present");
    }
    // The subject exists: no entity in the shipping scene carries the key.
    std::ifstream in(glowmere());
    const nlohmann::json scene = nlohmann::json::parse(in);
    for (const auto& e : scene.at("entities")) {
        REQUIRE_FALSE(e.contains("motionMatching"));
    }
    const std::uint64_t digest = traceOf(glowmere());
    WARN(fmt::format("Glowmere trace digest {:016x}", digest));
    // Recorded from the pre-wiring commit's binary; see ADR-623.
    CHECK(digest == kPreWiringDigest);
}

TEST_CASE("the trace is not blind: the key switched on for one alien changes it",
          "[motionmatching][defaultoff][aliens]") {
    // The control arm. The same scene with `rook` opted in must give a different digest, or the
    // equality above would be equally true of an instrument that could not see the matcher.
    if (!fs::exists(glowmere())) {
        SKIP("Glowmere is not present");
    }
    std::ifstream in(glowmere());
    nlohmann::json scene = nlohmann::json::parse(in);
    bool opted = false;
    for (auto& e : scene.at("entities")) {
        if (e.value("name", std::string()) == "rook") {
            e["motionMatching"] = {{"joints", {"foot.l", "foot.r", "head.x"}},
                                   {"contacts", {"foot.l", "foot.r"}}};
            opted = true;
        }
    }
    REQUIRE(opted);
    // Beside the original so its relative paths resolve, under a name nothing else uses.
    const fs::path arm = glowmere().parent_path() / ".adr623-control-arm.scene.json";
    {
        std::ofstream out(arm);
        out << scene.dump();
    }
    const std::uint64_t armDigest = traceOf(arm);
    std::error_code ec;
    fs::remove(arm, ec);
    const std::uint64_t offDigest = traceOf(glowmere());
    WARN(fmt::format("key off {:016x}, key on for rook {:016x}", offDigest, armDigest));
    CHECK(armDigest != offDigest);
}
