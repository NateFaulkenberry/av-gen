// What a clip IS, measured (ADR-821), and how a sequencer cue plays it.
//
// The Director compiles "Rook jumps, the Umbra pulse fires at the peak" onto these: a per-rig table
// of loop/once, length, flight and its takeoff/peak/touchdown, plants and releases, root motion and
// interruptibility -- and `ClipCue{playback, then}`, which plays a stunt once and hands the rig back.
//
// The board's rule, applied: an event is only worth something if the body actually does the thing
// at that time. So the scout arms check the events against the pose, sampled independently of the
// code that produced them -- feet off the ground in the flight, the body highest at the peak.

#include "app/engine.hpp"
#include "assets/asset_registry.hpp"
#include "entity/entity.hpp"
#include "scene/animation.hpp"
#include "scene/clip_semantics.hpp"
#include "scene/composition.hpp"
#include "seq/director.hpp"
#include "seq/sequence.hpp"
#include "support/project_round_trip.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

// A three-joint body standing on the ground: hips 1 above the origin, feet 0.9 below the hips.
scene::Skeleton body() {
    scene::Skeleton sk;
    sk.name = "hop";
    scene::Transform hips;
    hips.position = {0.0f, 1.0f, 0.0f};
    scene::Transform left;
    left.position = {-0.1f, -0.9f, 0.0f};
    scene::Transform right;
    right.position = {0.1f, -0.9f, 0.0f};
    sk.joints.push_back(scene::Joint{"root", -1, scene::Transform{}});
    sk.joints.push_back(scene::Joint{"hips", 0, hips});
    sk.joints.push_back(scene::Joint{"foot.l", 1, left});
    sk.joints.push_back(scene::Joint{"foot.r", 1, right});
    sk.palette = {0, 1, 2, 3};
    sk.inverseBind.assign(4, glm::mat4(1.0f));
    return sk;
}

// One second: stand, hop 0.4 high between 0.3 s and 0.7 s (a parabola, peak at 0.5 s), stand. With
// `endCrouched` the last key leaves the hips 0.4 lower, so the end does not join the start.
scene::AnimationClip hop(bool endCrouched) {
    scene::AnimationClip clip;
    clip.name = "rig|Hop";
    scene::AnimationChannel c;
    c.joint = 1;
    c.path = scene::AnimationPath::Translation;
    for (int i = 0; i <= 60; ++i) {
        const float t = static_cast<float>(i) / 60.0f;
        float y = 1.0f;
        if (t > 0.3f && t < 0.7f) {
            const float u = (t - 0.5f) / 0.2f;
            y += 0.4f * (1.0f - u * u);
        }
        if (endCrouched && t > 0.8f) {
            y -= 0.4f * (t - 0.8f) / 0.2f;
        }
        c.times.push_back(t);
        c.values.emplace_back(0.0f, y, 0.0f, 0.0f);
    }
    clip.start = 0.0f;
    clip.duration = 1.0f;
    clip.channels = {std::move(c)};
    return clip;
}

float eventAt(const scene::ClipSemantics& s, const char* name) {
    const scene::ClipEvent* e = s.event(name);
    REQUIRE(e != nullptr);
    return e->seconds;
}

fs::path scout() { return fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb"; }

} // namespace

