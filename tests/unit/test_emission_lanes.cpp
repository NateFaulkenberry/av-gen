// ADR-903..906 on the CPU: the emission lane every drawable carries after its material program, the
// instance variation it applies once, the scatter layers' own lanes, the ecology light as a
// parameter, a material layer's intensity as a parameter, and a field's clock that starts at an event.
//
// What the GPU suite (tests/rendering/test_emission_lanes_gpu.cpp) proves on pixels, this proves on
// the scene the renderer is handed: that each parameter lands on the number the shader reads, on
// every drawable it names and on nothing else -- the half of "reaches the output" a difference image
// cannot localise.

#include "assets/asset_registry.hpp"
#include "core/color.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/field_params.hpp"
#include "scene/material_params.hpp"
#include "scene/material_program.hpp"
#include "scene/scatter_anchors.hpp"
#include "signals/signal_bus.hpp"
#include "spatial/field.hpp"
#include "support/gltf_fixture.hpp"
#include "support/temp_dir.hpp"
#include "world/ecology.hpp"
#include "world/effects/effect_trigger.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {

double d(float v) {
    return static_cast<double>(v);
}

std::filesystem::path writeJson(const std::string& name, const std::string& text) {
    const auto path = testsupport::processTempDir() / ("avgen_emission_" + name + ".json");
    std::ofstream out(path);
    out << text;
    return path;
}

scene::MaterialOp op(scene::MaterialOpKind kind, int dst, int a = 0, int b = 0, int c = 0) {
    scene::MaterialOp o;
    o.kind = kind;
    o.dst = dst;
    o.srcA = a;
    o.srcB = b;
    o.srcC = c;
    return o;
}

scene::MaterialOp input(int dst, scene::MaterialInput in) {
    scene::MaterialOp o = op(scene::MaterialOpKind::Input, dst);
    o.input = in;
    return o;
}

scene::MaterialOp constant(int dst, glm::vec4 k) {
    scene::MaterialOp o = op(scene::MaterialOpKind::Constant, dst);
    o.constant = k;
    return o;
}

// The multiplier procedural.cpp bakes for an instance (ADR-054): the OKLCH rotation of the
// material's emissive colour, as a per-channel ratio of it, times the instance's gain.
glm::vec3 bakedMultiplier(const glm::vec3& base, float turns, float gain) {
    const glm::vec3 safe = glm::max(base, glm::vec3(1e-3f));
    const glm::vec3 rotated = glm::max(color::hueShift(safe, turns), glm::vec3(0.0f));
    return glm::clamp(rotated / safe, glm::vec3(0.0f), glm::vec3(96.0f)) * gain;
}

const scene::ProceduralGeometry* procedural(const scene::Scene& s, std::string_view name) {
    for (const scene::ProceduralGeometry& g : s.procedurals) {
        if (g.name == name) {
            return &g;
        }
    }
    return nullptr;
}

} // namespace

// ---- ADR-904: the program's two facts ---------------------------------------------------------------

