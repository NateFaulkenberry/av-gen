#pragma once

// The stem-analysis fixture (stems-multichannel M1): tools/make_stem_fixture.py, run by the build into
// AVGEN_STEM_FIXTURE_DIR (tests/CMakeLists.txt). `fixture.json` holds every event time, gain, pan, offset and
// format; the tests assert against it rather than against numbers copied into C++.
//
// Where it comes from, in order:
//   1. the build: tests/CMakeLists.txt runs the generator into AVGEN_STEM_FIXTURE_DIR before avgen_tests links;
//   2. the test itself, when (1) is missing or says it was unavailable: CI builds on one VM and runs the tests on
//      others that have only the binaries plus .github/actions/test-inputs' venv (numpy first on PATH), so the first
//      test that needs the fixture generates it with that `python3`. ctest -j runs tests as separate processes, so the
//      generator writes to a directory of this process's own and renames it into place; a loser of that race uses
//      the winner's (identical) files.
// When neither can produce it (no Python 3, no numpy) `requireStemFixture()` SKIPs with the reason -- never a silent
// pass, and never a broken build: the generator writes an "unavailable" marker instead of failing.

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "support/temp_dir.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace avgen::testsupport {

struct StemFixture {
    std::filesystem::path dir;
    nlohmann::json meta;
    [[nodiscard]] std::filesystem::path file(const std::string& name) const { return dir / name; }
    // Event times (seconds) of one list in a stem's metadata, e.g. ("drums", "kicks").
    [[nodiscard]] std::vector<double> times(const std::string& stem, const std::string& list) const {
        std::vector<double> out;
        for (const auto& e : meta["stems"][stem][list]) {
            out.push_back(e.is_number() ? e.get<double>() : e["start"].get<double>());
        }
        return out;
    }
};

inline nlohmann::json readStemFixtureMeta(const std::filesystem::path& dir) {
    std::ifstream in(dir / "fixture.json");
    return in ? nlohmann::json::parse(in, nullptr, false) : nlohmann::json();
}

inline bool stemFixtureUsable(const nlohmann::json& meta) {
    return !meta.is_discarded() && meta.is_object() && !meta.contains("unavailable");
}

// Loads the fixture (generating it if the build could not) or SKIPs the calling test with the reason.
inline const StemFixture& requireStemFixture() {
    static const StemFixture fixture = [] {
        StemFixture f;
#ifdef AVGEN_STEM_FIXTURE_DIR
        f.dir = AVGEN_STEM_FIXTURE_DIR;
#endif
        f.meta = readStemFixtureMeta(f.dir);
#ifdef AVGEN_SOURCE_DIR
        if (!stemFixtureUsable(f.meta) && !f.dir.empty()) {
            const std::filesystem::path script = std::filesystem::path(AVGEN_SOURCE_DIR) / "tools" / "make_stem_fixture.py";
            const std::filesystem::path mine = processTempDir() / "stem-fixture";
            std::error_code ec;
            std::filesystem::remove_all(mine, ec);
            const std::string command = "python3 \"" + script.string() + "\" --out \"" + mine.string() + "\"";
            if (std::system(command.c_str()) == 0 && stemFixtureUsable(readStemFixtureMeta(mine))) {
                std::filesystem::create_directories(f.dir.parent_path(), ec);
                // Another process may have put a good fixture in place meanwhile: then it is used, and never removed
                // from under a reader. Otherwise what is there is the build's "unavailable" marker (or nothing).
                if (!stemFixtureUsable(readStemFixtureMeta(f.dir))) {
                    std::filesystem::remove_all(f.dir, ec);
                    std::filesystem::rename(mine, f.dir, ec); // fails harmlessly if another process got there first
                }
                f.meta = readStemFixtureMeta(f.dir);
            } else {
                const auto marker = readStemFixtureMeta(mine);
                if (marker.is_object() && marker.contains("unavailable")) {
                    f.meta = marker; // the reason, for the SKIP
                }
            }
        }
#endif
        return f;
    }();
    if (fixture.meta.is_discarded() || !fixture.meta.is_object()) {
        SKIP("the stem fixture was not generated (" << fixture.dir.string() << "); build the avgen_stem_fixture target");
    }
    if (fixture.meta.contains("unavailable")) {
        SKIP("the stem fixture is unavailable: " << fixture.meta["unavailable"].get<std::string>());
    }
    return fixture;
}

} // namespace avgen::testsupport
