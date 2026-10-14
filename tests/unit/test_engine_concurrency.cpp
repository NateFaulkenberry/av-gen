// Two Engines on two threads at once (code audit CA-001).
//
// The Director's stills, evaluate and record jobs each build a whole `app::Engine` on a worker
// thread while the editor's own Engine keeps ticking on the main thread. Three pieces of
// process-wide state were shared by those Engines with no synchronisation:
//
//   - the generator registry (ADR-175), which every Engine constructor re-registers into while any
//     other Engine may be looking a generator up;
//   - the bounds cache behind `detail::primitiveBox` for a generated source, a `std::map` both
//     Engines insert into during procedural generation;
//   - `probe2::frame()`, the UI-responsiveness counters, written by every seek.
//
// No test ran two Engines at once, so the nightly TSan set could not see any of it. This one does
// exactly that, and is tagged [threading] so TSan's concurrency set (tools/ci/run-suite.sh,
// TSAN_FILTER) runs it. In a plain build it still checks the results are the single-threaded ones.

#include "app/engine.hpp"
#include "core/phase2_probe.hpp"
#include "organism/mushroom.hpp"
#include "scene/procedural.hpp"
#include "scene/procedural_detail.hpp"
#include "search/candidate_search.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <limits>
#include <thread>
#include <vector>

using namespace avgen;

namespace {

constexpr int kSpecs = 24;

scene::SourceSpec mushroomSpec(std::uint32_t index, int part) {
    scene::SourceSpec s;
    s.kind = scene::PrimitiveKind::Generated;
    s.generated.generator = "mushroom";
    s.generated.generatorVersion = organism::mushroomSchema().generatorVersion;
    s.generated.index = index;
    s.generated.values = search::sampleAt(organism::mushroomSchema().parameters, index);
    s.generatedPart = part;
    return s;
}

struct Box {
    glm::vec3 centre{0.0f};
    glm::vec3 half{0.0f};
};

Box boxOf(const scene::SourceSpec& s) {
    Box b;
    scene::detail::primitiveBox(s, b.centre, b.half);
    return b;
}

Box boxFromMesh(const scene::MeshData& mesh) {
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());
    for (const scene::Vertex& v : mesh.vertices) {
        lo = glm::min(lo, v.position);
        hi = glm::max(hi, v.position);
    }
    return Box{(lo + hi) * 0.5f, (hi - lo) * 0.5f};
}

} // namespace

TEST_CASE("Engines built and used on two threads at once share the process-wide state safely",
          "[unit][threading][engine]") {
    // The reference, single-threaded and straight from the generator (which `primitiveBox` would
    // memoise; `makeSourceMesh` does not), so the bounds cache is still empty for these keys when the
    // threads start: they are the ones that insert them. The base index keeps the keys out of
    // anything an earlier test cached.
    app::Engine first(app::EngineMode::Offline);
    REQUIRE(scene::hasGenerator("mushroom"));
    constexpr std::uint32_t kBase = 9100;
    std::array<Box, kSpecs> reference{};
    for (int i = 0; i < kSpecs; ++i) {
        auto mesh = scene::makeSourceMesh(mushroomSpec(kBase + static_cast<std::uint32_t>(i), i % 3));
        REQUIRE(mesh.has_value());
        REQUIRE_FALSE(mesh->vertices.empty());
        reference[static_cast<std::size_t>(i)] = boxFromMesh(*mesh);
    }

    std::atomic<int> ready{0};
    std::atomic<int> wrongBoxes{0};
    std::atomic<int> wrongProbes{0};
    const auto work = [&](int seed) {
        ready.fetch_add(1);
        while (ready.load() < 2) {
            std::this_thread::yield();
        }
        std::uint64_t seeks = 0;
        for (int round = 0; round < 3; ++round) {
            app::Engine engine(app::EngineMode::Offline); // registers the generators again
            engine.seekSeconds(0.25 * static_cast<double>(round + 1));
            ++seeks;
            for (int k = 0; k < kSpecs; ++k) {
                // Both threads walk the same keys from opposite ends, so they meet in the cache.
                const int i = seed == 0 ? k : kSpecs - 1 - k;
                const Box got = boxOf(mushroomSpec(kBase + static_cast<std::uint32_t>(i), i % 3));
                const Box& want = reference[static_cast<std::size_t>(i)];
                if (got.centre != want.centre || got.half != want.half) {
                    wrongBoxes.fetch_add(1);
                }
            }
        }
        // probe2 is per thread: this thread's counters saw this thread's seeks and nobody else's.
        if (probe2::frame().seeks != seeks) {
            wrongProbes.fetch_add(1);
        }
    };

    probe2::frame().clear();
    std::thread a(work, 0);
    std::thread b(work, 1);
    a.join();
    b.join();
    CHECK(wrongBoxes.load() == 0);
    CHECK(wrongProbes.load() == 0);
    // ...and the main thread's counters are its own: the workers' seeks never landed here.
    CHECK(probe2::frame().seeks == 0);
}
