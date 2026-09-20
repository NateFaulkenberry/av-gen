// The motion provider seam (ADR-541), its fallback chain, and the obligation the clip provider is
// held to.
//
// The tests that matter here are the ones about **memory and fallback**, because those are the
// properties the seam exists for and the ones a single-implementation seam otherwise never
// exercises. A test that only checked "the clip provider poses the skeleton" would pass on a seam
// that was pure decoration, which is the thing Phase A declined to build.

#include "entity/clip_motion_provider.hpp"
#include "entity/motion_chain.hpp"
#include "scene/animation.hpp"
#include "scene/skeleton.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace avgen;
using Catch::Approx;

namespace {

scene::Skeleton twoJoint() {
    scene::Skeleton sk;
    sk.name = "probe";
    sk.joints.push_back(scene::Joint{"root", -1, scene::Transform{}});
    sk.joints.push_back(scene::Joint{"tip", 0, scene::Transform{}});
    sk.palette = {0, 1};
    sk.inverseBind = {glm::mat4(1.0f), glm::mat4(1.0f)};
    return sk;
}

// A one-second loop that slides the tip from 0 to 1 and back, so a pose at a known time has a
// known, non-constant value -- which is what makes "did it advance" answerable.
scene::AnimationClip slider(std::string name) {
    scene::AnimationClip clip;
    clip.name = std::move(name);
    scene::AnimationChannel ch;
    ch.joint = 1;
    ch.path = scene::AnimationPath::Translation;
    ch.interpolation = scene::Interpolation::Linear;
    for (int i = 0; i <= 30; ++i) {
        const float t = static_cast<float>(i) / 30.0f;
        ch.times.push_back(t);
        ch.values.emplace_back(0.0f, std::sin(t * 3.14159265f), 0.0f, 0.0f);
    }
    clip.start = 0.0f;
    clip.duration = 1.0f;
    clip.channels.push_back(std::move(ch));
    return clip;
}

// A provider that always declines, with a reason. The half of a chain test that a chain of working
// providers cannot supply.
class Refuser final : public entity::IMotionProvider {
public:
    explicit Refuser(entity::MotionStatus why, std::string name)
        : why_(why), name_(std::move(name)) {}
    [[nodiscard]] std::string_view name() const override { return name_; }
    [[nodiscard]] entity::MotionResult evaluate(const entity::MotionRequest&,
                                                const entity::MotionMemory& in, double, float,
                                                const scene::Skeleton&, scene::Pose&,
                                                entity::MotionMemory& next) const override {
        // Deliberately scribbles on the memory before declining. A chain that handed `next`
        // straight to a failing provider would leave this behind, and the character's clip clock
        // would be replaced by a number from something that did not pose it.
        next = in;
        next.localTime = -999.0f;
        next.selection = 4242;
        entity::MotionResult r;
        r.status = why_;
        return r;
    }

private:
    entity::MotionStatus why_;
    std::string name_;
};

struct Fixture {
    scene::Skeleton skeleton = twoJoint();
    std::vector<scene::AnimationClip> clips;
    entity::ClipMotionProvider provider;

    Fixture() {
        clips.push_back(slider("Idle"));
        clips.push_back(slider("Walk"));
        clips.push_back(slider("Run"));
        provider.setClips(&clips);
        provider.addEntry({0, entity::MovementMode::Ground, "", 0.0f, true});
        provider.addEntry({1, entity::MovementMode::Ground, "", 1.6f, true});
        provider.addEntry({2, entity::MovementMode::Ground, "", 4.0f, true});
    }
};

entity::MotionRequest at(float speed) {
    entity::MotionRequest r;
    r.desiredVelocity = glm::vec3(0.0f, 0.0f, speed);
    return r;
}

} // namespace

TEST_CASE("the clip provider keeps nothing: the same memory gives the same pose", "[motion][provider]") {
    // **ADR-541's central claim, asserted rather than asserted-in-a-comment.** A provider is a pure
    // function of (request, memory, time, dt). If it kept anything, calling it twice with the same
    // memory would give two different answers, and a scrub would not reproduce a play.
    Fixture f;
    entity::MotionMemory memory;
    memory.localTime = 0.37f;
    memory.selection = 1;
    memory.generation = 5;

    scene::Pose a;
    scene::Pose b;
    entity::MotionMemory nextA;
    entity::MotionMemory nextB;
    const entity::MotionRequest req = at(1.6f);

    REQUIRE(f.provider.evaluate(req, memory, 10.0, 1.0f / 60.0f, f.skeleton, a, nextA).ok());
    // Call it again with the SAME input memory, out of order, after other work.
    scene::Pose junk;
    entity::MotionMemory junkNext;
    (void)f.provider.evaluate(at(4.0f), nextA, 99.0, 0.5f, f.skeleton, junk, junkNext);
    REQUIRE(f.provider.evaluate(req, memory, 10.0, 1.0f / 60.0f, f.skeleton, b, nextB).ok());

    REQUIRE(a.size() == b.size());
    for (std::size_t j = 0; j < a.size(); ++j) {
        INFO("joint " << j);
        CHECK(a.local[j].position.y == Approx(b.local[j].position.y).margin(1e-6));
    }
    CHECK(nextA.localTime == Approx(nextB.localTime).margin(1e-6));
    CHECK(nextA.selection == nextB.selection);
    CHECK(nextA.generation == nextB.generation);
    // And it really did move between the two calls it was given different memory for, so this is
    // not two identical answers from a provider that ignores its inputs (ADR-182).
    CHECK(junkNext.localTime != Approx(nextA.localTime).margin(1e-6));
}

