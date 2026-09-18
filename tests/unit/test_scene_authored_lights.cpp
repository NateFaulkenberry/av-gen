// A scene file authoring a light (ADR-278), and the unknown key that is no longer silent.
//
// ---- why every case here has a control, in this file more than most ---------------------------
//
// ADR-182, and this defect is the purest example of it in the tree. A test that loads
// `examples/labs/lod-geometry-lab.scene.json` and asserts "it has a directional light called 'key'
// that casts a shadow" **passes on the bug**: that is a description of `defaultKeyLight()`, which
// is what the scene was getting instead of the light its own file wrote. The only assertion that
// can fail when the feature is missing is one that names a quantity where the file and the default
// *disagree*, so every arm below is paired with the same scene with its `"lights"` key textually
// removed, and the control's job is to produce the other number.
//
// The three quantities that disagree, measured before the fix:
//
//   | quantity    | defaultKeyLight()                 | what the two fixtures wrote |
//   |-------------|-----------------------------------|-----------------------------|
//   | direction   | (-0.353209, -0.883022, -0.309058) | (-0.349843, -0.719676, -0.599730) -- 19.2 degrees apart |
//   | intensity   | 3.0                               | 4.0                         |
//   | temperature | 5600 K                            | 6500 K (the neutral default) |
//
// Colour is (1, 0.97, 0.92) on both, so colour is deliberately *not* what any arm turns on.

#include "assets/asset_registry.hpp"
#include "core/json_keys.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/light_rig.hpp"
#include "scene/scene_types.hpp"
#include "signals/signal_bus.hpp"

#include <nlohmann/json.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <regex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;
using namespace avgen;
using Catch::Approx;
using nlohmann::json;

namespace {

fs::path lightsRepoRoot() { return fs::path(AVGEN_SOURCE_DIR); }

// A composition built, attached and stepped one frame -- the state a renderer is handed. `lights`
// is only complete after `update`, because the rig and the ecology are appended per frame.
struct Built {
    std::unique_ptr<scene::Composition> comp;
    params::ParameterSet parameters;
    params::Modulator modulator;
    signals::SignalBus bus;

    const scene::Scene& scene() const { return comp->scene(); }
    const scene::PunctualLight* light(std::string_view name) const {
        for (const scene::PunctualLight& l : comp->scene().lights) {
            if (l.name == name) {
                return &l;
            }
        }
        return nullptr;
    }
};

// `base` is where relative asset paths resolve from; a copy of a fixture written into a scratch
// directory still has to find the fixture's own `../../assets/...`.
std::unique_ptr<Built> buildScene(const fs::path& file, const fs::path& base = {}) {
    auto built = std::make_unique<Built>();
    assets::AssetRegistry registry(base.empty() ? file.parent_path() : base);
    auto loaded = scene::Composition::loadFile(file, registry);
    REQUIRE(loaded.has_value());
    built->comp = std::move(*loaded);
    built->comp->attach(built->parameters, built->modulator);
    built->comp->setViewport(1280, 800);
    FrameTime time;
    time.renderTime = 0.0;
    time.deltaTime = 1.0 / 60.0;
    time.frameIndex = 0;
    built->parameters.resetFinals();
    built->comp->updateFields(time, built->bus, built->modulator);
    built->modulator.applyRoutes(built->bus, built->parameters, time.deltaTime);
    built->comp->updateBehaviour(time, built->bus);
    built->comp->update(time);
    return built;
}

// The control arm: the same file with its top-level `"lights"` array cut out, textually. Textual
// because a `json.load`/`json.dump` round trip rewrites every float literal (the repository's rule
// for editing scene files by hand, and the same reason applies to a test that writes one).
fs::path withoutLightsKey(const fs::path& source, const fs::path& scratch) {
    std::ifstream in(source);
    REQUIRE(in.good());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    // Exactly one leading space on both ends: the fixtures are `dump(1)`, so the array's own
    // members close with two, and a looser pattern stops at the first inner `],` and leaves a file
    // that is not JSON -- which fails the control as a parse error and looks like the arm working.
    const std::regex lightsKey(R"(\n "lights": \[[\s\S]*?\n \],)");
    std::smatch m;
    REQUIRE(std::regex_search(text, m, lightsKey));
    const std::string stripped = m.prefix().str() + m.suffix().str();
    REQUIRE(stripped.find("\"lights\"") == std::string::npos);
    fs::create_directories(scratch);
    const fs::path out = scratch / source.filename();
    std::ofstream os(out);
    os << stripped;
    os.close();
    return out;
}

fs::path scratchDir() {
    const fs::path dir = fs::temp_directory_path() / "avgen-authored-lights";
    fs::create_directories(dir);
    return dir;
}

// A minimal scene written to disk, so a case can author exactly the light it is about.
fs::path writeScene(std::string_view name, const json& body) {
    json doc = body;
    doc["format"] = "avgen-scene";
    doc["version"] = 1;
    doc["name"] = std::string(name);
    const fs::path out = scratchDir() / (std::string(name) + ".scene.json");
    std::ofstream os(out);
    os << doc.dump(1);
    os.close();
    return out;
}

// The two fixtures' authored key, as their files spell it.
constexpr float kAuthoredIntensity = 4.0f;
constexpr float kAuthoredTemperature = 6500.0f;
const glm::vec3 kAuthoredDirection = glm::normalize(glm::vec3(-0.35f, -0.72f, -0.6f));
// `defaultKeyLight()`'s, as `composition.cpp` builds it.
constexpr float kDefaultIntensity = 3.0f;
constexpr float kDefaultTemperature = 5600.0f;
const glm::vec3 kDefaultDirection = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.35f));

