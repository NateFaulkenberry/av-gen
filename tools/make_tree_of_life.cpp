// Emits the Tree of Life showcase: the scene file, the project file, the light rig, and the entry
// in examples/index.json that puts it in the menu.
//
// A build target rather than a script, for the reason tools/CMakeLists.txt already gives: it reads
// `scene::TreeLook`, `scene::TreeCameraView` and `scene::treeSchema()` directly, so the shipped
// scene carries exactly the numbers the C++ scene assembler was tuned with. A Python emitter would
// hold a second copy of every one of them and they would drift apart silently.
//
// The tree itself is NOT baked. Each node carries a `GeneratedSource` -- the hero's nineteen
// parameters, with the generator name, version, schema hash and candidate index -- so the scene
// regenerates it and an artist moving a slider gets a different tree.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "assets/image.hpp"
#include "scene/composition.hpp"
#include "core/log.hpp"
#include "scene/tree_foliage.hpp"
#include "scene/tree_generated.hpp"
#include "scene/tree_generator.hpp"
#include "scene/tree_scene.hpp"
#include "search/candidate_search.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>

using namespace avgen;
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

json vec3(const glm::vec3& v) {
    return json::array({v.x, v.y, v.z});
}

// Azimuth measured from world +Z toward +X, elevation above the horizon: the rig's convention, from
// the direction the light travels (which is what a PunctualLight stores).
json rigLight(const std::string& name, const std::string& type, const std::string& role,
              const glm::vec3& travel, float intensityRatio, const glm::vec3& color, float temperature,
              bool castsShadow, float volumetric, float size = 1.0f) {
    const glm::vec3 from = -glm::normalize(travel);
    json light;
    light["name"] = name;
    light["type"] = type;
    light["role"] = role;
    light["azimuth"] = glm::degrees(std::atan2(from.x, from.z));
    light["elevation"] = glm::degrees(std::asin(std::clamp(from.y, -1.0f, 1.0f)));
    light["distance"] = 3.0;
    light["intensity"] = intensityRatio;
    light["color"] = vec3(color);
    light["temperature"] = temperature;
    light["tint"] = 0.0;
    light["size"] = size;
    light["aspect"] = 1.0;
    light["castsShadow"] = castsShadow;
    light["volumetric"] = volumetric;
    light["followCamera"] = false;
    return light;
}

json generatedSource(const search::GeneratorSchema& schema, const search::Parameters& values,
                     std::uint32_t index, const char* generator) {
    json g;
    g["generator"] = generator;
    g["generatorVersion"] = schema.generatorVersion;
    g["schemaHash"] = schema.hash();
    g["index"] = index;
    g["values"] = values;
    return g;
}

json materialJson(const glm::vec3& base, float roughness, const glm::vec3& emissive, float intensity,
                  bool doubleSided, const std::string& program) {
    json m;
    m["baseColor"] = vec3(base);
    m["roughness"] = roughness;
    m["emissiveColor"] = vec3(emissive);
    m["emissiveIntensity"] = intensity;
    if (doubleSided) {
        m["doubleSided"] = true;
    }
    if (!program.empty()) {
        m["program"] = program;
    }
    return m;
}

json treeNode(const std::string& name, const json& source, int part, const json& material,
              const std::optional<json>& motion) {
    json node;
    node["name"] = name;
    node["kind"] = "procedural";
    json procedural;
    procedural["source"] = json{{"kind", "generated"}, {"generated", source}, {"generatedPart", part}};
    procedural["distribution"] = json{{"kind", "single"}};
    procedural["material"] = material;
    if (motion) {
        procedural["motion"] = *motion;
    }
    node["procedural"] = procedural;
    return node;
}

// How a tier answers the wind. The skinned rig (ADR-177) is the runtime animation path and is not
// expressible in a scene file, so a loaded scene falls back to the engine's own vegetation motion:
// mass and stiffness per tier reproduce the amplitude ladder, without the transform inheritance.
json motionFor(float mass, float stiffness, float tipAmplitude) {
    return json{{"mass", mass},        {"stiffness", stiffness}, {"damping", 0.62},
                {"windSensitivity", 0.5}, {"bendLimit", 0.05},   {"tipAmplitude", tipAmplitude}};
}

} // namespace