TEST_CASE("A program says whether it writes emission and whether its emission reads the instance's",
          "[emission][material]") {
    using scene::MaterialInput;
    using scene::MaterialOpKind;
    scene::MaterialProgram p;
    p.name = "probe";

    SECTION("no emission output: neither") {
        p.ops = {input(0, MaterialInput::InstanceEmissive)};
        p.baseColorRegister = 0;
        CHECK_FALSE(p.writesEmission());
        CHECK_FALSE(p.emissionReadsInstance());
    }
    SECTION("a constant colour: writes, and leaves the variation to the engine") {
        p.ops = {constant(4, {0.1f, 0.9f, 0.5f, 1.0f})};
        p.emissionRegister = 4;
        CHECK(p.writesEmission());
        CHECK_FALSE(p.emissionReadsInstance());
    }
    SECTION("instanceEmissive multiplied into the emission: the program owns the variation") {
        p.ops = {input(4, MaterialInput::InstanceEmissive), constant(6, {0.1f, 0.9f, 0.5f, 1.0f}),
                 op(MaterialOpKind::Multiply, 4, 4, 6)};
        p.emissionRegister = 4;
        CHECK(p.writesEmission());
        CHECK(p.emissionReadsInstance());
    }
    SECTION("materialEmission counts the same way") {
        p.ops = {input(2, MaterialInput::MaterialEmission), op(MaterialOpKind::Power, 3, 2)};
        p.emissionRegister = 3;
        CHECK(p.emissionReadsInstance());
    }
    SECTION("read, but only into the base colour: the emission does not carry it") {
        p.ops = {input(0, MaterialInput::InstanceEmissive), constant(4, {1.0f, 0.5f, 0.2f, 1.0f})};
        p.baseColorRegister = 0;
        p.emissionRegister = 4;
        CHECK(p.writesEmission());
        CHECK_FALSE(p.emissionReadsInstance());
    }
    SECTION("read, then overwritten before the emission is taken: the data flow, not the op list") {
        p.ops = {input(4, MaterialInput::InstanceEmissive), constant(4, {1.0f, 0.5f, 0.2f, 1.0f})};
        p.emissionRegister = 4;
        CHECK_FALSE(p.emissionReadsInstance());
    }
    SECTION("a disabled read does not count, exactly as the interpreter skips it") {
        scene::MaterialOp read = input(5, MaterialInput::InstanceEmissive);
        read.enabled = false;
        p.ops = {constant(4, {1.0f, 0.5f, 0.2f, 1.0f}), read, op(MaterialOpKind::Multiply, 4, 4, 5)};
        p.emissionRegister = 4;
        CHECK_FALSE(p.emissionReadsInstance());
    }
    SECTION("a layer's emission is an emission, and a disabled layer's is not") {
        scene::MaterialLayer layer;
        layer.name = "glow";
        layer.ops = {input(3, MaterialInput::InstanceEmissive)};
        layer.emissionRegister = 3;
        p.layers.push_back(layer);
        CHECK(p.writesEmission());
        CHECK(p.emissionReadsInstance());
        p.layers[0].enabled = false;
        CHECK_FALSE(p.writesEmission());
        CHECK_FALSE(p.emissionReadsInstance());
    }
    SECTION("both facts are packed for the shader") {
        p.ops = {input(4, MaterialInput::InstanceEmissive)};
        p.emissionRegister = 4;
        const scene::MaterialProgramGpu gpu = scene::packMaterialProgramWithSlots(p, {});
        CHECK(gpu.flags.x == (scene::kMaterialWritesEmission | scene::kMaterialEmissionReadsInstance));
        p.ops = {constant(4, {1.0f, 1.0f, 1.0f, 1.0f})};
        CHECK(scene::packMaterialProgramWithSlots(p, {}).flags.x == scene::kMaterialWritesEmission);
        p.emissionRegister = -1;
        CHECK(scene::packMaterialProgramWithSlots(p, {}).flags.x == 0);
    }
}

TEST_CASE("materialEmission is the material's own emission as the instance shows it", "[emission][material]") {
    scene::MaterialProgram p;
    p.ops = {input(4, scene::MaterialInput::MaterialEmission)};
    p.emissionRegister = 4;
    p.emissionIntensity = 2.0f;
    scene::MaterialContext ctx;
    ctx.materialEmission = {0.3f, 0.1f, 0.9f, 1.0f};
    const scene::MaterialResult r = scene::evaluateMaterialProgram(p, ctx, scene::MaterialResult{});
    CHECK_THAT(d(r.emission.x), WithinAbs(0.6, 1e-6));
    CHECK_THAT(d(r.emission.y), WithinAbs(0.2, 1e-6));
    CHECK_THAT(d(r.emission.z), WithinAbs(1.8, 1e-6));
    CHECK(scene::materialInputFromName("materialEmission") == scene::MaterialInput::MaterialEmission);
    CHECK(std::string(scene::materialInputName(scene::MaterialInput::MaterialEmission)) == "materialEmission");
}

TEST_CASE("The library's scatter programs leave the instance's variation to the engine", "[emission][material]") {
    // ADR-904: every one of these multiplied `instanceEmissive` -- a ratio made against the
    // MATERIAL's colour -- into a colour of its own, and the shader then multiplied the same ratio
    // in again: m squared, and a hue that went wherever the ratios pushed it. Each now asserts its
    // colour and lets the engine apply the variation once, as a rotation of the displayed hue.
    const auto materials = std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "materials";
    for (const char* file : {"glowmere-tissue.material.json", "frond-glow.material.json", "bush-glow.material.json",
                             "canopy-fireflies.material.json", "pine-fireflies.material.json"}) {
        INFO(file);
        const auto program = scene::MaterialProgram::loadFile(materials / file);
        REQUIRE(program.has_value());
        CHECK(program->writesEmission());
        CHECK_FALSE(program->emissionReadsInstance());
    }
    // The heroes' programs keep reading it: a single placement's variation is identity, and their
    // op paths are what the Glowmere projects' saved values are keyed by.
    for (const char* file : {"glowmere2-tissue.material.json", "glowmere2-tissue-cool.material.json"}) {
        INFO(file);
        const auto program = scene::MaterialProgram::loadFile(materials / file);
        REQUIRE(program.has_value());
        CHECK(program->emissionReadsInstance());
    }
}