void requireVec(const glm::vec3& got, const glm::vec3& want) {
    INFO("got (" << got.x << ", " << got.y << ", " << got.z << ") want (" << want.x << ", " << want.y
                 << ", " << want.z << ")");
    CHECK(got.x == Approx(want.x).margin(1e-5));
    CHECK(got.y == Approx(want.y).margin(1e-5));
    CHECK(got.z == Approx(want.z).margin(1e-5));
}

} // namespace

// ------------------------------------------------------------------------------------------------
// The defect itself, with the control that makes the arm mean something.
// ------------------------------------------------------------------------------------------------

TEST_CASE("A lab fixture is lit by the light its own file authors", "[scene][lights]") {
    const std::array<const char*, 2> fixtures{"examples/labs/lod-geometry-lab.scene.json",
                                              "examples/labs/visibility-culling-lab.scene.json"};
    for (const char* relative : fixtures) {
        const fs::path file = lightsRepoRoot() / relative;
        REQUIRE(fs::is_regular_file(file));
        INFO(relative);

        // ---- arm: the file as it ships -----------------------------------------------------------
        auto arm = buildScene(file);
        REQUIRE(arm->scene().lights.size() == 1);
        const scene::PunctualLight* key = arm->light("key");
        REQUIRE(key != nullptr);
        CHECK(key->type == scene::PunctualLight::Type::Directional);
        CHECK(key->castsShadow);
        // The three quantities where the file and the default disagree. Each of these is the whole
        // test: with the `lights` key unread they all take the default's value instead.
        requireVec(key->direction, kAuthoredDirection);
        CHECK(key->intensity == Approx(kAuthoredIntensity));
        CHECK(key->temperature == Approx(kAuthoredTemperature));

        // ---- control: the same scene with the `lights` key removed -------------------------------
        // This is the state the engine was in before ADR-278, reproduced through the *file* rather
        // than through the code, so it cannot drift away from what it is a control for. Every
        // assertion above is repeated against the other number.
        const fs::path stripped = withoutLightsKey(file, scratchDir() / "control");
        auto control = buildScene(stripped, file.parent_path());
        REQUIRE(control->scene().lights.size() == 1);
        const scene::PunctualLight* fallback = control->light("key");
        REQUIRE(fallback != nullptr);
        requireVec(fallback->direction, kDefaultDirection);
        CHECK(fallback->intensity == Approx(kDefaultIntensity));
        CHECK(fallback->temperature == Approx(kDefaultTemperature));
        // ...and the two are genuinely different lights, stated rather than implied: the direction
        // alone is 19.2 degrees apart, so no rounding tolerance can make the arm pass on the control.
        const float cosAngle = glm::dot(key->direction, fallback->direction);
        INFO("angle between the two keys: " << glm::degrees(std::acos(cosAngle)) << " degrees");
        CHECK(glm::degrees(std::acos(cosAngle)) > 15.0f);
    }
}

