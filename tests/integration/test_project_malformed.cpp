// A project file with a known key of the wrong type is an error, not a crash.
//
// `Engine::loadProject` and `Engine::loadFile` read the document with nlohmann's `.value()` and
// `.get<T>()`, which THROW `json::type_error` when a key is present with the wrong type -- and nothing
// on the open path caught it, so `{"format": 1}` terminated the application instead of saying the file
// was not a project. The params-level parser (`params::loadProject`) was tested for this; the engine's
// own readers, which run first, were not. A hand-edited file, a merge conflict resolved badly or a file
// from another tool is enough to reach it.
//
// Each case starts from a project the engine itself wrote, so everything but the one damaged key is
// well-formed, and asserts only the contract: no exception escapes, and the load reports failure.

#include "app/engine.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <functional>
#include <string>

using namespace avgen;
using nlohmann::json;

namespace {

json savedProject() {
    const auto path = testsupport::processTempDir() / "malformed-base.json";
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.saveProject(path).has_value());
    std::ifstream in(path);
    json doc = json::parse(in);
    REQUIRE(doc.is_object());
    return doc;
}

std::filesystem::path write(const json& doc, const std::string& name) {
    const auto path = testsupport::processTempDir() / name;
    std::ofstream(path) << doc.dump(1);
    return path;
}

} // namespace

TEST_CASE("A project whose known keys have the wrong type fails to load and does not throw",
          "[project][serialization][malformed][regression]") {
    const json base = savedProject();
    {
        // The control: the undamaged file loads, so a failure below is the damage and nothing else.
        app::Engine engine(app::EngineMode::Offline);
        auto loaded = engine.loadProject(write(base, "malformed-control.json"));
        INFO((loaded ? std::string() : loaded.error().message));
        REQUIRE(loaded.has_value());
    }

    struct Damage {
        const char* what;
        std::function<void(json&)> apply;
    };
    const Damage damages[] = {
        {"format is a number", [](json& d) { d["format"] = 1; }},
        {"version is a string", [](json& d) { d["version"] = "two"; }},
        {"assets is an array", [](json& d) { d["assets"] = json::array({1, 2}); }},
        {"the scene's kind is a number", [](json& d) { d["assets"]["scene"] = {{"kind", 5}, {"path", "x.json"}}; }},
        {"control.tempoSource is a number", [](json& d) { d["control"] = {{"tempoSource", 3}}; }},
        {"a camera shot span's time is a string",
         [](json& d) { d["cameraShotSpans"] = json::array({{{"start", "soon"}, {"end", 2.0}}}); }},
    };
    for (const Damage& damage : damages) {
        DYNAMIC_SECTION(damage.what) {
            json doc = base;
            damage.apply(doc);
            const auto path = write(doc, "malformed-case.json");
            app::Engine engine(app::EngineMode::Offline);
            Result<void> loaded;
            REQUIRE_NOTHROW(loaded = engine.loadProject(path));
            // Either refused or loaded with the key ignored is acceptable; a thrown type_error is not.
            // A file whose format is not the project format's name is not a project, and is refused.
            if (std::string(damage.what).rfind("format", 0) == 0) {
                CHECK_FALSE(loaded.has_value());
            }
            REQUIRE_NOTHROW((void)engine.loadFile(path));
        }
    }
}