// ---- ADR-904: the variation, read back ------------------------------------------------------------

TEST_CASE("An instance's baked multiplier reads back as the rotation and gain it was made from",
          "[emission][color]") {
    // The fungi's authored purple, the shelf fungi's green-cyan, the beacons' cyan (GV3's layers), each
    // turned only as far as stays inside the sRGB gamut: past it the bake clamps a channel at zero and
    // the multiplier is no longer a pure rotation (what is read back is then the clipped colour's hue
    // and lightness -- the change the program-less path would show).
    struct Case {
        glm::vec3 base;
        std::vector<float> turns;
    };
    const std::vector<Case> cases{{{0.341f, 0.0821f, 1.0f}, {-0.1f, -0.07f, 0.0f, 0.035f, 0.16f, 0.25f}},
                                  {{0.05f, 1.0f, 0.62f}, {-0.2f, -0.07f, 0.0f, 0.15f, 0.25f}},
                                  {{0.06f, 0.82f, 1.0f}, {-0.2f, -0.07f, 0.0f, 0.035f, 0.16f}}};
    for (const Case& c : cases) {
        const glm::vec3 base = c.base;
        for (const float turns : c.turns) {
            for (const float gain : {0.35f, 1.0f, 1.65f}) {
                INFO("base " << base.x << "," << base.y << "," << base.z << " turns " << turns << " gain " << gain);
                const glm::vec2 v = color::emissionVariationOf(base, bakedMultiplier(base, turns, gain));
                CHECK_THAT(d(v.x), WithinAbs(d(turns), 2e-3));
                CHECK_THAT(d(v.y), WithinAbs(d(gain), 2e-3 * d(gain)));
            }
        }
    }
    // Sparsity: a specimen that does not light has no gain, and no hue to speak of.
    const glm::vec2 dark = color::emissionVariationOf({0.341f, 0.0821f, 1.0f}, glm::vec3(0.0f));
    CHECK(dark.y == 0.0f);
    CHECK(dark.x == 0.0f);
    // An achromatic base carried no hue in its multiplier (rotating grey does nothing), so none is
    // read back; the gain still is.
    const glm::vec2 grey = color::emissionVariationOf(glm::vec3(0.8f), bakedMultiplier(glm::vec3(0.8f), 0.2f, 1.3f));
    CHECK(grey.x == 0.0f);
    CHECK_THAT(d(grey.y), WithinAbs(1.3, 1e-3));
}

// ---- ADR-903: emissiveBoost is the node's emission lane -------------------------------------------