TEST_CASE("the clock is the memory, so a replay at a fixed step reproduces a play",
          "[motion][provider][determinism]") {
    // The property `EntityWorld::seek` depends on. Sixty steps forward from zero must land where
    // sixty steps forward from zero landed, whatever else happened in between.
    Fixture f;
    const entity::MotionRequest req = at(1.6f);
    const auto run = [&](int steps) {
        entity::MotionMemory m;
        scene::Pose pose;
        for (int i = 0; i < steps; ++i) {
            entity::MotionMemory next;
            REQUIRE(f.provider.evaluate(req, m, static_cast<double>(i) / 60.0, 1.0f / 60.0f,
                                        f.skeleton, pose, next)
                        .ok());
            m = next;
        }
        return m;
    };
    const entity::MotionMemory a = run(45);
    const entity::MotionMemory b = run(45);
    CHECK(a.localTime == Approx(b.localTime).margin(1e-6));
    CHECK(a.phase == Approx(b.phase).margin(1e-6));
    // It advanced somewhere non-trivial rather than sitting at zero.
    CHECK(a.localTime > 0.1f);
    CHECK(a.hasPhase);
}

TEST_CASE("selection follows the body's speed, and a style that matches nothing declines",
          "[motion][provider]") {
    Fixture f;
    entity::MotionMemory m;
    scene::Pose pose;
    entity::MotionMemory next;

    const auto chosen = [&](float speed) {
        const entity::MotionResult r = f.provider.evaluate(at(speed), m, 0.0, 0.0f, f.skeleton, pose, next);
        REQUIRE(r.ok());
        return std::string(r.content);
    };
    CHECK(chosen(0.0f) == "Idle");
    CHECK(chosen(1.5f) == "Walk");
    CHECK(chosen(4.2f) == "Run");

    // A style nothing carries: the provider declines, and says why. The first version of the
    // provider treated an empty entry style as a wildcard, so this request resolved happily to the
    // default walk -- a style typo, or a pack that shipped without its styled clips, would have
    // been invisible forever. §64: do not hide failures.
    entity::MotionRequest styled = at(1.6f);
    styled.style = "limp";
    const entity::MotionResult r = f.provider.evaluate(styled, m, 0.0, 0.0f, f.skeleton, pose, next);
    CHECK_FALSE(r.ok());
    CHECK(r.status == entity::MotionStatus::NoContent);

    // And an airborne request against a ground-only pack declines too, rather than playing a walk
    // at somebody falling.
    entity::MotionRequest air = at(1.6f);
    air.mode = entity::MovementMode::Airborne;
    CHECK(f.provider.evaluate(air, m, 0.0, 0.0f, f.skeleton, pose, next).status ==
          entity::MotionStatus::NoContent);
}

TEST_CASE("the chain falls through to the provider that can answer", "[motion][chain]") {
    Fixture f;
    const Refuser neural(entity::MotionStatus::NotReady, "neural");
    const Refuser matcher(entity::MotionStatus::NoContent, "matcher");

    entity::MotionChain chain;
    REQUIRE(chain.add(&neural));
    REQUIRE(chain.add(&matcher));
    REQUIRE(chain.add(&f.provider));

    entity::MotionMemory m;
    m.localTime = 0.25f;
    m.selection = 1;
    m.generation = 3;
    scene::Pose pose;
    entity::MotionMemory next;
    const entity::MotionChainResult r =
        chain.resolve(at(1.6f), m, 1.0, 1.0f / 60.0f, f.skeleton, pose, next);

    CHECK(r.ok());
    CHECK(r.provider == 2);                 // the clip player answered
    CHECK(r.fellThrough == 2);              // and two declined first
    CHECK(r.firstDeclined == entity::MotionStatus::NotReady); // the interesting one, not the last

    // **The memory belongs to whoever posed the body.** Both refusers scribbled -999 and 4242 into
    // their copy; neither may survive. This is the assertion a chain of working providers cannot
    // make, and it is the reason `resolve` copies into scratch.
    INFO("localTime " << next.localTime << " selection " << next.selection);
    CHECK(next.localTime > 0.0f);
    CHECK(next.selection == 1);
    CHECK(next.localTime != Approx(-999.0f));
}

