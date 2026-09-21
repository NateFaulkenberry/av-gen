// Phase-aware transitions and inertialization (ADR-547).
//
// The two defects this unit exists for, both of them in one line of `AnimationPlayer::play`:
//
//   `current_.start = now;`
//
// It starts the incoming clip at its own frame zero. A walk at 73% of its cycle cross-fading into a
// run therefore lands on the run's frame 0, and whether the feet agree is luck. And the transition
// itself is a cross-fade, which evaluates BOTH clips for its whole duration and whose cost is
// therefore highest exactly when the character is busiest.
//
// **The standing rule (Phase A brief §2).** Both features are no-ops when off, so a test that only
// checks "the pose is reasonable" passes on an implementation that does nothing. Every arm here is
// paired: phase matching is measured against the same transition with it OFF and must differ in a
// known direction, and inertialization is measured against the cross-fade it replaces.
//
// The arms:
//
//   off           with matchPhase off, the incoming clip starts at frame zero, as it always did
//   on            with it on, the incoming clip starts at the phase the outgoing clip was at
//   unanalysed    a clip with no phase track falls back to frame zero rather than to garbage
//   same-state    asking for the state already playing changes nothing, phase matching or not
//   continuity    an inertialized transition starts exactly on the outgoing pose -- no step
//   decay         and reaches the incoming pose, monotonically, within a few half-lives
//   one-clip      inertialization evaluates the incoming clip only; the outgoing state's clip can
//                 be garbage after the transition instant and the result is unaffected
//   determinism   both are pure functions of (states, entry times, now)

#include "scene/animation.hpp"
#include "scene/motion_analysis.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace avgen::scene;
using Catch::Approx;

namespace {

Skeleton oneJointRig() {
    Skeleton sk;
    sk.name = "probe";
    sk.joints.push_back(Joint{"root", -1, Transform{}});
    sk.palette = {0};
    sk.inverseBind = {glm::mat4(1.0f)};
    return sk;
}

// A clip whose root translates along X as a ramp from `from` to `to` over `seconds`. Linear, so
// the value at any instant is known in closed form and the arms can assert against arithmetic
// rather than against the implementation.
AnimationClip rampClip(std::string name, float from, float to, float seconds) {
    AnimationClip clip;
    clip.name = std::move(name);
    AnimationChannel channel;
    channel.joint = 0;
    channel.path = AnimationPath::Translation;
    channel.interpolation = Interpolation::Linear;
    channel.times = {0.0f, seconds};
    channel.values = {glm::vec4(from, 0.0f, 0.0f, 0.0f), glm::vec4(to, 0.0f, 0.0f, 0.0f)};
    clip.start = 0.0f;
    clip.duration = seconds;
    clip.channels.push_back(std::move(channel));
    return clip;
}

// A phase track that runs 0 -> 1 linearly over `seconds`, which is what a one-cycle loop gets.
PhaseTrack linearPhase(float seconds, float rate = 30.0f) {
    PhaseTrack track;
    track.sampleRate = rate;
    track.cyclic = true;
    track.cycleSeconds = seconds;
    const auto samples = static_cast<std::size_t>(seconds * rate) + 1;
    for (std::size_t i = 0; i < samples; ++i) {
        track.phase.push_back(std::min(0.999999f, static_cast<float>(i) / static_cast<float>(samples - 1)));
    }
    return track;
}

// A clip that holds `value` until `hold` seconds and then dives to `then` by `seconds`. Used to
// poison an outgoing clip AFTER the transition instant while leaving it untouched before it.
AnimationClip divingClip(std::string name, float value, float hold, float then, float seconds) {
    AnimationClip clip;
    clip.name = std::move(name);
    AnimationChannel channel;
    channel.joint = 0;
    channel.path = AnimationPath::Translation;
    channel.interpolation = Interpolation::Linear;
    channel.times = {0.0f, hold, seconds};
    channel.values = {glm::vec4(value, 0.0f, 0.0f, 0.0f), glm::vec4(value, 0.0f, 0.0f, 0.0f),
                      glm::vec4(then, 0.0f, 0.0f, 0.0f)};
    clip.start = 0.0f;
    clip.duration = seconds;
    clip.channels.push_back(std::move(channel));
    return clip;
}

float rootX(const Pose& pose) { return pose.local.front().position.x; }

} // namespace

