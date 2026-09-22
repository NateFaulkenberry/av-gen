// Phase C §64/§92: the first true vertical slice. 100STYLE, retargeted onto the Glowmere alien
// through IK, analysed, packed, put in a database, and matched at runtime on a scout in a scene,
// with Phase B's layers on top.
//
// The pack is built by `avgen-motion pack ... --retarget-to alien-scout.glb --positional-legs ...`
// (the command is in docs/design/procedural-character-motion.md, "§64/§92"). It lives under
// assets/, which is gitignored, because 100STYLE may not be redistributed. So this skips wherever
// the pack is absent, which is everywhere but the machine that built it.

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
#include <set>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

fs::path sliceLab() {
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / "labs" / "motionmatch" / "alien-match-100style-lab.scene.json";
}

bool slicePresent() {
    return fs::exists(fs::path(AVGEN_SOURCE_DIR) / "assets" / "100style-scout-strutting-pack" / "pack.json") &&
           fs::exists(fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb");
}

struct Run {
    assets::AssetRegistry registry{sliceLab().parent_path()};
    std::unique_ptr<scene::Composition> comp;
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    Run() {
        auto loaded = scene::Composition::loadFile(sliceLab(), registry);
        REQUIRE(loaded.has_value());
        comp = std::move(*loaded);
        comp->attach(params, modulator);
        comp->setViewport(320, 180);
        comp->scene().detailLimits.entityDistanceCull = false;
    }
    void frame(int f) {
        FrameTime time;
        time.renderTime = static_cast<double>(f) / 60.0;
        time.deltaTime = f == 0 ? 0.0 : 1.0 / 60.0;
        time.frameIndex = static_cast<std::uint64_t>(f);
        params.resetFinals();
        comp->updateFields(time, bus, modulator);
        modulator.applyRoutes(bus, params, time.deltaTime);
        comp->updateBehaviour(time, bus);
        comp->update(time);
    }
};

} // namespace

TEST_CASE("§64/§92 a scout walks on retargeted 100STYLE, matched at runtime, with its layers on top",
          "[slice][motionmatching][aliens][phaseC]") {
    if (!slicePresent()) {
        SKIP("the retargeted 100STYLE pack is not present (see the phase log, §64/§92)");
    }
    Run run;
    std::set<std::string> clips;
    int matched = 0;
    int layered = 0;
    for (int f = 0; f <= 360; ++f) {
        run.frame(f);
        const entity::Entity* e = run.comp->entityWorld().find("alien-match");
        REQUIRE(e != nullptr);
        if (e->motionChainResult().provider == 0) {
            ++matched;
            clips.insert(std::string(e->motionChainResult().result.content));
        }
        const scene::Composition::MotionDebug d = run.comp->motionDebug("alien-match");
        int applied = 0;
        for (const auto& row : d.layers) {
            applied += (row.resolution == scene::LayerResolution::Applied || row.resolution == scene::LayerResolution::Clamped) ? 1 : 0;
        }
        layered += (d.posedByProvider && applied == 2) ? 1 : 0;
    }
    std::string names;
    for (const auto& c : clips) {
        names += c + " ";
    }
    WARN(fmt::format("{} of 361 frames matched, {} with both foot layers on the matched pose; clips: {}", matched,
                     layered, names));
    // Every frame from the matcher, and every clip it played is retargeted 100STYLE, not the scout's
    // own animation.
    CHECK(matched == 361);
    REQUIRE_FALSE(clips.empty());
    for (const auto& c : clips) {
        CHECK(c.rfind("Strutting_", 0) == 0);
    }
    CHECK(layered > 300);
}

TEST_CASE("§64/§92 the slice scrubs to the played frame", "[slice][motionmatching][aliens][scrub][phaseC]") {
    if (!slicePresent()) {
        SKIP("the retargeted 100STYLE pack is not present");
    }
    Run run;
    for (int f = 0; f <= 200; ++f) {
        run.frame(f);
    }
    const entity::MotionMemory played = run.comp->entityWorld().find("alien-match")->motionMemory();
    for (int f = 201; f <= 320; ++f) {
        run.frame(f);
    }
    run.comp->entityWorld().seek(200.0 / 60.0, &run.params);
    const entity::MotionMemory sought = run.comp->entityWorld().find("alien-match")->motionMemory();
    REQUIRE(played.provider == 0);
    CHECK(sought.selection == played.selection);
    CHECK(std::abs(sought.localTime - played.localTime) < 1e-4f);
}