TEST_CASE("a hop is measured where the body actually leaves the ground and comes back", "[motion][semantics]") {
    const scene::Skeleton sk = body();
    const scene::ClipSemanticsTable table = scene::clipSemantics(sk, {hop(false)});
    REQUIRE(table.feet == std::vector<std::string>{"foot.l", "foot.r"});
    const scene::ClipSemantics& s = table.clips.front();
    CHECK(s.clip == "Hop"); // the short name, the one the player knows
    CHECK(s.length == Catch::Approx(1.0f));
    CHECK(s.loops);          // stand ... stand: the end joins the start
    CHECK(s.ground == scene::ClipGround::Leaves);
    // The feet clear the 0.04 band once the rise passes 0.04: |t - 0.5| < 0.19, so the first 30 Hz
    // sample off the ground is 10/30 and the first one back is 21/30.
    CHECK(eventAt(s, "takeoff") == Catch::Approx(10.0f / 30.0f));
    CHECK(eventAt(s, "peak") == Catch::Approx(0.5f));
    CHECK(eventAt(s, "touchdown") == Catch::Approx(21.0f / 30.0f));
    CHECK(s.peakHeight == Catch::Approx(0.4f).margin(1e-3));
    CHECK(s.committed.empty()); // a loop is built to be left at any frame

    SECTION("an end that does not join the start is a one-shot, committed through its flight") {
        const scene::ClipSemantics once = scene::clipSemantics(sk, {hop(true)}).clips.front();
        CHECK_FALSE(once.loops);
        REQUIRE(once.committed.size() == 1);
        CHECK_FALSE(once.interruptibleAt(0.5f));
        CHECK(once.interruptibleAt(0.2f));
        CHECK(once.interruptibleAt(0.9f));
    }
    SECTION("control: standing still is grounded, with no flight and no peak") {
        scene::AnimationClip still = hop(false);
        for (glm::vec4& v : still.channels.front().values) {
            v.y = 1.0f;
        }
        const scene::ClipSemantics g = scene::clipSemantics(sk, {still}).clips.front();
        CHECK(g.ground == scene::ClipGround::Grounded);
        CHECK(g.event("peak") == nullptr);
        CHECK(g.event("takeoff") == nullptr);
    }
    SECTION("a rig with no feet says so rather than inventing a ground") {
        scene::Skeleton footless = sk;
        footless.joints[2].name = "leaf.l";
        footless.joints[3].name = "leaf.r";
        const scene::ClipSemanticsTable t = scene::clipSemantics(footless, {hop(false)});
        CHECK(t.feet.empty());
        CHECK_FALSE(t.warnings.empty());
        CHECK(t.clips.front().ground == scene::ClipGround::Grounded);
    }
}

