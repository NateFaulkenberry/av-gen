// ADR-623: the matcher on the product chain, in front of the clip provider, and the fallback that
// makes that safe.
//
// §35 names the failures the fallback exists for. Each case below provokes one and shows the
// chain landing on the clip provider (index 1). Each has a control arm with the same chain and a
// healthy matcher, which must *not* fall through, because a fallback that always fires would pass
// every "falls through" check and play no matched motion at all.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "entity/clip_motion_provider.hpp"
#include "entity/entity.hpp"
#include "entity/match_motion_provider.hpp"
#include "entity/motion_chain.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/motion_database.hpp"
#include "signals/signal_bus.hpp"
#include "support/motion_fixtures.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <cmath>
#include <filesystem>
#include <limits>
#include <set>
#include <string>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

constexpr int kMatcher = 0;
constexpr int kClips = 1;

struct Rig {
    scene::MotionPack pack = testsupport::probePack();
    scene::MotionDatabase db;
    entity::MatchMotionProvider matcher;
    entity::ClipMotionProvider clips;
    entity::MotionChain chain;

    Rig() {
        auto built = scene::buildMotionDatabase(pack, testsupport::probeOptions());
        REQUIRE(built.has_value());
        db = std::move(*built);
        matcher.setDatabase(&db);
        matcher.setClips(&pack.animation);
        matcher.setExpectedSkeleton(pack.skeletonDigest);
        clips.setClips(&pack.animation);
        entity::ClipEntry walk;
        walk.clip = 0;
        walk.mode = entity::MovementMode::Ground;
        walk.authoredSpeed = 1.2f;
        walk.loop = true;
        clips.addEntry(walk);
        chain.add(&matcher);
        chain.add(&clips);
    }
};

entity::MotionRequest walking() {
    entity::MotionRequest r;
    r.desiredVelocity = glm::vec3(0.0f, 0.0f, 1.2f);
    return r;
}

entity::MotionChainResult step(const entity::MotionChain& chain, const entity::MotionRequest& request,
                               entity::MotionMemory& memory, int frame) {
    entity::MotionMemory next;
    const entity::MotionChainResult r =
        chain.advance(request, memory, static_cast<double>(frame) / 60.0, 1.0f / 60.0f, next);
    memory = next;
    return r;
}

} // namespace

TEST_CASE("control: a healthy matcher is chosen, and the clip provider is not reached",
          "[motionmatching][fallback][phaseC]") {
    Rig rig;
    entity::MotionMemory memory;
    for (int f = 0; f < 30; ++f) {
        const auto r = step(rig.chain, walking(), memory, f);
        REQUIRE(r.ok());
        CHECK(r.provider == kMatcher);
        CHECK(r.fellThrough == 0u);
    }
}

TEST_CASE("no database: the chain falls through to the clip provider", "[motionmatching][fallback][phaseC]") {
    Rig rig;
    rig.matcher.setDatabase(nullptr);
    entity::MotionMemory memory;
    const auto r = step(rig.chain, walking(), memory, 0);
    REQUIRE(r.ok());
    CHECK(r.provider == kClips);
    CHECK(r.firstDeclined == entity::MotionStatus::NotReady);
}

TEST_CASE("an empty database: the chain falls through", "[motionmatching][fallback][phaseC]") {
    Rig rig;
    scene::MotionDatabase empty;
    empty.config = rig.db.config;
    empty.dimension = rig.db.dimension;
    rig.matcher.setDatabase(&empty);
    entity::MotionMemory memory;
    const auto r = step(rig.chain, walking(), memory, 0);
    REQUIRE(r.ok());
    CHECK(r.provider == kClips);
}

TEST_CASE("the filter empties the candidate set: the chain falls through", "[motionmatching][fallback][phaseC]") {
    // Every sample tagged airborne, and the matcher always rejects airborne for a grounded body.
    Rig rig;
    for (std::uint32_t& tags : rig.db.sampleTags) {
        tags |= static_cast<std::uint32_t>(scene::MotionTag::Airborne);
    }
    entity::MotionMemory memory;
    const auto r = step(rig.chain, walking(), memory, 0);
    REQUIRE(r.ok());
    CHECK(r.provider == kClips);
    CHECK(r.firstDeclined == entity::MotionStatus::NoContent);
}