TEST_CASE("Authoring a light replaces the default key; authoring none keeps it", "[scene][lights]") {
    // The precedence, as three files that differ in one key.
    const fs::path none = writeScene("precedence-none", json::object());
    auto plain = buildScene(none);
    REQUIRE(plain->scene().lights.size() == 1);
    CHECK(plain->scene().lights[0].name == "key");
    CHECK(plain->scene().lights[0].intensity == Approx(kDefaultIntensity)); // the default key

    const fs::path one = writeScene(
        "precedence-one",
        json{{"lights", json::array({json{{"name", "moon"},
                                          {"type", "directional"},
                                          {"direction", {0.0, -1.0, 0.0}},
                                          {"intensity", 11.0}}})}});
    auto authored = buildScene(one);
    REQUIRE(authored->scene().lights.size() == 1);
    CHECK(authored->scene().lights[0].name == "moon");
    CHECK(authored->scene().lights[0].intensity == Approx(11.0f));
    // The control for "the default is gone": there is no light called "key" at all, and the one
    // light present is not `defaultKeyLight()` wearing a different name.
    CHECK(authored->light("key") == nullptr);

    // A rig is not replaced -- it is appended alongside, the way a glTF lamp and a rig already
    // coexist. Two lights, not one, and the authored one is still the author's.
    const fs::path both = writeScene(
        "precedence-rig-and-light",
        json{{"lights", json::array({json{{"name", "moon"},
                                          {"type", "directional"},
                                          {"direction", {0.0, -1.0, 0.0}},
                                          {"intensity", 11.0}}})},
             {"lightRig",
              json{{"format", "avgen-lightrig"},
                   {"name", "one"},
                   {"lights", json::array({json{{"name", "rigkey"},
                                                {"type", "directional"},
                                                {"intensity", 1.0}}})}}}});
    auto mixed = buildScene(both);
    CHECK(mixed->scene().lights.size() == 2); // the authored moon, and the rig's one light
    REQUIRE(mixed->light("moon") != nullptr);
    CHECK(mixed->light("moon")->intensity == Approx(11.0f));
    CHECK(mixed->light("rigkey") != nullptr);
}

TEST_CASE("An authored light survives a save and reload", "[scene][lights]") {
    // ADR-207/230's world-effects bug in a new place: a save that does not write the key deletes
    // the author's lights, and nothing says so until the next load.
    const fs::path file = lightsRepoRoot() / "examples/labs/lod-geometry-lab.scene.json";
    assets::AssetRegistry registry(file.parent_path());
    auto loaded = scene::Composition::loadFile(file, registry);
    REQUIRE(loaded.has_value());
    const json saved = (*loaded)->toJson();

    REQUIRE(saved.contains("lights"));
    REQUIRE(saved.at("lights").is_array());
    REQUIRE(saved.at("lights").size() == 1);
    const json& written = saved.at("lights").at(0);
    // The fixture's own spelling, key for key: what the author wrote is what comes back, not a
    // twenty-six-key dump of the struct.
    CHECK(written.at("name") == "key");
    CHECK(written.at("type") == "directional");
    CHECK(written.at("castsShadow") == true);
    CHECK(written.at("intensity").get<float>() == Approx(4.0f));
    CHECK_FALSE(written.contains("width"));      // a default, so not written
    CHECK_FALSE(written.contains("temperature")); // 6500 K is the default
    CHECK_FALSE(written.contains("node"));

    // Reload what was written and compare the lights the scene actually delivers, which is the
    // claim that matters -- a key that round-trips into a field nobody reads is the defect again.
    const fs::path out = scratchDir() / "roundtrip.scene.json";
    {
        std::ofstream os(out);
        os << saved.dump(1);
    }
    auto again = buildScene(out, file.parent_path());
    auto original = buildScene(file);
    REQUIRE(again->scene().lights.size() == original->scene().lights.size());
    const scene::PunctualLight* a = again->light("key");
    const scene::PunctualLight* b = original->light("key");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    requireVec(a->direction, b->direction);
    requireVec(a->color, b->color);
    CHECK(a->intensity == Approx(b->intensity));
    CHECK(a->temperature == Approx(b->temperature));
    CHECK(a->castsShadow == b->castsShadow);

    // The control: a scene that authors no light writes no `lights` key, so every scene file in the
    // repository that never had one is byte-identical after a round trip.
    const fs::path bare = writeScene("roundtrip-none", json::object());
    assets::AssetRegistry bareRegistry(bare.parent_path());
    auto bareComp = scene::Composition::loadFile(bare, bareRegistry);
    REQUIRE(bareComp.has_value());
    CHECK_FALSE((*bareComp)->toJson().contains("lights"));
}

