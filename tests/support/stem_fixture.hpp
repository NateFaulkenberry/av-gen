#pragma once

// The stem-analysis fixture (stems-multichannel M1): tools/make_stem_fixture.py, run by the build into
// AVGEN_STEM_FIXTURE_DIR (tests/CMakeLists.txt). `fixture.json` holds every event time, gain, pan, offset and
// format; the tests assert against it rather than against numbers copied into C++.
//
// When the fixture could not be generated (no Python 3, or no numpy) the directory is missing or its fixture.json
// says why, and `requireStemFixture()` SKIPs with that reason -- never a silent pass.

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>
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

// Loads the fixture or SKIPs the calling test with the reason it is unavailable.
inline const StemFixture& requireStemFixture() {
    static const StemFixture fixture = [] {
        StemFixture f;
#ifdef AVGEN_STEM_FIXTURE_DIR
        f.dir = AVGEN_STEM_FIXTURE_DIR;
#endif
        std::ifstream in(f.dir / "fixture.json");
        if (in) {
            f.meta = nlohmann::json::parse(in, nullptr, false);
        }
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
