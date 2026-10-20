// ADR-1571: `avgen --sonic-trace` writes the stem (ADR-1520), beat-clock and envelope-source columns beside the sonic
// ones, so a stem-driven scene can be checked against its music without a render.

#include "app/sonic_trace_cli.hpp"
#include "support/stem_fixture.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace avgen;
namespace fs = std::filesystem;

TEST_CASE("sonic trace: the stems, the beat clock and envelope sources are columns", "[stems][sonic][adr1571]") {
    const testsupport::StemFixture& fx = testsupport::requireStemFixture();
    const fs::path dir = testsupport::processTempDir() / "sonic-trace-stems";
    fs::create_directories(dir);
    const nlohmann::json p = {
        {"format", "avgen-project"},
        {"version", 4},
        {"assets",
         {{"scene", {{"kind", "orb"}}},
          {"audio", fx.file("mix.wav").string()},
          {"stems", nlohmann::json::array({{{"key", "drums"}, {"label", "Drums"}, {"file", fx.file("drums.wav").string()}}})}}},
        {"sonic", nlohmann::json::object()},
        {"sources", nlohmann::json::array({{{"kind", "envelope"}, {"name", "kicks"},
                                            {"settings", {{"trigger", "stem.drums.onset"}}}}})},
        {"parameters", nlohmann::json::object()},
        {"routes", nlohmann::json::array()}};
    std::ofstream(dir / "project.json") << p.dump(1);
    REQUIRE(app::runSonicTraceCommand(dir / "project.json", dir / "trace.csv", 30.0) == 0);
    std::ifstream in(dir / "trace.csv");
    std::string header;
    REQUIRE(std::getline(in, header));
    for (const char* column : {"stem.drums.level", "stem.drums.active", "stem.drums.onset", "beat.phase",
                               "beat.phrasePulse", "env.kicks", "response.level"}) {
        INFO(column);
        CHECK(header.find(std::string(",") + column) != std::string::npos);
    }
    // The drum stem is active while it plays: some row reads 1 in its column.
    std::size_t col = 0;
    {
        std::stringstream ss(header);
        std::string name;
        for (std::size_t i = 0; std::getline(ss, name, ','); ++i) {
            if (name == "stem.drums.active") {
                col = i;
            }
        }
    }
    REQUIRE(col > 0);
    bool active = false;
    for (std::string row; std::getline(in, row) && !active;) {
        std::stringstream ss(row);
        std::string cell;
        for (std::size_t i = 0; std::getline(ss, cell, ','); ++i) {
            if (i == col && std::stod(cell) > 0.5) {
                active = true;
            }
        }
    }
    CHECK(active);
}
