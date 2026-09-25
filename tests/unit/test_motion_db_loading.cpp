// The product loads a baked motion database instead of building one (ADR-825; Phase C §37, §39,
// §40, §76, §81).
//
// Before this, `Composition::matchAssetFor` extracted features synchronously when the composition
// was built -- runtime work, on the thread that draws -- and `MotionDatabaseSlot` (async load,
// validation, atomic swap, keep-the-old-one-on-failure) was called by nothing but its own tests. The
// arms here hold the product path to the properties the slot was built for, and hold the baked
// database to the one thing that makes it a drop-in: it poses the body exactly as the runtime build
// of the same pack does.
//
// The pack and database come from `tools/make_scout_motion_db.sh` (gitignored output); a checkout
// without them skips, and says how to make them.

#include "app/engine.hpp"
#include "core/time.hpp"
#include "entity/entity.hpp"
#include "scene/composition.hpp"
#include "signals/signal_bus.hpp"
#include "support/project_round_trip.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <thread>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

fs::path root() { return fs::path(AVGEN_SOURCE_DIR); }
fs::path lab() { return root() / "examples" / "labs" / "motionmatch" / "alien-match-lab.scene.json"; }
fs::path pack() { return root() / "assets" / "aliens" / "scout-pack"; }
fs::path database() { return pack() / "databases" / "scout.motiondb"; }
bool present() { return fs::exists(database()) && fs::exists(root() / "assets" / "aliens" / "alien-scout.glb"); }

enum class Source { Runtime, Baked, BakedMissing };

// The lab's matching scout, its database from `source`.
void load(app::Engine& engine, Source source) {
    REQUIRE(engine.loadComposition(lab()).has_value());
    std::vector<entity::EntityDesc> descs = engine.composition()->entities();
    for (entity::EntityDesc& d : descs) {
        if (d.name != "alien-match") {
            continue;
        }
        d.motionMatching.pack = "scout-pack";
        d.motionMatching.packResolved = pack().string();
        if (source != Source::Runtime) {
            d.motionMatching.database = "scout.motiondb";
            d.motionMatching.databaseResolved =
                (source == Source::Baked ? database() : pack() / "databases" / "nowhere.motiondb").string();
        }
    }
    REQUIRE(engine.composition()->setEntities(descs).has_value());
}

struct Run {
    app::Engine engine;
    signals::SignalBus bus;
    long long frame = 0;
    explicit Run(Source source, app::EngineMode mode = app::EngineMode::Offline) : engine(mode) { load(engine, source); }
    void tick() {
        FrameTime time;
        time.renderTime = static_cast<double>(frame) / 60.0;
        time.deltaTime = frame == 0 ? 0.0 : 1.0 / 60.0;
        time.frameIndex = static_cast<std::uint64_t>(frame);
        engine.params().resetFinals();
        engine.composition()->updateBehaviour(time, bus);
        engine.composition()->update(time);
        ++frame;
    }
    [[nodiscard]] scene::Composition::MotionDebug debug() const { return engine.composition()->motionDebug("alien-match"); }
};

} // namespace

TEST_CASE("a baked database is named beside its pack, and only with it", "[motion][matching][database]") {
    nlohmann::json j = {{"name", "scout"},
                        {"motionMatching", {{"joints", {"foot.l", "foot.r"}}, {"pack", "scout-pack"}, {"database", "scout-pack/databases/scout.motiondb"}}}};
    auto desc = entity::entityFromJson(j, "/scenes");
    REQUIRE(desc.has_value());
    CHECK(desc->motionMatching.databaseResolved == "/scenes/scout-pack/databases/scout.motiondb");
    CHECK(entity::entityToJson(*desc)["motionMatching"]["database"] == "scout-pack/databases/scout.motiondb");
    nlohmann::json alone = j;
    alone["motionMatching"].erase("pack");
    CHECK_FALSE(entity::entityFromJson(alone, "/scenes").has_value());
}

