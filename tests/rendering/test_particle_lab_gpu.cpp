// The Particle / VFX Lab (lab #10, docs/particle-lab/README.md).
//
// The question is "how many particles are alive, where, and what are they writing?", and the
// instrument is deliberately NOT the image. An additive billboard field saturates, and a saturated
// region stops reporting its own population -- so every count here comes from
// `ParticleRenderer::readCounts`, which reads the GPU's own compaction counters, and from
// `readDrawArgs`, which reads what `DrawIndirect` was actually handed. ADR-387: a correct value is
// not a reached value, and a count that is right in the counters says nothing about the draw.
//
// The fixture is `examples/labs/particle-vfx-lab.scene.json`. Its design argument is in the file's
// own `_note` and in section 5 of the doc; the part that matters for reading these assertions is
// that `metered` and `saturated` are THE SAME EMITTER at two capacities, so the control shares the
// frame with the measurement and cannot be explained away by anything about the frame.

#include "app/engine.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/particle_renderer.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/composition.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace fs = std::filesystem;
using namespace avgen;

namespace {

// The systems, in the order the fixture declares them -- which is the order the flatten pushes them
// into `scene.particles` and therefore the pool index `readCounts` takes.
enum Slot : std::size_t { kMetered = 0, kSaturated = 1, kTrailed = 2, kBurst = 3 };

constexpr std::uint32_t kMeteredCapacity = 8192;
constexpr std::uint32_t kSaturatedCapacity = 256;
constexpr std::uint32_t kTrailedCapacity = 512;
constexpr std::uint32_t kBurstCapacity = 4096;
// rate * lifetime, from the fixture: 3000 a second, each living exactly half of one.
constexpr std::uint32_t kMeteredSteady = 1500;

struct ParticleBench {
    std::unique_ptr<gpu::Context> ctx;
    std::unique_ptr<gpu::ShaderLibrary> shaders;
    std::unique_ptr<rendering::SceneRenderer> renderer;
    std::unique_ptr<app::Engine> engine;
    FixedStepClock clock{60.0};
    FrameTime time{};

    static fs::path scenePath() {
        return fs::path(AVGEN_SOURCE_DIR) / "examples" / "labs" / "particle-vfx-lab.scene.json";
    }

    static ParticleBench make() {
        static bool logInit = false;
        if (!logInit) {
            log::init(log::Level::Warn);
            logInit = true;
        }
        ParticleBench b;
        auto ctx = gpu::Context::create(gpu::ContextDesc{});
        if (!ctx) {
            SKIP("no GPU adapter available: " << ctx.error().message);
        }
        b.ctx = std::move(*ctx);
        b.shaders = std::make_unique<gpu::ShaderLibrary>(*b.ctx,
                                                         std::vector{fs::path(AVGEN_SHADER_SOURCE_DIR)});
        b.renderer = std::make_unique<rendering::SceneRenderer>(*b.ctx, *b.shaders);
        REQUIRE(b.renderer->init().has_value());
        b.engine = std::make_unique<app::Engine>(app::EngineMode::Offline);
        REQUIRE(b.engine->loadComposition(scenePath()).has_value());
        b.engine->setViewport(640, 360);
        return b;
    }

    scene::Scene& scene() { return engine->composition()->scene(); }

    // One frame of the fixture, driven the ADR-091 way: tick, update, render. The render is what
    // encodes the compute passes, so a count read without one is last frame's.
    void step() {
        time = engine->tick(clock);
        engine->update(time);
        auto img = renderer->renderToImage(scene(), time, 640, 360);
        REQUIRE(img.has_value());
    }
    void steps(int n) {
        for (int i = 0; i < n; ++i) {
            step();
        }
    }

    rendering::ParticleCounts counts(std::size_t slot) {
        auto c = renderer->particles().readCounts(slot);
        REQUIRE(c.has_value());
        return *c;
    }
    std::array<std::uint32_t, 8> drawArgs(std::size_t slot) {
        auto a = renderer->particles().readDrawArgs(slot);
        REQUIRE(a.has_value());
        return *a;
    }
};

} // namespace

