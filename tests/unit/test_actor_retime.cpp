// A local retime of one actor (ADR-823): the Director's slow motion, as stretched keys and scaled clip
// speeds on the performer alone.
//
// The claim is that a retime is a reparameterisation and nothing else: at every retimed second the
// body is where it was at the original second that maps there, and the clip is at the frame it was.
// So the arms compare the retimed actor with the original through the time map, not with numbers
// written down beside the code.

#include "app/engine.hpp"
#include "entity/entity.hpp"
#include "scene/composition.hpp"
#include "seq/director.hpp"
#include "seq/retime.hpp"
#include "seq/sequence.hpp"
#include "support/project_round_trip.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>

using namespace avgen;
using Catch::Approx;

namespace {

seq::ActorKey key(double t, glm::vec3 p) {
    return seq::ActorKey{t, p, std::nullopt, std::nullopt, params::KeyInterp::Linear};
}

// The original second that the retimed second `T` came from: the map's inverse.
double inverse(double T, double a, double b, float rate) {
    const double r = static_cast<double>(rate);
    if (T <= a) {
        return T;
    }
    const double stretchedEnd = a + (b - a) / r;
    if (T <= stretchedEnd) {
        return a + (T - a) * r;
    }
    return T - (b - a) * (1.0 / r - 1.0);
}

// The clip second an actor's rig is at, at timeline second `t`, as the sequencer resolves it.
float clipTime(const seq::Actor& actor, double t) {
    const seq::ClipCue* cue = actor.clipAt(t);
    REQUIRE(cue != nullptr);
    const seq::AnimationCue as{.node = "rook", .clip = cue->clip, .startSeconds = cue->timeSeconds,
                               .speed = cue->speed, .offsetSeconds = cue->offsetSeconds};
    const auto resolved = seq::resolveCue(as, t, {});
    REQUIRE(resolved.has_value());
    return static_cast<float>((t - resolved->startSeconds) * static_cast<double>(resolved->speed));
}

} // namespace

TEST_CASE("the retime map stretches the window and pushes the rest later", "[motion][retime]") {
    CHECK(seq::retimeMap(1.0, 2.0, 4.0, 0.5f) == 1.0);
    CHECK(seq::retimeMap(3.0, 2.0, 4.0, 0.5f) == Approx(4.0));
    CHECK(seq::retimeMap(4.0, 2.0, 4.0, 0.5f) == Approx(6.0));
    CHECK(seq::retimeMap(7.0, 2.0, 4.0, 0.5f) == Approx(9.0));
    CHECK(seq::retimeMap(3.0, 2.0, 4.0, 2.0f) == Approx(2.5)); // fast motion compresses
}

TEST_CASE("a retimed actor is where the original was, at the second that maps there", "[motion][retime]") {
    seq::Actor original;
    original.id = "rook";
    // One key interval straddles BOTH edges of the window: the retime must cut it at each edge, or
    // the body would slow across the whole interval instead of inside the window only.
    original.keys = {key(1.0, {0, 0, 0}), key(5.0, {24, 0, 8}), key(7.0, {30, 0, 8})};
    seq::Actor slowed = original;
    REQUIRE(seq::retimeActor(slowed, 2.0, 4.0, 0.25f).has_value());
    float worst = 0.0f;
    for (double T = 0.0; T <= 14.0; T += 0.01) {
        const double t = inverse(T, 2.0, 4.0, 0.25f);
        worst = std::max(worst, glm::length(slowed.positionAt(T) - original.positionAt(t)));
    }
    CHECK(worst < 1e-4f);
    // Inside the window the body covers ground at a quarter of the pace.
    const float before = glm::length(original.positionAt(3.01) - original.positionAt(2.99)) / 0.02f;
    const float during = glm::length(slowed.positionAt(6.01) - slowed.positionAt(5.99)) / 0.02f;
    CHECK(during == Approx(before * 0.25f).epsilon(1e-3));
    REQUIRE(slowed.timeWarps.size() == 1);
    CHECK(slowed.timeWarps[0].endSeconds == Approx(10.0));
    CHECK(slowed.timeScaleAt(6.0) == 0.25f);
    CHECK(slowed.timeScaleAt(11.0) == 1.0f);

    SECTION("control: stretching the keys alone, without the edge keys, is a different motion") {
        seq::Actor naive = original;
        for (seq::ActorKey& k : naive.keys) {
            k.timeSeconds = seq::retimeMap(k.timeSeconds, 2.0, 4.0, 0.25f);
        }
        float off = 0.0f;
        for (double T = 0.0; T <= 14.0; T += 0.01) {
            off = std::max(off, glm::length(naive.positionAt(T) - original.positionAt(inverse(T, 2.0, 4.0, 0.25f))));
        }
        CHECK(off > 1.0f);
    }
}

TEST_CASE("a clip cut by the window carries on from the frame it had reached", "[motion][retime]") {
    seq::Actor original;
    original.id = "rook";
    original.keys = {key(0.0, {0, 0, 0}), key(10.0, {60, 0, 0})};
    original.clips.push_back(seq::ClipCue{.timeSeconds = 0.5, .clip = "Running", .speed = 1.2f});
    seq::Actor slowed = original;
    REQUIRE(seq::retimeActor(slowed, 2.0, 4.0, 0.5f).has_value());
    REQUIRE(slowed.clips.size() == 3);
    CHECK(slowed.clips[1].timeSeconds == Approx(2.0));
    CHECK(slowed.clips[1].speed == Approx(0.6f));
    CHECK(slowed.clips[2].timeSeconds == Approx(6.0));
    CHECK(slowed.clips[2].speed == Approx(1.2f));
    float worst = 0.0f;
    for (double T = 0.6; T <= 12.0; T += 0.01) {
        worst = std::max(worst, std::abs(clipTime(slowed, T) - clipTime(original, inverse(T, 2.0, 4.0, 0.5f))));
    }
    CHECK(worst < 1e-4f);
}