TEST_CASE("the scout's clips, measured, agree with what the body does in them", "[motion][semantics][aliens]") {
    if (!fs::exists(scout())) {
        SKIP("assets/aliens/alien-scout.glb is not present (gitignored; link the worktree's assets)");
    }
    assets::AssetRegistry registry(scout().parent_path());
    auto asset = registry.loadScene(scout());
    REQUIRE(asset.has_value());
    REQUIRE_FALSE((*asset)->scene.rigs.empty());
    const scene::SkinnedRig& rig = (*asset)->scene.rigs.front();
    const scene::ClipSemanticsTable* table = rig.semantics();
    REQUIRE(table != nullptr);
    CHECK(table->feet == std::vector<std::string>{"foot.l", "foot.r"});

    const auto get = [&](const char* name) -> const scene::ClipSemantics& {
        const scene::ClipSemantics* s = table->find(name);
        REQUIRE(s != nullptr);
        return *s;
    };
    // Loops and one-shots, as the clips close (or do not).
    for (const char* loop : {"Idle", "Walking", "Running", "Fall_loop"}) {
        CAPTURE(loop);
        CHECK(get(loop).loops);
    }
    for (const char* once : {"Landing", "Crazy", "Dying_forward"}) {
        CAPTURE(once);
        CHECK_FALSE(get(once).loops);
    }
    // The ground. Walking and running are grounded (a run's float is shorter than a flight); the
    // deaths lie on the ground (the torso and hands are what touch it); the fall and the jet are off it.
    for (const char* grounded : {"Idle", "Walking", "Running", "Dying_1_backpack", "Dying_forward"}) {
        CAPTURE(grounded);
        CHECK(get(grounded).ground == scene::ClipGround::Grounded);
    }
    CHECK(get("Fall_loop").ground == scene::ClipGround::Airborne);
    CHECK(get("Flying_jet").ground == scene::ClipGround::Airborne);
    CHECK(get("Fall_loop").event("peak") == nullptr);
    // The jumps, and the landing: a flight with an order to it.
    for (const char* jump : {"Jumping", "Jump_running"}) {
        CAPTURE(jump);
        const scene::ClipSemantics& j = get(jump);
        CHECK(j.ground == scene::ClipGround::Leaves);
        CHECK(eventAt(j, "takeoff") < eventAt(j, "peak"));
        CHECK(eventAt(j, "peak") < eventAt(j, "touchdown"));
        CHECK(j.peakHeight > 0.3f);
    }
    const scene::ClipSemantics& landing = get("Landing");
    CHECK(landing.event("takeoff") == nullptr); // it opens in the air
    CHECK(eventAt(landing, "touchdown") > 0.1f);
    CHECK(eventAt(landing, "touchdown") < 0.5f);
    REQUIRE_FALSE(landing.committed.empty());
    CHECK_FALSE(landing.interruptibleAt(0.1f));

    SECTION("independently: the feet are off the ground in the flight and the body is highest at the peak") {
        // Sampled here, not by the code under test: the rest-pose foot height as the ground, and the
        // pose at each event. Measure the joint, not the number written about it.
        const scene::ClipSemantics& j = get("Jumping");
        const int clipIndex = scene::findClip(rig.clips, "Jumping");
        REQUIRE(clipIndex >= 0);
        const scene::AnimationClip& clip = rig.clips[static_cast<std::size_t>(clipIndex)];
        const int footL = rig.skeleton.find("foot.l");
        const int footR = rig.skeleton.find("foot.r");
        const int bodyJoint = scene::clipTravelJoint(rig.skeleton, clip);
        const auto at = [&](float seconds) {
            scene::Pose pose;
            scene::setRestPose(rig.skeleton, pose);
            scene::sampleClip(clip, clip.start + seconds, pose);
            std::vector<glm::mat4> model;
            scene::poseToModel(rig.skeleton, pose, model);
            const float feet = std::min(model[static_cast<std::size_t>(footL)][3].y,
                                        model[static_cast<std::size_t>(footR)][3].y);
            return std::pair{feet, model[static_cast<std::size_t>(bodyJoint)][3].y};
        };
        const float ground = at(0.0f).first; // standing at the start
        const float mid = 0.5f * (eventAt(j, "takeoff") + eventAt(j, "touchdown"));
        CHECK(at(mid).first > ground + 0.1f);            // both feet well off the ground mid-flight
        CHECK(at(eventAt(j, "peak")).second > at(eventAt(j, "takeoff")).second);
        CHECK(at(eventAt(j, "peak")).second > at(eventAt(j, "touchdown")).second);
        float highest = -1e9f;
        for (float t = 0.0f; t <= clip.length(); t += 1.0f / 30.0f) {
            highest = std::max(highest, at(t).second);
        }
        CHECK(at(eventAt(j, "peak")).second == Catch::Approx(highest).margin(1e-4));
    }
}

