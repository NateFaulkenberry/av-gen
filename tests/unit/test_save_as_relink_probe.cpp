// PROBE for the reported defect: File -> Save Project As into a different folder, then reopening
// the saved project warns that its scene file is missing.
//
// `Engine::saveProject` does not copy assets; it rewrites references relative to the new project's
// folder, and `relativeTo` deliberately allows ".." so a project can sit beside its assets. So the
// reference SHOULD still resolve after a Save As. This asks whether it does.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <unistd.h>

#include "app/engine.hpp"
#include "support/gltf_fixture.hpp"

namespace fs = std::filesystem;
using namespace avgen;

// The original's bytes, so "unchanged" is the file itself rather than a hash of it.
static std::string readAll(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

TEST_CASE("probe: Save As into another folder keeps the scene reference resolvable", "[probe][saveas]") {
    const auto root = fs::temp_directory_path() /
                      ("avgen_saveas_probe_" + std::to_string(static_cast<long long>(::getpid())));
    fs::remove_all(root);
    const auto original = root / "original";
    const auto elsewhere = root / "elsewhere";
    fs::create_directories(original);
    fs::create_directories(elsewhere);

    const auto tmp = testsupport::writeTriangleGlb("saveas_probe");
    fs::copy_file(tmp, original / "tri.glb", fs::copy_options::overwrite_existing);
    fs::remove(tmp);

    const auto scenePath = original / "world.scene.json";
    const auto firstProject = original / "world.json";
    const auto secondProject = elsewhere / "copy.json";

    {
        app::Engine engine{app::EngineMode::Offline};
        engine.newComposition();
        REQUIRE(engine.saveComposition(scenePath).has_value());
        REQUIRE(engine.saveProject(firstProject).has_value());
        // The Save As: same session, a path in a different directory.
        REQUIRE(engine.saveProject(secondProject).has_value());
    }

    REQUIRE(fs::is_regular_file(secondProject));
    std::ifstream in(secondProject);
    REQUIRE(in.good());
    const nlohmann::json doc = nlohmann::json::parse(in);
    REQUIRE(doc.contains("assets"));
    REQUIRE(doc["assets"].contains("scene"));
    const auto& sceneRef = doc["assets"]["scene"];
    INFO("scene ref written by Save As: " << sceneRef.dump(2));
    REQUIRE(sceneRef.contains("path"));

    const auto& ref = sceneRef["path"];
    const std::string stored = ref.is_string() ? ref.get<std::string>() : ref.at("path").get<std::string>();
    INFO("stored path: " << stored);
    const fs::path resolved = fs::path(stored).is_absolute()
                                  ? fs::path(stored)
                                  : (secondProject.parent_path() / stored).lexically_normal();
    INFO("resolves to: " << resolved.string());
    CHECK(fs::is_regular_file(resolved));

    // And the project must actually open from its new home without losing the scene.
    {
        app::Engine engine{app::EngineMode::Offline};
        auto loaded = engine.loadProject(secondProject);
        INFO("loadProject: " << (loaded ? std::string("ok") : loaded.error().message));
        CHECK(loaded.has_value());
        CHECK(engine.composition() != nullptr);
    }
    fs::remove_all(root);
}

// The reported case exactly: glowmere-valley-2-multicam, Save As to a folder outside the repo.
TEST_CASE("probe: Save As of the multicam project to an outside folder", "[probe][saveas]") {
    const fs::path repo(AVGEN_SOURCE_DIR);
    const auto source = repo / "examples" / "world" / "glowmere-valley-2-multicam.json";
    if (!fs::is_regular_file(source)) {
        SKIP("multicam project not present");
    }
    const auto outside = fs::temp_directory_path() /
                         ("avgen_saveas_outside_" + std::to_string(static_cast<long long>(::getpid())));
    fs::remove_all(outside);
    fs::create_directories(outside);
    const auto copy = outside / "copy.json";

    {
        app::Engine engine{app::EngineMode::Offline};
        auto loaded = engine.loadProject(source);
        INFO("loadProject(source): " << (loaded ? std::string("ok") : loaded.error().message));
        REQUIRE(loaded.has_value());
        REQUIRE(engine.composition() != nullptr);
        auto saved = engine.saveProject(copy);
        INFO("saveProject(copy): " << (saved ? std::string("ok") : saved.error().message));
        REQUIRE(saved.has_value());
    }

    std::ifstream in(copy);
    REQUIRE(in.good());
    const nlohmann::json doc = nlohmann::json::parse(in);
    const auto& sceneRef = doc.at("assets").at("scene");
    INFO("scene ref: " << sceneRef.dump(2));
    REQUIRE(sceneRef.contains("path"));
    const auto& ref = sceneRef["path"];
    const std::string stored = ref.is_string() ? ref.get<std::string>() : ref.at("path").get<std::string>();
    const fs::path resolved = fs::path(stored).is_absolute()
                                  ? fs::path(stored)
                                  : (copy.parent_path() / stored).lexically_normal();
    INFO("stored: " << stored << "\nresolves to: " << resolved.string());
    CHECK(fs::is_regular_file(resolved));

    {
        app::Engine engine{app::EngineMode::Offline};
        auto reopened = engine.loadProject(copy);
        INFO("loadProject(copy): " << (reopened ? std::string("ok") : reopened.error().message));
        CHECK(reopened.has_value());
        CHECK(engine.composition() != nullptr);
        // THE POINT: a project whose scene is missing still "opens". The warnings are how it says
        // so, and a probe that only checks the Result cannot see the defect being reported.
        std::string joined;
        for (const auto& w : engine.projectWarnings()) {
            joined += "\n  - " + w;
        }
        INFO("warnings after reopen:" << (joined.empty() ? std::string(" (none)") : joined));
        CHECK(engine.projectWarnings().empty());
    }
    fs::remove_all(outside);
}

// THE REPORTED DEFECT, as an assertion. A project saved elsewhere kept pointing at the scene file it
// was opened from, so editing and saving through the copy wrote back over a scene other projects
// still use. Copying the files is not enough on its own: if the session keeps the paths it was
// opened with, Save As is a snapshot rather than a move and the defect survives its own fix.
TEST_CASE("Save As takes its own copies and stops writing back to the original", "[saveas]") {
    const fs::path repo(AVGEN_SOURCE_DIR);
    const auto source = repo / "examples" / "world" / "glowmere-valley-2-multicam.json";
    const auto originalScene = repo / "examples" / "world" / "glowmere-valley-2-multicam.scene.json";
    if (!fs::is_regular_file(source) || !fs::is_regular_file(originalScene)) {
        SKIP("multicam project not present");
    }
    const std::string before = readAll(originalScene);
    REQUIRE(!before.empty());

    const auto outside = fs::temp_directory_path() /
                         ("avgen_saveas_copies_" + std::to_string(static_cast<long long>(::getpid())));
    fs::remove_all(outside);
    fs::create_directories(outside);
    const auto copy = outside / "rebuild.json";

    {
        app::Engine engine{app::EngineMode::Offline};
        REQUIRE(engine.loadProject(source).has_value());
        REQUIRE(engine.composition() != nullptr);
        auto saved = engine.saveProjectAsCopy(copy);
        INFO("saveProjectAsCopy: " << (saved ? std::string("ok") : saved.error().message));
        REQUIRE(saved.has_value());

        // The session belongs to the copy now, not to where it came from.
        CHECK(fs::absolute(engine.projectPath()) == fs::absolute(copy));
        INFO("composition path after Save As: " << engine.compositionPath().string());
        CHECK(fs::absolute(engine.compositionPath()) != fs::absolute(originalScene));

        // And saving the scene through the copy must not touch the original.
        REQUIRE(engine.saveComposition(engine.compositionPath()).has_value());
    }

    const std::string after = readAll(originalScene);
    CHECK(before == after); // the shared scene is exactly as it was, byte for byte

    // EVERY asset the bundled scene names is present. This is the assertion that would have caught
    // all four copier gaps -- light rig, entity profile, procedural source, and the eighteen
    // `scatter[].asset` meshes that are every tree, rock and plant in the scene. The last of those
    // was found by a person looking at a render, because a scene with no trees loads cleanly, warns
    // about nothing, and is perfectly valid as far as the engine is concerned.
    {
        const auto bundled = copy.parent_path() / (copy.stem().string() + "_assets") /
                             "glowmere-valley-2-multicam.scene.json";
        REQUIRE(fs::is_regular_file(bundled));
        std::ifstream sin(bundled);
        const nlohmann::json sd = nlohmann::json::parse(sin);
        const auto sbase = bundled.parent_path();
        int refs = 0, absent = 0;
        std::string firstAbsent;
        std::function<void(const nlohmann::json&)> walk = [&](const nlohmann::json& v) {
            if (v.is_array()) {
                for (const auto& x : v) walk(x);
                return;
            }
            if (!v.is_object()) return;
            for (const auto& [k, child] : v.items()) {
                if (k == "asset" && child.is_string()) {
                    ++refs;
                    const auto r = (sbase / child.get<std::string>()).lexically_normal();
                    if (!fs::exists(r)) { ++absent; if (firstAbsent.empty()) firstAbsent = child.get<std::string>(); }
                } else {
                    walk(child);
                }
            }
        };
        walk(sd);
        INFO("bundled scene: " << refs << " asset refs, " << absent << " absent"
             << (firstAbsent.empty() ? std::string{} : (", first: " + firstAbsent)));
        CHECK(refs > 30);      // or "none absent" is satisfied by a scene that names nothing
        CHECK(absent == 0);
    }

    // Every reference the copy names lives under the copy's own folder.
    std::ifstream in(copy);
    REQUIRE(in.good());
    const nlohmann::json doc = nlohmann::json::parse(in);
    const auto& ref = doc.at("assets").at("scene").at("path");
    const std::string stored = ref.is_string() ? ref.get<std::string>() : ref.at("path").get<std::string>();
    INFO("scene reference in the copy: " << stored);
    CHECK(stored.find("..") == std::string::npos);
    CHECK(!fs::path(stored).is_absolute());
    const auto resolved = (copy.parent_path() / stored).lexically_normal();
    CHECK(fs::is_regular_file(resolved));

    // Opening the copy is clean: no relink, no missing asset.
    {
        app::Engine engine{app::EngineMode::Offline};
        auto reopened = engine.loadProject(copy);
        INFO("loadProject(copy): " << (reopened ? std::string("ok") : reopened.error().message));
        CHECK(reopened.has_value());
        CHECK(engine.composition() != nullptr);
        std::string joined;
        for (const auto& w : engine.projectWarnings()) {
            joined += "\n  - " + w;
        }
        INFO("warnings:" << (joined.empty() ? std::string(" (none)") : joined));
        // Save As is responsible for every reference resolving, not for every reaction binding.
        // The one warning this copy carries is `entity 'visitor': reaction 'parts/Light/...'`,
        // which never resolved in the ORIGINAL either: the profile targets parts by NAME and
        // `visitor` is a procedural node whose parts register as `parts/1`, `parts/2`, `parts/3`.
        // There is no `parts/Light` parameter in either project -- checked across the union of
        // both parameter sets. The bundle merely makes it visible, because a project saved from a
        // live session carries 488 more parameters than the on-disk original and the binding is
        // actually attempted. Asserting it away would hide a real content defect; asserting on it
        // here would make this test fail for something it does not own. So this asserts what Save
        // As IS responsible for: nothing missing, nothing unresolvable because of a path.
        for (const auto& w : engine.projectWarnings()) {
            INFO("warning: " << w);
            CHECK(w.find("cannot open") == std::string::npos);
            CHECK(w.find("missing") == std::string::npos);
            CHECK(w.find("relinked") == std::string::npos);
        }
    }
    fs::remove_all(outside);
}