#include "scene/motion_database.hpp"
#include "scene/motion_pack.hpp"

TEST_CASE("§64/§92 the retargeted walk travels forward, on the ground plane", "[slice][retarget][aliens][phaseC]") {
    // The database can only see a walk if the retargeted root travels horizontally in the rig's
    // model space. A translation carried in the wrong frame travels up, or not at all.
    if (!slicePresent()) {
        SKIP("the retargeted 100STYLE pack is not present");
    }
    auto pack = scene::readMotionPack(fs::path(AVGEN_SOURCE_DIR) / "assets" / "100style-scout-strutting-pack");
    REQUIRE(pack.has_value());
    const int root = pack->skeleton.find("root.x");
    REQUIRE(root >= 0);
    std::string report;
    for (std::size_t c = 0; c < pack->animation.size(); ++c) {
        const scene::AnimationClip& clip = pack->animation[c];
        scene::Pose pose;
        std::vector<glm::mat4> model;
        const auto at = [&](float t) {
            scene::setRestPose(pack->skeleton, pose);
            scene::sampleClip(clip, clip.start + t, pose);
            scene::poseToModel(pack->skeleton, pose, model);
            return glm::vec3(model[static_cast<std::size_t>(root)][3]);
        };
        const glm::vec3 a = at(0.0f);
        const glm::vec3 b = at(2.0f);
        report += fmt::format("  {:<16}", clip.name);
        for (const float s : {0.0f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f}) {
            const glm::vec3 p = at(s);
            report += fmt::format(" t{:.1f}({:+.2f},{:+.2f})", s, p.x, p.z);
        }
        report += "\n";
        if (clip.name == "Strutting_FW") {
            // Path length, not displacement: a 100STYLE subject walks loops in a small capture
            // volume, so two seconds of forward walking can end 0.8 m from where it began.
            float path = 0.0f;
            glm::vec3 prev = at(0.0f);
            for (int i = 1; i <= 120; ++i) {
                const glm::vec3 p = at(static_cast<float>(i) / 30.0f);
                path += std::hypot(p.x - prev.x, p.z - prev.z);
                prev = p;
            }
            WARN(fmt::format("Strutting_FW: {:.2f} m of path in 4 s ({:.2f} m/s)", path, path / 4.0f));
            // Strutting is a slow, showy walk: 0.65 m/s at the source's scale, 0.49 on the scout.
            CHECK(path / 4.0f > 0.3f);
            CHECK(std::abs(b.y - a.y) < 0.2f);
        }
    }
    WARN(report);
}

TEST_CASE("§64/§92 in the body's frame, the forward walk goes forward and the backward walk back",
          "[slice][retarget][aliens][phaseC]") {
    // A retargeted clip's facing comes from its pelvis (it travels). If the retarget turned the
    // body relative to its travel, a forward walk would read as a backward one to the matcher and a
    // wandering alien would pick the backward clips to go forward.
    if (!slicePresent()) {
        SKIP("the retargeted 100STYLE pack is not present");
    }
    auto pack = scene::readMotionPack(fs::path(AVGEN_SOURCE_DIR) / "assets" / "100style-scout-strutting-pack");
    REQUIRE(pack.has_value());
    scene::MotionDatabaseOptions options;
    options.config = scene::defaultBipedConfig("foot.l", "foot.r", "head.x");
    options.config.trajectoryTimes = {0.2f, 0.4f, 0.6f};
    auto db = scene::buildMotionDatabase(*pack, options);
    REQUIRE(db.has_value());
    const auto layout = scene::motionFeatureLayout(db->config);
    std::size_t rv = 0;
    while (layout[rv] != scene::MotionFeatureGroup::RootVelocity) {
        ++rv;
    }
    std::map<std::string, std::pair<double, int>> forward;
    for (std::uint32_t s = 0; s < db->sampleCount(); ++s) {
        const float* f = db->featuresFor(s);
        const double z = (f[rv + 2] / db->scale[rv + 2]) + db->mean[rv + 2];
        auto& acc = forward[db->clipNames[db->sampleClip[s]]];
        acc.first += z;
        ++acc.second;
    }
    std::string report;
    for (const auto& [clip, acc] : forward) {
        report += fmt::format("  {:<16} mean forward velocity {:+.3f} m/s\n", clip, acc.first / acc.second);
    }
    WARN(report);
    CHECK(forward["Strutting_FW"].first / forward["Strutting_FW"].second > 0.3);
    CHECK(forward["Strutting_BW"].first / forward["Strutting_BW"].second < -0.3);
}