TEST_CASE("a cue plays once when asked, or when the clip says so, and hands the rig on", "[motion][semantics][seq]") {
    scene::ClipSemantics jump;
    jump.clip = "Jumping";
    jump.length = 1.9f;
    jump.loops = true; // measured: the scout's jump closes
    scene::ClipSemantics landing;
    landing.clip = "Landing";
    landing.length = 1.0f;
    landing.loops = false;
    const seq::ClipLookup lookup = [&](std::string_view clip) -> const scene::ClipSemantics* {
        return clip == "Jumping" ? &jump : clip == "Landing" ? &landing : nullptr;
    };
    const auto cue = [](const char* clip, double at, seq::ClipPlayback playback, const char* then, float speed = 1.0f) {
        return seq::AnimationCue{.node = "rook", .clip = clip, .startSeconds = at, .speed = speed,
                                 .playback = playback, .then = then};
    };

    SECTION("auto asks the clip") {
        CHECK(seq::resolveCue(cue("Jumping", 2.0, seq::ClipPlayback::Auto, ""), 2.5, lookup)->loop == true);
        CHECK(seq::resolveCue(cue("Landing", 2.0, seq::ClipPlayback::Auto, ""), 2.5, lookup)->loop == false);
        // A clip nothing measured keeps the state's own default, as every cue did before.
        CHECK_FALSE(seq::resolveCue(cue("Unknown", 2.0, seq::ClipPlayback::Auto, ""), 2.5, lookup)->loop.has_value());
        CHECK_FALSE(seq::resolveCue(cue("Landing", 2.0, seq::ClipPlayback::Auto, ""), 2.5, {})->loop.has_value());
    }
    SECTION("once overrides a clip that loops, and `then: gait` hands the rig back at its end") {
        const auto c = cue("Jumping", 2.0, seq::ClipPlayback::Once, "gait", 1.9f); // 1 s of timeline
        const auto during = seq::resolveCue(c, 2.99, lookup);
        REQUIRE(during.has_value());
        CHECK(during->clip == "Jumping");
        CHECK(during->loop == false);
        CHECK_FALSE(during->gait);
        const auto after = seq::resolveCue(c, 3.0, lookup);
        REQUIRE(after.has_value());
        CHECK(after->gait);
    }
    SECTION("`then` a state: it follows at the one-shot's end, by its own rules") {
        const auto c = cue("Landing", 5.0, seq::ClipPlayback::Auto, "Jumping");
        const auto next = seq::resolveCue(c, 6.5, lookup);
        REQUIRE(next.has_value());
        CHECK(next->clip == "Jumping");
        CHECK(next->startSeconds == Catch::Approx(6.0));
        CHECK(next->loop == true);
    }
    SECTION("control: a loop never reaches its `then`") {
        const auto c = cue("Jumping", 2.0, seq::ClipPlayback::Loop, "gait");
        CHECK_FALSE(seq::resolveCue(c, 60.0, lookup)->gait);
    }
    SECTION("a clip event on the timeline is the cue's time plus the event over the speed") {
        CHECK(seq::clipEventSeconds(cue("Jumping", 90.0, seq::ClipPlayback::Once, "", 2.0f), 0.767f) ==
              Catch::Approx(90.3835));
    }
}

TEST_CASE("a clip cue's playback and then survive the project file", "[motion][semantics][seq]") {
    seq::Sequence piece;
    seq::Actor a;
    a.id = "rook";
    a.clips.push_back(seq::ClipCue{.timeSeconds = 1.0, .clip = "Jumping", .playback = seq::ClipPlayback::Once, .then = "gait"});
    a.clips.push_back(seq::ClipCue{.timeSeconds = 4.0, .clip = "Idle"});
    piece.actors.push_back(a);
    const nlohmann::json j = piece.toJson();
    CHECK_FALSE(j["actors"][0]["clips"][1].contains("playback")); // auto is the default and not written
    auto again = seq::Sequence::fromJson(j);
    REQUIRE(again.has_value());
    CHECK(again->actors[0].clips[0].playback == seq::ClipPlayback::Once);
    CHECK(again->actors[0].clips[0].then == "gait");
    CHECK(again->actors[0].clips[1].playback == seq::ClipPlayback::Auto);

    SECTION("an unknown playback is refused") {
        nlohmann::json bad = j;
        bad["actors"][0]["clips"][0]["playback"] = "twice";
        CHECK_FALSE(seq::Sequence::fromJson(bad).has_value());
    }
}

TEST_CASE("the player plays a looping state once for one request and loops it for the next", "[motion][semantics]") {
    const scene::Skeleton sk = body();
    scene::AnimationPlayer player;
    player.addState(scene::AnimationState{.name = "Hop", .clip = 0, .loop = true});
    player.addState(scene::AnimationState{.name = "Stand", .clip = 0, .loop = true});
    const std::vector<scene::AnimationClip> clips{hop(false)};
    REQUIRE(player.play("Hop", 0.0, 0.0f));
    player.setLooping(false);
    CHECK_FALSE(player.currentLooping());
    CHECK(player.stateTime(clips, 1.5) == Catch::Approx(1.0f)); // clamped at the end: a held last frame
    CHECK(player.finished(clips, 1.5));
    SECTION("control: the state's own default wraps") {
        player.setLooping(std::nullopt);
        CHECK(player.stateTime(clips, 1.5) == Catch::Approx(0.5f));
        CHECK_FALSE(player.finished(clips, 1.5));
    }
    SECTION("a new play starts from the state's default again") {
        REQUIRE(player.play("Stand", 2.0, 0.0f));
        CHECK(player.currentLooping());
    }
}

