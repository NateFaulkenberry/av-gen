#include "app/settings.hpp"

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


// ---- the output preview's view state (ADR-225, ADR-246) -----------------------------------------
//
// ADR-225: a setting the application does not keep is not a setting. The view mode is the one that
// matters most here -- a person who works in Output Frame mode and finds the editor back in
// Workspace every launch has a toggle, not a mode -- but every toggle on the toolbar is checked,
// because the way this goes wrong is one field quietly missing from `toJson` and nothing saying so.
//
// The round trip is asserted through the *file*, not only through the JSON object, because the two
// have failed apart before in this project: a document that serialises correctly and is then
// written with a save path nobody set is still a setting nobody keeps.

TEST_CASE("the output preview's view state survives a save and a load", "[app][settings][preview]") {
    app::AppSettings settings;
    settings.preview.mode = ui::PreviewViewMode::OutputFrame;
    settings.preview.outside = ui::OutsideFrame::Hide;
    settings.preview.quality = ui::PreviewQuality::Native;
    settings.preview.zoom = ui::PreviewZoom{false, 0.5f};
    settings.preview.guides.safeAreas = true;
    settings.preview.guides.thirds = true;
    settings.preview.guides.centreCross = true;
    settings.preview.guides.frameBorder = false;
    settings.preview.guides.safe.actionFraction = 0.90f;
    settings.preview.guides.safe.titleFraction = 0.80f;
    settings.preview.toolbar = false;

    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "avgen-output-preview-settings";
    std::filesystem::remove_all(dir);
    const std::filesystem::path file = app::AppSettings::pathIn(dir);
    REQUIRE(settings.save(file).has_value());
    REQUIRE(std::filesystem::exists(file));

    const auto loaded = app::AppSettings::load(file);
    REQUIRE(loaded.has_value());
    CHECK(loaded->preview.mode == ui::PreviewViewMode::OutputFrame);
    CHECK(loaded->preview.outside == ui::OutsideFrame::Hide);
    CHECK(loaded->preview.quality == ui::PreviewQuality::Native);
    CHECK_FALSE(loaded->preview.zoom.fit);
    CHECK(loaded->preview.zoom.scale == 0.5f);
    CHECK(loaded->preview.guides.safeAreas);
    CHECK(loaded->preview.guides.thirds);
    CHECK(loaded->preview.guides.centreCross);
    CHECK_FALSE(loaded->preview.guides.frameBorder);
    CHECK(loaded->preview.guides.safe.actionFraction == 0.90f);
    CHECK(loaded->preview.guides.safe.titleFraction == 0.80f);
    CHECK_FALSE(loaded->preview.toolbar);
    std::filesystem::remove_all(dir);
}

TEST_CASE("a settings file from before the output preview still opens", "[app][settings][preview]") {
    // The one that decides whether this change can ship: everybody's existing settings.json has no
    // "outputPreview" block at all, and it has to load onto the defaults rather than be refused.
    app::AppSettings settings;
    auto document = settings.toJson();
    document.erase("outputPreview");
    const auto loaded = app::AppSettings::fromJson(document);
    REQUIRE(loaded.has_value());
    // And the default is the editor everyone already has: the flexible canvas, unchanged.
    CHECK(loaded->preview.mode == ui::PreviewViewMode::Workspace);
    CHECK(loaded->preview.zoom.fit);
}

TEST_CASE("an output preview state that is not a state is refused, not repaired",
          "[app][settings][preview]") {
    app::AppSettings settings;
    auto document = settings.toJson();

    document["outputPreview"]["mode"] = "cinemascope";
    CHECK_FALSE(app::AppSettings::fromJson(document).has_value());
    document["outputPreview"]["mode"] = 3;
    CHECK_FALSE(app::AppSettings::fromJson(document).has_value());

    document = settings.toJson();
    document["outputPreview"]["quality"] = "cinematic";
    CHECK_FALSE(app::AppSettings::fromJson(document).has_value());

    document = settings.toJson();
    document["outputPreview"]["outside"] = "blur";
    CHECK_FALSE(app::AppSettings::fromJson(document).has_value());

    // A title-safe area outside the action-safe area draws two boxes in the wrong order and looks
    // entirely plausible on screen. Refused loudly rather than clamped quietly.
    document = settings.toJson();
    document["outputPreview"]["titleSafe"] = 0.99f;
    const auto inverted = app::AppSettings::fromJson(document);
    CHECK_FALSE(inverted.has_value());

    document = settings.toJson();
    document["outputPreview"]["zoomFit"] = false;
    document["outputPreview"]["zoomScale"] = 0.0f;
    CHECK_FALSE(app::AppSettings::fromJson(document).has_value());
}

