// What an engine holds after a project switch is what the open project needs, not the sum of every project opened
// (ADR-1392).
//
// The owner, 2026-10-15: quitting is slow after loading several projects in a row. The decoded-asset cache
// (`assets::AssetRegistry`) was never emptied: every glTF and image any project had decoded stayed resident for the
// rest of the session, and all of it was freed at once on the way out. Now a scene load keeps what it used and lets the
// rest go, so A -> B -> A holds exactly what A alone holds.

#include "app/engine.hpp"
#include "assets/asset_registry.hpp"
#include "core/process_memory.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using namespace avgen;
namespace fs = std::filesystem;

namespace {
struct Counts {
    std::size_t scenes = 0;
    std::size_t images = 0;
    bool operator==(const Counts&) const = default;
};
Counts countsOf(const app::Engine& e) { return {e.assetRegistry().sceneCount(), e.assetRegistry().imageCount()}; }
} // namespace

TEST_CASE("The asset cache returns to the open project's own after a switch", "[integration][switch][memory]") {
    const fs::path a = fs::path(AVGEN_SOURCE_DIR) / "examples/lab/character-animation-lab.json";
    const fs::path b = fs::path(AVGEN_SOURCE_DIR) / "examples/labs/lighting-lab.scene.json";
    const fs::path alien = fs::path(AVGEN_SOURCE_DIR) / "assets/aliens/alien-scout.glb";
    if (!fs::exists(alien)) {
        SKIP("the private character assets are not present");
    }
    app::Engine engine(app::EngineMode::Offline);
    engine.setLiveControl(false);
    REQUIRE(engine.loadProject(a).has_value());
    const Counts alone = countsOf(engine);
    REQUIRE(alone.scenes > 0); // or the comparison below says nothing

    REQUIRE(engine.loadFile(b).has_value());
    const Counts other = countsOf(engine);
    INFO("A: " << alone.scenes << " scene(s), " << alone.images << " image(s); B: " << other.scenes << ", "
               << other.images);
    // B holds what B opened in a fresh engine holds -- not A's plus B's.
    app::Engine freshB(app::EngineMode::Offline);
    freshB.setLiveControl(false);
    REQUIRE(freshB.loadFile(b).has_value());
    CHECK(other == countsOf(freshB));

    REQUIRE(engine.loadProject(a).has_value());
    CHECK(countsOf(engine) == alone);

    // And it does not creep: three more round trips leave the cache where one left it, and the process within a margin
    // of where it stood after the first (the allocator keeps freed pages, so this is a ceiling on growth, not a
    // reading of the cache).
    const core::ProcessMemory first = core::processMemory();
    for (int i = 0; i < 3; ++i) {
        REQUIRE(engine.loadFile(b).has_value());
        REQUIRE(engine.loadProject(a).has_value());
    }
    CHECK(countsOf(engine) == alone);
    const core::ProcessMemory last = core::processMemory();
    if (first.footprint > 0) {
        const double grewMb = (static_cast<double>(last.footprint) - static_cast<double>(first.footprint)) / 1048576.0;
        INFO("footprint grew " << grewMb << " MB over three more round trips");
        CHECK(grewMb < 64.0);
    }
}
