// Fifteen scenes asked for volumetric noise and fifteen films ran at zero (ADR-320's sibling, in
// the commit that corrected the key).
//
// The parser reads `"volumeNoise"`. Fifteen shipped scenes wrote `"volumeNoiseAmount"` -- the name
// of the C++ field, `VolumeSettings::volumeNoiseAmount` -- and every one of them has volumetrics on
// (`volumeDensity` 0.00055 to 8) and asked for 0.4 to 0.55 of noise. ADR-278's unknown-key report
// found it and pinned it at fifteen rather than repairing it, because the repair changes fifteen
// shipped films.
//
// `test_scene_authored_lights.cpp` keeps the sweep that says no scene writes the wrong key any
// more. This file keeps the two arms that sweep cannot make: that the right key **reaches the
// engine as a number**, and that the project sitting over the scene does not put the zero back.
//
// That second arm is the finding. Five projects carried `"scene/volumeNoise": 0.0` in their
// parameter block, and a project parameter sits over its scene (ADR-264), so correcting the key
// alone would have left glowmere-valley-2, -multicam, -atmospherics, -stylized and glowmere-lyrics
// running at exactly the zero they ran at before -- and the change would have looked like a working
// fix everywhere it did not matter.

#include "app/engine.hpp"
#include "assets/asset_registry.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using namespace avgen;
namespace fs = std::filesystem;
using nlohmann::json;

namespace {

fs::path repoRoot() { return fs::path(AVGEN_SOURCE_DIR); }

// The noise amount the scene's own parameter holds after it has been registered -- which is the
// number `Composition::buildEnvironment` hands the volume renderer as `env.volumeNoiseAmount`.
float authoredNoise(const json& sceneDoc) {
    assets::AssetRegistry registry;
    auto comp = scene::Composition::fromJson(sceneDoc, registry);
    REQUIRE(comp.has_value());
    params::ParameterSet parameters;
    params::Modulator modulator;
    (*comp)->attach(parameters, modulator);
    const auto* p = parameters.findAs<float>("scene/volumeNoise");
    REQUIRE(p != nullptr);
    return p->value();
}

json readJson(const fs::path& p) {
    std::ifstream in(p);
    json doc = json::parse(in, nullptr, false);
    REQUIRE_FALSE(doc.is_discarded());
    return doc;
}

} // namespace

TEST_CASE("a scene's authored volumetric noise reaches the parameter that runs it",
          "[volumetrics][scene][json]") {
    json doc{{"format", scene::Composition::kFormatName},
             {"version", scene::Composition::kFormatVersion},
             {"environment", json{{"volumeDensity", 0.006}, {"volumeNoise", 0.45}}},
             {"nodes", json::array()}};
    CHECK_THAT(authoredNoise(doc), Catch::Matchers::WithinAbs(0.45, 1e-5));

    // The control, and it is the defect kept as a measurement: the field's name is not the key's,
    // and a document that writes it gets the 0.0 default with no error. Nothing about that has
    // changed -- ADR-278 chose a warning over a refusal, deliberately -- so this is what the
    // fifteen scenes were doing and what the sweep in `test_scene_authored_lights.cpp` now says
    // none of them does.
    json wrong = doc;
    wrong["environment"].erase("volumeNoise");
    wrong["environment"]["volumeNoiseAmount"] = 0.45;
    CHECK_THAT(authoredNoise(wrong), Catch::Matchers::WithinAbs(0.0, 1e-5));
}

