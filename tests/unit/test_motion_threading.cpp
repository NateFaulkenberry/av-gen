// Phase C §74 (threading) and §75 (memory ownership): one immutable database and one provider
// serving many characters, possibly from several threads, with no allocation on the matching path.

#include "entity/match_motion_provider.hpp"
#include "scene/motion_database.hpp"

#include "support/motion_fixtures.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <new>
#include <thread>
#include <vector>

// ---- an allocation probe -------------------------------------------------------------------------
//
// The engine's own counters (core/alloc_counters.cpp) are compiled only with
// -DAVGEN_ALLOC_COUNTERS=ON. Without them this TU supplies the same interposition -- malloc and free,
// exactly as libc++'s own operator new does -- so the §75 test runs in the default build instead of
// only in a configuration nobody runs.
#ifdef AVGEN_ALLOC_COUNTERS
#include "core/phase_profiler.hpp"
namespace {
std::uint64_t threadAllocations() { return avgen::core::allocCounters().allocations; }
} // namespace
#else
namespace {
thread_local std::uint64_t tlsAllocations = 0;
std::uint64_t threadAllocations() { return tlsAllocations; }
} // namespace
void* operator new(std::size_t size) {
    ++tlsAllocations;
    if (void* p = std::malloc(size == 0 ? 1 : size)) {
        return p;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    ++tlsAllocations;
    return std::malloc(size == 0 ? 1 : size);
}
void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept {
    return ::operator new(size, tag);
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
#endif

using namespace avgen;

namespace {

struct Character {
    entity::MotionMemory memory;
    double time = 0.0;
    float heading = 0.0f;
};

// Characters asking for different things, so their searches land in different places and the
// shared provider is doing genuinely different work for each.
entity::MotionRequest requestFor(int character, int frame) {
    entity::MotionRequest r;
    const float phase = (static_cast<float>(frame) / 90.0f) + (0.37f * static_cast<float>(character));
    const float speed = 0.5f + (0.25f * static_cast<float>(character % 9));
    r.desiredVelocity = glm::vec3(std::sin(phase) * speed, 0.0f, std::cos(phase) * speed);
    r.desiredFacing = glm::normalize(r.desiredVelocity);
    return r;
}

void step(const entity::MatchMotionProvider& provider, Character& c, int index, int frame) {
    const float dt = 1.0f / 60.0f;
    entity::MotionMemory next;
    (void)provider.advance(requestFor(index, frame), c.memory, c.time, dt, next);
    c.memory = next;
    c.time += dt;
}

bool sameMemory(const entity::MotionMemory& a, const entity::MotionMemory& b) {
    if (a.selection != b.selection || a.generation != b.generation || a.localTime != b.localTime ||
        a.decisionTime != b.decisionTime || a.database != b.database) {
        return false;
    }
    for (std::size_t s = 0; s < entity::MotionMemory::kBlendSlots; ++s) {
        if (!(a.blends[s] == b.blends[s])) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("§74: characters matched in parallel against one provider equal a serial run",
          "[motionthreads][phaseC]") {
    auto glowmere = testsupport::glowmereMotion();
    scene::MotionPack pack = glowmere ? glowmere->pack : testsupport::probePack();
    scene::MotionDatabase db;
    if (glowmere) {
        db = glowmere->db;
    } else {
        auto built = scene::buildMotionDatabase(pack, testsupport::probeOptions());
        REQUIRE(built.has_value());
        db = std::move(*built);
    }
    REQUIRE(db.sampleCount() > 0);
    entity::MatchMotionProvider provider(&db, &pack.animation, "match");

    constexpr int kCharacters = 64;
    constexpr int kFrames = 240;
    constexpr int kThreads = 8;

    std::vector<Character> serial(kCharacters);
    for (int f = 0; f < kFrames; ++f) {
        for (int c = 0; c < kCharacters; ++c) {
            step(provider, serial[static_cast<std::size_t>(c)], c, f);
        }
    }
    const entity::MatchMotionProvider::Counters serialCounts = provider.counters();
    REQUIRE(serialCounts.searches > static_cast<std::uint64_t>(kCharacters));

    // The same work split across threads, each owning a disjoint set of characters and all sharing
    // the one provider and the one database -- §74's "per-character query state independent".
    provider.resetCounters();
    std::vector<Character> parallel(kCharacters);
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            for (int f = 0; f < kFrames; ++f) {
                for (int c = t; c < kCharacters; c += kThreads) {
                    step(provider, parallel[static_cast<std::size_t>(c)], c, f);
                }
            }
        });
    }
    for (std::thread& th : threads) {
        th.join();
    }

    int differing = 0;
    for (int c = 0; c < kCharacters; ++c) {
        if (!sameMemory(serial[static_cast<std::size_t>(c)].memory,
                        parallel[static_cast<std::size_t>(c)].memory)) {
            ++differing;
        }
    }
    CHECK(differing == 0);
    // The provider's shared counters are exact under contention: every search, from every thread,
    // counted once.
    const entity::MatchMotionProvider::Counters parallelCounts = provider.counters();
    CHECK(parallelCounts.searches == serialCounts.searches);
    CHECK(parallelCounts.continued == serialCounts.continued);
    CHECK(parallelCounts.scored == serialCounts.scored);
}

TEST_CASE("§75: the matching loop allocates nothing once warm", "[motionthreads][phaseC]") {
    const scene::MotionPack pack = testsupport::probePack();
    auto db = scene::buildMotionDatabase(pack, testsupport::probeOptions());
    REQUIRE(db.has_value());
    const entity::MatchMotionProvider provider(&*db, &pack.animation, "match");

    // Warm: the first searches on this thread size its scratch.
    Character c;
    for (int f = 0; f < 60; ++f) {
        step(provider, c, 3, f);
    }
    const std::uint64_t searchesBefore = provider.counters().searches;
    const std::uint64_t before = threadAllocations();
    for (int f = 60; f < 660; ++f) {
        step(provider, c, 3, f);
    }
    const std::uint64_t allocations = threadAllocations() - before;
    const std::uint64_t searches = provider.counters().searches - searchesBefore;
    // The subject exists: ten seconds of matching, with real searches in it, not only continuation.
    REQUIRE(searches > 20);
    // And the probe is live: an allocation on this thread is seen.
    const std::uint64_t probeBefore = threadAllocations();
    {
        // A direct call, not a new-expression: the compiler may elide the latter.
        void* p = ::operator new(16);
        ::operator delete(p);
    }
    REQUIRE(threadAllocations() == probeBefore + 1);

    INFO(allocations << " allocations over " << searches << " searches");
    CHECK(allocations == 0);
}

TEST_CASE("§75: per-character matching state stays a small plain value", "[motionthreads][phaseC]") {
    // §75: "per-character MotionMatchState should be compact". The state is `MotionMemory`; this is
    // the tripwire for somebody adding a container or a pose to it. 128 bytes is two cache lines --
    // today's value with its three blend slots is under it with room for a field or two.
    STATIC_CHECK(sizeof(entity::MotionMemory) <= 128);
    STATIC_CHECK(std::is_trivially_copyable_v<entity::MotionMemory>);
}