TEST_CASE("the particle fixture is the four systems it claims to be", "[particle][lab][gpu]") {
    // ADR-182 applied to the fixture before anything is measured through it. The counting cases
    // below name systems by pool index; an index that had silently shifted -- a node reordered, a
    // node dropped because a key stopped parsing -- would make every one of them a measurement of
    // the wrong system, and they would still pass.
    ParticleBench bench = ParticleBench::make();
    const scene::Scene& s = bench.scene();
    REQUIRE(s.particles.size() == 4);
    CHECK(s.particles[kMetered].name == "metered");
    CHECK(s.particles[kSaturated].name == "saturated");
    CHECK(s.particles[kTrailed].name == "trailed");
    CHECK(s.particles[kBurst].name == "burst");
    CHECK(s.particles[kMetered].capacity == kMeteredCapacity);
    CHECK(s.particles[kSaturated].capacity == kSaturatedCapacity);
    CHECK(s.particles[kTrailed].capacity == kTrailedCapacity);
    CHECK(s.particles[kBurst].capacity == kBurstCapacity);

    // The two counting systems really are the same emitter. If they ever stop being, case 2's
    // control stops being a control and starts being a second experiment.
    CHECK(s.particles[kMetered].spawnRate == s.particles[kSaturated].spawnRate);
    CHECK(s.particles[kMetered].lifetimeMin == s.particles[kSaturated].lifetimeMin);
    CHECK(s.particles[kMetered].lifetimeMax == s.particles[kSaturated].lifetimeMax);

    // And no field force, no wind, no curl turbulence on the systems the counts are read from:
    // this lab does not own the fields that push particles about, and a fixture that stirred the
    // counts with one would make every reading a joint measurement.
    for (const std::size_t slot : {kMetered, kSaturated}) {
        INFO("system " << s.particles[slot].name);
        CHECK(s.particles[slot].turbulence == 0.0f);
        CHECK(s.particles[slot].windInfluence == 0.0f);
        CHECK(s.particles[slot].fieldForces.empty());
    }
    CHECK(bench.ctx->errorCount() == 0);
}

TEST_CASE("a pool settles where spawnRate times lifetime says, unless capacity says otherwise",
          "[particle][lab][gpu]") {
    // Lab case 2. One second, which is two full lifetimes of the metered system, so both pools are
    // in steady state and neither is still filling.
    ParticleBench bench = ParticleBench::make();
    bench.steps(60);

    const rendering::ParticleCounts metered = bench.counts(kMetered);
    const rendering::ParticleCounts saturated = bench.counts(kSaturated);
    INFO("metered alive " << metered.alive << " of " << kMeteredCapacity << ", saturated alive "
                          << saturated.alive << " of " << kSaturatedCapacity);

    // 3000 a second at 60 fps is 50 a frame, and each lives 30 frames: 1500, plus or minus the one
    // frame's worth the kill boundary can fall either side of.
    CHECK(metered.alive >= kMeteredSteady - 60);
    CHECK(metered.alive <= kMeteredSteady + 60);

    // The control, in the same frame: the same emitter, bound by its capacity instead. `cs_emit`
    // takes `min(emitCount, deadCount)`, so a pool that cannot hold a lifetime's worth sits full.
    CHECK(saturated.alive == kSaturatedCapacity);

    // ...and the two must not be the same number, or the count is reporting something that is not
    // the pool. This is the assertion that fails if `readCounts` ever returns the request rather
    // than the grant.
    CHECK(metered.alive != saturated.alive);
    CHECK(bench.ctx->errorCount() == 0);
}

