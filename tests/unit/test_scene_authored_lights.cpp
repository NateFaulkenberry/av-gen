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
    // The one genuine finding was kept as an expectation so that fixing it is what changes this
    // test, and ADR-330 fixed it: fifteen scenes wrote the C++ field name `volumeNoiseAmount` where
    // the parser reads `volumeNoise`, all fifteen ran at 0 noise, and the five projects saved over
    // them agreed (`"scene/volumeNoise": 0.0`). The expectation is now **zero**, and the counter
    // stays rather than folding into `otherFindings`, because a named zero is what tells the next
    // reader that this key was the finding and has been closed -- an unnamed one would report the
    // regression as "some unknown key somewhere".
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
        // whatever the source wrote -- including the `volumeNoiseAmount` this case used to pin. The
        // agent that wrote this test measured 15 in a fresh worktree that had never run the
        // generator; the same test found 29 here, where it had. The number was never the
        // disagreement. The exclusion stays now that the count is zero, for the same reason: a
        // worktree that has run the generator would otherwise report fourteen stale copies as a
        // regression in files nobody wrote.
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
    CHECK(volumeNoiseAmount == 0);
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

// ---- stable ids, and the rename that used to orphan every binding -----------------------------
//
// ADR-350 prescribes exactly two assertions for the "built but unreachable" class and neither
// needs a GPU: every path is registered, with a negative control that an unregistered path is NOT
// found; and a non-default value survives save -> load -> **save**. The second save is the one that
// matters, because a writer that merely echoes what it just parsed still passes the first.

namespace {

bool registered(const params::ParameterSet& set, std::string_view path) {
    return set.find(path) != nullptr;
}

// A scene with one light of `type`, written to `dir`. Deliberately not a copy of a fixture: the
// area and spot arms need kinds no fixture in the repository authors.
fs::path writeOneLightScene(const fs::path& dir, const char* type, json extra = json::object()) {
    json light = json{{"name", "Key Light"}, {"type", type}, {"intensity", 7.5}};
    for (auto& [k, v] : extra.items()) {
        light[k] = v;
    }
    json doc = json{{"format", "avgen-scene"}, {"version", 1}, {"name", "one-light"},
                    {"nodes", json::array()}, {"lights", json::array({light})}};
    fs::create_directories(dir);
    const fs::path file = dir / "one-light.scene.json";
    std::ofstream out(file);
    out << doc.dump(1);
    return file;
}

} // namespace

TEST_CASE("Every authored-light property is a real parameter", "[scene][lights][parameters]") {
    const fs::path dir = fs::temp_directory_path() / "avgen-light-params";
    fs::remove_all(dir);

    SECTION("a spot light") {
        auto built = buildScene(writeOneLightScene(dir, "spot"));
        const params::ParameterSet& p = built->parameters;
        // The id is derived from the sanitised name, so "Key Light" becomes "Key_Light".
        for (const char* leaf : {"enabled", "intensity", "color", "azimuth", "elevation",
                                 "angularSize", "shadowStrength", "position", "range", "innerCone",
                                 "outerCone", "temperature", "tint", "castsShadow", "contactShadow",
                                 "shadowBias", "volumetric"}) {
            INFO(leaf);
            CHECK(registered(p, std::string("lights/Key_Light/") + leaf));
        }
        // THE CONTROL: a path that does not exist is not found. Without it every CHECK above
        // passes on a `find` that returns non-null for anything.
        CHECK_FALSE(registered(p, "lights/Key_Light/nonesuch"));
        CHECK_FALSE(registered(p, "lights/nonesuch/intensity"));
        // A spot has no extent, so the area knobs are absent rather than present and inert --
        // ADR-372's lesson, that a control which cannot move the picture should not be in the panel.
        CHECK_FALSE(registered(p, "lights/Key_Light/width"));
    }

    SECTION("a directional light has no range, and an area light has an extent") {
        auto sun = buildScene(writeOneLightScene(dir / "sun", "directional"));
        // Range is the inverse-square cutoff and means nothing to a directional light.
        CHECK_FALSE(registered(sun->parameters, "lights/Key_Light/range"));
        CHECK_FALSE(registered(sun->parameters, "lights/Key_Light/innerCone"));
        CHECK(registered(sun->parameters, "lights/Key_Light/azimuth"));

        auto rect = buildScene(writeOneLightScene(dir / "rect", "rect"));
        CHECK(registered(rect->parameters, "lights/Key_Light/width"));
        CHECK(registered(rect->parameters, "lights/Key_Light/height"));
        CHECK(registered(rect->parameters, "lights/Key_Light/range"));
        // A Rect is not aimed the way a spot is, so it gets no azimuth/elevation pair.
        CHECK_FALSE(registered(rect->parameters, "lights/Key_Light/azimuth"));
    }
}