TEST_CASE("the pan and the fullscreen state are deliberately not kept", "[app][settings][preview]") {
    // Not an oversight, and worth a case so it stays deliberate. A pan only means anything against
    // the canvas size it was made at, so restoring one onto a differently sized window puts the
    // frame somewhere nobody asked for; and a session that exits in fullscreen preview must not
    // reopen with every panel closed and no memory of which ones they were.
    app::AppSettings settings;
    settings.preview.panX = 120.0f;
    settings.preview.panY = -40.0f;
    settings.preview.fullscreen = true;
    const auto document = settings.toJson();
    REQUIRE(document.contains("outputPreview"));
    CHECK_FALSE(document.at("outputPreview").contains("panX"));
    CHECK_FALSE(document.at("outputPreview").contains("fullscreen"));
    const auto loaded = app::AppSettings::fromJson(document);
    REQUIRE(loaded.has_value());
    CHECK(loaded->preview.panX == 0.0f);
    CHECK_FALSE(loaded->preview.fullscreen);
}

TEST_CASE("Suspending the viewport during a render is remembered", "[settings][viewport]") {
    // ADR-364 / ADR-225. The default is on, so the value that has to survive is OFF -- a writer
    // that emitted nothing would pass a test that saved the default and read it back.
    app::AppSettings settings;
    REQUIRE(settings.suspendViewportDuringRender);   // the premise; if this flips, so must the test
    settings.suspendViewportDuringRender = false;

    const auto first = app::AppSettings::fromJson(settings.toJson());
    REQUIRE(first.has_value());
    CHECK_FALSE(first->suspendViewportDuringRender);

    // Twice, per ADR-350: a writer that echoes what it parsed can still lose it on the way back.
    const auto second = app::AppSettings::fromJson(first->toJson());
    REQUIRE(second.has_value());
    CHECK_FALSE(second->suspendViewportDuringRender);

    // A settings file written before the option existed keeps the default rather than reading a
    // missing key as false, which would silently reverse the behaviour for everyone who upgrades.
    auto older = settings.toJson();
    older["general"].erase("suspendViewportDuringRender");
    const auto upgraded = app::AppSettings::fromJson(older);
    REQUIRE(upgraded.has_value());
    CHECK(upgraded->suspendViewportDuringRender);
}

// ---- the adaptive render scale (ADR-480) --------------------------------------------------------
//
// ADR-225 again: a setting the application does not keep is not a setting. This one is a checkbox
// and a slider that a person reaches for precisely when the editor is unusable, so "it was off
// again after a restart" is the worst possible failure for it.
TEST_CASE("the adaptive render scale round-trips, and its budget is clamped rather than refused",
          "[app][settings][ui][resolution]") {
    app::AppSettings settings;
    // The default is on, which is the whole point of ADR-480: the manual lever it replaces was a
    // slider nobody ever moved.
    CHECK(settings.adaptiveCanvasScale);

    settings.adaptiveCanvasScale = false;
    settings.adaptiveCanvasBudgetMs = 33.3;
    const auto document = settings.toJson();
    REQUIRE(document.at("general").at("adaptiveCanvasScale") == false);
    const auto loaded = app::AppSettings::fromJson(document);
    REQUIRE(loaded.has_value());
    CHECK_FALSE(loaded->adaptiveCanvasScale);
    CHECK(loaded->adaptiveCanvasBudgetMs == 33.3);

    // A settings file from a future build, or a hand-edited one. A budget of zero would pin the
    // ladder at its floor for ever and a budget of an hour would make the controller dead weight;
    // neither is worth refusing the whole file over, so both are clamped into the range.
    {
        auto doc = document;
        doc["general"]["adaptiveCanvasBudgetMs"] = 0.0;
        const auto out = app::AppSettings::fromJson(doc);
        REQUIRE(out.has_value());
        CHECK(out->adaptiveCanvasBudgetMs == 4.0);
    }
    {
        auto doc = document;
        doc["general"]["adaptiveCanvasBudgetMs"] = 5000.0;
        const auto out = app::AppSettings::fromJson(doc);
        REQUIRE(out.has_value());
        CHECK(out->adaptiveCanvasBudgetMs == 200.0);
    }
    // A file written before this existed: the defaults are the answer, not a refusal.
    {
        auto doc = document;
        doc["general"].erase("adaptiveCanvasScale");
        doc["general"].erase("adaptiveCanvasBudgetMs");
        const auto out = app::AppSettings::fromJson(doc);
        REQUIRE(out.has_value());
        CHECK(out->adaptiveCanvasScale);
        CHECK(out->adaptiveCanvasBudgetMs == app::AppSettings{}.adaptiveCanvasBudgetMs);
    }
}
