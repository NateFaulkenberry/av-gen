// Phase C §26/§73: a matcher carrying on plays its motion at the authored speed, whatever the
// frame rate.
//
// The continuation used to follow `sampleNext` once per `advance`, whatever `dt` was. The database
// is sampled at 30 Hz. The entity steps at 60 Hz, and seeking replays at 1/60, so every
// continuation played at twice its authored speed, and at a different speed at a different frame
// rate. That is a determinism failure (ADR-086, §73) as well as a wrong-looking walk. Found while
// reading `avgen-motion explain`: the scout advanced 11 samples between two searches 0.2 s apart,
// where 6 was right.

#include "entity/match_motion_provider.hpp"
#include "scene/motion_database.hpp"
#include "support/motion_fixtures.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <cmath>
#include <vector>

using namespace avgen;

namespace {

struct Session {
    entity::MotionMemory memory;
    scene::Pose pose;
};

// Ask for a steady walk for `seconds` at `hz`, never searching after the first step, and return
// where the matcher ended up.
Session hold(const entity::MatchMotionProvider& provider, const scene::Skeleton& skeleton, float hz,
             float seconds) {
    entity::MotionRequest request;
    request.desiredVelocity = glm::vec3(0.0f, 0.0f, 1.2f);
    Session s;
    const float dt = 1.0f / hz;
    const auto steps = static_cast<int>(std::lround(seconds * hz));
    for (int i = 0; i <= steps; ++i) {
        entity::MotionMemory next;
        const entity::MotionResult r =
            provider.advance(request, s.memory, static_cast<double>(i) / static_cast<double>(hz), dt, next);
        REQUIRE(r.ok());
        s.memory = next;
    }
    REQUIRE(provider.pose(s.memory, skeleton, s.pose).ok());
    return s;
}

} // namespace

TEST_CASE("carrying on advances the clip by the time that passed", "[matching][continuation][phaseC]") {
    const scene::MotionPack pack = testsupport::probePack();
    auto db = scene::buildMotionDatabase(pack, testsupport::probeOptions());
    REQUIRE(db.has_value());
    entity::MatchMotionProvider provider(&*db, &pack.animation, "match");
    entity::MatchSettings settings;
    // Search once and then only carry on, so everything measured below is the continuation.
    settings.searchInterval = 100.0f;
    settings.minimumContinuation = 100.0f;
    provider.setSettings(settings);

    entity::MotionRequest request;
    request.desiredVelocity = glm::vec3(0.0f, 0.0f, 1.2f);
    entity::MotionMemory memory;
    entity::MotionMemory next;
    REQUIRE(provider.advance(request, memory, 0.0, 1.0f / 60.0f, next).ok());
    memory = next;
    const float start = memory.localTime;
    const std::uint32_t clip = db->sampleClip[memory.selection];
    const float period = pack.animation[clip].length();
    REQUIRE(period > 0.0f);

    // Half a second at 60 Hz.
    for (int i = 1; i <= 30; ++i) {
        REQUIRE(provider.advance(request, memory, static_cast<double>(i) / 60.0, 1.0f / 60.0f, next).ok());
        memory = next;
    }
    // The subject exists: it carried on in the same clip rather than searching.
    REQUIRE(db->sampleClip[memory.selection] == clip);
    REQUIRE(provider.counters().searches == 1u);

    const float expected = std::fmod((start - pack.animation[clip].start) + 0.5f, period) +
                           pack.animation[clip].start;
    WARN(fmt::format("from {:.4f} s, 0.5 s at 60 Hz reached clip time {:.4f} s; the authored speed "
                     "reaches {:.4f} s",
                     start, memory.localTime, expected));
    CHECK(std::abs(memory.localTime - expected) < 1e-3f);
}

TEST_CASE("the same second at 30, 60 and 120 Hz ends on the same pose", "[matching][continuation][determinism][phaseC]") {
    const scene::MotionPack pack = testsupport::probePack();
    auto db = scene::buildMotionDatabase(pack, testsupport::probeOptions());
    REQUIRE(db.has_value());
    entity::MatchMotionProvider provider(&*db, &pack.animation, "match");
    entity::MatchSettings settings;
    settings.searchInterval = 100.0f;
    settings.minimumContinuation = 100.0f;
    provider.setSettings(settings);

    const Session a = hold(provider, pack.skeleton, 30.0f, 1.3f);
    const Session b = hold(provider, pack.skeleton, 60.0f, 1.3f);
    const Session c = hold(provider, pack.skeleton, 120.0f, 1.3f);
    WARN(fmt::format("clip time after 1.3 s: {:.4f} at 30 Hz, {:.4f} at 60 Hz, {:.4f} at 120 Hz",
                     a.memory.localTime, b.memory.localTime, c.memory.localTime));
    CHECK(std::abs(a.memory.localTime - b.memory.localTime) < 1e-3f);
    CHECK(std::abs(a.memory.localTime - c.memory.localTime) < 1e-3f);
    float worst = 0.0f;
    for (std::size_t j = 0; j < a.pose.local.size(); ++j) {
        worst = std::max(worst, glm::length(a.pose.local[j].position - b.pose.local[j].position));
        worst = std::max(worst, glm::length(a.pose.local[j].position - c.pose.local[j].position));
    }
    CHECK(worst < 1e-3f);
}