TEST_CASE("a transition starts the incoming clip where phase matching says", "[animation][phase]") {
    const Skeleton sk = oneJointRig();
    // Two one-second clips. `walk` ramps 0 -> 10, `run` ramps 100 -> 110, so the value read off the
    // incoming clip says exactly which instant of it is playing: 100 is frame zero, 105 is halfway.
    const std::vector<AnimationClip> clips{rampClip("walk", 0.0f, 10.0f, 1.0f),
                                           rampClip("run", 100.0f, 110.0f, 1.0f)};
    const std::vector<PhaseTrack> phases{linearPhase(1.0f), linearPhase(1.0f)};

    const auto build = [&](bool match) {
        AnimationPlayer player;
        AnimationState walk;
        walk.name = "walk";
        walk.clip = 0;
        player.addState(walk);
        AnimationState run;
        run.name = "run";
        run.clip = 1;
        run.matchPhase = match;
        player.addState(run);
        return player;
    };

    Pose pose;
    Pose scratch;
    const AnimationPlayer::PhaseMatch match{&clips, &phases};

    SECTION("with matching OFF the incoming clip starts at frame zero, as it always did") {
        AnimationPlayer player = build(false);
        player.play("walk", 0.0);
        // Enter `run` at t = 0.6, with a snap so the sampled pose is purely the incoming clip.
        player.play("run", 0.6, 0.0f, match);
        player.evaluate(clips, sk, 0.6, pose, scratch);
        CHECK(rootX(pose) == Approx(100.0f).margin(0.05)); // run's frame zero
    }

    SECTION("with matching ON it starts at the outgoing clip's phase") {
        AnimationPlayer player = build(true);
        player.play("walk", 0.0);
        player.play("run", 0.6, 0.0f, match);
        player.evaluate(clips, sk, 0.6, pose, scratch);
        // The walk was 0.6 of the way through its cycle, so the run must enter 0.6 of the way
        // through its own: 100 + 0.6 * 10 = 106. This is the assertion the old behaviour fails by
        // the full six units.
        CHECK(rootX(pose) == Approx(106.0f).margin(0.2));
    }

    SECTION("the two differ, which is the whole point") {
        AnimationPlayer off = build(false);
        AnimationPlayer on = build(true);
        off.play("walk", 0.0);
        on.play("walk", 0.0);
        off.play("run", 0.6, 0.0f, match);
        on.play("run", 0.6, 0.0f, match);
        Pose a;
        Pose b;
        off.evaluate(clips, sk, 0.6, a, scratch);
        on.evaluate(clips, sk, 0.6, b, scratch);
        CHECK(std::fabs(rootX(a) - rootX(b)) > 5.0f);
    }

    SECTION("an unanalysed clip falls back to frame zero rather than to garbage") {
        AnimationPlayer player = build(true);
        const std::vector<PhaseTrack> empty{PhaseTrack{}, PhaseTrack{}};
        const AnimationPlayer::PhaseMatch none{&clips, &empty};
        player.play("walk", 0.0);
        player.play("run", 0.6, 0.0f, none);
        player.evaluate(clips, sk, 0.6, pose, scratch);
        CHECK(rootX(pose) == Approx(100.0f).margin(0.05));
    }

    SECTION("asking for the state already playing still changes nothing") {
        AnimationPlayer player = build(true);
        player.play("run", 0.0, 0.0f, match);
        const double startBefore = player.currentStart();
        player.play("run", 0.5, 0.0f, match);
        // A phase-matched re-entry into the same state would rebase the clock and restart the clip,
        // which is precisely the behaviour `play` has always refused so a behaviour can call it
        // every frame.
        CHECK(player.currentStart() == startBefore);
    }
}