TEST_CASE("An authored light rides the node it names", "[scene][lights]") {
    // The capability a `NodeKind::Light` was the other candidate for. The node is an orb, which
    // needs no asset; the light is a spot, which is the kind whose direction matters.
    const auto sceneWith = [](bool attach) {
        json light{{"name", "lamp"},
                   {"type", "spot"},
                   {"position", {0.0, 0.0, 0.0}},
                   {"direction", {0.0, 0.0, -1.0}},
                   {"intensity", 5.0},
                   {"range", 50.0}};
        if (attach) {
            light["node"] = "carrier";
        }
        return json{{"nodes", json::array({json{{"name", "carrier"},
                                                {"kind", "orb"},
                                                {"position", {7.0, 3.0, -2.0}},
                                                {"rotation", {0.0, 90.0, 0.0}}}})},
                    {"lights", json::array({light})}};
    };

    // ---- arm: the light names the node -------------------------------------------------------
    auto attached = buildScene(writeScene("riding-attached", sceneWith(true)));
    const scene::PunctualLight* lamp = attached->light("lamp");
    REQUIRE(lamp != nullptr);
    requireVec(lamp->position, glm::vec3(7.0f, 3.0f, -2.0f));
    // 90 degrees about +Y takes -Z to -X.
    requireVec(lamp->direction, glm::vec3(-1.0f, 0.0f, 0.0f));

    // ---- control: the identical light with no `node` -----------------------------------------
    // Without this the arm proves nothing: a light authored at the origin pointing down -Z and a
    // light that failed to be attached are only distinguishable by *where the other one is*.
    auto loose = buildScene(writeScene("riding-loose", sceneWith(false)));
    const scene::PunctualLight* still = loose->light("lamp");
    REQUIRE(still != nullptr);
    requireVec(still->position, glm::vec3(0.0f));
    requireVec(still->direction, glm::vec3(0.0f, 0.0f, -1.0f));

    // ---- and it keeps riding when the node moves ---------------------------------------------
    // The half the glTF asset-light path does not do, which is why it is asserted rather than
    // assumed: that path refreshes an imported lamp's intensity and colour per frame and never
    // re-places it.
    params::Parameter<glm::vec3>* position =
        attached->parameters.findAs<glm::vec3>("nodes/carrier/position");
    REQUIRE(position != nullptr);
    position->setBase(glm::vec3(-4.0f, 11.0f, 6.0f));
    FrameTime time;
    time.renderTime = 1.0 / 60.0;
    time.deltaTime = 1.0 / 60.0;
    time.frameIndex = 1;
    attached->parameters.resetFinals();
    attached->comp->updateFields(time, attached->bus, attached->modulator);
    attached->modulator.applyRoutes(attached->bus, attached->parameters, time.deltaTime);
    attached->comp->updateBehaviour(time, attached->bus);
    attached->comp->update(time);
    const scene::PunctualLight* moved = attached->light("lamp");
    REQUIRE(moved != nullptr);
    requireVec(moved->position, glm::vec3(-4.0f, 11.0f, 6.0f));
    requireVec(moved->direction, glm::vec3(-1.0f, 0.0f, 0.0f)); // the rotation did not change

    // Hiding the node puts the light out, the same rule the asset lights follow.
    params::Parameter<bool>* visible = attached->parameters.findAs<bool>("nodes/carrier/visible");
    REQUIRE(visible != nullptr);
    visible->setBase(false);
    time.frameIndex = 2;
    attached->parameters.resetFinals();
    attached->comp->updateFields(time, attached->bus, attached->modulator);
    attached->modulator.applyRoutes(attached->bus, attached->parameters, time.deltaTime);
    attached->comp->updateBehaviour(time, attached->bus);
    attached->comp->update(time);
    CHECK(attached->light("lamp")->intensity == Approx(0.0f));
}

