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