TEST_CASE("an inertialized transition is continuous and decays to the incoming clip",
          "[animation][inertialization]") {
    const Skeleton sk = oneJointRig();
    // A step of 100 units between the two clips: the discontinuity is large and known.
    const std::vector<AnimationClip> clips{rampClip("a", 0.0f, 0.0f, 2.0f),
                                           rampClip("b", 100.0f, 100.0f, 2.0f)};
    AnimationPlayer player;
    AnimationState a;
    a.name = "a";
    a.clip = 0;
    player.addState(a);
    AnimationState b;
    b.name = "b";
    b.clip = 1;
    player.addState(b);
    player.inertializeHalflife = 0.1f;

    Pose pose;
    Pose scratch;
    player.play("a", 0.0);
    player.evaluate(clips, sk, 0.4, pose, scratch);
    REQUIRE(rootX(pose) == Approx(0.0f).margin(1e-3));

    player.play("b", 0.5, 0.5f);

    SECTION("it starts exactly on the outgoing pose, so there is no step") {
        player.evaluate(clips, sk, 0.5, pose, scratch);
        // At the transition instant the offset is at full strength, so the result is the pose the
        // character was already in. A cross-fade gets this right too; an inertializer with the
        // wrong decay shape does not, and it is the single most visible artifact.
        CHECK(rootX(pose) == Approx(0.0f).margin(0.01));
    }

    SECTION("it decays monotonically to the incoming clip") {
        float previous = -1.0f;
        for (double t = 0.5; t <= 1.2; t += 0.02) {
            player.evaluate(clips, sk, t, pose, scratch);
            const float x = rootX(pose);
            INFO(t);
            CHECK(x >= previous - 1e-3f); // never goes backwards
            CHECK(x >= -0.5f);            // and never overshoots below the start...
            CHECK(x <= 100.5f);           // ...or above the target
            previous = x;
        }
        // Several half-lives later it is the incoming clip and nothing else.
        player.evaluate(clips, sk, 1.2, pose, scratch);
        CHECK(rootX(pose) == Approx(100.0f).margin(0.5));
    }

    SECTION("it differs from the cross-fade it replaces") {
        AnimationPlayer fade = player;
        fade.inertializeHalflife = 0.0f;
        Pose inert;
        Pose crossed;
        player.evaluate(clips, sk, 0.6, inert, scratch);
        fade.evaluate(clips, sk, 0.6, crossed, scratch);
        // Both are somewhere between 0 and 100 at this instant, and they are NOT the same number --
        // an inertializer that silently fell through to the cross-fade would pass every other arm
        // in this section.
        CHECK(std::fabs(rootX(inert) - rootX(crossed)) > 1.0f);
    }

    SECTION("it evaluates the incoming clip only") {
        // The outgoing clip is replaced with one whose values after the transition instant are
        // wildly different. A cross-fade reads them and changes; an inertializer captured its
        // offset at the transition and must not.
        std::vector<AnimationClip> poisoned = clips;
        // Identical to `a` up to and including the transition instant at 0.5, then it dives. A
        // cross-fade reads the dive and changes; an inertializer captured its offset at 0.5 and
        // must not.
        poisoned[0] = divingClip("a", 0.0f, 0.5f, -9999.0f, 2.0f);
        Pose clean;
        Pose dirty;
        player.evaluate(clips, sk, 0.7, clean, scratch);
        player.evaluate(poisoned, sk, 0.7, dirty, scratch);
        // `a` still reads 0 at the transition instant in both, because the ramp starts at 0 -- so
        // the captured offset is identical and the two results must agree.
        CHECK(rootX(clean) == Approx(rootX(dirty)).margin(0.01));
    }
}

TEST_CASE("both transition features are pure functions of when a state was entered",
          "[animation][inertialization][phase]") {
    // ADR-086's contract, which neither feature may break: the pose is a function of (states,
    // entry times, now). Evaluating the same instant twice, and evaluating out of order, must give
    // the same answer -- which is what makes a scrub reproduce.
    const Skeleton sk = oneJointRig();
    const std::vector<AnimationClip> clips{rampClip("a", 0.0f, 10.0f, 1.0f),
                                           rampClip("b", 50.0f, 60.0f, 1.0f)};
    const std::vector<PhaseTrack> phases{linearPhase(1.0f), linearPhase(1.0f)};
    AnimationPlayer player;
    AnimationState a;
    a.name = "a";
    a.clip = 0;
    player.addState(a);
    AnimationState b;
    b.name = "b";
    b.clip = 1;
    b.matchPhase = true;
    player.addState(b);
    player.inertializeHalflife = 0.15f;
    player.play("a", 0.0);
    player.play("b", 0.37, 0.4f, AnimationPlayer::PhaseMatch{&clips, &phases});

    Pose first;
    Pose second;
    Pose scratch;
    // Forwards...
    for (double t = 0.37; t <= 0.8; t += 0.05) {
        player.evaluate(clips, sk, t, first, scratch);
    }
    player.evaluate(clips, sk, 0.55, first, scratch);
    // ...and straight to the same instant from nowhere.
    player.evaluate(clips, sk, 0.55, second, scratch);
    CHECK(rootX(first) == rootX(second));
    // And it is not trivially zero: the transition really was in flight at that instant.
    CHECK(rootX(first) > 0.0f);
    CHECK(rootX(first) < 60.0f);
}
