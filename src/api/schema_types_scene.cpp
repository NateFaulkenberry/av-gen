// ADR-2152: the scene document's typing table (`schemas/avgen-scene.schema.json`).
//
// One entry per key of `kSceneKeys`, `kEnvironmentKeys`, `kSkyKeys`, `kAuthoredLightKeys` and the
// light rig's two lists. Each was typed by reading what `Composition::fromJsonImpl` (or the reader it
// hands the block to) does with the key; a block the Engine API PoC does not need is
// `x-avgen-untyped`. The `post` block's entries are generated from `scene::postFileKeys()`.
//
// "Valid" means the engine reads the value as written, without refusing it or warning about it. A
// value the parser silently ignores (a three-element colour written with four) is outside the type
// even though the file still loads.

#include "api/schema_types.hpp"

#include "scene/composition.hpp"
#include "scene/importance.hpp"
#include "scene/light_rig.hpp"
#include "scene/post_settings.hpp"
#include "scene/procedural.hpp"
#include "scene/scene_types.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace avgen::api {
namespace {

using namespace typing;

template <typename E, typename NameFn>
json namesOf(std::span<const E> values, NameFn name) {
    std::vector<std::string> out;
    for (const E v : values) {
        out.emplace_back(name(v));
    }
    return oneOfNames(out);
}

std::vector<std::string> keysOf(std::span<const std::string_view> keys) {
    return {keys.begin(), keys.end()};
}

json lightTypes() { return namesOf(scene::allLightTypes(), scene::lightTypeName); }
json lightRoles() { return namesOf(scene::allLightRoles(), scene::lightRoleName); }

json lightRigValue(std::string_view where) {
    return anyOf({string("a light rig file, relative to the scene file"), ref("lightRig")},
                 std::string("A light rig (ADR-033): a file path or the rig itself") + std::string(where));
}

void add(std::vector<TypingEntry>& out, std::string pointer, json fragment, bool writtenBack = true,
         std::string note = {}) {
    out.push_back({std::move(pointer), std::move(fragment), writtenBack, std::move(note)});
}

// ---- the document root (kSceneKeys) ----------------------------------------------------------------

void rootEntries(std::vector<TypingEntry>& e) {
    add(e, "/format", constant(scene::Composition::kFormatName));
    add(e, "/version", described(constant(scene::Composition::kFormatVersion), "Any other version is refused."));
    add(e, "/name", string("Defaults to the file's stem."));
    add(e, "/camera", ref("camera"));
    add(e, "/cameraDirection", untyped("The scene's camera rig and shots (CameraDirection::fromJson)."));
    add(e, "/lightRig", lightRigValue("; the environment's wins when both name one"), false,
        "written back as environment.lightRig, which the parser also reads");
    add(e, "/lights", arrayOf(ref("light"), "Authored lights (ADR-278); each registers lights/<id>/* parameters."),
        false, "an empty list is written as absent, which means the same");
    add(e, "/navBodyRadius", range(number("Metres; 0 keeps the navigator's default."), 0, 40));
    add(e, "/navCellSize", range(number("Metres; 0 disables pathfinding."), 0, 64));
    add(e, "/navWadeDepth", range(number("Metres; 0 stops walkers at the water."), 0, 8));
    add(e, "/wind", described(object({}, "drop"), "The weather (ADR-055); its keys are not typed yet."));
    e.back().fragment["x-avgen-untyped"] = true;
    add(e, "/post", ref("post"));
    add(e, "/environment", ref("environment"));
    add(e, "/composition", untyped("The 2D composition layer (CompositionData::fromJson)."));
    add(e, "/heroes", untyped("Hero points (world::HeroPoint)."), false,
        "an empty list is written as absent, which means the same");
    add(e, "/effects", untyped("Authored world effects (ADR-702)."));
    add(e, "/entityProfiles", string("An entity profile file, relative to the scene file."));
    add(e, "/entities", untyped("Entities (entity::entitiesFromJson)."), false,
        "an empty list is written as absent, which means the same");
    add(e, "/fields", untyped("Spatial fields (entity::fieldsFromJson)."));
    add(e, "/staging", untyped("Staging (stage::stagingFromJson)."));
    add(e, "/graph", untyped("A procedural graph file or document."));
    add(e, "/grids", untyped());
    add(e, "/materialPrograms", untyped());
    add(e, "/nodes", arrayOf(ref("node"), "The scene's objects, in order."));
    add(e, "/worldEvents", untyped());
    add(e, "/ecosystem", untyped("ADR-1200."));
    add(e, "/astral", untyped("ADR-1221."));
    add(e, "/clouds", untyped("ADR-1289."));
    add(e, "/attractorWorld", untyped("ADR-1850."));
    add(e, "/flight", untyped("ADR-1956."));
    add(e, "/meshWire", untyped("ADR-1862."));
    add(e, "/corruption", untyped("ADR-1873."));
    add(e, "/lunarWorld", untyped("ADR-1882."));
    add(e, "/portal", untyped("ADR-1879."));
    add(e, "/portals", untyped("ADR-1895; needs `portal`."));
    add(e, "/looks", untyped("ADR-1886: named parameter looks by portal crossing."));
    // ---- 115 Agent 3 (ADR-2225): the navigated crowd. Untyped until its shape settles; typed before the B3
    // crowd.* Session tools.
    add(e, "/crowd", untyped("ADR-2225: the navigated crowd (crowd::crowdFromJson): groups, walls, a time-keyed schedule."));
}

// ---- environment (kEnvironmentKeys) and its sky (kSkyKeys) ------------------------------------------

void environmentEntries(std::vector<TypingEntry>& e) {
    const auto env = [&](const char* key, json fragment, bool writtenBack = true, std::string note = {}) {
        add(e, std::string("/environment/") + key, std::move(fragment), writtenBack, std::move(note));
    };
    env("map", string("An HDR environment map, relative to the scene file."));
    env("lightRig", lightRigValue(""));
    env("intensity", number("Image-based lighting strength."));
    env("stylized", boolean());
    for (const char* key : {"rotation", "skyIntensity", "skyBloom", "ecologyLight", "ecologyLightRange",
                            "ecologyGlowCell", "ecologyPoolReach", "ecologyPoolFaintest", "fogHeightAmount",
                            "styledAmbientFloor", "waterSurface"}) {
        env(key, number());
    }
    env("fogSky", range(number("Clamped to 0..1."), 0, 1));
    env("fogSkyDistance", atLeast(number("Metres; 0 is automatic."), 0));
    env("fogSkyBeyond", atLeast(number(), 0));
    env("dayNight", untyped("The day/night cycle (ADR-343)."));
    env("skyTint", vec3("ADR-2103."));
    for (const char* key : {"skybox", "proceduralSkyBackground", "lightFromEnvironment", "fallbackAmbient",
                            "ambientOcclusion", "contactShadows", "depthPrepass", "glossyReflections"}) {
        env(key, boolean());
    }
    for (const char* key : {"fogColor", "background", "styledSkyAmbient", "styledGroundAmbient"}) {
        env(key, vec3("Linear RGB; any other shape is ignored."));
    }
    for (const char* key : {"fogSpectral", "waterDownwelling"}) {
        env(key, vec3());
    }
    for (const char* key : {"volumeDensity", "fogHeight", "fogHeightFalloff", "fogUpperDensity", "fogHeightCurve",
                            "fogGroundFollow", "fogPooling", "horizonDensity", "volumeShadowStrength",
                            "volumeScattering", "volumeAbsorption", "volumeAnisotropy", "volumeLocalLights",
                            "volumeNoise", "volumeNoiseScale", "volumeNoiseSpeed", "volumeEmission",
                            "volumeMaxDistance", "shadowRange", "shadowCasterReach", "aoFadeStart", "aoFadeEnd",
                            "aoRadius", "horizonFill", "shadowSplitLambda"}) {
        env(key, number());
    }
    env("tileDynamics", untyped("ADR-1953."));
    env("bands", untyped("Reflection bands (ADR-1151)."));
    env("vortex", untyped("The pre-ADR-387 vortex, read and converted into an effect."), false,
        "migrated to an effect instance in `effects` (ADR-441); not written back here");
    env("shadowCascades", atLeast(integer("0 = the quality tier; more than 4 is read as 4."), 0));
    env("shadowResolution",
        anyOf({constant(0), range(integer(), 256, 8192)}, "0 = the quality tier, or 256..8192 texels (ADR-1867)."));
    env("volumeSteps", integer("Clamped to 4..256."));
    env("volumeShadowSteps", integer("Clamped to 0..16."));
    env("volumeJitter", range(number("Clamped to 0..1."), 0, 1));
    env("volumeDensityField", string("A field name."));
    env("volumeColorField", string("A field name."));
    env("sky", ref("sky"));

    const auto sky = [&](const char* key, json fragment) {
        add(e, std::string("/environment/sky/") + key, std::move(fragment));
    };
    for (const char* key : {"enabled", "background", "useKeyLight"}) {
        sky(key, boolean());
    }
    for (const char* key : {"zenithColor", "horizonColor", "groundColor", "sunColor", "sunDirection"}) {
        sky(key, vec3("Any other shape is ignored."));
    }
    for (const char* key : {"haze", "sunIntensity", "sunSize", "sunGlow", "intensity"}) {
        sky(key, number());
    }
    sky("cirrus", untyped("ADR-1864."));
}

// ---- authored lights (kAuthoredLightKeys) and the light rig (rigFileKeys, rigLightKeys) -------------

void lightEntries(std::vector<TypingEntry>& e) {
    const auto light = [&](const char* key, json fragment) {
        add(e, std::string("/lights/*/") + key, std::move(fragment));
    };
    light("name", string("Required and non-empty: the light's identity in reports and parameter paths."));
    light("id", string("The parameter-path id, when it has stopped matching the name (after a rename)."));
    light("type", lightTypes());
    light("role", lightRoles());
    light("node", string("A node this light rides on."));
    for (const char* key : {"position", "direction", "up", "color"}) {
        light(key, vec3());
    }
    light("intensity", atLeast(number(), 0));
    for (const char* key : {"temperature", "tint", "range", "width", "height", "radius", "shadowStrength",
                            "shadowBias", "softness", "volumetric", "flicker", "flickerRate"}) {
        light(key, number());
    }
    light("innerCone", number("Degrees."));
    light("outerCone", number("Degrees."));
    for (const char* key : {"castsShadow", "contactShadow", "diffuseOnly", "specularOnly", "enabled"}) {
        light(key, boolean());
    }

    const auto rig = [&](const char* key, json fragment) {
        add(e, std::string("/lightRig/") + key, std::move(fragment));
    };
    rig("format", constant("avgen-lightrig"));
    {
        json version = integer("A version newer than 1 is refused.");
        version["maximum"] = 1;
        rig("version", std::move(version));
    }
    rig("name", string());
    rig("description", string());
    rig("keyIntensity", number());
    rig("ambientIntensity", number());
    rig("ambientColor", vec3());
    rig("ambientTemperature", number());
    rig("lights", arrayOf(ref("rigLight")));

    const auto rl = [&](const char* key, json fragment) {
        add(e, std::string("/lightRig/lights/*/") + key, std::move(fragment));
    };
    rl("name", string());
    rl("type", lightTypes());
    rl("role", lightRoles());
    rl("azimuth", number("Degrees from the camera's view direction about the subject's up axis."));
    rl("elevation", number("Degrees above the horizon."));
    rl("distance", number("Subject radii."));
    rl("size", number("Subject radii."));
    rl("cone", number("Spot cone, degrees."));
    for (const char* key : {"intensity", "temperature", "tint", "aspect", "shadowStrength", "softness", "volumetric"}) {
        rl(key, number());
    }
    rl("color", vec3());
    for (const char* key : {"castsShadow", "contactShadow", "followCamera"}) {
        rl(key, boolean());
    }
}

// ---- post (generated from scene::postFileKeys) --------------------------------------------------------

std::vector<std::string> postKeys() {
    std::vector<std::string> out;
    for (const scene::PostFileKey& k : scene::postFileKeys()) {
        out.emplace_back(k.key);
    }
    return out;
}

void postEntries(std::vector<TypingEntry>& e) {
    for (const scene::PostFileKey& k : scene::postFileKeys()) {
        json fragment;
        switch (k.type) {
        case scene::PostKeyType::Number: fragment = number(); break;
        case scene::PostKeyType::Boolean: fragment = boolean(); break;
        case scene::PostKeyType::Vec3: fragment = vec3(); break;
        case scene::PostKeyType::Vec2: fragment = numbers(2); break;
        case scene::PostKeyType::UnsignedInteger: fragment = atLeast(integer(), 0); break;
        }
        add(e, "/post/" + std::string(k.key), std::move(fragment));
    }
}

// ---- the keys ADR-2151 found a save does not keep, below any key list --------------------------------

void detailEntries(std::vector<TypingEntry>& e) {
    add(e, "/nodes/*/<unknown>", untyped(), false,
        "a node key the parser does not read is dropped on save (only `_` keys are kept): nodes have no key list "
        "yet (ADR-2151, reported)");
    add(e, "/nodes/*/world/biomes", untyped(), false,
        "an empty list is written as absent, and a world read without the key gets the default biomes "
        "(src/world/world_map.cpp worldMapToJson; ADR-2151, reported)");
}

// ---- authored $defs --------------------------------------------------------------------------------

json cameraDef() {
    return object({{"distance", number("Orbit distance; 0 or less fits the bounds.")},
                   {"height", number()},
                   {"orbitSpeed", number()},
                   {"orbitPivotWeight", range(number("Clamped to 0..1 (ADR-1123)."), 0, 1)},
                   {"orbitPivot", vec3()},
                   {"mode", range(integer("0..3; clamped."), 0, 3)},
                   {"journey", untyped("ADR-1042.")},
                   {"spline", string("A spline node the camera rides.")},
                   {"position", vec3()},
                   {"target", vec3()},
                   {"fov", number("Degrees.")},
                   {"near", atLeast(number("ADR-1058; 0 is automatic."), 0)},
                   {"far", atLeast(number("ADR-1280; 0 is automatic."), 0)}},
                  "drop");
}

json materialDef() {
    // The reader's keys (procedural `material`, and a node's `material` on terrain and orb nodes).
    // Optics (readMaterialOptics), toon and wire are further keys, not typed here.
    return object({{"baseColor", vec3()},
                   {"emissiveColor", vec3()},
                   {"opacity", number()},
                   {"emissiveIntensity", number()},
                   {"roughness", number()},
                   {"metallic", number()},
                   {"doubleSided", boolean("Procedural only.")},
                   {"unlit", boolean("Procedural only.")},
                   {"alphaCutoff", number("Procedural only.")},
                   {"baseColorTexture", string("Procedural only.")},
                   {"program", string()},
                   {"alphaMode", oneOfNames(std::vector<std::string>{"opaque", "mask", "blend"})},
                   {"toon", untyped("ADR-1071.")},
                   {"wire", untyped("ADR-1073.")}},
                  "drop");
}

json kindsExcept(std::initializer_list<scene::NodeKind> typed) {
    std::vector<std::string> names;
    for (const scene::NodeKind k : scene::allNodeKinds()) {
        if (std::find(typed.begin(), typed.end(), k) == typed.end()) {
            names.emplace_back(scene::nodeKindName(k));
        }
    }
    return oneOfNames(names);
}

json nodeDef() {
    using scene::NodeKind;
    // Keys every kind reads. Typed where the PoC needs them; the rest are named and untyped.
    json common = {
        {"name", string("Sanitised (`.`, `/` and space become `_`) and made unique.")},
        {"kind", namesOf(scene::allNodeKinds(), scene::nodeKindName)},
        {"asset", string("A path relative to the scene file, or an asset:// id.")},
        {"parent", string("Another node's name.")},
        {"position", vec3()},
        {"rotation", vec3("Euler degrees. Written back as Euler degrees from a quaternion (ADR-2151).")},
        {"scale", vec3()},
        {"visible", boolean()},
        {"locked", boolean()},
        {"castsShadow", boolean("ADR-1547.")},
        {"tint", vec3("Declaring one registers nodes/<name>/tint (ADR-1044).")},
        {"emissiveBoost", number()},
        {"roughnessScale", number()},
        {"opacity", range(number(), 0, 1)},
        {"importance", namesOf(scene::allImportances(), scene::importanceName)},
        {"temporalSlot", range(integer("ADR-1934."), 0, 16)},
        {"tags", arrayOf(string(), "Non-empty strings.")},
        {"program", string("ADR-1572.")},
        {"material", ref("material")},
        {"journey", untyped("ADR-1042: {distance}.")},
        {"bob", string("A cloud formation's name (ADR-2112).")},
        {"energy", untyped()},
        {"wind", untyped()},
        {"canopySource", untyped()},
        {"canopyFrom", untyped()},
        {"vortexAttractor", untyped()},
        {"vortexReach", untyped()},
        {"float", untyped()},
        {"procedural", ref("procedural")},
    };
    json gltf = {{"kind", constant("gltf")},
                 {"lod",
                  json{{"type", "object"},
                       {"additionalProperties", false},
                       {"properties",
                        {{"enabled", boolean()},
                         {"thinning", boolean()},
                         {"hysteresis", number()},
                         {"maxScreenError", number()},
                         {"ratios", arrayOf(number())}}},
                       {"description", "ADR-351; an unknown key is refused."}}},
                 {"animation", untyped("ADR-086: a skinned character's opening state.")},
                 {"waterMaterials", untyped("ADR-2020: glTF material name -> water.")}};
    json group = {{"kind", constant("group")}};
    json procedural = {{"kind", constant("procedural")}};
    json typedKinds = json::array({
        json{{"properties", gltf}, {"description", "gltf: a glTF asset."}},
        json{{"properties", group}, {"description", "group: an empty transform that parents other nodes."}},
        json{{"properties", procedural}, {"description", "procedural: geometry from the `procedural` block."}},
        json{{"properties", {{"kind", kindsExcept({NodeKind::Gltf, NodeKind::Group, NodeKind::Procedural})}}},
             {"x-avgen-untyped", true},
             {"description", "Every other kind: its own blocks (world, terrain, scatter, particles, ...) are not typed."}},
    });
    json node = object(std::move(common), "drop", {"kind"});
    node["anyOf"] = std::move(typedKinds);
    node["description"] = "A scene node. A key the parser does not read is dropped on save; `_` keys are kept.";
    return node;
}

json proceduralDef() {
    using scene::DistributionKind;
    using scene::PrimitiveKind;
    std::vector<std::string> otherSources;
    for (const PrimitiveKind k : scene::allPrimitiveKinds()) {
        if (k != PrimitiveKind::Box) {
            otherSources.emplace_back(scene::primitiveKindName(k));
        }
    }
    std::vector<std::string> otherDistributions;
    for (const DistributionKind k : scene::allDistributionKinds()) {
        if (k != DistributionKind::Single) {
            otherDistributions.emplace_back(scene::distributionKindName(k));
        }
    }
    json source = anyOf(
        {json{{"type", "object"},
              {"properties",
               {{"kind", constant(scene::primitiveKindName(PrimitiveKind::Box))},
                {"size", vec3("Metres.")},
                {"subdivisions", integer()},
                {"bevel", number()},
                {"bevelSegments", integer()}}},
              {"required", {"kind"}},
              {"description", "box"}},
         json{{"type", "object"},
              {"properties", {{"kind", oneOfNames(otherSources)}}},
              {"x-avgen-untyped", true},
              {"description", "Every other primitive kind, untyped."}}},
        "What is placed (SourceSpec).");
    json distribution = anyOf(
        {json{{"type", "object"},
              {"properties",
               {{"kind", constant(scene::distributionKindName(DistributionKind::Single))}, {"count", integer()}}},
              {"required", {"kind"}},
              {"description", "single: one copy."}},
         json{{"type", "object"},
              {"properties", {{"kind", oneOfNames(otherDistributions)}}},
              {"x-avgen-untyped", true},
              {"description", "Every other distribution kind, untyped."}}},
        "Where copies go (Distribution).");
    json props = {{"source", std::move(source)},
                  {"distribution", std::move(distribution)},
                  {"material", ref("material")},
                  {"name", string()},
                  {"partOf", string()},
                  {"visible", boolean()},
                  {"castsShadow", boolean()},
                  {"emissiveField", string()},
                  {"emissiveFieldAmount", number()},
                  {"normalStep", atLeast(number("Metres."), 0)}};
    for (const char* key : {"flatFarRungs", "slabs", "slabFarPixels", "slabSidePixels", "slabEdgeOn",
                            "slabDebugFaces", "sourceTransform", "distributionTransform", "extraLane", "variation",
                            "deformers", "ops", "effectors", "motion", "lod", "partMaterials", "materialVariation",
                            "hierarchy", "grammar"}) {
        props[key] = untyped();
    }
    return described(object(std::move(props), "drop"),
                     "Procedural geometry (ProceduralGeometry::fromJson): a source, a distribution and a material. "
                     "Typed for a box placed once; every other source and distribution kind is untyped.");
}

} // namespace

