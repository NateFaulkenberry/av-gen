// Phase C §37 (offline/runtime separation), §38 (MotionPack extension) and §82 (offline build
// cache): the motion database as a stored artefact.
//
// The claim these defend is narrow and checkable: **what the runtime loads is bit-for-bit what the
// offline builder produced**, and anything else -- a stale pack, a corrupt file, a changed schema,
// a different rig -- is refused with a message rather than searched.

#include "scene/motion_database_io.hpp"
#include "scene/motion_library.hpp"

#include "support/motion_fixtures.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <fstream>
#include <limits>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path dir = testsupport::processTempDir() / "motiondb" / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

scene::MotionDatabase buildProbe(const scene::MotionPack& pack) {
    auto db = scene::buildMotionDatabase(pack, testsupport::probeOptions());
    REQUIRE(db.has_value());
    return std::move(*db);
}

// Queries drawn from the database and perturbed, each with a current sample, so continuity and
// transition costs take part and not only the feature distance.
std::vector<scene::MotionQuery> probeQueries(const scene::MotionDatabase& db) {
    std::vector<scene::MotionQuery> out;
    for (std::uint32_t s = 0; s < db.sampleCount(); s += 7) {
        scene::MotionQuery q;
        q.features.assign(db.featuresFor(s), db.featuresFor(s) + db.dimension);
        for (std::size_t d = 0; d < q.features.size(); ++d) {
            q.features[d] += 0.05f * static_cast<float>((d * 7 + s) % 5) - 0.1f;
        }
        q.current = (s + 3) % db.sampleCount();
        out.push_back(std::move(q));
    }
    return out;
}

void corruptByte(const fs::path& file, std::streamoff fromEnd) {
    std::fstream f(file, std::ios::binary | std::ios::in | std::ios::out);
    REQUIRE(f);
    f.seekg(-fromEnd, std::ios::end);
    char c = 0;
    f.read(&c, 1);
    f.seekp(-fromEnd, std::ios::end);
    c = static_cast<char>(c ^ 0x5A);
    f.write(&c, 1);
}

} // namespace

TEST_CASE("§38: a stored database loads bit-identical to the one that was built",
          "[motiondb][io][phaseC]") {
    const scene::MotionPack pack = testsupport::probePack();
    const scene::MotionDatabase built = buildProbe(pack);
    // The subject exists: a database with samples, a stamped identity and a build record.
    REQUIRE(built.sampleCount() == 62);
    REQUIRE(built.identity != 0);
    REQUIRE_FALSE(built.build.buildKey.empty());
    REQUIRE_FALSE(built.build.sourcePackDigest.empty());

    const fs::path file = scene::motionDatabasePath(scratch("roundtrip"), "probe");
    REQUIRE(scene::writeMotionDatabase(built, file).has_value());
    REQUIRE(fs::exists(file));
    CHECK_FALSE(fs::exists(fs::path(file.string() + ".partial")));

    auto loaded = scene::readMotionDatabase(file);
    REQUIRE(loaded.has_value());
    CHECK(scene::firstMotionDatabaseDifference(built, *loaded).empty());
    INFO("first difference: " << scene::firstMotionDatabaseDifference(built, *loaded));

    // And it searches the same: every query returns the same sample at the same cost.
    const scene::MotionCostWeights weights;
    const auto queries = probeQueries(built);
    REQUIRE(queries.size() > 5);
    for (const scene::MotionQuery& q : queries) {
        const scene::MotionMatch a = scene::searchMotion(built, q, weights);
        const scene::MotionMatch b = scene::searchMotion(*loaded, q, weights);
        REQUIRE(a.found());
        CHECK(a.sample == b.sample);
        CHECK(a.cost == b.cost);
    }

    // A second write of the loaded database is byte-identical to the first file: save -> load ->
    // save does not drift (testing.md #31).
    const fs::path again = scene::motionDatabasePath(scratch("roundtrip2"), "probe");
    REQUIRE(scene::writeMotionDatabase(*loaded, again).has_value());
    std::ifstream a(file, std::ios::binary);
    std::ifstream b(again, std::ios::binary);
    const std::string bytesA((std::istreambuf_iterator<char>(a)), std::istreambuf_iterator<char>());
    const std::string bytesB((std::istreambuf_iterator<char>(b)), std::istreambuf_iterator<char>());
    CHECK(bytesA.size() > built.features.size() * sizeof(float));
    CHECK(bytesA == bytesB);
}

