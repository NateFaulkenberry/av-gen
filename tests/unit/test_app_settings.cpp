#include "app/settings.hpp"

#include <catch2/catch_test_macros.hpp>

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