json sceneNodeDefs() {
    return {{"node", nodeDef()}, {"procedural", proceduralDef()}, {"material", materialDef()}};
}

DocumentTyping sceneTyping() {
    DocumentTyping t;
    t.tableFile = "src/api/schema_types_scene.cpp";
    t.keyLists = {
        {"", "", keysOf(scene::sceneFileKeys()), "kSceneKeys in src/scene/composition.cpp",
         object({}, "warn", {"format", "version"})},
        {"/environment", "environment", keysOf(scene::sceneEnvironmentKeys()),
         "kEnvironmentKeys in src/scene/composition.cpp", object({}, "warn-drop")},
        {"/environment/sky", "sky", keysOf(scene::sceneSkyKeys()), "kSkyKeys in src/scene/composition.cpp",
         object({}, "warn-drop")},
        {"/lights/*", "light", keysOf(scene::sceneLightKeys()), "kAuthoredLightKeys in src/scene/composition.cpp",
         object({}, "warn-drop", {"name"})},
        {"/lightRig", "lightRig", keysOf(scene::rigFileKeys()), "kRigKeys in src/scene/light_rig.cpp",
         object({}, "warn-drop", {"lights"})},
        {"/lightRig/lights/*", "rigLight", keysOf(scene::rigLightKeys()), "kRigLightKeys in src/scene/light_rig.cpp",
         object({}, "warn-drop")},
        {"/post", "post", postKeys(), "scene::postFileKeys() in src/scene/post_settings.cpp",
         described(object({}, "warn"), "Post-processing base values (ADR-059); kept verbatim and written back.")},
    };
    rootEntries(t.entries);
    environmentEntries(t.entries);
    lightEntries(t.entries);
    postEntries(t.entries);
    detailEntries(t.entries);
    t.defs = sceneNodeDefs();
    t.defs["camera"] = cameraDef();
    return t;
}

} // namespace avgen::api