TEST_CASE("emissiveBoost lands on every drawable a node owns, after the program, and nowhere else",
          "[emission][composition]") {
    const auto glb = testsupport::writeTriangleGlb("emission_tri");
    assets::AssetRegistry registry{testsupport::processTempDir()};
    const std::string text = R"({
      "format": "avgen-scene", "version": 1, "name": "lanes",
      "materialPrograms": [
        { "name": "glow", "ops": [ { "kind": "constant", "dst": 4, "constant": [0.1, 0.9, 0.5, 1.0] } ],
          "emission": 4, "emissionIntensity": 2.0 } ],
      "nodes": [
        { "name": "lit", "kind": "procedural", "procedural": {
            "source": { "kind": "box", "size": [1, 1, 1] },
            "material": { "program": "glow", "emissiveColor": [1, 0, 0], "emissiveIntensity": 0.0 } } },
        { "name": "plain", "kind": "procedural", "procedural": {
            "source": { "kind": "box", "size": [1, 1, 1] },
            "material": { "emissiveColor": [1, 0.5, 0.2], "emissiveIntensity": 1.5 } } },
        { "name": "mesh", "kind": "gltf", "asset": "@GLB@" },
        { "name": "orb", "kind": "sdf", "sdf": { "tree": { "root": { "kind": "sphere", "radius": 1.0 } },
            "material": { "emissiveColor": [0.2, 0.4, 1.0], "emissiveIntensity": 0.8 } } },
        { "name": "sparks", "kind": "particles", "particles": { "spawnRate": 10.0, "emissive": 2.5 } }
      ]
    })";
    std::string filled = text;
    filled.replace(filled.find("@GLB@"), 5, glb.filename().string());
    const auto path = writeJson("boost", filled);
    auto comp = scene::Composition::loadFile(path.filename(), registry);
    if (!comp) {
        FAIL(comp.error().message);
    }
    params::ParameterSet params;
    params::Modulator modulator;
    (*comp)->attach(params, modulator);
    (*comp)->update(FrameTime{});
    const scene::Scene& s = (*comp)->scene();

    const auto* lit = procedural(s, "lit");
    const auto* plain = procedural(s, "plain");
    REQUIRE(lit != nullptr);
    REQUIRE(plain != nullptr);
    REQUIRE_FALSE(s.entities.empty());
    REQUIRE(s.sdfs.size() == 1);
    REQUIRE(s.particles.size() == 1);
    const float plainIntensity = plain->material.emissiveIntensity;
    const float meshIntensity = s.entities[0].material.emissiveIntensity;
    CHECK(lit->emissionGain == 1.0f);
    CHECK(plain->emissionGain == 1.0f);
    CHECK(s.entities[0].emissionGain == 1.0f);
    CHECK(s.sdfs[0].emissionGain == 1.0f);
    CHECK_THAT(d(s.particles[0].emissive), WithinAbs(2.5, 1e-6));

    for (const char* node : {"lit", "plain", "mesh", "orb", "sparks"}) {
        auto* boost = params.findAs<float>(std::string("nodes/") + node + "/emissiveBoost");
        REQUIRE(boost != nullptr);
        boost->setBase(3.0f);
    }
    // Several frames: the lane is written, never multiplied onto last frame's value.
    for (int frame = 0; frame < 3; ++frame) {
        params.resetFinals();
        (*comp)->update(FrameTime{});
    }
    CHECK(lit->emissionGain == 3.0f);
    CHECK(plain->emissionGain == 3.0f);
    CHECK(s.entities[0].emissionGain == 3.0f);
    CHECK(s.sdfs[0].emissionGain == 3.0f);
    CHECK_THAT(d(s.particles[0].emissive), WithinAbs(7.5, 1e-5));
    // ...and not through the material lane, which a program that writes emission discards (ADR-179):
    // the materials are exactly what the file said.
    CHECK(plain->material.emissiveIntensity == plainIntensity);
    CHECK(s.entities[0].material.emissiveIntensity == meshIntensity);

    // One node's boost is its own: moving "lit" back leaves the others where they are.
    params.findAs<float>("nodes/lit/emissiveBoost")->setBase(1.0f);
    params.resetFinals();
    (*comp)->update(FrameTime{});
    CHECK(lit->emissionGain == 1.0f);
    CHECK(plain->emissionGain == 3.0f);
    std::filesystem::remove(path);
    std::filesystem::remove(glb);
}

TEST_CASE("A route onto emissiveBoost reaches the lane the same frame", "[emission][composition][modulation]") {
    assets::AssetRegistry registry{testsupport::processTempDir()};
    const auto path = writeJson("boost_route", R"({
      "format": "avgen-scene", "version": 1, "name": "routed",
      "materialPrograms": [
        { "name": "glow", "ops": [ { "kind": "constant", "dst": 4, "constant": [0.1, 0.9, 0.5, 1.0] } ],
          "emission": 4 } ],
      "nodes": [ { "name": "lit", "kind": "procedural", "procedural": {
            "source": { "kind": "box", "size": [1, 1, 1] }, "material": { "program": "glow" } } } ]
    })");
    auto comp = scene::Composition::loadFile(path.filename(), registry);
    REQUIRE(comp.has_value());
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    const signals::SignalId kick = bus.declare("test.kick");
    (*comp)->attach(params, modulator);
    modulator.clearRoutes(); // the composition's default audio routes: this bus has no audio
    params::ModRoute route;
    route.source = "test.kick";
    route.target = "nodes/lit/emissiveBoost";
    route.amount = 2.0f;
    modulator.addRoute(route);
    REQUIRE(modulator.bind(bus, params).has_value());

    const auto frame = [&](float kickValue) {
        bus.set(kick, kickValue);
        params.resetFinals();
        modulator.applyRoutes(bus, params, 1.0 / 60.0);
        (*comp)->update(FrameTime{});
        return procedural((*comp)->scene(), "lit")->emissionGain;
    };
    CHECK(frame(0.0f) == 1.0f); // the control: a route at rest moves nothing
    CHECK(frame(1.0f) == 3.0f);
    CHECK(frame(0.0f) == 1.0f);
    std::filesystem::remove(path);
}

// ---- ADR-905: a material layer's intensity ----------------------------------------------------------

