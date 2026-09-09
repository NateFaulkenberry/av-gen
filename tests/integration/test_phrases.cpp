// Musical structure above the bar (ADR-041): phrase and section signals, so states escalate over
// musical time rather than on every beat.

#include "app/engine.hpp"
#include "core/time.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>

using namespace avgen;
using Catch::Matchers::WithinAbs;
namespace fs = std::filesystem;

TEST_CASE("Phrase and section signals follow the beat clock", "[integration][phrase]") {
    app::Engine engine(app::EngineMode::Offline);
    FixedStepClock clock(60.0);
    engine.update(engine.tick(clock));

    for (const char* name : {"beat.phrase", "beat.phraseCount", "beat.phrasePulse", "beat.section",
                             "beat.sectionCount"}) {
        INFO(name);
        CHECK(engine.signals().find(name).has_value());
    }
    CHECK(engine.phraseBars() == 4);
    CHECK(engine.sectionPhrases() == 4);

    // The defaults describe a 16-bar section: 4 bars a phrase, 4 phrases a section.
    engine.setPhraseBars(2);
    CHECK(engine.phraseBars() == 2);
    engine.setPhraseBars(0); // clamped, never zero
    CHECK(engine.phraseBars() == 1);
    engine.setPhraseBars(4);

    const auto phraseId = *engine.signals().find("beat.phrase");
    const auto phraseCountId = *engine.signals().find("beat.phraseCount");
    const auto sectionId = *engine.signals().find("beat.section");
    // Without a tempo the clock does not advance, so the phases stay put and stay in range.
    for (int i = 0; i < 30; ++i) {
        engine.update(engine.tick(clock));
        const float phase = engine.signals().value(phraseId);
        const float section = engine.signals().value(sectionId);
        CHECK(phase >= 0.0f);
        CHECK(phase <= 1.0f);
        CHECK(section >= 0.0f);
        CHECK(section <= 1.0f);
        CHECK(engine.signals().value(phraseCountId) >= 0.0f);
    }
}

TEST_CASE("The musical structure is saved with the project", "[integration][phrase]") {
    const auto dir = fs::temp_directory_path() / "avgen_phrase_project";
    fs::create_directories(dir);
    app::Engine engine(app::EngineMode::Offline);
    FixedStepClock clock(60.0);
    engine.update(engine.tick(clock));
    engine.setPhraseBars(8);
    engine.setSectionPhrases(2);
    const auto project = dir / "piece.json";
    REQUIRE(engine.saveProject(project).has_value());

    app::Engine other(app::EngineMode::Offline);
    REQUIRE(other.loadProject(project).has_value());
    CHECK(other.phraseBars() == 8);
    CHECK(other.sectionPhrases() == 2);
    fs::remove_all(dir);
}
