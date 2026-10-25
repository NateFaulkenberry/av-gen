// ADR-1530: the Live panel shows no hit meters.
//
// The Live panel is one immediate-mode ImGui function with no headless harness, so the first case reads its
// source: it must not read hit envelopes. The other pins what stays: the response hit signals on the bus (none
// naming an instrument) and the five response parameters carried across a live switch.

#include "app/live_scenes.hpp"
#include "signals/signal_bus.hpp"
#include "sonic/response.hpp"

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

TEST_CASE("The response hit signals are declared for scenes, and none names an instrument", "[sonic][adr1530]") {
    signals::SignalBus bus;
    sonic::ResponseModel model;
    model.declare(bus);
    for (const char* name : {"response.low", "response.lowEnv", "response.onset", "response.onsetEnv", "response.note",
                             "response.noteEnv", "response.level", "response.bass"}) {
        INFO(name);
        CHECK(bus.find(name).has_value());
    }
    for (const char* name : {"response.kick", "response.kickEnv", "response.snare", "response.snareEnv",
                             "response.hat", "response.hatEnv", "response.hatRate"}) {
        INFO(name);
        CHECK_FALSE(bus.find(name).has_value());
    }
    CHECK(app::kResponseParameterPaths.size() == 5);
    CHECK(std::string_view(app::kResponseParameterPaths[1]) == "sonic/response/transient");
}