TEST_CASE("the wrong skeleton: the chain falls through rather than pose another rig's motion",
          "[motionmatching][fallback][phaseC]") {
    Rig rig;
    rig.matcher.setExpectedSkeleton("some-other-skeleton");
    entity::MotionMemory memory;
    const auto r = step(rig.chain, walking(), memory, 0);
    REQUIRE(r.ok());
    CHECK(r.provider == kClips);
    CHECK(r.firstDeclined == entity::MotionStatus::NotReady);
}

TEST_CASE("an invalid query: the chain falls through", "[motionmatching][fallback][phaseC]") {
    Rig rig;
    entity::MotionRequest bad = walking();
    bad.desiredVelocity.x = std::numeric_limits<float>::quiet_NaN();
    entity::MotionMemory memory;
    const auto r = step(rig.chain, bad, memory, 0);
    REQUIRE(r.ok());
    CHECK(r.provider == kClips);
    CHECK(r.firstDeclined == entity::MotionStatus::Unsupported);
}

TEST_CASE("a database swap in flight re-selects rather than falling through or posing stale memory",
          "[motionmatching][fallback][phaseC]") {
    // Not a fallback case, and deliberately so: a matcher whose database was swapped is healthy. It
    // must re-select in the new database, and a memory settled against the old one must never
    // pose.
    Rig rig;
    entity::MotionMemory memory;
    for (int f = 0; f < 10; ++f) {
        REQUIRE(step(rig.chain, walking(), memory, f).provider == kMatcher);
    }
    const entity::MotionMemory before = memory;

    auto rebuilt = scene::buildMotionDatabase(rig.pack, testsupport::probeOptions());
    REQUIRE(rebuilt.has_value());
    scene::MotionDatabase swapped = std::move(*rebuilt);
    swapped.identity = rig.db.identity + 1u; // a different database, as a hot swap publishes
    rig.matcher.setDatabase(&swapped);

    scene::Pose pose;
    CHECK_FALSE(rig.matcher.pose(before, rig.pack.skeleton, pose).ok());
    const auto r = step(rig.chain, walking(), memory, 10);
    REQUIRE(r.ok());
    CHECK(r.provider == kMatcher);
    CHECK(memory.database == swapped.identity);
    CHECK(rig.matcher.pose(memory, rig.pack.skeleton, pose).ok());
}

TEST_CASE("falling through hands the clip provider a fresh memory, not the matcher's",
          "[motionmatching][fallback][phaseC]") {
    // A memory's `selection` indexes its own provider's space: a database sample for the matcher,
    // a clip for the clip player. Handed over as-is, a fallback read sample 20 as clip 20.
    Rig rig;
    entity::MotionMemory memory;
    for (int f = 0; f < 20; ++f) {
        REQUIRE(step(rig.chain, walking(), memory, f).provider == kMatcher);
    }
    // The subject exists: the matcher's memory is well into a clip.
    REQUIRE(memory.generation > 0u);
    REQUIRE(memory.localTime > 0.05f);

    rig.matcher.setDatabase(nullptr);
    const auto r = step(rig.chain, walking(), memory, 20);
    REQUIRE(r.ok());
    CHECK(r.provider == kClips);
    // A fresh start of its own walk: clip 0, from its beginning, as its own first selection.
    CHECK(memory.selection == 0u);
    CHECK(memory.localTime == 0.0f);
    CHECK(memory.generation == 1u);
    CHECK_FALSE(memory.blending());
}

