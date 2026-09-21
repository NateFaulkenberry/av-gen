// Attention (Phase B §22).
//
// **Two numbers, not one.** Switch count alone is the wrong measure of a selector: one that
// switches rarely and holds each target for a fifth of a second reads worse than one that switches
// slightly more often and commits, because what a viewer notices is the snap away and back. So
// every stress case here reports **switches and dwell**, and the baseline arm is a selector with
// the hysteresis turned off so the numbers have something to be better than (ADR-182).

#include "entity/attention.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

constexpr double kDt = 1.0 / 60.0;

entity::AttentionCandidate candidate(std::uint64_t id, float salience, glm::vec3 at = glm::vec3(0.0f)) {
    entity::AttentionCandidate c;
    c.id = id;
    c.salience = salience;
    c.position = at;
    return c;
}

// Drive the selector and report what a viewer would be reading: how often it changed its mind, and
// how long it committed each time.
struct Watch {
    entity::AttentionState state;
    entity::AttentionResult result;
    double time = 0.0;
    int switches = 0;
    std::vector<float> dwells;   // completed holds, in seconds
    std::uint64_t lastTarget = 0;
    double lastChange = 0.0;

    void step(const entity::AttentionSettings& s, const std::vector<entity::AttentionCandidate>& c) {
        entity::AttentionState next;
        result = entity::chooseAttention(s, state, c, time, next);
        if (state.started && result.target != lastTarget) {
            ++switches;
            dwells.push_back(static_cast<float>(time - lastChange));
            lastChange = time;
        }
        lastTarget = result.target;
        state = next;
        time += kDt;
    }
    void run(const entity::AttentionSettings& s, const std::vector<entity::AttentionCandidate>& c,
             double seconds) {
        const int steps = static_cast<int>((seconds / kDt) + 0.5);
        for (int i = 0; i < steps; ++i) {
            step(s, c);
        }
    }
    [[nodiscard]] float shortestDwell() const {
        return dwells.empty() ? 0.0f : *std::min_element(dwells.begin(), dwells.end());
    }
};

} // namespace

TEST_CASE("attention acquires the most salient thing and holds it", "[attention]") {
    entity::AttentionSettings s;
    Watch w;
    w.run(s, {candidate(1, 0.3f), candidate(2, 0.8f)}, 0.5);
    CHECK(w.result.target == 2);
    CHECK(w.result.hasTarget);
    CHECK(w.result.dwell > 0.4f);
    // It eased in rather than snapping to full weight on the acquiring frame.
    CHECK(w.result.weight > 0.9f);
}

TEST_CASE("two near-equal candidates do not make a character twitch", "[attention][hysteresis]") {
    // **The stress case, measured in both dimensions.** Two things of almost identical salience,
    // with the lead swapping every frame -- 600 opportunities to change target across ten seconds.
    entity::AttentionSettings s;
    Watch w;
    w.state.started = false;
    for (int i = 0; i < 600; ++i) {
        const float a = 0.50f + ((i % 2 == 0) ? 0.01f : -0.01f);
        const float b = 0.50f + ((i % 2 == 0) ? -0.01f : 0.01f);
        w.step(s, {candidate(1, a), candidate(2, b)});
    }
    INFO("switches " << w.switches << ", shortest dwell " << w.shortestDwell() << " s");
    // The margin alone would settle this, but the assertion is on what a viewer reads: it changed
    // its mind a handful of times at most across ten seconds, and never held for an eyeblink.
    CHECK(w.switches <= 4);
    if (!w.dwells.empty()) {
        CHECK(w.shortestDwell() > 0.3f);
    }

    // **The baseline arm.** With the margin and the dwell removed, the same input thrashes -- so
    // the numbers above are a measurement of the hysteresis and not of the input being easy.
    entity::AttentionSettings none;
    none.switchMargin = 0.0f;
    none.minDwellSeconds = 0.0f;
    Watch bare;
    for (int i = 0; i < 600; ++i) {
        const float a = 0.50f + ((i % 2 == 0) ? 0.01f : -0.01f);
        const float b = 0.50f + ((i % 2 == 0) ? -0.01f : 0.01f);
        bare.step(none, {candidate(1, a), candidate(2, b)});
    }
    INFO("baseline switches " << bare.switches);
    CHECK(bare.switches > 100);
}