TEST_CASE("A program's base intensity is registered only where the base emits", "[emission][material][params]") {
    params::ParameterSet set;
    scene::MaterialProgram emits;
    emits.ops = {constant(4, {1.0f, 0.5f, 0.2f, 1.0f})};
    emits.emissionRegister = 4;
    const auto a = scene::registerMaterialProgramParameters(set, emits, "material/emits/");
    CHECK(a.emissionIntensity != nullptr);
    CHECK(set.find("material/emits/emissionIntensity") != nullptr);
    scene::MaterialProgram dark;
    dark.ops = {constant(0, {1.0f, 0.5f, 0.2f, 1.0f})};
    dark.baseColorRegister = 0;
    scene::MaterialLayer paint;
    paint.name = "paint";
    paint.baseColorRegister = 0; // a layer that paints and does not glow has no intensity either
    dark.layers.push_back(paint);
    const auto b = scene::registerMaterialProgramParameters(set, dark, "material/dark/");
    CHECK(b.emissionIntensity == nullptr);
    CHECK(b.layerEmissionIntensity.empty());
    CHECK(set.find("material/dark/emissionIntensity") == nullptr);
    CHECK(set.find("material/dark/layer/1/paint/emissionIntensity") == nullptr);
}

TEST_CASE("A material layer's emission intensity is a parameter, by index and name", "[emission][material][params]") {
    const auto program = scene::MaterialProgram::loadFile(std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" /
                                                          "materials" / "glowmere-fireflies-crown.material.json");
    REQUIRE(program.has_value());
    REQUIRE_FALSE(program->layers.empty());
    // Why it has to exist: the base of this program writes no emission at all, so its program-level
    // `emissionIntensity` -- the only one there was -- multiplies nothing.
    CHECK(program->emissionRegister == -1);
    params::ParameterSet set;
    const auto p = scene::registerMaterialProgramParameters(set, *program, "material/crown/");
    // ...so that one is not registered: a key on it is reported at load, not bound in silence.
    CHECK(set.find("material/crown/emissionIntensity") == nullptr);
    CHECK(p.emissionIntensity == nullptr);
    const std::string path = "material/crown/layer/1/" + program->layers[0].name + "/emissionIntensity";
    auto* intensity = set.findAs<float>(path);
    REQUIRE(intensity != nullptr);
    REQUIRE(p.layerEmissionIntensity.size() == program->layers.size());
    CHECK(intensity->base() == program->layers[0].emissionIntensity);
    intensity->setBase(program->layers[0].emissionIntensity * 0.25f);
    set.resetFinals();
    scene::MaterialProgram live;
    scene::applyMaterialProgramParameters(p, *program, live);
    CHECK(live.layers[0].emissionIntensity == program->layers[0].emissionIntensity * 0.25f);
    // It reaches the program's output: the layer's emission scales with it. The contexts sit where
    // this program lights at all -- its gate admits instanceRandom.w below 0.39 between 30 and 340 m,
    // and its mask wants the crown (local y above 3.9) near a voronoi cell's centre.
    scene::MaterialContext ctx;
    ctx.depth = 100.0f;
    scene::MaterialResult base;
    float before = 0.0f;
    float after = 0.0f;
    for (int i = 0; i < 256; ++i) {
        ctx.instanceRandom = glm::vec4(0.003f * static_cast<float>(i), 0.3f, 0.6f, 0.1f);
        ctx.localPosition = glm::vec3(0.13f * static_cast<float>(i % 16), 4.2f, 0.11f * static_cast<float>(i / 16));
        before += color::luminance(scene::evaluateMaterialProgram(*program, ctx, base).emission);
        after += color::luminance(scene::evaluateMaterialProgram(live, ctx, base).emission);
    }
    REQUIRE(before > 0.0f);
    CHECK_THAT(d(after / before), WithinAbs(0.25, 0.02));
    scene::unregisterMaterialProgramParameters(set, p);
    CHECK(set.find(path) == nullptr);
}

// ---- ADR-905: scatter layers' JSON --------------------------------------------------------------------

TEST_CASE("A scatter layer's emission lane round-trips and replants nothing", "[emission][ecology]") {
    const auto parsed = world::ecologyFromJson(nlohmann::json::parse(R"([
      { "name": "fungi", "asset": "x.gltf", "densities": { "marsh": 0.03 },
        "emissionGain": 1.5, "hueOffset": -0.1, "emissiveField": "ripple", "emissiveFieldAmount": 3.0 },
      { "name": "moss", "asset": "y.gltf", "densities": { "marsh": 0.01 } } ])"));
    REQUIRE(parsed.has_value());
    const world::ScatterLayer& fungi = parsed->layers[0];
    CHECK(fungi.emissionGain == 1.5f);
    CHECK(fungi.hueOffset == -0.1f);
    CHECK(fungi.emissiveField == "ripple");
    CHECK(fungi.emissiveFieldAmount == 3.0f);
    const nlohmann::json out = world::ecologyToJson(*parsed);
    CHECK(out[0].at("emissionGain") == 1.5f);
    CHECK(out[0].at("emissiveField") == "ripple");
    // Written only when set, so a scene that never heard of them round-trips byte-identically.
    for (const char* key : {"emissionGain", "hueOffset", "emissiveField", "emissiveFieldAmount"}) {
        CHECK_FALSE(out[1].contains(key));
    }
    // Per-frame values: none of the four may move the layer's structural hash, which seeds every
    // instance's variation and keys the terrain's cache.
    world::ScatterLayer moved = fungi;
    moved.emissionGain = 7.0f;
    moved.hueOffset = 0.4f;
    moved.emissiveField = "other";
    moved.emissiveFieldAmount = 0.0f;
    CHECK(moved.structuralHash() == fungi.structuralHash());
    CHECK_FALSE(world::ecologyFromJson(nlohmann::json::parse(
                    R"([{ "name": "a", "asset": "x", "densities": {}, "emissiveField": 3 }])"))
                    .has_value());
}