TEST_CASE("on the benchmark, a stunt cue plays once on Rook and hands him back to his gait",
          "[motion][semantics][handoff][benchmark]") {
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(fs::path(AVGEN_SOURCE_DIR) / "examples/world/glowmere-valley-2-multicam.json"));
    const entity::Entity* rook = engine.composition()->entityWorld().find("rook");
    REQUIRE(rook != nullptr);
    const glm::vec3 start = rook->state().position();
    const scene::CompositionNode* node = engine.composition()->findNode("rook");
    REQUIRE(node != nullptr);
    REQUIRE_FALSE(node->rigs.empty());
    const auto rig = [&]() -> const scene::SkinnedRig& { return engine.composition()->scene().rigs.at(node->rigs.front()); };
    // His autonomous clips are untouched: before any performance the gait's clip loops.
    testsupport::stepFrames(engine, 30);
    CHECK(rig().player.currentLooping());

    seq::Sequence piece = engine.sequence();
    seq::Actor actor;
    actor.id = "rook";
    actor.keys.push_back(seq::ActorKey{1.0, start, std::nullopt, std::nullopt, params::KeyInterp::Linear});
    actor.keys.push_back(seq::ActorKey{6.0, start + glm::vec3(30.0f, 0.0f, 0.0f), std::nullopt, std::nullopt,
                                       params::KeyInterp::Linear});
    // Jumping measures as a loop; the stunt plays it once, then gives the rig back to the gait.
    actor.clips.push_back(seq::ClipCue{.timeSeconds = 2.0, .clip = "Jumping", .playback = seq::ClipPlayback::Once, .then = "gait"});
    piece.actors.push_back(actor);
    REQUIRE(engine.setSequence(piece).has_value());
    const scene::ClipSemanticsTable* table = engine.composition()->clipSemanticsFor("rook");
    REQUIRE(table != nullptr);
    const float jumpLength = table->find("Jumping")->length;

    testsupport::stepFrames(engine, 151); // to 2.5 s
    CHECK(rig().player.currentState() == "Jumping");
    CHECK_FALSE(rig().player.currentLooping());
    CHECK(rook->locomotion().clipOwned);

    const double after = 2.0 + static_cast<double>(jumpLength) + 0.2;
    testsupport::stepFrames(engine, static_cast<int>(std::lround((after - 2.5) * 60.0)), 2.5 + 1.0 / 60.0);
    CHECK_FALSE(rook->locomotion().clipOwned);
    CHECK(rook->directorMotion().active); // still performing: only the clip was handed back
    CHECK(rig().player.currentState() == "Running"); // 6 m/s path, his gait's own choice
    CHECK(rig().player.currentLooping());
}

TEST_CASE("an autonomous react plays Crazy once and holds its crouch (the owner's ruling)",
          "[motion][semantics][benchmark]") {
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(fs::path(AVGEN_SOURCE_DIR) / "examples/world/glowmere-valley-2-multicam.json"));
    const scene::CompositionNode* node = engine.composition()->findNode("rook");
    REQUIRE(node != nullptr);
    const auto rig = [&]() -> const scene::SkinnedRig& { return engine.composition()->scene().rigs.at(node->rigs.front()); };
    entity::ActionDesc react;
    react.kind = entity::ActionKind::Pose;
    react.activity = "react";
    react.duration = 6.0;
    testsupport::stepFrames(engine, 60);
    REQUIRE(engine.composition()->entityWorld().direct("rook", {react}, 1.0));
    testsupport::stepFrames(engine, 60 * 4, 1.0); // to 5 s: Crazy (2.63 s) is over
    REQUIRE(rig().player.currentState() == "Crazy");
    CHECK_FALSE(rig().player.currentLooping());
    CHECK(rig().player.finished(rig().clips, 5.0));
    SECTION("control: a clip that closes still loops for the same body") {
        entity::ActionDesc idle = react;
        idle.activity = "idle";
        REQUIRE(engine.composition()->entityWorld().direct("rook", {idle}, 5.0));
        testsupport::stepFrames(engine, 60 * 4, 5.0);
        REQUIRE(rig().player.currentState() == "Idle");
        CHECK(rig().player.currentLooping());
    }
}