TEST_CASE("a one-shot's hand-back moves with the retime", "[motion][retime]") {
    scene::ClipSemantics jump;
    jump.clip = "Jumping";
    jump.length = 1.9f;
    const seq::ClipLookup lookup = [&](std::string_view c) { return c == "Jumping" ? &jump : nullptr; };
    seq::Actor actor;
    actor.id = "rook";
    actor.keys = {key(0.0, {0, 0, 0}), key(6.0, {20, 0, 0})};
    actor.clips.push_back(seq::ClipCue{.timeSeconds = 1.5, .clip = "Jumping", .playback = seq::ClipPlayback::Once,
                                       .then = "gait"});
    // Originally over at 3.4 s; with [2, 3] at half speed that second lands at 4.4 s.
    REQUIRE(seq::retimeActor(actor, 2.0, 3.0, 0.5f).has_value());
    const auto at = [&](double t) {
        const seq::ClipCue* c = actor.clipAt(t);
        REQUIRE(c != nullptr);
        return seq::resolveCue(seq::AnimationCue{.node = "rook", .clip = c->clip, .startSeconds = c->timeSeconds,
                                                 .speed = c->speed, .playback = c->playback, .then = c->then,
                                                 .offsetSeconds = c->offsetSeconds},
                               t, lookup);
    };
    CHECK_FALSE(at(4.39)->gait);
    CHECK(at(4.41)->gait);
}

TEST_CASE("a retime refuses what it cannot do exactly, and changes nothing", "[motion][retime]") {
    seq::Actor actor;
    actor.id = "rook";
    actor.keys = {key(0.0, {0, 0, 0}), key(6.0, {20, 0, 0})};
    const seq::Actor before = actor;
    CHECK_FALSE(seq::retimeActor(actor, 2.0, 3.0, 0.0f).has_value());
    CHECK_FALSE(seq::retimeActor(actor, 3.0, 3.0, 0.5f).has_value());
    REQUIRE(seq::retimeActor(actor, 2.0, 3.0, 0.5f).has_value());
    const seq::Actor once = actor;
    CHECK_FALSE(seq::retimeActor(actor, 3.5, 5.0, 0.5f).has_value()); // overlaps [2, 4]
    CHECK(actor.keys.size() == once.keys.size());
    seq::Actor pathed = before;
    pathed.path.active = true;
    pathed.path.startSeconds = 1.0;
    pathed.path.endSeconds = 5.0;
    CHECK_FALSE(seq::retimeActor(pathed, 2.0, 3.0, 0.5f).has_value());
}

TEST_CASE("a retimed actor survives the project file", "[motion][retime]") {
    seq::Actor actor;
    actor.id = "rook";
    actor.keys = {key(0.0, {0, 0, 0}), key(6.0, {20, 0, 0})};
    actor.clips.push_back(seq::ClipCue{.timeSeconds = 0.0, .clip = "Running"});
    REQUIRE(seq::retimeActor(actor, 2.0, 3.0, 0.5f).has_value());
    seq::Sequence piece;
    piece.actors.push_back(actor);
    auto again = seq::Sequence::fromJson(piece.toJson());
    REQUIRE(again.has_value());
    const seq::Actor& back = again->actors.front();
    REQUIRE(back.timeWarps.size() == 1);
    CHECK(back.timeWarps[0].rate == 0.5f);
    REQUIRE(back.clips.size() == 3);
    CHECK(back.clips[1].offsetSeconds == Approx(2.0f));
}

TEST_CASE("on the benchmark, a slowed run stays a run, slowed", "[motion][retime][handoff][benchmark]") {
    const auto run = [](bool keepWarp) {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadProject(std::filesystem::path(AVGEN_SOURCE_DIR) /
                                   "examples/world/glowmere-valley-2-multicam.json"));
        const entity::Entity* rook = engine.composition()->entityWorld().find("rook");
        REQUIRE(rook != nullptr);
        const glm::vec3 start = rook->state().position();
        seq::Sequence piece = engine.sequence();
        seq::Actor actor;
        actor.id = "rook";
        actor.keys = {key(1.0, start), key(5.0, start + glm::vec3(24.0f, 0.0f, 0.0f))}; // 6 m/s
        REQUIRE(seq::retimeActor(actor, 2.0, 3.0, 0.25f).has_value());                // 1.5 m/s on screen
        if (!keepWarp) {
            actor.timeWarps.clear();
        }
        piece.actors.push_back(actor);
        REQUIRE(engine.setSequence(piece).has_value());
        testsupport::stepFrames(engine, 60 * 4 + 1); // to 4.0 s: inside the stretched window [2, 6]
        return std::pair{rook->locomotion().activity, rook->locomotion().playbackRate};
    };
    const auto [activity, rate] = run(true);
    CHECK(activity == entity::Activity::Run);
    const auto [naiveActivity, naiveRate] = run(false);
    INFO("with the warp: rate " << rate << "; without: activity " << static_cast<int>(naiveActivity) << " rate " << naiveRate);
    // Control: the same stretched keys with no record of the warp read as the slower speed they are.
    CHECK(naiveActivity != entity::Activity::Run);
    CHECK(rate < 0.5f); // the run clip plays slowed
}