TEST_CASE("A malformed authored light fails the file rather than vanishing", "[scene][lights]") {
    assets::AssetRegistry registry(scratchDir());
    const auto refuses = [&](std::string_view name, const json& light) {
        const fs::path file = writeScene(name, json{{"lights", json::array({light})}});
        auto loaded = scene::Composition::loadFile(file, registry);
        INFO(name << ": " << (loaded.has_value() ? std::string("loaded") : loaded.error().message));
        CHECK_FALSE(loaded.has_value());
    };
    refuses("bad-noname", json{{"type", "directional"}});
    refuses("bad-type", json{{"name", "x"}, {"type", "lantern"}});
    refuses("bad-role", json{{"name", "x"}, {"role", "gaffer"}});
    refuses("bad-negative", json{{"name", "x"}, {"intensity", -1.0}});
    refuses("bad-zero-direction", json{{"name", "x"}, {"type", "spot"}, {"direction", {0, 0, 0}}});
    refuses("bad-vector", json{{"name", "x"}, {"direction", {0, -1}}});

    // The control for every line above: the same document, valid, loads. Without it "the file was
    // refused" is equally consistent with "this test writes files the loader cannot read".
    const fs::path good =
        writeScene("good", json{{"lights", json::array({json{{"name", "x"},
                                                             {"type", "spot"},
                                                             {"role", "practical"},
                                                             {"direction", {0, -1, 0}},
                                                             {"intensity", 1.0}}})}});
    auto loaded = scene::Composition::loadFile(good, registry);
    INFO((loaded.has_value() ? std::string("loaded") : loaded.error().message));
    CHECK(loaded.has_value());

    // A duplicate name is refused by `setAuthoredLights`, because a name is how a light is reported.
    const json twice = json{{"lights", json::array({json{{"name", "x"}}, json{{"name", "x"}}})}};
    CHECK_FALSE(scene::Composition::loadFile(writeScene("bad-duplicate", twice), registry).has_value());
    // ...and two lights with different names are not.
    const json apart = json{{"lights", json::array({json{{"name", "x"}}, json{{"name", "y"}}})}};
    CHECK(scene::Composition::loadFile(writeScene("good-two", apart), registry).has_value());
}

// ------------------------------------------------------------------------------------------------
// The generalisable half: a key nobody reads is not silent.
// ------------------------------------------------------------------------------------------------

TEST_CASE("An unknown key is reported by name, and a known one is not", "[scene][lights][json]") {
    // `unknownKeys` is the whole of the decision, so the test asserts it directly rather than
    // fishing a warning out of a log -- the same reason `particleExtentFromRadius` lives in
    // `scene/` and not in the panel (ADR-271).
    constexpr std::array<std::string_view, 3> known{"camera", "nodes", "lights"};
    const json doc{{"camera", 1}, {"nodes", 2}, {"lights", 3}, {"_note", "a comment"},
                   {"lightz", 4}, {"coneDegrees", 5}};
    const std::vector<std::string> unknown = json_keys::unknownKeys(doc, known);
    CHECK(unknown == std::vector<std::string>{"coneDegrees", "lightz"});
    // The controls, in the same call: every known key is absent from the report (or the check is
    // "everything is unknown", which would also pass on a typo), and `_note` is exempt by rule
    // rather than by a per-parser special case -- nine scene files carry one.
    CHECK(std::find(unknown.begin(), unknown.end(), "camera") == unknown.end());
    CHECK(std::find(unknown.begin(), unknown.end(), "_note") == unknown.end());
    // A non-object is not this function's complaint to make.
    CHECK(json_keys::unknownKeys(json::array({1, 2}), known).empty());
    CHECK(json_keys::unknownKeys(json(7), known).empty());
}