TEST_CASE("the compaction accounts for every slot, every frame", "[particle][lab][gpu]") {
    // Lab case 3. alive + dead == capacity is a property of the prefix-sum pass rather than of the
    // scene: both lists come out of one scan. A pool that loses slots leaks capacity no later frame
    // can recover, and the symptom is a system that quietly stops emitting after a minute.
    //
    // Checked at three times on purpose: at t = 0 every pool is empty, at 1 s two of them are in
    // steady state and one is full, and an accounting bug that only shows at one extreme is the
    // likely kind.
    ParticleBench bench = ParticleBench::make();
    const std::array<const char*, 4> names{"metered", "saturated", "trailed", "burst"};
    const std::array<std::uint32_t, 4> capacities{kMeteredCapacity, kSaturatedCapacity, kTrailedCapacity,
                                                  kBurstCapacity};

    for (const int frames : {1, 30, 60}) {
        bench.steps(frames == 1 ? 1 : frames - (frames == 30 ? 1 : 30));
        for (std::size_t i = 0; i < 4; ++i) {
            const rendering::ParticleCounts c = bench.counts(i);
            INFO("after " << frames << " frames, system " << names[i] << ": alive " << c.alive << " dead "
                          << c.dead << " capacity " << capacities[i]);
            CHECK(c.alive + c.dead == capacities[i]);
            CHECK(c.alive <= capacities[i]);
        }
    }
    CHECK(bench.ctx->errorCount() == 0);
}

TEST_CASE("the indirect draw carries the alive count, and the ribbon draw only when there is a ribbon",
          "[particle][lab][gpu]") {
    // Lab case 4. `readDrawArgs` returns {billboard, ribbon} x {vertexCount, instanceCount,
    // firstVertex, firstInstance}.
    ParticleBench bench = ParticleBench::make();
    bench.steps(60);

    for (const std::size_t slot : {kMetered, kSaturated, kTrailed, kBurst}) {
        const rendering::ParticleCounts c = bench.counts(slot);
        const std::array<std::uint32_t, 8> args = bench.drawArgs(slot);
        INFO("pool " << slot << ": alive " << c.alive << ", billboard instances " << args[1]
                     << ", ribbon instances " << args[5]);
        CHECK(args[1] == c.alive);
    }

    // The control, and it is the half that can fire the other way: three of the four systems author
    // no trail at all. A non-zero ribbon instance count on those is an indirect argument written by
    // a pass that should not have run -- which a probe over the trailed system alone could never
    // catch, because that system is named in every branch that draws ribbons.
    CHECK(bench.drawArgs(kTrailed)[5] == bench.counts(kTrailed).alive);
    for (const std::size_t slot : {kMetered, kSaturated, kBurst}) {
        INFO("pool " << slot << " authors no trail");
        CHECK(bench.drawArgs(slot)[5] == 0);
    }
    CHECK(bench.ctx->errorCount() == 0);
}

TEST_CASE("an authored burst emits what the file says", "[particle][lab][gpu]") {
    // Lab case 5, and the defect this lab found on its first render (ADR-399). `burst` authors
    // spawnRate 0 and burst 64, so every frame owes exactly 64 particles and nothing else -- and
    // the system rendered nothing, because `registerParticleParameters` seeded
    // `particles/<name>/burst` with a hard-coded 0 rather than the scene's value.
    ParticleBench bench = ParticleBench::make();

    // One frame first, and that ordering is load bearing: `applyParticleParameters` runs inside
    // `Engine::update`, so the flattened scene still holds the file's value until something has
    // ticked. Asserted before a step, this passes even with the defect present.
    bench.step();
    REQUIRE(bench.scene().particles[kBurst].burst == 64.0f);
    REQUIRE(bench.scene().particles[kBurst].spawnRate == 0.0f);

    bench.steps(29); // half a second in total; the lifetime is 0.8 s, so nothing has died yet
    const rendering::ParticleCounts burst = bench.counts(kBurst);
    INFO("burst alive after 30 frames: " << burst.alive);

    // 64 a frame for 30 frames, none of them dead yet, and the first frame emits nothing because it
    // carries no delta -- so 29 frames of 64. Bounded rather than exact, because what is being
    // asserted is that the impulse fires at all and at about the authored size, not the clock.
    CHECK(burst.alive > 1500);
    CHECK(burst.alive <= 30 * 64);

    // The control, in the same frame: the neighbouring `metered` system's rate is authored the same
    // way, through a parameter that WAS seeded from the scene. If both were empty the fixture would
    // be wrong rather than the engine, and only one of them ever was.
    CHECK(bench.counts(kMetered).alive > 0);
    CHECK(bench.ctx->errorCount() == 0);
}
