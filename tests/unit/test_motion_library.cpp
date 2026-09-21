// Phase C §39 (database loading), §40 (hot-swap safety), §41 (multi-character sharing), §76 (hot
// reload) and §81 (UI responsiveness): `MotionDatabaseSlot`.
//
// Each test pins one arrow of §76's diagram -- asynchronously, validated, atomic swap, migrate or
// fall back -- to an observation that would be different if the arrow were missing.

#include "entity/match_motion_provider.hpp"
#include "scene/motion_database_io.hpp"
#include "scene/motion_library.hpp"

#include "support/motion_fixtures.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <future>
#include <thread>

using namespace avgen;
namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

struct OnDisk {
    fs::path pack;
    fs::path narrow; // trajectory 0.2, 0.4
    fs::path wide;   // trajectory 0.2, 0.4, 0.6: a different database of the same pack
};

OnDisk writeProbe(const std::string& name) {
    const fs::path root = testsupport::processTempDir() / "motionlib" / name;
    std::error_code ec;
    fs::remove_all(root, ec);
    OnDisk out;
    out.pack = root / "probe.motionpack";
    const scene::MotionPack pack = testsupport::probePack();
    REQUIRE(scene::writeMotionPack(pack, out.pack).has_value());

    scene::MotionDatabaseOptions narrow = testsupport::probeOptions();
    auto a = scene::buildMotionDatabase(pack, narrow);
    REQUIRE(a.has_value());
    out.narrow = scene::motionDatabasePath(out.pack, "narrow");
    REQUIRE(scene::writeMotionDatabase(*a, out.narrow).has_value());

    scene::MotionDatabaseOptions wide = narrow;
    wide.config.trajectoryTimes = {0.2f, 0.4f, 0.6f};
    auto b = scene::buildMotionDatabase(pack, wide);
    REQUIRE(b.has_value());
    REQUIRE(b->identity != a->identity);
    out.wide = scene::motionDatabasePath(out.pack, "wide");
    REQUIRE(scene::writeMotionDatabase(*b, out.wide).has_value());
    return out;
}

} // namespace

TEST_CASE("§39/§81: a load never runs on the caller's thread, and nothing half-built is visible",
          "[motionlib][phaseC]") {
    const OnDisk disk = writeProbe("async");
    scene::MotionDatabaseSlot slot;
    REQUIRE(slot.current() == nullptr);

    const std::thread::id caller = std::this_thread::get_id();
    std::atomic<bool> ranOnCaller{false};
    std::atomic<bool> hookRan{false};
    std::promise<void> release;
    std::shared_future<void> released = release.get_future().share();
    slot.beforePublish = [&] {
        hookRan = true;
        if (std::this_thread::get_id() == caller) {
            ranOnCaller = true; // a synchronous load; do not wait, or the test would deadlock
            return;
        }
        released.wait();
    };

    slot.requestLoad(disk.pack, disk.narrow);
    // The request returned. If the load had been synchronous the hook would already have run here,
    // on this thread.
    CHECK_FALSE(ranOnCaller.load());
    // Validated and about to publish, and still not visible: until the swap, readers see nothing.
    for (int i = 0; i < 400 && !hookRan; ++i) {
        std::this_thread::sleep_for(5ms);
    }
    REQUIRE(hookRan.load());
    CHECK(slot.current() == nullptr);
    CHECK(slot.status().state == scene::MotionLoadState::Loading);

    release.set_value();
    REQUIRE(slot.waitIdle(5s));
    const auto asset = slot.current();
    REQUIRE(asset != nullptr);
    CHECK(asset->db.sampleCount() == 62);
    CHECK(slot.status().state == scene::MotionLoadState::Ready);
    CHECK(slot.status().publishes == 1);
}

TEST_CASE("§76: a failed load leaves the previous database published", "[motionlib][phaseC]") {
    const OnDisk disk = writeProbe("fallback");
    scene::MotionDatabaseSlot slot;
    slot.requestLoad(disk.pack, disk.narrow);
    REQUIRE(slot.waitIdle(5s));
    const auto before = slot.current();
    REQUIRE(before != nullptr);

    slot.requestLoad(disk.pack, disk.pack / "databases" / "missing.motiondb");
    REQUIRE(slot.waitIdle(5s));
    CHECK(slot.status().state == scene::MotionLoadState::Failed);
    CHECK_FALSE(slot.status().error.empty());
    CHECK(slot.current() == before);
    CHECK(slot.status().publishes == 1);
}