TEST_CASE("The scene parser's key list matches what it reads", "[scene][lights][json]") {
    // The failure mode of a hand-maintained key list is that it drifts from the parser. This is the
    // arm that catches the expensive direction: a key the parser reads but the list omits produces
    // a warning on a file that is correct, and the first such warning is the one that gets the
    // check switched off. Every scene and rig in the repository is loaded, and every key any of
    // them writes must be either read, `_`-prefixed, or a genuine finding named here.
    const fs::path examples = lightsRepoRoot() / "examples";
    REQUIRE(fs::is_directory(examples));
    // The one genuine finding, kept as an expectation so that fixing it is what changes this test.
    // Fifteen scenes write the C++ field name `volumeNoiseAmount` where the parser reads
    // `volumeNoise`; all fifteen run at 0 noise, and the projects that were saved over them agree
    // (`"scene/volumeNoise": 0.0`). Correcting it changes fifteen shipped films, so it is reported
    // rather than silently repaired.
    int volumeNoiseAmount = 0;
    int otherFindings = 0;
    int scenes = 0;
    for (const fs::directory_entry& entry : fs::recursive_directory_iterator(examples)) {
        if (!entry.is_regular_file() || !entry.path().filename().string().ends_with(".scene.json")) {
            continue;
        }
        // Generated output is not authorship, and sweeping it makes this test answer differently
        // on different machines. `examples/world/_bench/` is gitignored (`.gitignore:26`) and built
        // by `tools/make_bench_scenes.py` from the scenes beside it, so its fourteen copies inherit
        // whatever the source wrote -- including the `volumeNoiseAmount` this case is pinning. The
        // agent that wrote this test measured 15 in a fresh worktree that had never run the
        // generator; the same test found 29 here, where it had. The number was never the disagreement.
        if (entry.path().string().find("/_bench/") != std::string::npos) {
            continue;
        }
        std::ifstream in(entry.path());
        const json doc = json::parse(in, nullptr, false);
        if (doc.is_discarded() || !doc.is_object()) {
            continue;
        }
        ++scenes;
        const auto count = [&](const json& obj, std::span<const std::string_view> known,
                               std::string_view label) {
            for (const std::string& key : json_keys::unknownKeys(obj, known)) {
                if (key == "volumeNoiseAmount") {
                    ++volumeNoiseAmount;
                } else {
                    ++otherFindings;
                    UNSCOPED_INFO(entry.path().filename().string() << ": " << label << ": " << key);
                }
            }
        };
        count(doc, scene::sceneFileKeys(), "top level");
        if (doc.contains("environment") && doc.at("environment").is_object()) {
            count(doc.at("environment"), scene::sceneEnvironmentKeys(), "environment");
            if (doc.at("environment").contains("sky") && doc.at("environment").at("sky").is_object()) {
                count(doc.at("environment").at("sky"), scene::sceneSkyKeys(), "environment.sky");
            }
        }
    }
    CHECK(scenes >= 50);
    CHECK(otherFindings == 0);
    CHECK(volumeNoiseAmount == 15);
}

TEST_CASE("A light rig's unknown key is reported too", "[scene][lights][json]") {
    // ADR-272 §7 reported this and did not fix it: a rig writing `"coneDegrees"` gets the
    // 45-degree default from `"cone"` and no error. The lab's own fixture was written that way
    // first, and reading the parser is what caught it.
    const json rig{{"format", "avgen-lightrig"},
                   {"name", "typo"},
                   {"lights", json::array({json{{"name", "spot"},
                                                {"type", "spot"},
                                                {"coneDegrees", 16.0}}})}};
    auto parsed = scene::LightRig::fromJson(rig);
    REQUIRE(parsed.has_value());
    // The behaviour is unchanged -- the key is still ignored, deliberately -- so what the test can
    // assert is that it is *named*. `unknownKeys` is what the parser calls.
    CHECK(parsed->lights.at(0).coneDegrees == Approx(45.0f));
    const std::vector<std::string> reported =
        json_keys::unknownKeys(rig.at("lights").at(0), scene::rigLightKeys());
    CHECK(reported == std::vector<std::string>{"coneDegrees"});

    // The control: the correct spelling is read and is not reported. Without it the check passes
    // on a key list that contains nothing at all.
    json fixed = rig;
    fixed["lights"][0].erase("coneDegrees");
    fixed["lights"][0]["cone"] = 16.0;
    auto right = scene::LightRig::fromJson(fixed);
    REQUIRE(right.has_value());
    CHECK(right->lights.at(0).coneDegrees == Approx(16.0f));
    CHECK(json_keys::unknownKeys(fixed.at("lights").at(0), scene::rigLightKeys()).empty());

    // And every rig that ships is clean, at the root and in each light -- the same sweep the scene
    // files get, for the same reason.
    int findings = 0;
    int rigs = 0;
    for (const fs::directory_entry& entry :
         fs::recursive_directory_iterator(lightsRepoRoot() / "examples")) {
        if (!entry.is_regular_file() || !entry.path().filename().string().ends_with(".rig.json")) {
            continue;
        }
        std::ifstream in(entry.path());
        const json doc = json::parse(in, nullptr, false);
        if (doc.is_discarded() || !doc.is_object()) {
            continue;
        }
        ++rigs;
        for (const std::string& key : json_keys::unknownKeys(doc, scene::rigFileKeys())) {
            ++findings;
            UNSCOPED_INFO(entry.path().filename().string() << ": " << key);
        }
        if (doc.contains("lights") && doc.at("lights").is_array()) {
            for (const json& light : doc.at("lights")) {
                for (const std::string& key : json_keys::unknownKeys(light, scene::rigLightKeys())) {
                    ++findings;
                    UNSCOPED_INFO(entry.path().filename().string() << ": light: " << key);
                }
            }
        }
    }
    CHECK(rigs >= 20);
    CHECK(findings == 0);
}