TEST_CASE("the motionMatching key round-trips, and its absence writes nothing",
          "[motionmatching][serialise][phaseC]") {
    const nlohmann::json authored = {
        {"name", "matched"},
        {"motionMatching",
         {{"joints", {"foot.l", "foot.r", "head.x"}}, {"contacts", {"foot.l", "foot.r"}}, {"trajectory", {0.25, 0.5}}}}};
    auto desc = entity::entityFromJson(authored, {}, nullptr);
    REQUIRE(desc.has_value());
    CHECK(desc->motionMatching.enabled);
    CHECK(desc->proceduralMotion); // implied: providers only run on that path
    CHECK(desc->motionMatching.joints == std::vector<std::string>{"foot.l", "foot.r", "head.x"});
    CHECK(desc->motionMatching.contacts == std::vector<std::string>{"foot.l", "foot.r"});
    CHECK(desc->motionMatching.trajectory == std::vector<float>{0.25f, 0.5f});

    // Through the writer the product saves with, and back.
    const nlohmann::json saved = entity::entitiesToJson({*desc});
    REQUIRE(saved.is_array());
    auto again = entity::entityFromJson(saved.at(0), {}, nullptr);
    REQUIRE(again.has_value());
    CHECK(again->motionMatching == desc->motionMatching);
    CHECK(again->proceduralMotion);

    // Absent: off, and nothing written, so a save of any existing scene is byte-identical.
    auto plain = entity::entityFromJson(nlohmann::json{{"name", "plain"}}, {}, nullptr);
    REQUIRE(plain.has_value());
    CHECK_FALSE(plain->motionMatching.enabled);
    CHECK_FALSE(entity::entitiesToJson({*plain}).at(0).contains("motionMatching"));

    // Malformed is an error, not a silent no-op.
    CHECK_FALSE(entity::entityFromJson(nlohmann::json{{"name", "x"}, {"motionMatching", {{"contacts", {"foot.l"}}}}}, {}, nullptr)
                    .has_value());
    CHECK_FALSE(entity::entityFromJson(nlohmann::json{{"name", "x"}, {"motionMatching", true}}, {}, nullptr).has_value());
}

// ---- the demo scene -------------------------------------------------------------------------------

namespace {

fs::path matchLab() {
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / "labs" / "motionmatch" / "alien-match-lab.scene.json";
}

struct Lab {
    assets::AssetRegistry registry;
    std::unique_ptr<scene::Composition> comp;
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;