TEST_CASE("§36/§38: an incompatible database is refused, never silently loaded",
          "[motiondb][io][phaseC]") {
    const scene::MotionPack pack = testsupport::probePack();
    const scene::MotionDatabase built = buildProbe(pack);
    const fs::path dir = scratch("refusals");
    const fs::path file = scene::motionDatabasePath(dir, "probe");
    REQUIRE(scene::writeMotionDatabase(built, file).has_value());
    REQUIRE(scene::readMotionDatabase(file).has_value());

    SECTION("a flipped byte in the payload fails the content digest") {
        corruptByte(file, 5);
        const auto loaded = scene::readMotionDatabase(file);
        REQUIRE_FALSE(loaded.has_value());
        CHECK(loaded.error().message.find("digest") != std::string::npos);
    }
    SECTION("a different rig is refused before the payload is read") {
        const auto loaded = scene::readMotionDatabase(file, {"another-skeleton", {}, {}});
        REQUIRE_FALSE(loaded.has_value());
        CHECK(loaded.error().message.find("skeleton") != std::string::npos);
    }
    SECTION("a database left behind by an older pack is stale") {
        scene::MotionPack changed = pack;
        changed.animation[1].channels[1].values[4].z += 0.01f; // one key of one foot
        const std::string content = scene::motionPackContentDigest(changed);
        REQUIRE(content != built.build.sourcePackDigest);
        const auto loaded = scene::readMotionDatabase(file, {{}, content, {}});
        REQUIRE_FALSE(loaded.has_value());
        CHECK(loaded.error().message.find("stale") != std::string::npos);
    }
    SECTION("a config whose schema no longer matches the recorded one") {
        scene::MotionDatabase edited = built;
        edited.build.featureSchema = "0000000000000000";
        edited.identity = 0; // unstamped, so the writer recomputes rather than refusing
        REQUIRE(scene::writeMotionDatabase(edited, file).has_value());
        const auto loaded = scene::readMotionDatabase(file);
        REQUIRE_FALSE(loaded.has_value());
        CHECK(loaded.error().message.find("feature schema") != std::string::npos);
    }
    SECTION("a file version this build does not know") {
        std::fstream f(file, std::ios::binary | std::ios::in | std::ios::out);
        f.seekp(4);
        const std::uint32_t future = scene::kMotionDatabaseFileVersion + 1;
        f.write(reinterpret_cast<const char*>(&future), sizeof(future));
        f.close();
        const auto loaded = scene::readMotionDatabase(file);
        REQUIRE_FALSE(loaded.has_value());
        CHECK(loaded.error().message.find("file version") != std::string::npos);
    }
    SECTION("a database changed after stamping is refused at write, where the cause is visible") {
        scene::MotionDatabase retuned = built;
        retuned.config.rootVelocityWeight = 3.0f;
        const auto written = scene::writeMotionDatabase(retuned, file);
        REQUIRE_FALSE(written.has_value());
        CHECK(written.error().message.find("stamp") != std::string::npos);
    }
}

TEST_CASE("§38: the schema digest tracks meaning, not weights", "[motiondb][io][phaseC]") {
    scene::MotionFeatureConfig a = testsupport::probeOptions().config;
    scene::MotionFeatureConfig b = a;
    b.jointPositionWeight = 2.0f; // a weight: same dimensions, same meaning
    CHECK(scene::motionFeatureSchemaDigest(a) == scene::motionFeatureSchemaDigest(b));
    b.trajectoryTimes = {0.2f, 0.5f}; // a horizon: dimension 5 now means something else
    CHECK(scene::motionFeatureSchemaDigest(a) != scene::motionFeatureSchemaDigest(b));
    scene::MotionFeatureConfig c = a;
    c.joints = {"foot.r", "foot.l"}; // the same joints in another order are another layout
    CHECK(scene::motionFeatureSchemaDigest(a) != scene::motionFeatureSchemaDigest(c));
}

TEST_CASE("§82: the build key moves with every input and nothing else", "[motiondb][cache][phaseC]") {
    const scene::MotionPack pack = testsupport::probePack();
    const scene::MotionDatabaseOptions options = testsupport::probeOptions();
    const std::string source = scene::motionPackContentDigest(pack);
    const std::string key =
        scene::motionDatabaseBuildKey(source, options.config, options.sampleRate, options.toolVersion);
    // Deterministic: the same inputs twice, and a pack rebuilt from scratch, give the same key.
    CHECK(key == scene::motionDatabaseBuildKey(scene::motionPackContentDigest(testsupport::probePack()),
                                               options.config, options.sampleRate,
                                               options.toolVersion));
    CHECK(buildProbe(pack).build.buildKey == key);

    scene::MotionPack retargeted = pack;
    retargeted.provenance[0].processing.push_back("retarget profile=alien-v2");
    CHECK(scene::motionPackContentDigest(retargeted) != source);

    scene::MotionPack rekeyed = pack;
    rekeyed.animation[0].channels[2].values[10].z += 1e-4f;
    CHECK(scene::motionPackContentDigest(rekeyed) != source);

    scene::MotionFeatureConfig weighted = options.config;
    weighted.jointVelocityWeight = 0.5f;
    CHECK(scene::motionDatabaseBuildKey(source, weighted, options.sampleRate, options.toolVersion) != key);
    CHECK(scene::motionDatabaseBuildKey(source, options.config, 60.0f, options.toolVersion) != key);
    CHECK(scene::motionDatabaseBuildKey(source, options.config, options.sampleRate, "avgen-motion-db/2") !=
          key);
}

