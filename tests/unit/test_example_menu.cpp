// File > Live Projects and File > Examples (2026-10-07): which index entries each menu shows, under which headings.
// The menus themselves are ImGui; the decisions are app::exampleSections, tested here without a window.

#include "app/examples.hpp"
#include "app/live_scenes.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

using namespace avgen;

namespace {

app::ExampleInfo example(std::string name, std::string category, bool live) {
    app::ExampleInfo e{std::move(name), "d", std::move(category), std::filesystem::path("/ex") / "x.json"};
    e.live = live;
    return e;
}

std::vector<std::string> names(const std::vector<app::ExampleInfo>& all, const app::ExampleSection& s) {
    std::vector<std::string> out;
    for (const std::size_t i : s.entries) {
        out.push_back(all.at(i).name);
    }
    return out;
}

} // namespace

TEST_CASE("Live entries get their own menu; a recurring category is one heading", "[examples][live]") {
    const std::vector<app::ExampleInfo> all{
        example("Bench", "Benchmark", false),       example("Forge Live", "Astral Forge", true),
        example("Alien", "Lab", false),             example("VFX - Lake", "Sonic VFX", true),
        example("Temple", "Showcase", false),       example("Garden", "Lab", false), // "Lab" again, later
        example("VFX - Storm", "Sonic VFX", true),  example("Live", "Sonic Garden", true)};

    const auto live = app::exampleSections(all, true);
    REQUIRE(live.size() == 3);
    CHECK(live[0].category == "Astral Forge");
    CHECK(live[1].category == "Sonic VFX");
    CHECK(names(all, live[1]) == std::vector<std::string>{"VFX - Lake", "VFX - Storm"});
    CHECK(live[2].category == "Sonic Garden");

    const auto other = app::exampleSections(all, false);
    REQUIRE(other.size() == 3); // Benchmark, Lab, Showcase: "Lab" is not headed twice
    CHECK(other[0].category == "Benchmark");
    CHECK(other[1].category == "Lab");
    CHECK(names(all, other[1]) == std::vector<std::string>{"Alien", "Garden"});
    CHECK(other[2].category == "Showcase");

    // Every entry is in exactly one menu.
    std::size_t total = 0;
    for (const auto* sections : {&live, &other}) {
        for (const auto& s : *sections) {
            total += s.entries.size();
        }
    }
    CHECK(total == all.size());
    CHECK(app::exampleSections({}, true).empty()); // the menu is disabled, not empty
}

TEST_CASE("The index's 'live' must be a boolean", "[examples][live]") {
    const auto dir = std::filesystem::temp_directory_path() / "avgen-example-menu-test";
    std::filesystem::create_directories(dir);
    const auto index = dir / "index.json";
    std::ofstream(index) << R"({"examples":[{"name":"A","category":"C","description":"d","project":"a.json","live":"yes"}]})";
    CHECK_FALSE(app::loadExampleIndex(index).has_value());
    std::ofstream(index) << R"({"examples":[{"name":"A","category":"C","description":"d","project":"a.json","live":true},
                                            {"name":"B","category":"C","description":"d","project":"b.json"}]})";
    const auto ok = app::loadExampleIndex(index);
    REQUIRE(ok.has_value());
    REQUIRE(ok->size() == 2);
    CHECK((*ok)[0].live);
    CHECK_FALSE((*ok)[1].live);
    std::filesystem::remove_all(dir);
}

// The real index: Live Projects leads with the live families, Examples keeps everything else, and moving the live
// projects into their own menu did not change what the live scene switcher (ADR-1063/1074) steps through, because the
// switcher's sets are categories and the live projects kept theirs.
TEST_CASE("The real index: Live Projects and the switcher's sets", "[examples][live]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    const auto all = app::loadExampleIndex(std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "index.json");
    REQUIRE(all.has_value());

    const auto live = app::exampleSections(*all, true);
    std::set<std::string> liveNames;
    std::set<std::string> liveCategories;
    for (const auto& s : live) {
        liveCategories.insert(s.category);
        for (const std::size_t i : s.entries) {
            liveNames.insert((*all)[i].name);
        }
    }
    for (const char* name : {"Astral Forge Live", "Digital Mosh Live", "Sonic Live", "Sonic Abstract - Glitch Signal",
                             "Sonic VFX - Event Horizon"}) {
        INFO(name);
        CHECK(liveNames.contains(name));
    }
    for (const char* category : {"Sonic VFX", "Sonic Abstract"}) {
        INFO(category);
        CHECK(liveCategories.contains(category));
    }
    // ... and none of them is also under Examples.
    for (const auto& s : app::exampleSections(*all, false)) {
        INFO(s.category);
        CHECK_FALSE(liveCategories.contains(s.category));
        for (const std::size_t i : s.entries) {
            CHECK_FALSE((*all)[i].live);
        }
    }

    // The switcher: Sonic Live then the sixteen Sonic VFX scenes, and the nine Sonic Abstract scenes, all live.
    const auto vfx = app::liveSceneSet(*all, "Sonic VFX");
    REQUIRE(vfx.size() == 17);
    CHECK(vfx.front().name == "Sonic Live");
    const auto abstract = app::liveSceneSet(*all, "Sonic Abstract");
    CHECK(abstract.size() == 9);
    for (const auto* set : {&vfx, &abstract}) {
        for (const auto& e : *set) {
            INFO(e.name);
            CHECK(e.live);
        }
    }
#endif
}