    Lab() : registry(matchLab().parent_path()) {
        auto loaded = scene::Composition::loadFile(matchLab(), registry);
        INFO((loaded.has_value() ? std::string() : loaded.error().message));
        REQUIRE(loaded.has_value());
        comp = std::move(*loaded);
        comp->attach(params, modulator);
        comp->setViewport(640, 360);
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

struct Snapshot {
    entity::MotionMemory memory;
    std::vector<scene::Transform> pose;
};

Snapshot snapshot(const Lab& lab, const char* name) {
    Snapshot s;
    const entity::Entity* e = lab.comp->entityWorld().find(name);
    REQUIRE(e != nullptr);
    s.memory = e->motionMemory();
    for (const auto& node : lab.comp->nodes()) {
        if (node->name == name && !node->rigs.empty()) {
            s.pose = lab.comp->scene().rigs[node->rigs.front()].pose.local;
        }
    }
    return s;
}

} // namespace

TEST_CASE("an alien runs on the matcher in a scene, and its clip-provider twin does not",
          "[motionmatching][lab][aliens][phaseC]") {
    if (!fs::exists(matchLab()) ||
        !fs::exists(fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb")) {
        SKIP("the match lab or the scout is not present");
    }
    Lab lab;
    int matcherFrames = 0;
    int clipFrames = 0;
    int moving = 0;
    std::set<std::string> matched;
    for (int f = 0; f <= 360; ++f) {
        lab.frame(f);
        const entity::Entity* m = lab.comp->entityWorld().find("alien-match");
        const entity::Entity* c = lab.comp->entityWorld().find("alien-clip");
        REQUIRE(m != nullptr);
        REQUIRE(c != nullptr);
        matcherFrames += m->motionChainResult().provider == kMatcher ? 1 : 0;
        clipFrames += c->motionChainResult().provider == 0 ? 1 : 0;
        moving += m->state().speed > 0.2f ? 1 : 0;
        if (m->motionChainResult().ok()) {
            matched.insert(std::string(m->motionChainResult().result.content));
        }
    }
    std::string clipsSeen;
    for (const auto& n : matched) {
        clipsSeen += n + " ";
    }
    WARN(fmt::format("alien-match: {} of 361 frames from the matcher, moving on {}; clips: {}",
                     matcherFrames, moving, clipsSeen));
    // The subject exists: the body moves, so the matcher had something to match.
    REQUIRE(moving > 60);
    CHECK(matcherFrames == 361);
    // The twin's chain is the clip provider alone, so its index 0 IS the clip provider.
    const entity::Entity* twin = lab.comp->entityWorld().find("alien-clip");
    CHECK(twin->motionChain()->size() == 1u);
    CHECK(clipFrames == 361);
    CHECK(lab.comp->entityWorld().find("alien-match")->motionChain()->size() == 2u);
}

TEST_CASE("a scrub lands on the frame a play reached, on the matcher (ADR-360)",
          "[motionmatching][lab][aliens][scrub][phaseC]") {
    // The matcher's pose is a pure function of its memory (ADR-556), so the memory is what has to
    // agree. A seek replays the simulation from the top, and the played frame and the sought frame
    // must hold the same sample, the same clip time and the same blends in flight.
    if (!fs::exists(matchLab()) ||
        !fs::exists(fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb")) {
        SKIP("the match lab or the scout is not present");
    }
    constexpr int kFrame = 200;
    Lab played;
    for (int f = 0; f <= kFrame; ++f) {
        played.frame(f);
    }
    const Snapshot atPlay = snapshot(played, "alien-match");

    // Played on past it, then sought back, as a person scrubbing the timeline does.
    for (int f = kFrame + 1; f <= kFrame + 120; ++f) {
        played.frame(f);
    }
    const Snapshot later = snapshot(played, "alien-match");
    played.comp->entityWorld().seek(static_cast<double>(kFrame) / 60.0, &played.params);
    const Snapshot atScrub = snapshot(played, "alien-match");

    // The subject exists: by frame 200 the matcher has been running and has chosen something, and
    // two seconds later it is somewhere else, so agreement is not the memory standing still.
    REQUIRE(atPlay.memory.provider == kMatcher);
    REQUIRE(atPlay.memory.generation > 0u);
    REQUIRE((later.memory.selection != atPlay.memory.selection ||
             later.memory.localTime != atPlay.memory.localTime));
    WARN(fmt::format("frame {}: play selection {} t {:.4f}; scrub selection {} t {:.4f}", kFrame,
                     atPlay.memory.selection, atPlay.memory.localTime, atScrub.memory.selection,
                     atScrub.memory.localTime));
    CHECK(atScrub.memory.provider == atPlay.memory.provider);
    CHECK(atScrub.memory.selection == atPlay.memory.selection);
    CHECK(atScrub.memory.generation == atPlay.memory.generation);
    CHECK(std::abs(atScrub.memory.localTime - atPlay.memory.localTime) < 1e-4f);
    for (std::size_t b = 0; b < entity::MotionMemory::kBlendSlots; ++b) {
        CHECK(atScrub.memory.blends[b].from == atPlay.memory.blends[b].from);
        CHECK(atScrub.memory.blends[b].to == atPlay.memory.blends[b].to);
        CHECK(std::abs(atScrub.memory.blends[b].elapsed - atPlay.memory.blends[b].elapsed) < 1e-4f);
    }
}

TEST_CASE("§59/§66: the matcher chooses the pose and Phase B's layers still adapt it",
          "[motionmatching][lab][aliens][phaseC]") {
    // §59: "motion matching should choose a good source motion, while Phase B procedural systems
    // adapt it." §66: "do not bypass Phase B." The matched alien's base pose comes from the
    // matcher, and its foot layers (ground-driven, with body compensation) must still resolve on
    // top of it, on the same frames. The twin, on the clip provider, is the control: the layers
    // behave the same way on both, so the matcher did not route around them.
    if (!fs::exists(matchLab()) ||
        !fs::exists(fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb")) {
        SKIP("the match lab or the scout is not present");
    }
    Lab lab;
    int matchedWithLayers = 0;
    int twinWithLayers = 0;
    for (int f = 0; f <= 180; ++f) {
        lab.frame(f);
        for (const auto& [node, counter] : {std::pair{"alien-match", &matchedWithLayers}, std::pair{"alien-clip", &twinWithLayers}}) {
            const scene::Composition::MotionDebug d = lab.comp->motionDebug(node);
            REQUIRE(d.found);
            if (!d.posedByProvider) {
                continue;
            }
            int applied = 0;
            for (const auto& row : d.layers) {
                applied += (row.resolution == scene::LayerResolution::Applied ||
                            row.resolution == scene::LayerResolution::Clamped)
                               ? 1
                               : 0;
            }
            *counter += applied == 2 ? 1 : 0; // both feet
        }
    }
    WARN(fmt::format("frames with both foot layers resolved on a provider pose: matched {}, twin {}",
                     matchedWithLayers, twinWithLayers));
    CHECK(matchedWithLayers > 150);
    CHECK(twinWithLayers > 150);
    // And the matched body really is on the matcher on those frames.
    CHECK(lab.comp->motionDebug("alien-match").provider == kMatcher);
}
