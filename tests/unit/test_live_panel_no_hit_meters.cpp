// ADR-1530: the Live panel no longer shows kick / snare / hat meters, while the detection behind them stays.
//
// The Live panel is one immediate-mode ImGui function with no headless harness, so the first case reads its
// source: it must not read the hit envelopes it used to show. The others pin what stays: the response hit signals
// on the bus, the five response parameters carried across a live switch, and the detector's hits on a drum kit.

#include "analysis/analysis_track.hpp"
#include "analysis/causal_onsets.hpp"
#include "app/live_scenes.hpp"
#include "signals/signal_bus.hpp"
#include "sonic/response.hpp"
#include "support/drum_kit.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

using namespace avgen;

TEST_CASE("The Live panel shows no kick, snare or hat meters", "[ui][live][adr1530]") {
    const std::filesystem::path path = std::filesystem::path(AVGEN_SOURCE_DIR) / "src/ui/live_panel.cpp";
    std::ifstream in(path);
    REQUIRE(in.good());
    const std::string source((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    REQUIRE(source.find("sonic/response/transient") != std::string::npos); // the right file, and Transients stays
    for (const std::string_view gone : {"response.kickEnv", "response.snareEnv", "response.hatEnv", "response.noteEnv",
                                        "kicks, snares, hats", "hit detector"}) {
        INFO(gone);
        CHECK(source.find(gone) == std::string::npos);
    }
}

TEST_CASE("The response hit signals are still declared for scenes", "[sonic][adr1530]") {
    // ADR-1620: kick, snare and hat stay declared (ids unchanged) for a project whose feeds name a drum source; on a mix
    // they never fire (test_sonic_response.cpp).
    signals::SignalBus bus;
    sonic::ResponseModel model;
    model.declare(bus);
    for (const char* name : {"response.kick", "response.kickEnv", "response.snare", "response.snareEnv", "response.hat",
                             "response.hatEnv", "response.onsetRate", "response.low", "response.onset", "response.note",
                             "response.level", "response.bass"}) {
        INFO(name);
        CHECK(bus.find(name).has_value());
    }
    CHECK(app::kResponseParameterPaths.size() == 5);
    CHECK(std::string_view(app::kResponseParameterPaths[1]) == "sonic/response/transient");
}

TEST_CASE("The causal detector still finds the kit's kicks, snares and hats", "[analysis][adr1530]") {
    const testsupport::Kit kit = testsupport::makeKit(8);
    const analysis::AnalysisTrack track = analysis::AnalysisTrack::analyze(kit.file, analysis::AnalyzerConfig{});
    std::size_t kicks = 0, snares = 0, hats = 0;
    for (const analysis::AnalysisFrame& f : track.frames()) {
        REQUIRE(f.causal.valid);
        kicks += f.causal.hit[static_cast<std::size_t>(analysis::HitClass::Kick)] ? 1u : 0u;
        snares += f.causal.hit[static_cast<std::size_t>(analysis::HitClass::Snare)] ? 1u : 0u;
        hats += f.causal.hit[static_cast<std::size_t>(analysis::HitClass::Hat)] ? 1u : 0u;
    }
    CHECK(kicks > 0);
    CHECK(snares > 0);
    CHECK(hats > 0);
}