TEST_CASE("A light's parameters reach the renderer", "[scene][lights][parameters]") {
    const fs::path dir = fs::temp_directory_path() / "avgen-light-reach";
    fs::remove_all(dir);
    auto built = buildScene(writeOneLightScene(dir, "point", json{{"position", json::array({1.0, 2.0, 3.0})}}));

    REQUIRE(built->light("Key Light") != nullptr);
    CHECK(built->light("Key Light")->position.x == Approx(1.0f));

    // Move it the way a gizmo does -- by writing the parameter's base -- and step a frame.
    auto* position = built->parameters.findAs<glm::vec3>("lights/Key_Light/position");
    REQUIRE(position != nullptr);
    position->setBase(glm::vec3(10.0f, 20.0f, 30.0f));
    FrameTime time;
    time.renderTime = 1.0 / 60.0;
    time.deltaTime = 1.0 / 60.0;
    time.frameIndex = 1;
    built->parameters.resetFinals();
    built->comp->update(time);

    // The light the renderer is handed has moved, not merely the parameter.
    REQUIRE(built->light("Key Light") != nullptr);
    CHECK(built->light("Key Light")->position.x == Approx(10.0f));
    CHECK(built->light("Key Light")->position.z == Approx(30.0f));
    // And the authored copy moved with it, or the next save writes the old position (ADR-225).
    REQUIRE(built->comp->authoredLights().size() == 1);
    CHECK(built->comp->authoredLights()[0].light.position.y == Approx(20.0f));
}

TEST_CASE("A renamed light keeps the routes bound to it", "[scene][lights][parameters][rename]") {
    // The whole point of separating the id from the display name. Under the old model this test
    // could not be written: the route's target contained the name, so renaming re-pathed the
    // parameter and `Modulator::bind` skipped the route without a word.
    const fs::path dir = fs::temp_directory_path() / "avgen-light-rename";
    fs::remove_all(dir);
    auto built = buildScene(writeOneLightScene(dir, "point"));

    built->bus.declare("audio.bass", 0.0f, 1.0f);
    params::ModRoute route;
    route.source = "audio.bass";
    route.target = "lights/Key_Light/intensity";
    built->modulator.addRoute(route);
    built->modulator.bind(built->bus, built->parameters);

    // Whether *this* route found its parameter, not whether every route in the scene did: `bind`
    // reports failure for the whole set, and these fixtures carry routes to signals no test bus
    // declares. A resolved route is one whose `targetParam` is no longer null.
    const auto resolved = [&](std::string_view target) {
        for (const params::ModRoute& r : built->modulator.routes()) {
            if (r.target == target) {
                return r.targetParam != nullptr;
            }
        }
        return false;
    };
    REQUIRE(resolved("lights/Key_Light/intensity"));

    // Rename it, the way the panel will: the display name changes, the id does not.
    std::vector<scene::Composition::AuthoredLight> lights = built->comp->authoredLights();
    REQUIRE(lights.size() == 1);
    const std::string idBefore = lights[0].id;
    lights[0].light.name = "Moon Key";
    REQUIRE(built->comp->setAuthoredLights(std::move(lights)).has_value());

    CHECK(built->comp->authoredLights()[0].id == idBefore);
    CHECK(built->comp->authoredLights()[0].light.name == "Moon Key");
    // The parameter is still there under the id, and still the one the route names.
    CHECK(registered(built->parameters, "lights/Key_Light/intensity"));
    built->modulator.bind(built->bus, built->parameters);
    CHECK(resolved("lights/Key_Light/intensity"));
    // The CONTROL: the display name is NOT a parameter path, so a rename that had re-keyed the
    // parameters would have produced this one instead.
    CHECK_FALSE(registered(built->parameters, "lights/Moon_Key/intensity"));
}

