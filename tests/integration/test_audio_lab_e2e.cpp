// ADR-1720: the Audio Response Lab end to end, one case per fixture category (owner's brief §6, §9.4).
//
// Each case takes a generated fixture (tools/audio_lab/make_fixtures.py, run by the build into
// AVGEN_AUDIO_LAB_FIXTURE_DIR), writes the lab project for it from file names only (`lab.py prepare`: no ground truth
// reaches the engine), runs the PRODUCTION pipeline in-process (`runSonicTraceCommand`, the engine `--sonic-trace` runs:
// at the hop clock, and at 30 fps with every route target's final), runs the live analysis path on the same audio
// (ADR-1725's causal replay, when its binary is built), and scores the traces against the fixture's manifest
// (`lab.py score`). Every GATE must pass; FINDINGS (measured behaviour the owner has chosen to treat as separate work:
// the beat clock, a detector's quality in noise) are reported in results.json and never gate.
//
// What a gate is, and why each bound, is in docs/prototypes/audio-response-lab/01-plan.md §6 and in each check's `why`.
//
// Needs Python 3 with numpy (the stem fixture's requirement too); without it every case SKIPs and says why.

#include "app/sonic_trace_cli.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

std::string quoted(const fs::path& p) {
    return "'" + p.string() + "'";
}

fs::path fixtureRoot() {
#ifdef AVGEN_AUDIO_LAB_FIXTURE_DIR
    return AVGEN_AUDIO_LAB_FIXTURE_DIR;
#else
    return {};
#endif
}

fs::path labScript() {
    return fs::path(AVGEN_SOURCE_DIR) / "tools" / "audio_lab" / "lab.py";
}

// The fixture's directory, generating the whole set with the tree's `python3` when the build could not (a CI test VM
// that has the binaries and a venv, as the stem fixture does). SKIPs, saying why, when that fails too.
fs::path requireFixture(const std::string& name) {
    fs::path root = fixtureRoot();
    if (root.empty()) {
        root = testsupport::processTempDir() / "audio-lab-fixtures";
    }
    const fs::path manifest = root / name / "manifest.json";
    if (!fs::exists(manifest) && name != "drums-real") {
        const std::string cmd = "python3 " + quoted(fs::path(AVGEN_SOURCE_DIR) / "tools/audio_lab/make_fixtures.py") +
                                " --out " + quoted(root) + " > /dev/null 2>&1";
        if (std::system(cmd.c_str()) != 0 || !fs::exists(manifest)) {
            SKIP("the Audio Response Lab fixtures could not be generated (python3 with numpy is needed): " +
                 manifest.string());
        }
    }
    if (!fs::exists(manifest)) {
        SKIP("no " + manifest.string());
    }
    std::ifstream in(manifest);
    const auto m = nlohmann::json::parse(in, nullptr, false);
    if (m.is_discarded() || m.contains("unavailable")) {
        SKIP("fixture '" + name + "' unavailable here: " +
             (m.is_object() ? m.value("unavailable", std::string("unreadable")) : std::string("unreadable")));
    }
    return root / name;
}

nlohmann::json runAndScore(const std::string& name) {
    const fs::path fixture = requireFixture(name);
    const fs::path run = testsupport::processTempDir() / "audio-lab" / name;
    fs::create_directories(run);
    REQUIRE(std::system(("python3 " + quoted(labScript()) + " prepare " + quoted(fixture) + " " + quoted(run)).c_str()) ==
            0);
    REQUIRE(app::runSonicTraceCommand(run / "project.json", run / "hop.csv", 48000.0 / 512.0, false) == 0);
    REQUIRE(app::runSonicTraceCommand(run / "project.json", run / "frame.csv", 30.0, true) == 0);
#ifdef AVGEN_AUDIO_LAB_REPLAY
    if (fs::exists(AVGEN_AUDIO_LAB_REPLAY)) {
        const std::string replay =
            quoted(AVGEN_AUDIO_LAB_REPLAY) + " " + quoted(fixture / "mix.wav") + " " + quoted(run / "live") + " > /dev/null";
        CHECK(std::system(replay.c_str()) == 0);
    } else {
        WARN("the causal replay binary is not here; the live path is not scored: " << AVGEN_AUDIO_LAB_REPLAY);
    }
#endif
    const std::string score = "python3 " + quoted(labScript()) + " score " + quoted(fixture) + " " + quoted(run) +
                              " > " + quoted(run / "score.txt") + " 2>&1";
    const int status = std::system(score.c_str());
    std::ifstream in(run / "results.json");
    REQUIRE(in.good());
    nlohmann::json res = nlohmann::json::parse(in);
    // Every gate, by name, so a failure says which and with what value.
    int gates = 0;
    for (const auto& c : res["checks"]) {
        if (!c.value("gate", true)) {
            continue;
        }
        ++gates;
        INFO(name << ": " << c["check"].get<std::string>() << " = " << c["value"].dump() << " (bound "
                  << c["bound"].dump() << "; " << c["why"].get<std::string>() << ")");
        CHECK(c["pass"].get<bool>());
    }
    CHECK(gates > 0);
    CHECK(status == 0);
    return res;
}

} // namespace

TEST_CASE("Audio Response Lab e2e: sparse", "[audiolab][e2e][adr1720]") {
    const auto r = runAndScore("sparse");
    // Silence is silent: no false onset in measured digital silence.
    CHECK(r["onsets"]["response.onset@hop"]["falsePerMinuteSilence"].get<double>() == 0.0);
}

TEST_CASE("Audio Response Lab e2e: dense", "[audiolab][e2e][adr1720][adr1723]") {
    const auto r = runAndScore("dense");
    // ADR-1723: the fast levels are band-selective (the gates hold the bounds; this pins that they were judged).
    int judged = 0;
    for (const auto& c : r["checks"]) {
        judged += c["check"].get<std::string>().rfind("band selectivity", 0) == 0 ? 1 : 0;
    }
    CHECK(judged == 3);
}

TEST_CASE("Audio Response Lab e2e: noisy", "[audiolab][e2e][adr1720]") {
    const auto r = runAndScore("noisy");
    CHECK(r.contains("noisyByKind"));
}

TEST_CASE("Audio Response Lab e2e: syncopated", "[audiolab][e2e][adr1720]") {
    const auto r = runAndScore("syncopated");
    CHECK(r["beat"]["applicable"].get<bool>());
}

TEST_CASE("Audio Response Lab e2e: dynamic", "[audiolab][e2e][adr1720]") {
    const auto r = runAndScore("dynamic");
    CHECK(r["lock"]["segments"].size() == 2); // 120 BPM, then 96
}

TEST_CASE("Audio Response Lab e2e: isolation", "[audiolab][e2e][stems][adr1720]") {
    const auto r = runAndScore("isolation");
    CHECK(r["sources"]["pad"]["silentThroughout"].get<bool>());
}

TEST_CASE("Audio Response Lab e2e: adversarial", "[audiolab][e2e][adr1720]") {
    runAndScore("adversarial");
}

TEST_CASE("Audio Response Lab e2e: real drum one-shots (needs the owner's samples)", "[audiolab][e2e][adr1726]") {
    const auto r = runAndScore("drums-real");
    CHECK(r["sources"]["drums"].contains("drumClassConfusion"));
}