// ---- ADR-905: scatter layers' lanes and the ecology light, through a composition --------------------

TEST_CASE("A scatter layer's lane reaches every part of it, and the light it casts follows",
          "[emission][composition][ecology]") {
    const std::filesystem::path asset =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "assets/quaternius/glTF/Mushroom_Common.gltf";
    if (!std::filesystem::exists(asset)) {
        SKIP("the Quaternius library is not present in this checkout");
    }
    std::string text = R"({
      "format": "avgen-scene", "version": 1, "name": "eco",
      "camera": { "mode": 1, "position": [0, 20, 40], "target": [0, 0, 0], "fov": 50.0 },
      "environment": { "ecologyLight": 1.4, "ecologyLightRange": 400.0 },
      "materialPrograms": [
        { "name": "tissue", "ops": [ { "kind": "constant", "dst": 4, "constant": [0.08, 0.95, 0.53, 1.0] } ],
          "emission": 4, "emissionIntensity": 2.0 } ],
      "nodes": [
        { "name": "ground", "kind": "terrain", "world": { "name": "small", "size": [160, 160] },
          "terrain": { "chunkSize": 40.0, "resolution": 8, "lodLevels": 2, "viewDistance": 400.0 },
          "scatter": [
            { "name": "fungi", "asset": "@ASSET@", "densities": { "meadow": 0.02, "forest": 0.02 },
              "height": 0.5, "emissiveIntensity": 6.0, "emissiveColor": [0.34, 0.08, 1.0],
              "materialProgram": "tissue", "emissiveField": "ring", "emissiveFieldAmount": 2.0 },
            { "name": "beacons", "asset": "@ASSET@", "densities": { "meadow": 0.01, "forest": 0.01 },
              "height": 1.5, "emissiveIntensity": 6.0, "emissiveColor": [0.06, 0.82, 1.0] } ] },
        { "name": "ring", "kind": "field", "field": { "name": "ring", "kind": "wave" } }
      ]
    })";
    text.replace(text.find("@ASSET@"), 7, asset.string());
    text.replace(text.find("@ASSET@"), 7, asset.string());
    const auto path = writeJson("scatter_lanes", text);
    assets::AssetRegistry registry{testsupport::processTempDir()};
    auto comp = scene::Composition::loadFile(path.filename(), registry);
    if (!comp) {
        FAIL(comp.error().message);
    }
    params::ParameterSet params;
    params::Modulator modulator;
    (*comp)->attach(params, modulator);
    (*comp)->update(FrameTime{});
    const scene::Scene& s = (*comp)->scene();

    std::vector<const scene::ProceduralGeometry*> fungi;
    std::vector<const scene::ProceduralGeometry*> beacons;
    const std::string fungiName = scene::scatterObjectName("ground", "fungi");
    const std::string beaconName = scene::scatterObjectName("ground", "beacons");
    for (const scene::ProceduralGeometry& g : s.procedurals) {
        if (g.name.rfind(fungiName, 0) == 0) fungi.push_back(&g);
        if (g.name.rfind(beaconName, 0) == 0) beacons.push_back(&g);
    }
    REQUIRE_FALSE(fungi.empty());
    REQUIRE_FALSE(beacons.empty());
    for (const auto* g : fungi) {
        CHECK(g->emissiveField == "ring");
        CHECK(g->emissiveFieldAmount == 2.0f);
        CHECK(g->emissionGain == 1.0f);
        CHECK(g->emissionHue == 0.0f);
    }

    // Stable names: by the layer's name, not its index.
    auto* gain = params.findAs<float>("nodes/ground/scatter/fungi/emissionGain");
    auto* hue = params.findAs<float>("nodes/ground/scatter/fungi/hueOffset");
    auto* field = params.findAs<float>("nodes/ground/scatter/fungi/emissiveFieldAmount");
    auto* ecology = params.findAs<float>("scene/ecologyLight");
    REQUIRE(gain != nullptr);
    REQUIRE(hue != nullptr);
    REQUIRE(field != nullptr);
    REQUIRE(ecology != nullptr);
    CHECK(ecology->base() == 1.4f);
    REQUIRE(params.findAs<float>("nodes/ground/scatter/beacons/emissionGain") != nullptr);

    const auto ecologyLights = [&s](const std::string& colourOf) {
        (void)colourOf;
        double total = 0.0;
        std::size_t count = 0;
        for (const scene::PunctualLight& l : s.lights) {
            if (l.name.starts_with("ecology")) {
                total += static_cast<double>(l.intensity);
                ++count;
            }
        }
        return std::pair{total, count};
    };
    const auto [lightBefore, lightCount] = ecologyLights("");
    REQUIRE(lightCount > 0);

    gain->setBase(2.5f);
    hue->setBase(0.2f);
    field->setBase(4.0f);
    params.findAs<float>("nodes/ground/emissiveBoost")->setBase(2.0f);
    params.resetFinals();
    (*comp)->update(FrameTime{});
    for (const auto* g : fungi) { // every part of the layer, the terrain's boost under it
        CHECK(g->emissionGain == 5.0f);
        CHECK(g->emissionHue == 0.2f);
        CHECK(g->emissiveFieldAmount == 4.0f);
    }
    for (const auto* g : beacons) { // and the other layer only the terrain's boost
        CHECK(g->emissionGain == 2.0f);
        CHECK(g->emissionHue == 0.0f);
    }
    const auto [lightAfter, lightCountAfter] = ecologyLights("");
    CHECK(lightCountAfter == lightCount);
    CHECK(lightAfter > lightBefore * 1.9); // the boost doubles every layer's light, the gain more of the fungi's

    // The ecology light as a parameter: the light the mushrooms cast, and none at all at zero.
    ecology->setBase(0.0f);
    params.resetFinals();
    (*comp)->update(FrameTime{});
    CHECK(ecologyLights("").second == 0u);
    ecology->setBase(2.8f);
    params.resetFinals();
    (*comp)->update(FrameTime{});
    const auto [doubled, doubledCount] = ecologyLights("");
    CHECK(doubledCount == lightCount);
    CHECK_THAT(doubled, WithinAbs(lightAfter * 2.0, lightAfter * 1e-4));

    // A save keeps what the user set, on the layer and on the environment.
    const nlohmann::json saved = (*comp)->toJson();
    CHECK(saved.at("environment").at("ecologyLight") == 2.8f);
    const nlohmann::json& layers = saved.at("nodes")[0].at("scatter");
    CHECK(layers[0].at("emissionGain") == 2.5f);
    CHECK(layers[0].at("hueOffset") == 0.2f);
    CHECK_FALSE(layers[1].contains("emissionGain"));
    std::filesystem::remove(path);
}

