// A live scene switch must not carry one scene's look into the next. The owner, 2026-10-14: after switching scenes
// "they all started to have a spinning tunnel effect on them that seemed to carry over from one scene". Feedback
// Mirror turns the temporal feedback on (zoom 0.972, rotate 3: a spinning tunnel), and most live projects never
// mention temporal/feedback/*. Opening a project resets every serialised parameter to its default, but the temporal
// parameters re-register from the engine's own TemporalSettings, which the frames had synced to Feedback Mirror's
// values, so "the default" was Feedback Mirror's feedback. Only after frames have run: a load straight after a load
// never showed it.
//
// So: every parameter of scene B, opened after scene A has played for a while in the same engine, must equal scene B
// opened in a fresh engine -- base values and the values modulation produces, in the live editor's mode and offline.

#include "app/engine.hpp"
#include "core/time.hpp"
#include "params/parameter_set.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <sstream>
#include <string>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

std::string differences(const params::ParameterSet& a, const params::ParameterSet& b, bool finals) {
    std::ostringstream out;
    for (const params::IParameter* p : b.ordered()) {
        const params::IParameter* q = a.find(p->path());
        if (q == nullptr || q->componentCount() != p->componentCount()) {
            continue;
        }
        for (std::size_t i = 0; i < p->componentCount(); ++i) {
            const float x = finals ? q->finalComponent(i) : q->baseComponent(i);
            const float y = finals ? p->finalComponent(i) : p->baseComponent(i);
            if (x != y) {
                out << p->path() << "[" << i << "]: " << x << " (after the switch) vs " << y << " (fresh); ";
            }
        }
    }
    return out.str();
}

void play(app::Engine& engine, int frames) {
    for (int f = 0; f < frames; ++f) {
        engine.update(FrameTime{f / 60.0, f == 0 ? 0.0 : 1.0 / 60.0, static_cast<std::uint64_t>(f)});
    }
}

} // namespace

TEST_CASE("A live scene switch carries no parameter from the scene before it", "[integration][live][switch]") {
    const fs::path ex = fs::path(AVGEN_SOURCE_DIR) / "examples";
    for (const auto mode : {app::EngineMode::Live, app::EngineMode::Offline}) {
        for (const char* to : {"sonic-abstract/neon-vector.json", "sonic-abstract/glitch-signal.json",
                               "sonic-vfx/event-horizon.json", "sonic-garden/sonic-live.json"}) {
            INFO((mode == app::EngineMode::Live ? "live: " : "offline: ") << "feedback-mirror -> " << to);
            // Live control off on both engines (ADR-1390's audit): in Live mode an engine opens this machine's MIDI and
            // OSC, and with a controller connected the two engines read different knob positions -- 2026-10-15 this
            // failed on `lights/heart/intensity 0 vs 2.05` with one MIDI source attached, and passed with it off. The
            // test is about what a switch carries, not about what a device sends.
            app::Engine switched(mode);
            switched.setLiveControl(false);
            REQUIRE(switched.loadProject(ex / "sonic-vfx" / "feedback-mirror.json").has_value());
            play(switched, 30);
            REQUIRE(switched.loadProject(ex / to).has_value());
            app::Engine fresh(mode);
            fresh.setLiveControl(false);
            REQUIRE(fresh.loadProject(ex / to).has_value());
            const std::string afterLoad = differences(switched.params(), fresh.params(), false);
            INFO("after the load: " << afterLoad);
            CHECK(afterLoad.empty());
            play(switched, 30);
            play(fresh, 30);
            const std::string base = differences(switched.params(), fresh.params(), false);
            const std::string modulated = differences(switched.params(), fresh.params(), true);
            INFO("base: " << base);
            INFO("modulated: " << modulated);
            CHECK(base.empty());
            CHECK(modulated.empty());
        }
    }
}