TEST_CASE("a chain whose every provider declines says so instead of posing a bind pose",
          "[motion][chain]") {
    // §64: do not hide failures, and degrade predictably. The body keeps the memory it had -- a
    // frozen character -- rather than being snapped to its bind pose because a model was loading.
    const Refuser a(entity::MotionStatus::NotReady, "neural");
    const Refuser b(entity::MotionStatus::Unsupported, "matcher");
    entity::MotionChain chain;
    REQUIRE(chain.add(&a));
    REQUIRE(chain.add(&b));

    entity::MotionMemory m;
    m.localTime = 0.6f;
    m.selection = 7;
    scene::Pose pose;
    entity::MotionMemory next;
    const scene::Skeleton sk = twoJoint();
    const entity::MotionChainResult r = chain.resolve({}, m, 0.0, 0.0f, sk, pose, next);

    CHECK_FALSE(r.ok());
    CHECK(r.provider == -1);
    CHECK(r.fellThrough == 2);
    CHECK(r.result.status == entity::MotionStatus::NotReady); // the first reason, which is the useful one
    // The memory is untouched, not zeroed and not scribbled on.
    CHECK(next.localTime == Approx(0.6f));
    CHECK(next.selection == 7);
}

TEST_CASE("an empty chain is a distinguishable state, not a crash", "[motion][chain]") {
    entity::MotionChain chain;
    entity::MotionMemory m;
    entity::MotionMemory next;
    scene::Pose pose;
    const scene::Skeleton sk = twoJoint();
    const entity::MotionChainResult r = chain.resolve({}, m, 0.0, 0.0f, sk, pose, next);
    CHECK_FALSE(r.ok());
    CHECK(r.provider == -1);
    CHECK(r.fellThrough == 0);
    CHECK(r.result.status == entity::MotionStatus::NoContent);
}

TEST_CASE("the clip provider agrees with AnimationPlayer joint by joint", "[motion][provider][parity]") {
    // **ADR-541 corollary 1's obligation, measured.** The provider is a second implementation of
    // clip playback, and a second implementation that quietly disagrees with the first is worse
    // than no second implementation at all. For the case they both cover -- one looping clip at a
    // steady rate -- they must give the same pose.
    Fixture f;
    scene::AnimationState state;
    state.name = "Walk";
    state.clip = 1; // the "Walk" clip, by index
    state.loop = true;
    state.speed = 1.0f;
    scene::AnimationPlayer player;
    player.addState(state);
    REQUIRE(player.play("Walk", 0.0));

    entity::MotionMemory m;
    scene::Pose fromProvider;
    scene::Pose fromPlayer;
    scene::Pose scratch;
    const entity::MotionRequest req = at(1.6f); // rate 1.0 exactly, so the two clocks agree

    // **Prime the provider at the instant the player was started**, which is the frame the first
    // draft of this test skipped -- and it was off by exactly one frame for all ninety comparisons
    // as a result. `play("Walk", 0.0)` starts the player's clock at 0; a provider first asked at
    // 1/60 selects its clip on that call and is therefore at local 0 while the player is at 1/60.
    // The engine calls every frame including the first, so priming is what actually happens; the
    // fixture was wrong, not the provider. (ADR-204's one-frame family, again.)
    entity::MotionMemory primed;
    REQUIRE(f.provider.evaluate(req, m, 0.0, 0.0f, f.skeleton, fromProvider, primed).ok());
    m = primed;

    for (int i = 1; i <= 90; ++i) {
        const double now = static_cast<double>(i) / 60.0;
        entity::MotionMemory next;
        REQUIRE(f.provider.evaluate(req, m, now, 1.0f / 60.0f, f.skeleton, fromProvider, next).ok());
        m = next;
        player.evaluate(f.clips, f.skeleton, now, fromPlayer, scratch);

        INFO("frame " << i << " local " << m.localTime);
        REQUIRE(fromProvider.size() == fromPlayer.size());
        for (std::size_t j = 0; j < fromProvider.size(); ++j) {
            // A frame of tolerance is not allowed here; these are the same clip sampled at times
            // that must be equal, not merely close.
            CHECK(fromProvider.local[j].position.y == Approx(fromPlayer.local[j].position.y).margin(1e-4));
        }
    }
    // The clip really did move over those 90 frames, so this is not ninety comparisons of a rest
    // pose against itself.
    CHECK(m.localTime > 0.0f);
    CHECK(m.generation == 1); // one selection, never re-selected
}
