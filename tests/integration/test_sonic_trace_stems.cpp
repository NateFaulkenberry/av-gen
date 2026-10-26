// ADR-1571: `avgen --sonic-trace` writes the stem (ADR-1520), beat-clock and envelope-source columns beside the sonic
// ones, so a stem-driven scene can be checked against its music without a render.

#include "app/sonic_trace_cli.hpp"
#include "support/stem_fixture.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <vector>
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

// ADR-1720: an event lives one frame and Engine::update clears it before returning, so the trace read 0 in every event
// column (response.onset, stem.<k>.onset, beat.pulse) on every row. It now writes the strength the event fired with.
// It also carries the mix analysis (audio.*), which the Audio Response Lab's band meters read.
TEST_CASE("sonic trace: event columns carry the frame's events; audio.* are columns", "[stems][sonic][adr1720]") {
    const testsupport::StemFixture& fx = testsupport::requireStemFixture();
    const fs::path dir = testsupport::processTempDir() / "sonic-trace-events";
    fs::create_directories(dir);
    const nlohmann::json p = {
        {"format", "avgen-project"},
        {"version", 4},
        {"assets",
         {{"scene", {{"kind", "orb"}}},
          {"audio", fx.file("mix.wav").string()},
          {"stems", nlohmann::json::array({{{"key", "drums"}, {"label", "Drums"}, {"role", "drums"},
                                            {"file", fx.file("drums.wav").string()}}})}}},
        {"sonic", nlohmann::json::object()},
        {"parameters", nlohmann::json::object()},
        {"routes", nlohmann::json::array()}};
    std::ofstream(dir / "project.json") << p.dump(1);
    REQUIRE(app::runSonicTraceCommand(dir / "project.json", dir / "trace.csv", 30.0) == 0);
    std::ifstream in(dir / "trace.csv");
    std::string header;
    REQUIRE(std::getline(in, header));
    std::map<std::string, std::size_t> col;
    {
        std::stringstream ss(header);
        std::string name;
        for (std::size_t i = 0; std::getline(ss, name, ','); ++i) {
            col[name] = i;
        }
    }
    for (const char* c : {"stem.drums.onset", "stem.drums.kick", "response.onset", "beat.pulse", "audio.midLevel",
                          "audio.tempoConfidence", "audio.onset", "response.mid", "response.high", "beat.source"}) {
        INFO(c);
        REQUIRE(col.contains(c));
    }
    std::map<std::string, int> fired;
    double source = -1.0;
    for (std::string row; std::getline(in, row);) {
        std::vector<double> cells;
        std::stringstream ss(row);
        for (std::string cell; std::getline(ss, cell, ',');) {
            cells.push_back(std::stod(cell));
        }
        source = cells[col["beat.source"]];
        for (const char* c : {"stem.drums.onset", "stem.drums.kick", "response.onset", "beat.pulse"}) {
            if (cells[col[c]] > 0.0) {
                ++fired[c];
            }
        }
    }
    // The drum stem's kicks: one per beat at 120 BPM for 20 s, less the 2 s gap (36). Every class fires; the rows that
    // fired are a share of the frames, not all of them (an event is one frame, not a held value).
    const auto kicks = fx.times("drums", "kicks").size();
    CHECK(fired["stem.drums.kick"] >= static_cast<int>(kicks * 9 / 10));
    CHECK(fired["stem.drums.kick"] <= static_cast<int>(kicks + 2));
    CHECK(fired["stem.drums.onset"] >= fired["stem.drums.kick"]);
    CHECK(fired["response.onset"] > 0);
    // The grid's tempo is not this case's question (it reads this fixture at half time: 20 pulses in 20 s at 120 BPM,
    // a finding of the Audio Response Lab's report); that the pulse is a column with events in it is.
    CHECK(fired["beat.pulse"] >= 10);
    // ADR-1724: a file with an analysed grid is clock source 2 (the whole-file grid, which has no lock state).
    CHECK(source == 2.0);
}
