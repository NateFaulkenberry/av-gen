#include "assets/asset_catalog.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace avgen;

TEST_CASE("asset catalog distinguishes built-in and project assets", "[assets][catalog]") {
    const fs::path root = fs::temp_directory_path() / "avgen_asset_catalog_test";
    fs::remove_all(root);
    fs::create_directories(root / "builtin" / "models");
    fs::create_directories(root / "project" / "assets" / "models");
    std::ofstream(root / "builtin" / "models" / "tree.glb") << "fixture";
    std::ofstream(root / "project" / "assets" / "models" / "custom.glb") << "fixture";
    std::ofstream(root / "project" / "assets" / "moon.hdr") << "fixture";

    const auto records = assets::catalogAssets({root / "builtin", root / "project"}, root / "project");
    REQUIRE(records.has_value());
    REQUIRE(records->size() == 3);
    const auto find = [&](const std::string& name) -> const assets::AssetRecord* {
        for (const auto& record : *records) {
            if (record.name == name) return &record;
        }
        return nullptr;
    };
    REQUIRE(find("tree") != nullptr);
    REQUIRE(find("custom") != nullptr);
    REQUIRE(find("moon") != nullptr);
    CHECK(find("tree")->source == assets::AssetSource::Builtin);
    CHECK(find("custom")->source == assets::AssetSource::Project);
    CHECK(find("moon")->type == "environment");
    CHECK(find("custom")->id.rfind("asset://project/", 0) == 0);
    CHECK(find("tree")->id.rfind("asset://builtin/", 0) == 0);

    const auto matches = assets::searchAssets(*records, "custom");
    REQUIRE(matches.size() == 1);
    CHECK(matches.front().id == find("custom")->id);
    fs::remove_all(root);
}

TEST_CASE("asset catalog search is case-insensitive and bounded", "[assets][catalog]") {
    const fs::path root = fs::temp_directory_path() / "avgen_asset_catalog_search";
    fs::remove_all(root);
    fs::create_directories(root / "assets");
    for (const char* name : {"Fern.glb", "Rock.glb", "Fungus.png"}) {
        std::ofstream(root / "assets" / name) << "fixture";
    }
    const auto records = assets::catalogAssets({root / "assets"}, {}, 2);
    REQUIRE(records.has_value());
    CHECK(records->size() == 2);
    const auto matches = assets::searchAssets(*records, "FERN", 1);
    CHECK(matches.size() <= 1);
    fs::remove_all(root);
}
