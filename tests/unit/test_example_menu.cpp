// File > Examples > Live and Offline (2026-10-07; one Examples menu with the two submenus since 2026-10-14): which
// index entries each submenu shows, under which headings, and whether an entry can open on this machine. The menus
// themselves are ImGui; the decisions are app::exampleSections and app::exampleStatus, tested here without a window.

#include "app/examples.hpp"
#include "app/live_scenes.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
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

// Nothing is hidden: an entry whose files are missing is listed, greyed, with what is missing and how to make it.
TEST_CASE("An example whose files are missing says so, and how to make them", "[examples]") {
    const auto dir = std::filesystem::temp_directory_path() / "avgen_example_status_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    // A project that names a scene and a song, neither of which exists yet.
    std::ofstream(dir / "p.json") << R"({"format":"avgen-project","assets":{
        "scene":{"kind":"composition","path":{"path":"p.scene.json"}},"audio":{"path":"song.wav"}}})";
    std::ofstream(dir / "index.json") << R"({"examples":[
        {"name":"Gen","category":"C","description":"d","project":"p.json","generate":"python3 make.py"},
        {"name":"Gone","category":"C","description":"d","project":"absent.json"}]})";
    const auto all = app::loadExampleIndex(dir / "index.json");
    REQUIRE(all.has_value());
    REQUIRE(all->size() == 2);
    const app::ExampleInfo& gen = (*all)[0];
    CHECK(gen.generate == "python3 make.py");
    REQUIRE(gen.required.size() == 1);
    CHECK(gen.required[0] == (dir / "p.scene.json").lexically_normal());
    CHECK(gen.audio == (dir / "song.wav").lexically_normal());

    // The scene is missing: it cannot open, and the label and tooltip say why and how to fix it.
    auto status = app::exampleStatus(gen);
    CHECK(status.readiness == app::ExampleReadiness::Unavailable);
    CHECK(app::exampleLabel(gen, status) == "Gen  (needs generating)");
    const std::string tip = app::exampleTooltip(gen, status);
    CHECK(tip.find("p.scene.json") != std::string::npos);
    CHECK(tip.find("song.wav") != std::string::npos);
    CHECK(tip.find("python3 make.py") != std::string::npos);

    // The generator has run (no restart): the scene opens, but without its song.
    std::ofstream(dir / "p.scene.json") << "{}";
    status = app::exampleStatus(gen);
    CHECK(status.readiness == app::ExampleReadiness::NoAudio);
    CHECK(app::exampleLabel(gen, status) == "Gen  (no audio)");
    CHECK(app::exampleTooltip(gen, status).find("python3 make.py") == std::string::npos);

    std::ofstream(dir / "song.wav") << "x";
    status = app::exampleStatus(gen);
    CHECK(status.readiness == app::ExampleReadiness::Ready);
    CHECK(status.missing.empty());
    CHECK(app::exampleLabel(gen, status) == "Gen");
    CHECK(app::exampleTooltip(gen, status) == "d");

    // A missing file with no generator is a broken entry, and is labelled as such rather than "needs generating".
    const auto gone = app::exampleStatus((*all)[1]);
    CHECK(gone.readiness == app::ExampleReadiness::Unavailable);
    CHECK(app::exampleLabel((*all)[1], gone) == "Gone  (missing files)");

    // `generate` is a command or nothing.
    std::ofstream(dir / "index.json") << R"({"examples":[{"name":"A","category":"C","description":"d","project":"p.json","generate":1}]})";
    CHECK_FALSE(app::loadExampleIndex(dir / "index.json").has_value());
    std::filesystem::remove_all(dir);
}

// "~/..." in a project is the user's home, exactly as the project loader reads it (app/project_paths.hpp).
TEST_CASE("An example's song under ~ is looked for in the home folder", "[examples]") {
    const char* home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') {
        SKIP("no HOME");
    }
    const auto dir = std::filesystem::temp_directory_path() / "avgen_example_home_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "p.json") << R"({"format":"avgen-project","assets":{"audio":{"path":"~/x/song.wav"}}})";
    std::ofstream(dir / "index.json") << R"({"examples":[{"name":"A","category":"C","description":"d","project":"p.json"}]})";
    const auto all = app::loadExampleIndex(dir / "index.json");
    REQUIRE(all.has_value());
    CHECK((*all)[0].audio == (std::filesystem::path(home) / "x" / "song.wav").lexically_normal());
    std::filesystem::remove_all(dir);
}