TEST_CASE("§82: an identical build is reused, a changed one is rebuilt", "[motiondb][cache][phaseC]") {
    const scene::MotionPack pack = testsupport::probePack();
    const scene::MotionDatabaseOptions options = testsupport::probeOptions();
    const fs::path file = scene::motionDatabasePath(scratch("cache"), "probe");

    auto first = scene::buildMotionDatabaseCached(pack, options, file);
    REQUIRE(first.has_value());
    CHECK_FALSE(first->reused);
    REQUIRE(first->db.sampleCount() == 62);

    auto second = scene::buildMotionDatabaseCached(pack, options, file);
    REQUIRE(second.has_value());
    CHECK(second->reused);
    CHECK(scene::firstMotionDatabaseDifference(first->db, second->db).empty());

    scene::MotionPack changed = pack;
    changed.animation[0].channels[1].values[3].z += 0.02f;
    auto third = scene::buildMotionDatabaseCached(changed, options, file);
    REQUIRE(third.has_value());
    CHECK_FALSE(third->reused);
    CHECK(third->db.build.sourcePackDigest != first->db.build.sourcePackDigest);

    // A corrupt cache entry with a matching key is rebuilt rather than trusted.
    corruptByte(file, 9);
    auto fourth = scene::buildMotionDatabaseCached(changed, options, file);
    REQUIRE(fourth.has_value());
    CHECK_FALSE(fourth->reused);
    CHECK(scene::readMotionDatabase(file).has_value());
}

TEST_CASE("§37: the runtime loads the real Glowmere database without extracting a feature",
          "[motiondb][io][phaseC][aliens]") {
    auto glowmere = testsupport::glowmereMotion();
    if (!glowmere) {
        SKIP("the Glowmere alien is not present");
    }
    REQUIRE(glowmere->db.sampleCount() > 1000);

    // The pack goes to disk too, because the runtime reads both and the database must be checked
    // against the pack as READ, not as built: a pack whose write/read lost anything the database
    // was built from would make every stored database stale on arrival.
    const fs::path dir = scratch("glowmere") / "glowmere-scout.motionpack";
    REQUIRE(scene::writeMotionPack(glowmere->pack, dir).has_value());
    const fs::path file = scene::motionDatabasePath(dir, "biped");
    REQUIRE(scene::writeMotionDatabase(glowmere->db, file).has_value());

    auto asset = scene::loadMotionAsset(dir, file);
    if (!asset) {
        FAIL("loadMotionAsset: " << asset.error().message);
    }
    CHECK(scene::firstMotionDatabaseDifference(glowmere->db, (*asset)->db).empty());
    CHECK((*asset)->pack.animation.size() == glowmere->pack.animation.size());

    // Offline versus runtime, timed: the build poses every frame of every clip, the load reads
    // arrays. Minima over repeats (ADR-170).
    double buildMs = std::numeric_limits<double>::max();
    double loadMs = std::numeric_limits<double>::max();
    for (int r = 0; r < 3; ++r) {
        const auto t0 = std::chrono::steady_clock::now();
        auto rebuilt = scene::buildMotionDatabase(glowmere->pack, glowmere->options);
        const auto t1 = std::chrono::steady_clock::now();
        auto reloaded = scene::readMotionDatabase(file);
        const auto t2 = std::chrono::steady_clock::now();
        REQUIRE(rebuilt.has_value());
        REQUIRE(reloaded.has_value());
        buildMs = std::min(buildMs, std::chrono::duration<double, std::milli>(t1 - t0).count());
        loadMs = std::min(loadMs, std::chrono::duration<double, std::milli>(t2 - t1).count());
    }
    INFO("build " << buildMs << " ms, load " << loadMs << " ms, "
                  << glowmere->db.sampleCount() << " samples");
    CHECK(loadMs * 5.0 < buildMs);
}