TEST_CASE("A renamed light writes its id, and an unrenamed one does not", "[scene][lights]") {
    const fs::path dir = fs::temp_directory_path() / "avgen-light-idkey";
    fs::remove_all(dir);
    auto built = buildScene(writeOneLightScene(dir, "point"));

    // Nobody has renamed it, so the id still equals the sanitised name and the key stays out of the
    // file -- which is what keeps every scene in the repository byte-stable through this change.
    CHECK_FALSE(built->comp->toJson().at("lights").at(0).contains("id"));

    std::vector<scene::Composition::AuthoredLight> lights = built->comp->authoredLights();
    lights[0].light.name = "Moon Key";
    REQUIRE(built->comp->setAuthoredLights(std::move(lights)).has_value());

    const json written = built->comp->toJson().at("lights").at(0);
    REQUIRE(written.contains("id"));
    CHECK(written.at("id") == "Key_Light");
    CHECK(written.at("name") == "Moon Key");
}

TEST_CASE("A saved scene keeps its lights through two round trips", "[scene][lights]") {
    // ADR-350's second arm. The second save is the one that matters.
    const fs::path dir = fs::temp_directory_path() / "avgen-light-roundtrip";
    fs::remove_all(dir);
    const fs::path source = writeOneLightScene(
        dir, "spot",
        json{{"position", json::array({4.0, 5.0, 6.0})}, {"outerCone", 33.0}, {"temperature", 3200.0}});

    auto first = buildScene(source);
    const fs::path out1 = dir / "out1.scene.json";
    REQUIRE(first->comp->saveFile(out1).has_value());

    assets::AssetRegistry registry(source.parent_path());
    auto reloaded = scene::Composition::loadFile(out1, registry);
    REQUIRE(reloaded.has_value());
    const fs::path out2 = dir / "out2.scene.json";
    REQUIRE((*reloaded)->saveFile(out2).has_value());

    std::ifstream in(out2);
    json doc = json::parse(in);
    REQUIRE(doc.contains("lights"));
    REQUIRE(doc.at("lights").size() == 1);
    const json& light = doc.at("lights").at(0);
    // Named keys, not counts: a save was observed dropping two whole top-level keys while the
    // parameter count went up.
    CHECK(light.at("name") == "Key Light");
    CHECK(light.at("type") == "spot");
    CHECK(light.at("intensity").get<float>() == Approx(7.5f));
    CHECK(light.at("outerCone").get<float>() == Approx(33.0f));
    CHECK(light.at("temperature").get<float>() == Approx(3200.0f));
    CHECK(light.at("position").at(1).get<float>() == Approx(5.0f));
}

TEST_CASE("The ecology's name prefix is refused to an author", "[scene][lights]") {
    // `updateEcologyLights` erases last frame's glow by a prefix match over ALL of `scene_.lights`,
    // so a light called "ecology.glow.x" would be deleted on the first frame the ecology ran and
    // would shift every authored light after it relative to `authoredLightFirst_`. Unreachable
    // while only a text editor could name a light; a Lights panel is what makes it reachable.
    const fs::path dir = fs::temp_directory_path() / "avgen-light-reserved";
    fs::remove_all(dir);
    auto built = buildScene(writeOneLightScene(dir, "point"));

    std::vector<scene::Composition::AuthoredLight> lights = built->comp->authoredLights();
    lights[0].light.name = "ecology.glow.7";
    lights[0].id.clear();
    CHECK_FALSE(built->comp->setAuthoredLights(std::move(lights)).has_value());

    // THE CONTROL: a name that merely resembles it is fine. A guard that refused everything would
    // pass the arm above for the wrong reason.
    std::vector<scene::Composition::AuthoredLight> ok = built->comp->authoredLights();
    ok[0].light.name = "ecology glow";
    ok[0].id.clear();
    CHECK(built->comp->setAuthoredLights(std::move(ok)).has_value());
}

TEST_CASE("Two lights cannot share an id", "[scene][lights]") {
    const fs::path dir = fs::temp_directory_path() / "avgen-light-dupid";
    fs::remove_all(dir);
    auto built = buildScene(writeOneLightScene(dir, "point"));

    std::vector<scene::Composition::AuthoredLight> two = built->comp->authoredLights();
    scene::Composition::AuthoredLight clone = two[0];
    clone.light.name = "Second";  // a different name...
    two.push_back(clone);         // ...but the same id, which is the parameter path
    CHECK_FALSE(built->comp->setAuthoredLights(std::move(two)).has_value());

    // THE CONTROL: distinct ids are accepted, so the refusal above is about the collision and not
    // about having two lights at all.
    std::vector<scene::Composition::AuthoredLight> good = built->comp->authoredLights();
    scene::Composition::AuthoredLight other = good[0];
    other.light.name = "Second";
    other.id = "second";
    good.push_back(other);
    CHECK(built->comp->setAuthoredLights(std::move(good)).has_value());
}