TEST_CASE("the product loads the baked database through the slot, and matches on it", "[motion][matching][database]") {
    if (!present()) {
        SKIP("run tools/make_scout_motion_db.sh to build the scout's pack and database");
    }
    Run baked(Source::Baked);
    baked.tick();
    const auto status = baked.engine.composition()->motionLoadStatus();
    REQUIRE(status.size() == 1);
    CHECK(status.front().second.state == scene::MotionLoadState::Ready);
    CHECK(status.front().second.publishes == 1);
    for (int i = 0; i < 120; ++i) {
        baked.tick();
    }
    const auto d = baked.debug();
    CHECK(d.posedByProvider);
    CHECK(d.matching);
    CHECK(d.databaseSamples == 1738);
    CHECK(d.provider == 0); // the matcher, in front of the clip provider

    SECTION("and it poses the body exactly as the runtime build of the same pack does") {
        Run runtime(Source::Runtime);
        for (int i = 0; i <= 120; ++i) {
            runtime.tick();
        }
        CHECK(runtime.debug().matching);
        CHECK(runtime.engine.composition()->motionLoadStatus().empty()); // no slot: built in memory
        const entity::Entity* a = baked.engine.composition()->entityWorld().find("alien-match");
        const entity::Entity* b = runtime.engine.composition()->entityWorld().find("alien-match");
        CHECK(a->motionMemory().generation == b->motionMemory().generation);
        CHECK(glm::length(a->visualPosition() - b->visualPosition()) == 0.0f);
        const auto rigOf = [](const Run& r) -> const scene::SkinnedRig& {
            const scene::CompositionNode* n = r.engine.composition()->findNode("alien-match");
            return r.engine.composition()->scene().rigs.at(n->rigs.front());
        };
        float worst = 0.0f;
        const auto& pa = rigOf(baked).palette;
        const auto& pb = rigOf(runtime).palette;
        REQUIRE(pa.size() == pb.size());
        for (std::size_t j = 0; j < pa.size(); ++j) {
            worst = std::max(worst, glm::length(glm::vec3(pa[j][3]) - glm::vec3(pb[j][3])));
        }
        CHECK(worst < 1e-5f);
    }
}

TEST_CASE("a live session plays the clips until the database arrives, then matches", "[motion][matching][database]") {
    if (!present()) {
        SKIP("run tools/make_scout_motion_db.sh to build the scout's pack and database");
    }
    Run live(Source::Baked);
    live.engine.composition()->setBlockingMotionLoads(false);
    // Rebuild the entities so the request is made without blocking.
    REQUIRE(live.engine.composition()->setEntities(live.engine.composition()->entities()).has_value());
    live.tick();
    // The request returned at once; whatever state the load is in, the body was not left unposed.
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (std::chrono::steady_clock::now() < until) {
        const auto s = live.engine.composition()->motionLoadStatus();
        if (!s.empty() && s.front().second.state == scene::MotionLoadState::Ready) {
            break;
        }
        live.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    for (int i = 0; i < 30; ++i) {
        live.tick();
    }
    CHECK(live.debug().matching);
}

TEST_CASE("a database that will not load leaves the body on its clips and says why", "[motion][matching][database]") {
    if (!present()) {
        SKIP("run tools/make_scout_motion_db.sh to build the scout's pack and database");
    }
    Run broken(Source::BakedMissing);
    for (int i = 0; i < 60; ++i) {
        broken.tick();
    }
    const auto status = broken.engine.composition()->motionLoadStatus();
    REQUIRE(status.size() == 1);
    CHECK(status.front().second.state == scene::MotionLoadState::Failed);
    CHECK_FALSE(status.front().second.error.empty());
    const auto d = broken.debug();
    CHECK(d.posedByProvider);
    CHECK_FALSE(d.matching); // the clip provider alone: the fallback, not an unposed body
}
