// Asset browser (ADR-031 follow-up): classification, scanning, thumbnails and filtering.

#include "app/asset_browser.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using namespace avgen;
namespace fs = std::filesystem;

namespace {
void write(const fs::path& p, const std::string& body) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << body;
}
} // namespace

TEST_CASE("Files are classified by extension and JSON format", "[assetbrowser]") {
    const auto dir = fs::temp_directory_path() / "avgen_assets_kind";
    fs::remove_all(dir);
    write(dir / "a.json", R"({"format":"avgen-project","name":"A"})");
    write(dir / "b.json", R"({"format":"avgen-scene"})");
    write(dir / "c.json", R"({"format":"avgen-graph"})");
    write(dir / "d.json", R"({"other":1})");
    write(dir / "e.json", "not json at all {");
    write(dir / "f.glb", "x");
    write(dir / "g.hdr", "x");
    write(dir / "h.wgsl", "x");
    write(dir / "i.wav", "x");
    write(dir / "j.txt", "x");
    CHECK(app::assetKindForFile(dir / "a.json") == app::AssetKind::Project);
    CHECK(app::assetKindForFile(dir / "b.json") == app::AssetKind::Scene);
    CHECK(app::assetKindForFile(dir / "c.json") == app::AssetKind::Graph);
    CHECK(app::assetKindForFile(dir / "d.json") == app::AssetKind::Unknown);
    CHECK(app::assetKindForFile(dir / "e.json") == app::AssetKind::Unknown);
    CHECK(app::assetKindForFile(dir / "f.glb") == app::AssetKind::Model);
    CHECK(app::assetKindForFile(dir / "g.hdr") == app::AssetKind::Environment);
    CHECK(app::assetKindForFile(dir / "h.wgsl") == app::AssetKind::Shader);
    CHECK(app::assetKindForFile(dir / "i.wav") == app::AssetKind::Audio);
    CHECK(app::assetKindForFile(dir / "j.txt") == app::AssetKind::Unknown);
    CHECK(std::string(app::assetKindName(app::AssetKind::Graph)) == "graph");
    fs::remove_all(dir);
}

TEST_CASE("Scanning collects metadata, thumbnails and sorts by category then name", "[assetbrowser]") {
    const auto dir = fs::temp_directory_path() / "avgen_assets_scan";
    fs::remove_all(dir);
    write(dir / "worlds" / "temple.json",
          R"({"format":"avgen-project","name":"The Temple","category":"Showcase","description":"columns"})");
    write(dir / "worlds" / "helix.json", R"({"format":"avgen-project","name":"The Helix","category":"Showcase"})");
    write(dir / "graphs" / "tower.json", R"({"format":"avgen-graph","name":"Tower","category":"Generators"})");
    write(dir / "models" / "thing.glb", "x");
    write(app::thumbnailPathFor(dir / "worlds" / "temple.json"), "png");
    write(dir / ".hidden" / "skip.json", R"({"format":"avgen-project","name":"hidden"})");

    const auto assets = app::scanAssets({dir});
    REQUIRE(assets.size() == 4);
    CHECK(assets[0].category == "Generators"); // sorted by category
    CHECK(assets[0].name == "Tower");
    CHECK(assets[1].name == "The Helix");
    CHECK(assets[2].name == "The Temple");
    CHECK(assets[2].description == "columns");
    CHECK(assets[2].thumbnail == app::thumbnailPathFor(dir / "worlds" / "temple.json"));
    CHECK(assets[1].thumbnail.empty());
    CHECK(assets[3].kind == app::AssetKind::Model);
    CHECK(assets[3].category == "models"); // directory name when the file has no metadata
    CHECK(assets[3].bytes == 1);
    for (const auto& a : assets) {
        CHECK(a.name != "hidden"); // dot directories are skipped
    }

    const auto projects = app::filterAssets(assets, app::AssetKind::Project, "");
    CHECK(projects.size() == 2);
    const auto search = app::filterAssets(assets, app::AssetKind::Unknown, "TEMP");
    REQUIRE(search.size() == 1);
    CHECK(search[0]->name == "The Temple");
    CHECK(app::filterAssets(assets, app::AssetKind::Unknown, "nothing here").empty());
    CHECK(app::scanAssets({dir / "missing"}).empty());
    fs::remove_all(dir);
}

TEST_CASE("The repository's own examples and graphs are catalogued", "[assetbrowser]") {
    const fs::path examples = fs::path(AVGEN_SOURCE_DIR) / "examples";
    if (!fs::exists(examples)) {
        return;
    }
    const auto assets = app::scanAssets({examples});
    CHECK(!assets.empty());
    std::size_t projects = 0;
    std::size_t scenes = 0;
    for (const auto& a : assets) {
        projects += a.kind == app::AssetKind::Project ? 1 : 0;
        scenes += a.kind == app::AssetKind::Scene ? 1 : 0;
    }
    CHECK(projects >= 5);
    CHECK(scenes >= 5);
}
