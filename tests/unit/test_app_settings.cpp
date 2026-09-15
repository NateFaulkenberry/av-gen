#include "app/settings.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using namespace avgen;

TEST_CASE("application appearance preference round-trips", "[app][settings][ui]") {
    app::AppSettings settings;
    settings.appearance = app::AppearanceTheme::Light;
    const auto document = settings.toJson();
    REQUIRE(document.at("general").at("appearance") == "Light");
    const auto loaded = app::AppSettings::fromJson(document);
    REQUIRE(loaded.has_value());
    CHECK(loaded->appearance == app::AppearanceTheme::Light);
    CHECK(loaded->canvasRenderScale == settings.canvasRenderScale);
}

TEST_CASE("invalid application appearance is rejected safely", "[app][settings][ui]") {
    app::AppSettings settings;
    auto document = settings.toJson();
    document["general"]["appearance"] = "Solarized";
    CHECK_FALSE(app::AppSettings::fromJson(document).has_value());
    document["general"]["appearance"] = 4;
    CHECK_FALSE(app::AppSettings::fromJson(document).has_value());
}

// ---- the Auto-director's controls (ADR-225) -----------------------------------------------------
//
// `AutoDirectorSettings` lived only on `DirectorState`, which is a member of the running
// `Application` and of nothing that is written anywhere, so every control in the Auto-director
// panel reset on the next launch. The two that matter most reset to "off": `maxViewRate` and
// `maxCameraSpeed` are 0 by default, which is the setting they exist to move away from, and
// `dwellShots` resets to 1, which ADR-203 added precisely because 1 was not enough.

TEST_CASE("the Auto-director's controls survive a round trip", "[app][settings][director]") {
    app::AppSettings settings;
    // Every field the panel edits, each moved off its default, so a field that is not written is a
    // field this catches rather than one it happens to agree about.
    settings.director.mode = app::DirectorMode::EditedSequence;
    settings.director.minShotSeconds = 3.5;
    settings.director.minBuildShotSeconds = 1.25;
    settings.director.maxShotSeconds = 18.0;
    settings.director.wideFocalLength = 21.0f;
    settings.director.heroFocalLength = 85.0f;
    settings.director.maxCameraSpeed = 0.4f;
    settings.director.maxViewRate = 8.0f;
    settings.director.dwellShots = 6;
    settings.director.seed = 4242;
    REQUIRE(settings.director.validate().has_value());

    const auto document = settings.toJson();
    const auto loaded = app::AppSettings::fromJson(document);
    REQUIRE(loaded.has_value());
    CHECK(loaded->director == settings.director);
    // The other sections are untouched by the new one.
    CHECK(loaded->canvasRenderScale == settings.canvasRenderScale);
    CHECK(loaded->appearance == settings.appearance);
}

TEST_CASE("a settings file with no director section is a first run, not a fault",
          "[app][settings][director]") {
    // The compatibility this section is allowed to assume: a file written before it existed loads,
    // and the defaults are the answer. This is why the format version was not bumped for it.
    app::AppSettings settings;
    auto document = settings.toJson();
    document.erase("director");
    const auto loaded = app::AppSettings::fromJson(document);
    REQUIRE(loaded.has_value());
    CHECK(loaded->director == app::AutoDirectorSettings{});
}

TEST_CASE("an out-of-range director setting is refused rather than silently reset",
          "[app][settings][director]") {
    // The AI section's rule, for the same reason: this file is machine-written, so the only route
    // to a value outside `validate()`'s range is somebody editing it by hand, and being told which
    // field is wrong beats launching with defaults and wondering where the settings went.
    app::AppSettings settings;
    auto document = settings.toJson();
    document["director"]["dwell"] = 40;
    CHECK_FALSE(app::AppSettings::fromJson(document).has_value());
    document["director"]["dwell"] = 3;
    document["director"]["mode"] = "Improvised";
    CHECK_FALSE(app::AppSettings::fromJson(document).has_value());
    document["director"]["mode"] = "edited";
    // ...and the same document with both put right is accepted, or the two checks above would pass
    // for reasons unconnected to the fields they name.
    const auto loaded = app::AppSettings::fromJson(document);
    REQUIRE(loaded.has_value());
    CHECK(loaded->director.dwellShots == 3);
    CHECK(loaded->director.mode == app::DirectorMode::EditedSequence);
}

TEST_CASE("the Auto-director's controls survive the file, not just the document",
          "[app][settings][director]") {
    // Through `save`/`load` rather than `toJson`/`fromJson`, because the defect was never about
    // JSON: the settings reached no file at all.
    const std::filesystem::path dir = testsupport::processTempDir() / "director-settings";
    std::filesystem::create_directories(dir);
    const auto path = app::AppSettings::pathIn(dir);
    REQUIRE_FALSE(path.empty());

    app::AppSettings written;
    written.director.maxViewRate = 12.0f;
    written.director.dwellShots = 5;
    written.director.seed = 99;
    REQUIRE(written.save(path).has_value());

    const auto reopened = app::AppSettings::load(path);
    REQUIRE(reopened.has_value());
    CHECK(reopened->director.maxViewRate == 12.0f);
    CHECK(reopened->director.dwellShots == 5);
    CHECK(reopened->director.seed == 99u);
}