TEST_CASE("§40: a database built for another pack is refused at publish", "[motionlib][phaseC]") {
    const OnDisk disk = writeProbe("mismatch");
    // A pack whose content differs by one key, beside the database built from the original.
    scene::MotionPack changed = testsupport::probePack();
    changed.animation[0].channels[1].values[2].z += 0.05f;
    auto db = scene::readMotionDatabase(disk.narrow);
    REQUIRE(db.has_value());
    auto asset = std::make_shared<scene::MotionAsset>();
    asset->pack = changed;
    asset->db = *db;
    scene::MotionDatabaseSlot slot;
    const auto published = slot.publish(asset);
    REQUIRE_FALSE(published.has_value());
    CHECK(published.error().message.find("rebuild") != std::string::npos);
    CHECK(slot.current() == nullptr);
}

TEST_CASE("§76: the last request wins, even when an earlier one finishes later",
          "[motionlib][phaseC]") {
    const OnDisk disk = writeProbe("lastwins");
    scene::MotionDatabaseSlot slot;
    std::promise<void> release;
    std::shared_future<void> released = release.get_future().share();
    std::atomic<int> calls{0};
    slot.beforePublish = [&] {
        if (calls.fetch_add(1) == 0) {
            released.wait(); // hold the first load at the brink of publishing
        }
    };
    slot.requestLoad(disk.pack, disk.narrow);
    for (int i = 0; i < 400 && calls.load() == 0; ++i) {
        std::this_thread::sleep_for(5ms);
    }
    REQUIRE(calls.load() == 1);
    slot.requestLoad(disk.pack, disk.wide); // issued while the first is still in flight
    release.set_value();
    REQUIRE(slot.waitIdle(5s));
    const auto asset = slot.current();
    REQUIRE(asset != nullptr);
    CHECK(asset->db.config.trajectoryTimes.size() == 3);
    CHECK(slot.status().publishes == 1); // the superseded one was dropped, not published first
}

TEST_CASE("§40/§76: characters mid-motion migrate across a swap instead of posing a stale index",
          "[motionlib][phaseC]") {
    const OnDisk disk = writeProbe("migrate");
    scene::MotionDatabaseSlot slot;
    slot.requestLoad(disk.pack, disk.narrow);
    REQUIRE(slot.waitIdle(5s));
    auto held = slot.current();
    REQUIRE(held != nullptr);

    entity::MatchMotionProvider provider(&held->db, &held->pack.animation, "match");
    entity::MotionRequest request;
    request.desiredVelocity = glm::vec3(0.0f, 0.0f, 1.2f);
    entity::MotionMemory memory;
    double time = 0.0;
    const float dt = 1.0f / 60.0f;
    for (int f = 0; f < 90; ++f) {
        entity::MotionMemory next;
        REQUIRE(provider.advance(request, memory, time, dt, next).ok());
        memory = next;
        time += dt;
    }
    REQUIRE(memory.generation > 0);
    CHECK(memory.database == held->db.identity);

    // The swap: a different database of the same pack, published while the character is mid-clip.
    slot.requestLoad(disk.pack, disk.wide);
    REQUIRE(slot.waitIdle(5s));
    auto swapped = slot.current();
    REQUIRE(swapped != nullptr);
    REQUIRE(swapped != held);
    provider.setDatabase(&swapped->db);
    provider.setClips(&swapped->pack.animation);

    // The old index is still in range in the new database -- which is exactly the danger.
    REQUIRE(memory.selection < swapped->db.sampleCount());
    scene::Pose pose;
    const entity::MotionResult stale = provider.pose(memory, swapped->pack.skeleton, pose);
    CHECK(stale.status == entity::MotionStatus::NoContent);

    // One advance migrates it: a fresh selection in the new database, with nothing to blend from.
    entity::MotionMemory next;
    REQUIRE(provider.advance(request, memory, time, dt, next).ok());
    CHECK(next.database == swapped->db.identity);
    CHECK_FALSE(next.blending());
    CHECK(provider.pose(next, swapped->pack.skeleton, pose).ok());

    // The old asset is kept alive by the holder and released with it.
    std::weak_ptr<const scene::MotionAsset> old = held;
    held.reset();
    CHECK(old.expired());
}

TEST_CASE("§41: a hundred characters share one published database", "[motionlib][phaseC]") {
    const OnDisk disk = writeProbe("share");
    scene::MotionDatabaseSlot slot;
    slot.requestLoad(disk.pack, disk.narrow);
    REQUIRE(slot.waitIdle(5s));
    const auto asset = slot.current();
    REQUIRE(asset != nullptr);
    REQUIRE(asset->db.sampleCount() > 0);

    std::vector<entity::MatchMotionProvider> providers;
    providers.reserve(100);
    for (int i = 0; i < 100; ++i) {
        providers.emplace_back(&asset->db, &asset->pack.animation, "match");
    }
    for (const entity::MatchMotionProvider& p : providers) {
        REQUIRE(p.database() != nullptr);
        CHECK(p.database()->features.data() == asset->db.features.data());
    }
    // One asset: the slot's reference and this one, however many providers point into it.
    CHECK(asset.use_count() == 2);
}