TEST_CASE("a brief spike does not steal attention", "[attention][hysteresis]") {
    // **The failure the dwell catches and the margin does not.** A rival that is *briefly* much
    // stronger would beat any margin; only a minimum commitment stops the character snapping to it
    // and back, which is precisely the motion a viewer reads as a malfunction.
    entity::AttentionSettings s;
    Watch w;
    w.run(s, {candidate(1, 0.6f), candidate(2, 0.1f)}, 0.3);   // settled on 1, inside the dwell
    REQUIRE(w.result.target == 1);

    // Two frames of a much louder rival.
    w.step(s, {candidate(1, 0.6f), candidate(2, 5.0f)});
    w.step(s, {candidate(1, 0.6f), candidate(2, 5.0f)});
    INFO("target during the spike: " << w.result.target);
    CHECK(w.result.target == 1);   // held through it

    // And once the dwell is satisfied, a sustained rival does win -- so the hold is a delay and
    // not a lock.
    w.run(s, {candidate(1, 0.6f), candidate(2, 5.0f)}, 0.6);
    CHECK(w.result.target == 2);
}

TEST_CASE("attention decays rather than staring forever", "[attention]") {
    // §22's `duration`. A character that stares at one thing forever is as wrong as one that
    // cannot choose.
    entity::AttentionSettings s;
    s.maxHoldSeconds = 1.0f;
    Watch w;
    w.run(s, {candidate(1, 0.9f)}, 0.5);
    REQUIRE(w.result.target == 1);
    CHECK(w.result.weight > 0.5f);

    // The weight eases out as the hold runs down rather than cutting.
    w.run(s, {candidate(1, 0.9f)}, 0.45);
    INFO("weight near the end of the hold: " << w.result.weight);
    CHECK(w.result.weight < 0.6f);

    // A per-candidate duration overrides the default, which is what §22 asks for.
    entity::AttentionCandidate brief = candidate(7, 0.9f);
    brief.duration = 0.25f;
    Watch b;
    b.run(s, {brief}, 0.5);
    CHECK(b.switches >= 1); // it let go and re-acquired
}

TEST_CASE("a target that vanishes is released rather than held", "[attention]") {
    entity::AttentionSettings s;
    Watch w;
    w.run(s, {candidate(1, 0.9f)}, 0.4);
    REQUIRE(w.result.target == 1);
    w.run(s, {}, 0.1);   // the world no longer offers it
    CHECK(w.result.target == 0);
    CHECK_FALSE(w.result.hasTarget);
    CHECK(w.result.weight == Approx(0.0f));
}

TEST_CASE("nothing worth looking at means looking at nothing", "[attention]") {
    // The acquire/release band, so a candidate hovering at the threshold does not blink in and out.
    entity::AttentionSettings s;
    Watch w;
    w.run(s, {candidate(1, 0.01f)}, 0.5);
    CHECK(w.result.target == 0);

    // It acquires above the threshold and is not dropped until below the lower one.
    w.run(s, {candidate(1, 0.2f)}, 0.2);
    CHECK(w.result.target == 1);
    w.run(s, {candidate(1, 0.03f)}, 0.2);   // between the two thresholds
    CHECK(w.result.target == 1);
}

TEST_CASE("ties break on identity, not on list order", "[attention][determinism]") {
    // The candidate list is rebuilt every frame by whoever perceives the world. Two equally
    // salient things would swap places whenever that rebuild reordered them -- a thrash with no
    // cause in the world at all, and one that would look like a perception bug.
    entity::AttentionSettings s;
    Watch w;
    // **Inside `maxHoldSeconds`, deliberately.** The first version ran for five seconds against a
    // four-second hold, so it caught a legitimate refractory release and reported it as a
    // reordering thrash -- the test measuring a different mechanism from the one it names. Three
    // seconds keeps the only variable the list order.
    for (int i = 0; i < 180; ++i) {
        std::vector<entity::AttentionCandidate> c = {candidate(11, 0.5f), candidate(22, 0.5f)};
        if (i % 2 == 0) {
            std::swap(c[0], c[1]);   // the perception layer reordered its output
        }
        w.step(s, c);
    }
    INFO("switches across 180 reorderings: " << w.switches);
    CHECK(w.switches == 0);
    CHECK(w.result.target == 11);   // the lower id, deterministically
}

TEST_CASE("the selector keeps nothing, so a replay reproduces a play", "[attention][determinism]") {
    entity::AttentionSettings s;
    const auto run = [&](int steps) {
        Watch w;
        for (int i = 0; i < steps; ++i) {
            const float a = i < 120 ? 0.7f : 0.2f;
            w.step(s, {candidate(1, a), candidate(2, 0.5f)});
        }
        return w;
    };
    const Watch a = run(300);
    const Watch b = run(300);
    CHECK(a.result.target == b.result.target);
    CHECK(a.switches == b.switches);
    CHECK(a.state.acquired == Approx(b.state.acquired));
    CHECK(a.switches > 0);   // it really did change its mind
}