int main(int argc, char** argv) {
    log::init(log::Level::Info);
    const fs::path root = argc > 1 ? fs::path(argv[1]) : fs::path(AVGEN_SOURCE_DIR);
    const fs::path dir = root / "examples" / "tree";
    fs::create_directories(dir);

    const scene::TreeLook look;
    const scene::TreeCameraView camera;
    const search::GeneratorSchema schema = scene::treeSchema();
    const search::Parameters values = search::sampleAt(schema.parameters, scene::kHeroCandidate);
    const json source = generatedSource(schema, values, scene::kHeroCandidate, "tree");
    const json envSource = generatedSource(schema, values, scene::kHeroCandidate, "tree-environment");

    // The leaf-spray mask, written beside the scene. It is generated rather than an imported asset
    // -- the tree is a pure function of its parameters and an asset would break that -- but it has
    // to reach the scene file as a PATH, because a scene file cannot carry a texture inline and a
    // procedural node's material now resolves one the same way a glTF node does.
    scene::LeafSpraySettings spray = look.leafSpray;
    // The same seed the generator gives the spray, so the shipped PNG is the one the C++
    // scene assembler would have made in memory.
    spray.seed = (1u + static_cast<std::uint32_t>(std::lround(values.back()))) ^ 0x1EAFu;
    const scene::TextureData leaf = scene::makeLeafSprayTexture(spray);
    if (auto ok = assets::writePng(dir / "leaf-spray.png", leaf.width, leaf.height, leaf.data); !ok) {
        std::cerr << "leaf texture: " << ok.error().message << "\n";
        return 1;
    }

    // ---- the light rig ------------------------------------------------------------------------
    // Intensities are ratios against keyIntensity, and the directional kinds pass through as-is, so
    // a rig whose key is the scene's key intensity reproduces the hand-tuned values exactly for the
    // three directionals. The disk is an area emitter and the rig converts it by distance and
    // emitter area, so its ratio was solved for and then checked against a render.
    json rig;
    rig["format"] = "avgen-lightrig";
    rig["version"] = 1;
    // No spaces: a rig's name becomes a parameter path ("lightrig/<name>/..."), which an existing
    // test enforces and which caught this. CamelCase is the shipped convention.
    rig["name"] = "TreeOfLife";
    rig["description"] =
        "A moon key for the silhouette, a violet rim for separation, almost no fill, and a wide weak "
        "disk under the crown so its underside has layers rather than being flat.";
    rig["keyIntensity"] = look.keyIntensity;
    rig["ambientIntensity"] = 0.03;
    rig["ambientColor"] = vec3(glm::vec3(0.16f, 0.30f, 0.46f));
    rig["ambientTemperature"] = 9000;
    json lights = json::array();
    lights.push_back(rigLight("moon", "directional", "key", glm::vec3(-0.42f, -0.68f, -0.60f), 1.0f,
                              glm::vec3(1.0f), 8200.0f, true, 0.22f));
    lights.push_back(rigLight("rim", "directional", "rim", glm::vec3(0.55f, -0.22f, 0.78f),
                              look.rimIntensity / look.keyIntensity, glm::vec3(0.72f, 0.58f, 1.0f), 7400.0f,
                              false, 0.0f));
    lights.push_back(rigLight("skyfill", "directional", "fill", glm::vec3(0.15f, -1.0f, 0.1f),
                              look.fillIntensity / look.keyIntensity, glm::vec3(1.0f), 11000.0f, false, 0.0f));
    lights.back()["castsShadow"] = false;
    rig["lights"] = lights;
    {
        std::ofstream out(root / "examples" / "lightrigs" / "tree-of-life.rig.json");
        out << rig.dump(2) << "\n";
    }

    // ---- the scene ----------------------------------------------------------------------------
    json scene;
    scene["format"] = "avgen-scene";
    scene["version"] = 1;
    scene["name"] = "tree of life";
    scene["camera"] = json{{"mode", 1},
                           {"position", vec3(camera.eye)},
                           {"target", vec3(camera.target)},
                           {"fov", glm::degrees(camera.fovYRadians)},
                           {"orbitSpeed", 0.0}};
    scene["lightRig"] = "../lightrigs/tree-of-life.rig.json";

    json environment;
    environment["fogColor"] = vec3(look.fogColor);
    environment["fogDensity"] = look.fogDensity;
    environment["fogHeight"] = look.fogHeight;
    environment["fogHeightFalloff"] = look.fogHeightFalloff;
    environment["volumeDensity"] = look.volumeDensity;
    environment["volumeAnisotropy"] = 0.12;
    environment["volumeSteps"] = 24;
    environment["volumeMaxDistance"] = 180.0;
    environment["intensity"] = 0.30;
    environment["skyIntensity"] = 0.55;
    environment["sky"] = json{{"zenithColor", vec3(look.zenith)},
                              {"horizonColor", vec3(look.horizon)},
                              {"background", true},
                              {"sunIntensity", 0.0}};
    scene["environment"] = environment;

    scene["post"] = json{{"tonemap", 1},          {"bloomEnabled", true}, {"bloomIntensity", 0.20},
                         {"bloomThreshold", 1.0}, {"bloomKnee", 0.5},     {"bloomRadius", 1.15},
                         {"bloomEmissionWeight", 0.75}, {"antialias", 0.75}, {"vignette", 0.22}};

    // The vein program, emitted from the same generator the C++ scene uses.
    scene::VeinSettings veins = look.veins;
    veins.color = look.veinColor;
    json programs = json::array();
    programs.push_back(scene::makeVeinProgram("tree.veins", veins).toJson());
    scene["materialPrograms"] = programs;

    json nodes = json::array();
    const json branchMaterial =
        materialJson(look.barkColor, look.barkRoughness, look.veinColor, 0.0f, false, "tree.veins");
    const json rootMaterial = materialJson(look.rootColor, 0.85f, look.veinColor, 0.0f, false, "tree.veins");
    nodes.push_back(treeNode("tree-roots", source, 0, rootMaterial, std::nullopt));
    nodes.push_back(treeNode("tree-trunk", source, 1, branchMaterial, motionFor(140.0f, 22.0f, 0.008f)));
    nodes.push_back(treeNode("tree-primary", source, 2, branchMaterial, motionFor(24.0f, 6.0f, 0.03f)));
    nodes.push_back(treeNode("tree-secondary", source, 3, branchMaterial, motionFor(4.0f, 1.6f, 0.07f)));
    nodes.push_back(treeNode("tree-tertiary", source, 4, branchMaterial, motionFor(0.8f, 0.7f, 0.12f)));
    for (int t = 0; t < scene::kFoliageTints; ++t) {
        const auto i = static_cast<std::size_t>(t);
        json m = materialJson(look.foliageColor[i], look.foliageRoughness, look.foliageEmissiveTint[i],
                              look.foliageEmissiveIntensity * look.foliageEmissiveScale[i], true, "");
        // Cutout, not blend: masked geometry is in the depth prepass and takes the shadow mask,
        // blended geometry is in neither, and a canopy that does not self-shadow has no interior.
        m["alphaMode"] = "mask";
        m["alphaCutoff"] = look.foliageAlphaCutoff;
        m["baseColorTexture"] = "leaf-spray.png";
        nodes.push_back(treeNode(fmt::format("tree-foliage{}", t), source, 5 + t, m,
                                 motionFor(0.35f, 0.55f, 0.16f)));
    }
    // The environment. Without it the tree loads into an empty world and reads as an eight-metre
    // tree again -- an empty ground plane has no size, which is the whole of why these exist.
    nodes.push_back(treeNode("tree-ground", envSource, 0,
                             materialJson(look.groundColor, 0.92f, glm::vec3(0.0f), 0.0f, false, ""),
                             std::nullopt));
    json distant = treeNode("tree-distant", envSource, 1,
                            materialJson(look.barkColor * 0.6f, 0.95f, glm::vec3(0.0f), 0.0f, false, ""),
                            std::nullopt);
    // They must not cast. The hero owns the one cascaded directional light this renderer allows, and
    // a ring of distant trees inside the cascade fit pushes its far plane out and coarsens every
    // shadow on the hero itself.
    distant["procedural"]["castsShadow"] = false;
    nodes.push_back(distant);
    scene["nodes"] = nodes;

    {
        std::ofstream out(dir / "tree.scene.json");
        out << scene.dump(2) << "\n";
    }

    // ---- the project --------------------------------------------------------------------------
    json project;
    project["format"] = "avgen-project";
    project["version"] = 4;
    project["app"] = json{{"name", "Tree of Life"}};
    project["assets"] = json{{"scene", json{{"kind", "composition"}, {"path", "tree.scene.json"}}}};
    // The audio mapping (phase 9): audio drives the WIND, never a branch's transform, so the signal
    // moves the spring's target and the spring decides how the joint gets there.
    json routes = json::array();
    const auto route = [](const char* src, const char* dst, float amount, float attack, float decay) {
        return json{{"source", src}, {"target", dst}, {"amount", amount}, {"op", "add"},
                    {"chain", json{{"attackMs", attack}, {"decayMs", decay}}}};
    };
    routes.push_back(route("audio.bass", "scene/windSpeed", 0.10f, 900.0f, 3000.0f));
    // There is no separate gust parameter -- the wind field has one speed knob and generates its own
    // gusts -- so the low mids join the bass on it at a shorter time constant rather than being
    // routed somewhere that does not exist.
    routes.push_back(route("audio.lowMid", "scene/windSpeed", 0.06f, 400.0f, 1600.0f));
    routes.push_back(route("music.build", "scene/windSpeed", 0.16f, 2500.0f, 4000.0f));
    routes.push_back(route("music.break", "scene/windSpeed", -0.13f, 2000.0f, 3500.0f));
    for (int t = 0; t < scene::kFoliageTints; ++t) {
        // `material/emissive`, not `material/emissiveIntensity`: the registered path is the one
        // `registerProceduralParameters` writes, and a route naming anything else resolves to
        // nothing. The engine says so at load, which is how this was caught.
        routes.push_back(route("audio.rms", fmt::format("procedural/tree-foliage{}/material/emissive", t).c_str(),
                               0.22f, 700.0f, 2400.0f));
    }
    project["routes"] = routes;
    {
        std::ofstream out(dir / "tree.json");
        out << project.dump(2) << "\n";
    }

    // ---- the menu entry -----------------------------------------------------------------------
    const fs::path indexPath = root / "examples" / "index.json";
    std::ifstream in(indexPath);
    json index = json::parse(in, nullptr, false);
    if (index.is_discarded() || !index.contains("examples")) {
        std::cerr << "examples/index.json could not be read\n";
        return 1;
    }
    bool present = false;
    for (json& entry : index["examples"]) {
        if (entry.value("name", std::string{}) == "The Tree of Life") {
            present = true;
            entry["project"] = "tree/tree.json";
        }
    }
    if (!present) {
        index["examples"].push_back(json{
            {"name", "The Tree of Life"},
            {"category", "Showcase"},
            {"description",
             "One generated tree, chosen from ninety-six candidates by a person after the evaluator "
             "ranked their architecture. Eight nodes carry one parameter vector: the scene "
             "regenerates the tree rather than storing it."},
            {"project", "tree/tree.json"}});
    }
    {
        std::ofstream out(indexPath);
        out << index.dump(2) << "\n";
    }
    // VERIFY BY LOADING IT. A scene file ignores unknown keys by design -- that is what lets an old
    // reader open a new file -- so a misspelled key is not an error, it is a setting that silently
    // does nothing. `zenith` instead of `zenithColor` was exactly that: the sky kept its bright
    // default and the whole shot came back washed out with nothing in any log to say why. Reading
    // the values back and comparing them against what was meant is the only thing that catches it.
    {
        assets::AssetRegistry registry(dir);
        auto loaded = scene::Composition::loadFile(dir / "tree.scene.json", registry);
        if (!loaded) {
            std::cerr << "the emitted scene does not load: " << loaded.error().message << "\n";
            return 1;
        }
        // A `Scene` is a per-frame derivation, so it does not exist until the composition has been
        // attached to a parameter set and updated once. Reading `scene()` straight after `loadFile`
        // returns defaults for everything, which is a convincing way to fail this check for the
        // wrong reason.
        params::ParameterSet parameters;
        params::Modulator modulator;
        (*loaded)->attach(parameters, modulator);
        (*loaded)->update(FrameTime{});
        const scene::Scene& s = (*loaded)->scene();
        int problems = 0;
        const auto near = [&problems](const char* what, float got, float want) {
            if (std::abs(got - want) > 1e-3f) {
                std::cerr << fmt::format("  {}: file says {}, meant {}\n", what, got, want);
                ++problems;
            }
        };
        near("sky zenith r", s.environment.sky.zenithColor.r, look.zenith.r);
        near("sky horizon g", s.environment.sky.horizonColor.g, look.horizon.g);
        near("fog density", s.environment.fogDensity, look.fogDensity);
        near("fog height", s.environment.fogHeight, look.fogHeight);
        near("volume density", s.environment.volumeDensity, look.volumeDensity);
        near("camera fov", s.camera.fovYRadians, camera.fovYRadians);
        near("camera eye x", s.camera.position.x, camera.eye.x);
        if ((*loaded)->nodes().size() != 10) {
            std::cerr << fmt::format("  nodes: file has {}, meant 10\n", (*loaded)->nodes().size());
            ++problems;
        }

        // And then SAVE IT BACK AND RELOAD IT, which is the requirement an editable showcase has to
        // meet and a different one from "the file I wrote loads". A scene that opens correctly and
        // saves wrong is worse than one that never opened: the artist's work is what is lost.
        const fs::path resaved = fs::temp_directory_path() / "avgen_tree_resave.scene.json";
        if (auto ok = (*loaded)->saveFile(resaved); !ok) {
            std::cerr << "the loaded scene does not save: " << ok.error().message << "\n";
            return 1;
        }
        assets::AssetRegistry again(dir);
        auto reloaded = scene::Composition::loadFile(resaved, again);
        if (!reloaded) {
            std::cerr << "the re-saved scene does not load: " << reloaded.error().message << "\n";
            return 1;
        }
        if ((*reloaded)->nodes().size() != (*loaded)->nodes().size()) {
            std::cerr << fmt::format("  re-save lost nodes: {} -> {}\n", (*loaded)->nodes().size(),
                                     (*reloaded)->nodes().size());
            ++problems;
        }
        for (const auto& node : (*reloaded)->nodes()) {
            const auto& src = node->procedural.source;
            if (src.kind != scene::PrimitiveKind::Generated) {
                continue;
            }
            // The parameter vector is the thing the scene owns. If a re-save drops it the node
            // regenerates a different organism, or nothing, under the same name.
            if (src.generated.values.size() != schema.parameters.size()) {
                std::cerr << fmt::format("  node '{}': re-save has {} parameter values, meant {}\n",
                                         node->name, src.generated.values.size(), schema.parameters.size());
                ++problems;
            }
            if (src.generated.schemaHash != schema.hash()) {
                std::cerr << fmt::format("  node '{}': re-save lost the schema hash\n", node->name);
                ++problems;
            }
        }
        fs::remove(resaved);

        if (problems > 0) {
            std::cerr << problems << " emitted setting(s) did not survive the round trip\n";
            return 1;
        }
    }

    std::cout << "wrote " << (dir / "tree.scene.json").string() << "\n"
              << "      " << (dir / "tree.json").string() << "\n"
              << "      " << (root / "examples" / "lightrigs" / "tree-of-life.rig.json").string() << "\n"
              << "      " << indexPath.string() << "\n";
    return 0;
}