// ---- ADR-906: a field's clock can start at an event ------------------------------------------------

TEST_CASE("A field's trigger round-trips, and a proximity is refused by name", "[emission][fields][trigger]") {
    auto f = spatial::FieldSpec::fromJson(nlohmann::json::parse(R"({
      "name": "ripple", "kind": "wave", "waveGeometry": "radial", "waveShape": "pulse",
      "trigger": { "source": "marker", "name": "drop" } })"));
    REQUIRE(f.has_value());
    REQUIRE(f->trigger.has_value());
    CHECK(f->trigger->source == world::TriggerSource::TimelineMarker);
    CHECK(f->trigger->name == "drop");
    const nlohmann::json j = f->toJson();
    REQUIRE(j.contains("trigger"));
    auto back = spatial::FieldSpec::fromJson(j);
    REQUIRE(back.has_value());
    CHECK(*back->trigger == *f->trigger);
    CHECK(back->structuralHash() == f->structuralHash());
    // A field without one writes none, and hashes as it always did.
    spatial::FieldSpec plain = *f;
    plain.trigger.reset();
    CHECK_FALSE(plain.toJson().contains("trigger"));
    CHECK(plain.structuralHash() != f->structuralHash());

    const auto proximity = spatial::FieldSpec::fromJson(nlohmann::json::parse(R"({
      "name": "near", "kind": "wave", "trigger": { "source": "proximity", "entity": "cow", "radius": 3 } })"));
    REQUIRE_FALSE(proximity.has_value());
    CHECK(proximity.error().message.find("proximity") != std::string::npos);
}