TEST_CASE("uniqueAuthoredLightId avoids what is taken", "[scene][lights]") {
    const std::vector<std::string> taken{"key", "key-2"};
    CHECK(scene::Composition::uniqueAuthoredLightId("Fill", taken) == "Fill");
    CHECK(scene::Composition::uniqueAuthoredLightId("key", taken) == "key-3");
    // A name that sanitises to nothing still yields something addressable.
    CHECK_FALSE(scene::Composition::uniqueAuthoredLightId("", taken).empty());
}

// ---- the project's record of its session's lights ----------------------------------------------
//
// ADR-207/230/276/330's family, a fifth time. A project saves its scene **by reference**, so a
// light added in the editor and not written into the project document lives in the window and in
// no document any render reads.

TEST_CASE("authoredLightsAgainst records a difference and nothing else", "[scene][lights]") {
    // `liveLights` is `Composition::toJson()["lights"]` -- already canonical -- so the arms below
    // build it the way `Engine::saveProject` does rather than by hand. That is not a detail: the
    // reader **normalises `direction`**, and the default (-0.4, -1, -0.35) is 1.1435 long, so a
    // parsed light always differs from a default-constructed one and always writes a `direction`
    // key. A hand-written "identical" array is therefore not identical, and comparing against one
    // would have made every untouched project write a lights key.
    const fs::path dir = fs::temp_directory_path() / "avgen-light-against";
    fs::remove_all(dir);
    const fs::path file = writeOneLightScene(dir, "directional");
    std::ifstream in(file);
    const json sceneDoc = json::parse(in);
    auto built = buildScene(file);
    const json live = built->comp->toJson().value("lights", json::array());

    SECTION("an untouched list writes nothing") {
        // THE CONTROL that makes every positive arm mean something: a project nobody edited stays
        // byte-stable through a save (ADR-182).
        CHECK(scene::authoredLightsAgainst(live, sceneDoc).is_null());
    }

    SECTION("a scene spelling out a default still reads as untouched") {
        // Why the comparison canonicalises instead of comparing raw. 6500 K is the default, so
        // `toJson` omits it; a scene that writes it anyway must not read as an edit, or every
        // project carrying that scene starts writing a lights key it does not need.
        json spelled = sceneDoc;
        spelled["lights"][0]["temperature"] = 6500.0;
        CHECK(scene::authoredLightsAgainst(live, spelled).is_null());
    }

    SECTION("a changed value is recorded whole") {
        std::vector<scene::Composition::AuthoredLight> edited = built->comp->authoredLights();
        edited[0].light.intensity = 9.0f;
        REQUIRE(built->comp->setAuthoredLights(std::move(edited)).has_value());
        const json record =
            scene::authoredLightsAgainst(built->comp->toJson().value("lights", json::array()), sceneDoc);
        REQUIRE_FALSE(record.is_null());
        REQUIRE(record.is_array());
        REQUIRE(record.size() == 1);
        CHECK(record.at(0).at("intensity").get<float>() == Approx(9.0f));
    }

    SECTION("an added light is recorded, and so is an emptied list") {
        std::vector<scene::Composition::AuthoredLight> two = built->comp->authoredLights();
        scene::Composition::AuthoredLight fill;
        fill.light.name = "fill";
        fill.light.type = scene::PunctualLight::Type::Point;
        two.push_back(fill);
        REQUIRE(built->comp->setAuthoredLights(std::move(two)).has_value());
        CHECK(scene::authoredLightsAgainst(built->comp->toJson().value("lights", json::array()), sceneDoc)
                  .size() == 2);

        // A deletion that only survives while the process does is the same defect pointing the
        // other way, so an emptied list is a difference like any other.
        REQUIRE(built->comp->setAuthoredLights({}).has_value());
        const json emptied =
            scene::authoredLightsAgainst(built->comp->toJson().value("lights", json::array()), sceneDoc);
        REQUIRE_FALSE(emptied.is_null());
        CHECK(emptied.empty());
    }

    SECTION("an unreadable scene document is not a deletion") {
        // Mirrors `nodeEditsAgainst`'s "no document, no record" guard: a scene this build cannot
        // parse is not evidence that the session deleted the lights.
        CHECK(scene::authoredLightsAgainst(json::array(), json::object()).is_null());
    }
}