TEST_CASE("the owner's project runs the noise its scene asks for", "[volumetrics][project]") {
    // The whole chain, on the owner's own film: scene key -> parameter default -> project override
    // -> the value a render uses. Before the correction this was 0 at both ends.
    const fs::path project = repoRoot() / "examples/world/glowmere-valley-2-multicam.json";
    REQUIRE(fs::is_regular_file(project));

    // What the scene asks for, read from the file rather than quoted, so this case cannot drift
    // from the art direction if somebody retunes it.
    const fs::path scene = repoRoot() / "examples/world/glowmere-valley-2-multicam.scene.json";
    const json sceneDoc = readJson(scene);
    REQUIRE(sceneDoc.contains("environment"));
    REQUIRE(sceneDoc["environment"].contains("volumeNoise"));
    const double asked = sceneDoc["environment"]["volumeNoise"].get<double>();
    CHECK(asked > 0.0);

    app::Engine engine(app::EngineMode::Offline);
    auto loaded = engine.loadProject(project);
    INFO((loaded.has_value() ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    const auto* p = engine.params().findAs<float>("scene/volumeNoise");
    REQUIRE(p != nullptr);
    INFO("scene asks " << asked << ", the project runs " << p->value());
    CHECK_THAT(static_cast<double>(p->value()), Catch::Matchers::WithinAbs(asked, 1e-5));
}

TEST_CASE("no project overrides its scene's volumetric noise back to zero", "[volumetrics][project]") {
    // The arm that keeps the residue from coming back. A saved project photographs the run
    // (ADR-264), so the next save of a session that was running at zero for some *other* reason
    // would write this key again -- and it would be indistinguishable from an author asking for
    // clear air. The rule is narrow and is the one the residue broke: a project does not carry a
    // zero over a scene that asks for noise.
    //
    // A real zero is still authorable: say it in the scene, as `glowmere-valley-2-song.scene.json`
    // does. That file is this defect fossilised -- the correct key at 0.0, written by "Save Scene
    // As..." from a session running at zero because of it -- and it is deliberately left alone,
    // because its authored intent is not recoverable from the file and inventing one would be an
    // art direction rather than a repair.
    const fs::path examples = repoRoot() / "examples";
    REQUIRE(fs::is_directory(examples));
    int projects = 0;
    int checked = 0;
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(examples)) {
        if (!entry.is_regular_file() || !entry.path().filename().string().ends_with(".json") ||
            entry.path().filename().string().ends_with(".scene.json")) {
            continue;
        }
        if (entry.path().string().find("/_bench/") != std::string::npos) {
            continue; // generated, not authored (see the same exclusion in the key sweep)
        }
        const json doc = readJson(entry.path());
        if (!doc.is_object() || doc.value("format", std::string{}) != "avgen-project") {
            continue;
        }
        ++projects;
        const auto params = doc.find("parameters");
        if (params == doc.end() || !params->is_object() || !params->contains("scene/volumeNoise")) {
            continue;
        }
        const double override_ = (*params)["scene/volumeNoise"].get<double>();
        if (override_ != 0.0) {
            continue;
        }
        // A zero override is only wrong when the scene under it asks for something else.
        const auto assets = doc.find("assets");
        if (assets == doc.end() || !assets->is_object() || !assets->contains("scene")) {
            continue;
        }
        const json& ref = (*assets)["scene"];
        if (!ref.contains("path")) {
            continue;
        }
        const json& pathRef = ref["path"];
        const std::string rel = pathRef.is_object() ? pathRef.value("path", std::string{})
                                                    : pathRef.get<std::string>();
        const fs::path scene = (entry.path().parent_path() / rel).lexically_normal();
        if (!fs::is_regular_file(scene)) {
            continue;
        }
        ++checked;
        const json sceneDoc = readJson(scene);
        // Under *either* spelling, and that is not tidiness. Reading only the key the parser reads
        // would leave this case unfailable for as long as the fifteen scenes misspelled it: the
        // scene would look as though it asked for nothing, and a project zeroing it would look
        // correct. Measured -- with the scene files put back the way they were, every other arm in
        // this file failed and this one passed. Asking what the author wrote, rather than what the
        // parser happened to find, is what gives it a control.
        const json env = sceneDoc.contains("environment") && sceneDoc["environment"].is_object()
                             ? sceneDoc["environment"]
                             : json::object();
        const double asked = env.value("volumeNoise", env.value("volumeNoiseAmount", 0.0));
        INFO(entry.path().filename().string() << " overrides its scene's " << asked << " with 0");
        CHECK(asked == 0.0);
    }
    CHECK(projects >= 20); // the control: this swept something
    INFO("projects " << projects << ", zero overrides examined " << checked);
}