TEST_CASE("A triggered field counts from the latest event, is silent before the first, and is pure in time",
          "[emission][fields][trigger]") {
    spatial::FieldSet set;
    spatial::FieldSpec wave;
    wave.name = "ripple";
    wave.kind = spatial::FieldKind::Wave;
    wave.waveGeometry = spatial::WaveGeometry::Radial;
    wave.waveShape = spatial::WaveShape::Pulse;
    wave.wavelength = 2.0f;
    wave.waveSpeed = 10.0f;
    wave.waveWidth = 0.0f;
    world::Trigger trigger;
    trigger.source = world::TriggerSource::TimelineMarker;
    trigger.name = "drop";
    wave.trigger = trigger;
    set.fields.push_back(wave);

    world::TriggerClock clock;
    const std::array markers{world::TriggerMarker{4.0, "drop"}, world::TriggerMarker{9.0, "drop"},
                             world::TriggerMarker{6.0, "verse"}};
    clock.setMarkers(markers);

    const auto sampleAt = [&](double seconds, float radius) {
        scene::resolveFieldTriggers(set, &clock, seconds);
        return spatial::sampleScalar(set.fields[0], glm::vec3(radius, 0.0f, 0.0f), seconds, &set);
    };
    // Before the first drop: silent everywhere, the front nowhere.
    scene::resolveFieldTriggers(set, &clock, 3.0);
    CHECK(set.fields[0].silent());
    CHECK(sampleAt(3.0, 0.0f) == 0.0f);
    CHECK(sampleAt(3.0, 10.0f) == 0.0f);
    // 1 s after the drop at 4 s the front is 10 m out; 2 s after, 20 m. A verse marker is not a drop.
    CHECK(sampleAt(5.0, 10.0f) > 0.99f);
    CHECK(sampleAt(5.0, 20.0f) < 0.01f);
    CHECK(sampleAt(6.5, 25.0f) > 0.99f);
    CHECK_THAT(set.fields[0].triggerAge, WithinAbs(2.5, 1e-9));
    // The second drop restarts it.
    CHECK(sampleAt(9.5, 5.0f) > 0.99f);
    CHECK(sampleAt(9.5, 55.0f) < 0.01f);

    // Pure: the answer at a second is the same however the clock got there -- stepped from zero at
    // 60 fps, or asked cold, in either order.
    spatial::FieldSet stepped = set;
    for (int frame = 0; frame <= 600; ++frame) {
        scene::resolveFieldTriggers(stepped, &clock, static_cast<double>(frame) / 60.0);
    }
    spatial::FieldSet cold = set;
    scene::resolveFieldTriggers(cold, &clock, 10.0);
    CHECK(stepped.fields[0].triggerAge == cold.fields[0].triggerAge);
    // And no clock is silence, not the transport clock.
    scene::resolveFieldTriggers(set, nullptr, 5.0);
    CHECK(set.fields[0].silent());

    // A field WITHOUT a trigger is left alone and keeps the transport clock.
    spatial::FieldSpec free = wave;
    free.trigger.reset();
    free.triggerAge = 123.0;
    spatial::FieldSet freeSet;
    freeSet.fields.push_back(free);
    scene::resolveFieldTriggers(freeSet, &clock, 5.0);
    CHECK(freeSet.fields[0].triggerAge == 123.0);
    CHECK(freeSet.fields[0].clock(5.0) == 5.0);
}

TEST_CASE("A composition resolves its fields' triggers from the clock it is handed", "[emission][fields][trigger]") {
    assets::AssetRegistry registry{testsupport::processTempDir()};
    const auto path = writeJson("field_trigger", R"({
      "format": "avgen-scene", "version": 1, "name": "triggered",
      "nodes": [ { "name": "ripple", "kind": "field", "field": {
          "name": "ripple", "kind": "wave", "waveShape": "pulse",
          "trigger": { "source": "beat", "everyN": 4, "offset": 1 } } } ]
    })");
    auto comp = scene::Composition::loadFile(path.filename(), registry);
    if (!comp) {
        FAIL(comp.error().message);
    }
    params::ParameterSet params;
    params::Modulator modulator;
    (*comp)->attach(params, modulator);
    world::TriggerClock clock;
    std::vector<double> beats;
    for (int b = 0; b < 32; ++b) {
        beats.push_back(0.5 * b); // 120 BPM
    }
    clock.setBeats(beats);
    (*comp)->update(FrameTime{}); // no clock yet: silent
    const auto& fields = (*comp)->scene().fields.fields;
    REQUIRE(fields.size() == 1);
    CHECK(fields[0].silent());
    (*comp)->setTriggerClock(&clock);
    clock.setFrame(3.2);
    (*comp)->update(FrameTime{});
    // Every 4th beat from beat 1: 0.5, 2.5, 4.5 ... the latest at or before 3.2 s is 2.5 s.
    CHECK_THAT(fields[0].triggerAge, WithinAbs(0.7, 1e-9));
    std::filesystem::remove(path);
}
