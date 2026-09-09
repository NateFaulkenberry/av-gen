// The built-in node types of the procedural graph (ADR-028). Every evaluator is a pure function
// of its resolved inputs, its parameters and the evaluation time; Output nodes are the only ones
// that write into the GraphOutput.
//
// Three encodings the header cannot express (documented in docs/procedural-graph.md):
//
// (a) Deformers travel as PinType::Any values holding the JSON text of a scene::Deformer (the
//     member names of scene/procedural.cpp's deformer block, plus the Path fields "spline",
//     "pathOffset", "pathScale" and "pathRoll"). `output/procedural` collects them from its multi
//     input `deformers` in link order and parses them into the emitted deformer stack.
// (b) Effectors come in two flavours. `effectors/effector` applies its field to a point cloud on
//     the CPU at evaluation time (static shaping of the cloud). Live GPU effectors are declared
//     on `output/procedural`: its multi `fields` input names the fields (which are emitted with
//     the object so the renderer can bind them) and its `effectors` parameter is a JSON array of
//     {op, blend, strength, weight, axis, scaleAxis, target}, entry i belonging to linked field i.
// (c) `output/procedural` prefers a Distribution specification: every distributions/* node has a
//     String output pin `spec` carrying its Distribution as JSON, and the output node uses it
//     when linked. Otherwise, an evaluated point cloud on the `points` pin is emitted as a
//     Grammar of Place rules (one per point, at most kMaxGrammarPlacements, warning past that).
//
// Signals: `audio/signal` and `time/beatPhase` exist for routing. A direct link from one into a
// parameter pin of an Output node emits a params::ModRoute from the signal to the emitted
// parameter ("procedural/<name>/material/emissive", "field/<name>/strength", ...); the pin's
// static value is then left to the emitted object (the route modulates it). Linking a signal
// through a math node instead makes the value static and emits no route.

#include "core/noise.hpp"
#include "graph/detail.hpp"
#include "graph/graph.hpp"
#include "scene/grammar.hpp"
#include "spatial/attributes.hpp"
#include "spatial/effector.hpp"
#include "spatial/spatial_ops.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace avgen::graph {
namespace {

using detail::Inputs;
using detail::Num;
using detail::Params;
using nlohmann::json;

constexpr std::size_t kMaxGrammarPlacements = 4096;

// ---- descriptor helpers -------------------------------------------------------------------------

PinInfo pin(std::string name, PinType type, Value defaultValue = {}, bool multi = false) {
    PinInfo info;
    info.name = std::move(name);
    info.type = type;
    info.defaultValue = std::move(defaultValue);
    info.multi = multi;
    return info;
}

ParamInfo pf(std::string name, float value, float softMin = 0.0f, float softMax = 1.0f,
             bool structural = false) {
    ParamInfo info;
    info.name = std::move(name);
    info.type = PinType::Float;
    info.defaultValue = value;
    info.softMin = softMin;
    info.softMax = softMax;
    info.structural = structural;
    return info;
}

ParamInfo pi(std::string name, int value, float softMin = 0.0f, float softMax = 64.0f,
             bool structural = true) {
    ParamInfo info;
    info.name = std::move(name);
    info.type = PinType::Int;
    info.defaultValue = value;
    info.softMin = softMin;
    info.softMax = softMax;
    info.structural = structural;
    return info;
}

ParamInfo pb(std::string name, bool value, bool structural = true) {
    ParamInfo info;
    info.name = std::move(name);
    info.type = PinType::Bool;
    info.defaultValue = value;
    info.structural = structural;
    return info;
}

ParamInfo pv3(std::string name, glm::vec3 value, bool structural = false) {
    ParamInfo info;
    info.name = std::move(name);
    info.type = PinType::Vec3;
    info.defaultValue = value;
    info.structural = structural;
    return info;
}

ParamInfo pcol(std::string name, glm::vec4 value) {
    ParamInfo info;
    info.name = std::move(name);
    info.type = PinType::Color;
    info.defaultValue = value;
    return info;
}

ParamInfo ps(std::string name, std::string value = {}, bool structural = true) {
    ParamInfo info;
    info.name = std::move(name);
    info.type = PinType::String;
    info.defaultValue = std::move(value);
    info.structural = structural;
    return info;
}

ParamInfo pe(std::string name, int value, std::vector<std::string> choices, bool structural = true) {
    ParamInfo info;
    info.name = std::move(name);
    info.type = PinType::Int;
    info.defaultValue = value;
    info.choices = std::move(choices);
    info.structural = structural;
    info.softMin = 0.0f;
    info.softMax = static_cast<float>(info.choices.size());
    return info;
}

void append(std::vector<ParamInfo>& into, std::vector<ParamInfo> extra) {
    for (ParamInfo& param : extra) {
        into.push_back(std::move(param));
    }
}

void add(NodeRegistry& registry, std::string type, std::string label, NodeCategory category,
         std::vector<PinInfo> inputs, std::vector<PinInfo> outputs, std::vector<ParamInfo> params,
         std::string description, NodeEvaluator evaluator) {
    NodeTypeInfo info;
    info.type = std::move(type);
    info.label = std::move(label);
    info.category = category;
    info.inputs = std::move(inputs);
    info.outputs = std::move(outputs);
    info.params = std::move(params);
    info.description = std::move(description);
    registry.add(std::move(info), std::move(evaluator));
}

// ---- parameter readers ---------------------------------------------------------------------------

void readF(const Params& p, const char* name, float& target) {
    if (p.has(name)) {
        target = p.f(name);
    }
}
void readI(const Params& p, const char* name, int& target) {
    if (p.has(name)) {
        target = p.i(name);
    }
}
void readU32(const Params& p, const char* name, std::uint32_t& target) {
    if (p.has(name)) {
        target = p.u32(name);
    }
}
void readB(const Params& p, const char* name, bool& target) {
    if (p.has(name)) {
        target = p.b(name);
    }
}
void readV3(const Params& p, const char* name, glm::vec3& target) {
    if (p.has(name)) {
        target = p.v3(name);
    }
}
void readV4(const Params& p, const char* name, glm::vec4& target) {
    if (p.has(name)) {
        target = p.v4(name);
    }
}
void readIv3(const Params& p, const char* name, glm::ivec3& target) {
    if (p.has(name)) {
        const glm::vec3 v = p.v3(name);
        target = glm::ivec3(static_cast<int>(v.x), static_cast<int>(v.y), static_cast<int>(v.z));
    }
}
void readS(const Params& p, const char* name, std::string& target) {
    if (p.has(name)) {
        target = p.s(name);
    }
}

glm::quat quatFromDegrees(const glm::vec3& degrees) {
    return glm::quat(glm::radians(degrees));
}

scene::Transform transformFromParams(const Params& p, const char* positionName, const char* rotationName,
                                     const char* scaleName) {
    scene::Transform t;
    t.position = p.has(positionName) ? p.v3(positionName) : glm::vec3(0.0f);
    t.rotation = quatFromDegrees(p.has(rotationName) ? p.v3(rotationName) : glm::vec3(0.0f));
    t.scale = p.has(scaleName) ? p.v3(scaleName) : glm::vec3(1.0f);
    return t;
}

// ---- point cloud helpers -------------------------------------------------------------------------

const spatial::PointCloud& cloudOf(const Value& value) {
    static const spatial::PointCloud empty;
    const spatial::PointCloud* cloud = std::get_if<spatial::PointCloud>(&value);
    return cloud != nullptr ? *cloud : empty;
}

spatial::PointCloud cloudFromPlacements(const scene::Distribution& distribution,
                                        const spatial::Spline* spline) {
    const int count = std::max(0, distribution.instanceCount(spline));
    spatial::PointCloud cloud(static_cast<std::size_t>(count));
    const auto positions = cloud.positions();
    const auto rotations = cloud.rotations();
    const auto scales = cloud.scales();
    for (int i = 0; i < count; ++i) {
        const scene::Transform t = distribution.placement(i, spline);
        const auto index = static_cast<std::size_t>(i);
        positions[index] = t.position;
        rotations[index] = glm::vec4(t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w);
        scales[index] = t.scale;
    }
    cloud.renumberIndices();
    return cloud;
}

// The header declares seven distribution kinds, seven deformer kinds and six primitive kinds. The
// scene implementation of the *Name/*FromName helpers is still catching up with some of them
// (Spline/Grammar distributions, the Path deformer, Procedural sources), so the graph keeps its
// own complete tables for the enums it serialises; the names are the ones scene/procedural.cpp
// uses for the kinds it already covers.
constexpr std::array<const char*, 7> kDistributionKindNames{"single", "linear", "grid",   "radial",
                                                            "spiral", "spline", "grammar"};
constexpr std::array<const char*, 7> kDeformerKindNames{"bend",         "twist", "sine", "noise",
                                                        "displacement", "field", "path"};
constexpr std::array<const char*, 6> kPrimitiveKindNames{"box",   "cylinder", "sphere",
                                                         "torus", "point",    "procedural"};

template <typename Enum, std::size_t N>
const char* enumName(const std::array<const char*, N>& names, Enum value) {
    const auto index = static_cast<std::size_t>(value);
    return index < N ? names[index] : names[0];
}
template <typename Enum, std::size_t N>
std::optional<Enum> enumFromName(const std::array<const char*, N>& names, std::string_view name) {
    for (std::size_t i = 0; i < N; ++i) {
        if (name == names[i]) {
            return static_cast<Enum>(i);
        }
    }
    return std::nullopt;
}

// ---- distribution JSON ----------------------------------------------------------------------------

json distributionToJson(const scene::Distribution& d) {
    json j = json::object();
    j["kind"] = enumName(kDistributionKindNames, d.kind);
    j["count"] = d.count;
    j["start"] = json::array({d.start.x, d.start.y, d.start.z});
    j["end"] = json::array({d.end.x, d.end.y, d.end.z});
    j["orientAlong"] = d.orientAlong;
    j["spacing"] = d.spacing;
    j["gridCount"] = json::array({d.gridCount.x, d.gridCount.y, d.gridCount.z});
    j["gridSpacing"] = json::array({d.gridSpacing.x, d.gridSpacing.y, d.gridSpacing.z});
    j["radius"] = d.radius;
    j["startAngle"] = d.startAngle;
    j["endAngle"] = d.endAngle;
    j["plane"] = scene::distributionPlaneName(d.plane);
    j["center"] = json::array({d.center.x, d.center.y, d.center.z});
    j["orientation"] = scene::orientationModeName(d.orientation);
    j["radiusGrowth"] = d.radiusGrowth;
    j["turns"] = d.turns;
    j["spiralHeight"] = d.spiralHeight;
    j["spiralAngle"] = d.spiralAngle;
    j["spline"] = d.spline;
    j["splineStart"] = d.splineStart;
    j["splineEnd"] = d.splineEnd;
    j["alignToSpline"] = d.alignToSpline;
    j["roll"] = d.roll;
    j["splineOffset"] = json::array({d.splineOffset.x, d.splineOffset.y, d.splineOffset.z});
    return j;
}

glm::vec3 vec3Of(const json& j, const char* key, glm::vec3 fallback) {
    if (const auto it = j.find(key); it != j.end() && it->is_array() && it->size() == 3) {
        return glm::vec3(it->at(0).get<float>(), it->at(1).get<float>(), it->at(2).get<float>());
    }
    return fallback;
}
float floatOf(const json& j, const char* key, float fallback) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<float>() : fallback;
}
int intOf(const json& j, const char* key, int fallback) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number() ? it->get<int>() : fallback;
}
bool boolOf(const json& j, const char* key, bool fallback) {
    const auto it = j.find(key);
    return it != j.end() && it->is_boolean() ? it->get<bool>() : fallback;
}
std::string stringOf(const json& j, const char* key, const std::string& fallback = {}) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : fallback;
}

Result<scene::Distribution> distributionFromJson(const json& j) {
    if (!j.is_object()) {
        return fail("distribution spec must be a JSON object");
    }
    scene::Distribution d;
    if (const auto kind =
            enumFromName<scene::DistributionKind>(kDistributionKindNames, stringOf(j, "kind", "radial"));
        kind) {
        d.kind = *kind;
    } else {
        return fail("unknown distribution kind '{}'", stringOf(j, "kind"));
    }
    d.count = intOf(j, "count", d.count);
    d.start = vec3Of(j, "start", d.start);
    d.end = vec3Of(j, "end", d.end);
    d.orientAlong = boolOf(j, "orientAlong", d.orientAlong);
    d.spacing = floatOf(j, "spacing", d.spacing);
    const glm::vec3 gridCount = vec3Of(j, "gridCount", glm::vec3(d.gridCount));
    d.gridCount = glm::ivec3(static_cast<int>(gridCount.x), static_cast<int>(gridCount.y),
                             static_cast<int>(gridCount.z));
    d.gridSpacing = vec3Of(j, "gridSpacing", d.gridSpacing);
    d.radius = floatOf(j, "radius", d.radius);
    d.startAngle = floatOf(j, "startAngle", d.startAngle);
    d.endAngle = floatOf(j, "endAngle", d.endAngle);
    if (const auto plane = scene::distributionPlaneFromName(stringOf(j, "plane", "xz")); plane) {
        d.plane = *plane;
    }
    d.center = vec3Of(j, "center", d.center);
    if (const auto orientation = scene::orientationModeFromName(stringOf(j, "orientation", "outward"));
        orientation) {
        d.orientation = *orientation;
    }
    d.radiusGrowth = floatOf(j, "radiusGrowth", d.radiusGrowth);
    d.turns = floatOf(j, "turns", d.turns);
    d.spiralHeight = floatOf(j, "spiralHeight", d.spiralHeight);
    d.spiralAngle = floatOf(j, "spiralAngle", d.spiralAngle);
    d.spline = stringOf(j, "spline", d.spline);
    d.splineStart = floatOf(j, "splineStart", d.splineStart);
    d.splineEnd = floatOf(j, "splineEnd", d.splineEnd);
    d.alignToSpline = boolOf(j, "alignToSpline", d.alignToSpline);
    d.roll = floatOf(j, "roll", d.roll);
    d.splineOffset = vec3Of(j, "splineOffset", d.splineOffset);
    return d;
}

// ---- deformer JSON --------------------------------------------------------------------------------

json deformerToJson(const scene::Deformer& d) {
    json j = json::object();
    j["kind"] = enumName(kDeformerKindNames, d.kind);
    j["enabled"] = d.enabled;
    j["amount"] = d.amount;
    j["space"] = scene::deformSpaceName(d.space);
    j["speed"] = d.speed;
    j["phase"] = d.phase;
    j["axis"] = json::array({d.axis.x, d.axis.y, d.axis.z});
    j["center"] = json::array({d.center.x, d.center.y, d.center.z});
    j["falloff"] = d.falloff;
    j["frequency"] = d.frequency;
    j["displacementAxis"] = json::array({d.displacementAxis.x, d.displacementAxis.y, d.displacementAxis.z});
    j["scale"] = d.scale;
    j["seed"] = d.seed;
    j["axisMask"] = json::array({d.axisMask.x, d.axisMask.y, d.axisMask.z});
    j["pattern"] = d.pattern;
    j["field"] = d.field;
    j["alongNormal"] = d.alongNormal;
    j["spline"] = d.spline;
    j["pathOffset"] = d.pathOffset;
    j["pathScale"] = d.pathScale;
    j["pathRoll"] = d.pathRoll;
    return j;
}

Result<scene::Deformer> deformerFromJson(const json& j) {
    if (!j.is_object()) {
        return fail("deformer must be a JSON object");
    }
    scene::Deformer d;
    if (const auto kind = enumFromName<scene::DeformerKind>(kDeformerKindNames, stringOf(j, "kind", "twist"));
        kind) {
        d.kind = *kind;
    } else {
        return fail("unknown deformer kind '{}'", stringOf(j, "kind"));
    }
    d.enabled = boolOf(j, "enabled", d.enabled);
    d.amount = floatOf(j, "amount", d.amount);
    if (const auto space = stringOf(j, "space", "local") == "world"
                               ? std::optional(scene::DeformSpace::World)
                               : std::optional(scene::DeformSpace::Local);
        space) {
        d.space = *space;
    }
    d.speed = floatOf(j, "speed", d.speed);
    d.phase = floatOf(j, "phase", d.phase);
    d.axis = vec3Of(j, "axis", d.axis);
    d.center = vec3Of(j, "center", d.center);
    d.falloff = floatOf(j, "falloff", d.falloff);
    d.frequency = floatOf(j, "frequency", d.frequency);
    d.displacementAxis = vec3Of(j, "displacementAxis", d.displacementAxis);
    d.scale = floatOf(j, "scale", d.scale);
    d.seed = static_cast<std::uint32_t>(std::max(0, intOf(j, "seed", static_cast<int>(d.seed))));
    d.axisMask = vec3Of(j, "axisMask", d.axisMask);
    d.pattern = intOf(j, "pattern", d.pattern);
    d.field = stringOf(j, "field", d.field);
    d.alongNormal = boolOf(j, "alongNormal", d.alongNormal);
    d.spline = stringOf(j, "spline", d.spline);
    d.pathOffset = floatOf(j, "pathOffset", d.pathOffset);
    d.pathScale = floatOf(j, "pathScale", d.pathScale);
    d.pathRoll = floatOf(j, "pathRoll", d.pathRoll);
    return d;
}

// ---- generators ------------------------------------------------------------------------------------

void registerGenerators(NodeRegistry& registry) {
    add(registry, "generators/primitive", "Primitive", NodeCategory::Generators, {},
        {pin("mesh", PinType::Mesh)},
        {pe("kind", 1, {"box", "cylinder", "sphere", "torus", "point", "procedural"}),
         pv3("size", glm::vec3(1.0f), true), pi("subdivisions", 1, 1.0f, 16.0f),
         pf("radius", 0.5f, 0.01f, 10.0f, true), pf("height", 2.0f, 0.01f, 20.0f, true),
         pi("radialSegments", 24, 3.0f, 64.0f), pi("heightSegments", 1, 1.0f, 32.0f), pb("caps", true),
         pi("segments", 32, 3.0f, 64.0f), pi("rings", 16, 2.0f, 32.0f),
         pf("majorRadius", 1.0f, 0.01f, 10.0f, true), pf("minorRadius", 0.25f, 0.01f, 5.0f, true),
         pi("majorSegments", 48, 3.0f, 96.0f), pi("minorSegments", 16, 3.0f, 32.0f),
         pf("pointSize", 0.05f, 0.001f, 1.0f, true), ps("reference"), pv3("position", glm::vec3(0.0f)),
         pv3("rotation", glm::vec3(0.0f)), pv3("scale", glm::vec3(1.0f))},
        "A source primitive (box, cylinder, sphere, torus, point) with its object-space transform.",
        [](const Node& node, const std::vector<Value>&, std::vector<Value>& outputs,
           EvalContext&) -> Result<void> {
            const Params p(node);
            MeshValue mesh;
            scene::SourceSpec& s = mesh.source;
            if (const auto kind = enumFromName<scene::PrimitiveKind>(kPrimitiveKindNames, p.label("kind"));
                kind) {
                s.kind = *kind;
            }
            readV3(p, "size", s.size);
            readI(p, "subdivisions", s.subdivisions);
            readF(p, "radius", s.radius);
            readF(p, "height", s.height);
            readI(p, "radialSegments", s.radialSegments);
            readI(p, "heightSegments", s.heightSegments);
            readB(p, "caps", s.caps);
            readI(p, "segments", s.segments);
            readI(p, "rings", s.rings);
            readF(p, "majorRadius", s.majorRadius);
            readF(p, "minorRadius", s.minorRadius);
            readI(p, "majorSegments", s.majorSegments);
            readI(p, "minorSegments", s.minorSegments);
            readF(p, "pointSize", s.pointSize);
            readS(p, "reference", s.reference);
            mesh.transform = transformFromParams(p, "position", "rotation", "scale");
            if (auto ok = s.validate(); !ok) {
                return std::unexpected(ok.error());
            }
            outputs[0] = std::move(mesh);
            return {};
        });

    add(registry, "generators/spline", "Spline", NodeCategory::Generators, {},
        {pin("spline", PinType::Spline)},
        {pe("generator", 2, {"points", "line", "circle", "spiral", "helix", "bezier", "noise"}),
         pe("kind", 1, {"polyline", "catmullRom", "bezier", "hermite"}),
         pb("closed", false),
         pi("count", 16, 2.0f, 256.0f),
         pv3("start", glm::vec3(0.0f), true),
         pv3("end", glm::vec3(0.0f, 0.0f, 10.0f), true),
         pf("radius", 5.0f, 0.0f, 50.0f, true),
         pf("radiusGrowth", 0.0f, -20.0f, 20.0f, true),
         pf("turns", 1.0f, 0.0f, 10.0f, true),
         pf("height", 10.0f, -50.0f, 50.0f, true),
         pv3("axis", glm::vec3(0.0f, 1.0f, 0.0f), true),
         pv3("center", glm::vec3(0.0f), true),
         pf("startAngle", 0.0f, -6.2832f, 6.2832f, true),
         pv3("p0", glm::vec3(0.0f), true),
         pv3("p1", glm::vec3(0.0f, 5.0f, 5.0f), true),
         pv3("p2", glm::vec3(0.0f, 5.0f, 10.0f), true),
         pv3("p3", glm::vec3(0.0f, 0.0f, 15.0f), true),
         pf("noiseAmount", 0.0f, 0.0f, 5.0f, true),
         pf("noiseScale", 0.3f, 0.01f, 5.0f, true),
         pi("seed", 1, 0.0f, 999.0f),
         pf("tension", 0.5f, 0.0f, 1.0f, true),
         pi("samplesPerSegment", 16, 4.0f, 64.0f),
         pv3("up", glm::vec3(0.0f, 1.0f, 0.0f), true)},
        "A generated spline (line, circle, spiral, helix, Bezier, noise) named after the node.",
        [](const Node& node, const std::vector<Value>&, std::vector<Value>& outputs,
           EvalContext&) -> Result<void> {
            const Params p(node);
            spatial::Spline spline;
            spline.name = node.name;
            if (const auto generator = spatial::splineGeneratorFromName(p.label("generator")); generator) {
                spline.generator = *generator;
            }
            if (const auto kind = spatial::splineKindFromName(p.label("kind")); kind) {
                spline.kind = *kind;
            }
            readB(p, "closed", spline.closed);
            readI(p, "count", spline.count);
            readV3(p, "start", spline.start);
            readV3(p, "end", spline.end);
            readF(p, "radius", spline.radius);
            readF(p, "radiusGrowth", spline.radiusGrowth);
            readF(p, "turns", spline.turns);
            readF(p, "height", spline.height);
            readV3(p, "axis", spline.axis);
            readV3(p, "center", spline.center);
            readF(p, "startAngle", spline.startAngle);
            readV3(p, "p0", spline.p0);
            readV3(p, "p1", spline.p1);
            readV3(p, "p2", spline.p2);
            readV3(p, "p3", spline.p3);
            readF(p, "noiseAmount", spline.noiseAmount);
            readF(p, "noiseScale", spline.noiseScale);
            readU32(p, "seed", spline.seed);
            readF(p, "tension", spline.tension);
            readI(p, "samplesPerSegment", spline.samplesPerSegment);
            readV3(p, "up", spline.up);
            if (auto ok = spline.validate(); !ok) {
                return std::unexpected(ok.error());
            }
            outputs[0] = std::move(spline);
            return {};
        });

    add(registry, "generators/sdfPrimitive", "SDF Primitive", NodeCategory::Generators, {},
        {pin("sdf", PinType::Sdf)},
        {pe("kind", 0, {"sphere", "box", "roundedBox", "cylinder", "capsule", "torus", "plane", "cone"}),
         pf("radius", 1.0f, 0.01f, 10.0f, true), pf("height", 2.0f, 0.01f, 20.0f, true),
         pv3("size", glm::vec3(1.0f), true), pf("rounding", 0.1f, 0.0f, 1.0f, true),
         pv3("axis", glm::vec3(0.0f, 1.0f, 0.0f), true), pf("offset", 0.0f, -10.0f, 10.0f, true)},
        "One signed-distance primitive.",
        [](const Node& node, const std::vector<Value>&, std::vector<Value>& outputs,
           EvalContext&) -> Result<void> {
            const Params p(node);
            SdfValue value;
            spatial::SdfNode& n = value.tree.root;
            if (const auto kind = spatial::sdfNodeKindFromName(p.label("kind")); kind) {
                n.kind = *kind;
            }
            readF(p, "radius", n.radius);
            readF(p, "height", n.height);
            readV3(p, "size", n.size);
            readF(p, "rounding", n.rounding);
            readV3(p, "axis", n.axis);
            readF(p, "offset", n.offset);
            outputs[0] = std::move(value);
            return {};
        });

    add(registry, "generators/grammar", "Grammar", NodeCategory::Generators, {},
        {pin("points", PinType::PointCloud)},
        {ps("grammar", "{}"), ps("axiom"), pi("maxDepth", 8, 1.0f, 16.0f),
         pi("maxInstances", 100000, 1.0f, 100000.0f), pi("seed", 1, 0.0f, 999.0f)},
        "Expands a compositional grammar (JSON in `grammar`) into a point cloud of placements.",
        [](const Node& node, const std::vector<Value>&, std::vector<Value>& outputs,
           EvalContext&) -> Result<void> {
            const Params p(node);
            const std::string text = p.s("grammar");
            json parsed = json::object();
            if (!text.empty()) {
                parsed = json::parse(text, nullptr, false);
                if (parsed.is_discarded()) {
                    return fail("'grammar' is not valid JSON");
                }
            }
            auto grammar = scene::Grammar::fromJson(parsed);
            if (!grammar) {
                return std::unexpected(grammar.error());
            }
            if (const std::string axiom = p.s("axiom"); !axiom.empty()) {
                grammar->axiom = axiom;
            }
            readI(p, "maxDepth", grammar->maxDepth);
            readI(p, "maxInstances", grammar->maxInstances);
            readU32(p, "seed", grammar->seed);
            outputs[0] = grammar->expand();
            return {};
        });

    add(registry, "generators/points", "Points", NodeCategory::Generators, {},
        {pin("points", PinType::PointCloud)},
        {pi("count", 16, 1.0f, 1024.0f), pi("seed", 1, 0.0f, 999.0f), pv3("position", glm::vec3(0.0f), true)},
        "A bare point cloud of `count` points at `position` (feed it to the points/* operators).",
        [](const Node& node, const std::vector<Value>&, std::vector<Value>& outputs,
           EvalContext&) -> Result<void> {
            const Params p(node);
            int count = 16;
            readI(p, "count", count);
            count = std::clamp(count, 0, 1 << 20);
            spatial::PointCloud cloud(static_cast<std::size_t>(count));
            const glm::vec3 position = p.v3("position");
            for (glm::vec3& value : cloud.positions()) {
                value = position;
            }
            cloud.reseed(p.u32("seed"));
            cloud.renumberIndices();
            outputs[0] = std::move(cloud);
            return {};
        });
}

// ---- distributions ---------------------------------------------------------------------------------

std::vector<ParamInfo> commonDistributionParams() {
    return {pi("count", 32, 1.0f, 256.0f), pv3("center", glm::vec3(0.0f), true),
            pe("orientation", 1, {"none", "outward", "inward", "tangent"})};
}

scene::Distribution distributionFromParams(const Node& node, scene::DistributionKind kind) {
    const Params p(node);
    scene::Distribution d;
    d.kind = kind;
    readI(p, "count", d.count);
    readV3(p, "start", d.start);
    readV3(p, "end", d.end);
    readB(p, "orientAlong", d.orientAlong);
    readF(p, "spacing", d.spacing);
    readIv3(p, "gridCount", d.gridCount);
    readV3(p, "gridSpacing", d.gridSpacing);
    readF(p, "radius", d.radius);
    readF(p, "startAngle", d.startAngle);
    readF(p, "endAngle", d.endAngle);
    readV3(p, "center", d.center);
    readF(p, "radiusGrowth", d.radiusGrowth);
    readF(p, "turns", d.turns);
    readF(p, "spiralHeight", d.spiralHeight);
    readF(p, "spiralAngle", d.spiralAngle);
    readF(p, "splineStart", d.splineStart);
    readF(p, "splineEnd", d.splineEnd);
    readB(p, "alignToSpline", d.alignToSpline);
    readF(p, "roll", d.roll);
    readV3(p, "splineOffset", d.splineOffset);
    if (p.has("plane")) {
        if (const auto plane = scene::distributionPlaneFromName(p.label("plane")); plane) {
            d.plane = *plane;
        }
    }
    if (p.has("orientation")) {
        if (const auto orientation = scene::orientationModeFromName(p.label("orientation")); orientation) {
            d.orientation = *orientation;
        }
    }
    return d;
}

void addDistribution(NodeRegistry& registry, const char* type, const char* label,
                     scene::DistributionKind kind, std::vector<ParamInfo> params, const char* description) {
    add(registry, type, label, NodeCategory::Distributions, {},
        {pin("points", PinType::PointCloud), pin("spec", PinType::String)}, std::move(params), description,
        [kind](const Node& node, const std::vector<Value>&, std::vector<Value>& outputs,
               EvalContext&) -> Result<void> {
            const scene::Distribution d = distributionFromParams(node, kind);
            if (auto ok = d.validate(); !ok) {
                return std::unexpected(ok.error());
            }
            outputs[0] = cloudFromPlacements(d, nullptr);
            outputs[1] = distributionToJson(d).dump();
            return {};
        });
}

void registerDistributions(NodeRegistry& registry) {
    std::vector<ParamInfo> linear = commonDistributionParams();
    append(linear,
           {pv3("start", glm::vec3(-5.0f, 0.0f, 0.0f), true), pv3("end", glm::vec3(5.0f, 0.0f, 0.0f), true),
            pb("orientAlong", false), pf("spacing", 0.0f, 0.0f, 10.0f, true)});
    addDistribution(registry, "distributions/linear", "Linear", scene::DistributionKind::Linear,
                    std::move(linear), "Instances evenly between `start` and `end`.");

    std::vector<ParamInfo> grid = commonDistributionParams();
    append(grid,
           {pv3("gridCount", glm::vec3(4.0f, 1.0f, 4.0f), true), pv3("gridSpacing", glm::vec3(2.0f), true)});
    addDistribution(registry, "distributions/grid", "Grid", scene::DistributionKind::Grid, std::move(grid),
                    "A centred 3D grid of countX x countY x countZ instances.");

    std::vector<ParamInfo> radial = commonDistributionParams();
    append(radial, {pf("radius", 6.0f, 0.0f, 30.0f, true), pf("startAngle", 0.0f, -6.2832f, 6.2832f, true),
                    pf("endAngle", 6.2831853f, -6.2832f, 6.2832f, true), pe("plane", 0, {"xz", "xy", "yz"})});
    addDistribution(registry, "distributions/radial", "Radial", scene::DistributionKind::Radial,
                    std::move(radial), "Instances around a circle of `radius` in the chosen plane.");

    std::vector<ParamInfo> spiral = commonDistributionParams();
    append(spiral, {pf("radius", 6.0f, 0.0f, 30.0f, true), pf("radiusGrowth", 0.0f, -20.0f, 20.0f, true),
                    pf("turns", 3.0f, 0.0f, 10.0f, true), pf("spiralHeight", 8.0f, -20.0f, 20.0f, true),
                    pf("spiralAngle", 0.0f, -6.2832f, 6.2832f, true), pe("plane", 0, {"xz", "xy", "yz"})});
    addDistribution(registry, "distributions/spiral", "Spiral", scene::DistributionKind::Spiral,
                    std::move(spiral), "A rising spiral of `turns` turns about the plane normal.");

    add(registry, "distributions/spline", "Along Spline", NodeCategory::Distributions,
        {pin("spline", PinType::Spline)}, {pin("points", PinType::PointCloud), pin("spec", PinType::String)},
        {pi("count", 32, 1.0f, 256.0f), pf("spacing", 0.0f, 0.0f, 10.0f, true),
         pf("splineStart", 0.0f, 0.0f, 1.0f, true), pf("splineEnd", 1.0f, 0.0f, 1.0f, true),
         pb("alignToSpline", true), pf("roll", 0.0f, -6.2832f, 6.2832f, true),
         pv3("splineOffset", glm::vec3(0.0f), true)},
        "Instances along the linked spline (by count or by arc-length spacing).",
        [](const Node& node, const std::vector<Value>& values, std::vector<Value>& outputs,
           EvalContext&) -> Result<void> {
            const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
            const Inputs in(*info, values);
            scene::Distribution d = distributionFromParams(node, scene::DistributionKind::Spline);
            const spatial::Spline* spline = std::get_if<spatial::Spline>(&in.single("spline"));
            if (spline != nullptr) {
                d.spline = spline->name;
            }
            if (auto ok = d.validate(); !ok) {
                return std::unexpected(ok.error());
            }
            outputs[0] = cloudFromPlacements(d, spline);
            outputs[1] = distributionToJson(d).dump();
            return {};
        });
}

// ---- point operators --------------------------------------------------------------------------------

spatial::PointOp pointOpFromParams(const Node& node, spatial::PointOpKind kind) {
    const Params p(node);
    spatial::PointOp op;
    op.kind = kind;
    readB(p, "enabled", op.enabled);
    readF(p, "amount", op.amount);
    readV3(p, "offset", op.offset);
    readV3(p, "axis", op.axis);
    readF(p, "angle", op.angle);
    readV3(p, "pivot", op.pivot);
    readV3(p, "factor", op.factor);
    readB(p, "scaleInstances", op.scaleInstances);
    readV3(p, "position", op.position);
    readV3(p, "rotationDegrees", op.rotationDegrees);
    readV3(p, "scale", op.scale);
    readF(p, "frequency", op.frequency);
    readV3(p, "axisMask", op.axisMask);
    readU32(p, "seed", op.seed);
    readV3(p, "randomPosition", op.randomPosition);
    readV3(p, "randomRotation", op.randomRotation);
    readV3(p, "randomScale", op.randomScale);
    readF(p, "randomUniformScale", op.randomUniformScale);
    readV3(p, "range", op.range);
    readF(p, "threshold", op.threshold);
    readB(p, "probabilistic", op.probabilistic);
    readS(p, "attribute", op.attribute);
    readF(p, "value", op.value);
    readF(p, "minDistance", op.minDistance);
    readF(p, "maxDistance", op.maxDistance);
    readF(p, "probability", op.probability);
    readV3(p, "boundsMin", op.boundsMin);
    readV3(p, "boundsMax", op.boundsMax);
    readB(p, "invert", op.invert);
    readI(p, "component", op.component);
    readB(p, "descending", op.descending);
    readI(p, "copies", op.copies);
    readI(p, "stride", op.stride);
    readI(p, "count", op.count);
    readI(p, "start", op.start);
    if (p.has("compare")) {
        op.compare = p.enumOf("compare");
    }
    return op;
}

void addPointOp(NodeRegistry& registry, const char* type, const char* label, spatial::PointOpKind kind,
                std::vector<ParamInfo> params, const char* description) {
    params.insert(params.begin(), pb("enabled", true));
    add(registry, type, label, NodeCategory::Points, {pin("points", PinType::PointCloud)},
        {pin("points", PinType::PointCloud)}, std::move(params), description,
        [kind](const Node& node, const std::vector<Value>& values, std::vector<Value>& outputs,
               EvalContext&) -> Result<void> {
            const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
            const Inputs in(*info, values);
            spatial::PointCloud cloud = cloudOf(in.single("points"));
            const spatial::PointOp op = pointOpFromParams(node, kind);
            if (auto ok = spatial::applyPointOp(cloud, op); !ok) {
                return std::unexpected(ok.error());
            }
            outputs[0] = std::move(cloud);
            return {};
        });
}

const std::vector<std::string>& compareChoices() {
    static const std::vector<std::string> choices{"less",         "lessEqual", "equal",
                                                  "greaterEqual", "greater",   "notEqual"};
    return choices;
}

void registerPointOps(NodeRegistry& registry) {
    addPointOp(registry, "points/transform", "Transform", spatial::PointOpKind::Transform,
               {pv3("position", glm::vec3(0.0f), true), pv3("rotationDegrees", glm::vec3(0.0f), true),
                pv3("scale", glm::vec3(1.0f), true)},
               "Transforms every point (position, rotation and scale).");
    addPointOp(registry, "points/translate", "Translate", spatial::PointOpKind::Translate,
               {pv3("offset", glm::vec3(0.0f), true), pf("amount", 1.0f, 0.0f, 2.0f, true)},
               "Adds `offset * amount` to every position.");
    addPointOp(registry, "points/rotate", "Rotate", spatial::PointOpKind::Rotate,
               {pv3("axis", glm::vec3(0.0f, 1.0f, 0.0f), true), pf("angle", 0.0f, -6.2832f, 6.2832f, true),
                pv3("pivot", glm::vec3(0.0f), true), pf("amount", 1.0f, 0.0f, 2.0f, true)},
               "Rotates positions and rotations about `axis` around `pivot`.");
    addPointOp(registry, "points/scale", "Scale", spatial::PointOpKind::Scale,
               {pv3("factor", glm::vec3(1.0f), true), pv3("pivot", glm::vec3(0.0f), true),
                pb("scaleInstances", true)},
               "Scales positions about `pivot` (and instance scales when `scaleInstances`).");
    addPointOp(registry, "points/noise", "Noise", spatial::PointOpKind::Noise,
               {pf("frequency", 1.0f, 0.01f, 5.0f, true), pf("amount", 1.0f, 0.0f, 5.0f, true),
                pv3("offset", glm::vec3(0.0f), true), pv3("axisMask", glm::vec3(1.0f), true),
                pi("seed", 1, 0.0f, 999.0f)},
               "Displaces positions by value-noise fBM.");
    addPointOp(registry, "points/randomize", "Randomize", spatial::PointOpKind::Randomize,
               {pv3("randomPosition", glm::vec3(0.0f), true), pv3("randomRotation", glm::vec3(0.0f), true),
                pv3("randomScale", glm::vec3(0.0f), true), pf("randomUniformScale", 0.0f, 0.0f, 1.0f, true),
                pi("seed", 1, 0.0f, 999.0f), pf("amount", 1.0f, 0.0f, 2.0f, true)},
               "Seeded per-point position/rotation/scale jitter.");
    addPointOp(registry, "points/scatter", "Scatter", spatial::PointOpKind::Scatter,
               {pv3("range", glm::vec3(0.5f), true), pi("seed", 1, 0.0f, 999.0f),
                pf("amount", 1.0f, 0.0f, 2.0f, true)},
               "Uniform position jitter within +/- range.");
    addPointOp(registry, "points/filterDensity", "Filter Density", spatial::PointOpKind::FilterDensity,
               {pf("threshold", 0.5f, 0.0f, 1.0f, true), pb("probabilistic", false), pb("invert", false),
                pi("seed", 1, 0.0f, 999.0f)},
               "Keeps points whose density passes the threshold.");
    addPointOp(registry, "points/filterAttribute", "Filter Attribute", spatial::PointOpKind::FilterAttribute,
               {ps("attribute", "density"), pf("value", 0.0f, -10.0f, 10.0f, true),
                pe("compare", 3, compareChoices()), pb("invert", false)},
               "Keeps points whose attribute compares true against `value`.");
    addPointOp(registry, "points/filterDistance", "Filter Distance", spatial::PointOpKind::FilterDistance,
               {pv3("pivot", glm::vec3(0.0f), true), pf("minDistance", 0.0f, 0.0f, 50.0f, true),
                pf("maxDistance", 10.0f, 0.0f, 50.0f, true), pb("invert", false)},
               "Keeps points within a distance band around `pivot`.");
    addPointOp(registry, "points/filterProbability", "Filter Probability",
               spatial::PointOpKind::FilterProbability,
               {pf("probability", 0.5f, 0.0f, 1.0f, true), pi("seed", 1, 0.0f, 999.0f), pb("invert", false)},
               "Keeps each point with a seeded probability.");
    addPointOp(registry, "points/filterBounds", "Filter Bounds", spatial::PointOpKind::FilterBounds,
               {pv3("boundsMin", glm::vec3(-1.0f), true), pv3("boundsMax", glm::vec3(1.0f), true),
                pb("invert", false)},
               "Keeps points inside an axis-aligned box.");
    addPointOp(registry, "points/sort", "Sort", spatial::PointOpKind::Sort,
               {ps("attribute", "position"), pi("component", 0, 0.0f, 3.0f), pb("descending", false)},
               "Stable sort by an attribute component, then renumbers indices.");
    addPointOp(registry, "points/duplicate", "Duplicate", spatial::PointOpKind::Duplicate,
               {pi("copies", 1, 0.0f, 16.0f), pv3("offset", glm::vec3(0.0f), true),
                pv3("axis", glm::vec3(0.0f, 1.0f, 0.0f), true), pf("angle", 0.0f, -6.2832f, 6.2832f, true),
                pv3("factor", glm::vec3(1.0f), true)},
               "Appends `copies` cumulative copies of the cloud.");
    addPointOp(registry, "points/sample", "Sample", spatial::PointOpKind::Sample,
               {pi("stride", 2, 1.0f, 16.0f), pi("count", 0, 0.0f, 1024.0f), pi("start", 0, 0.0f, 16.0f)},
               "Keeps every stride-th point, or `count` points evenly.");

    add(registry, "points/merge", "Merge", NodeCategory::Points,
        {pin("points", PinType::PointCloud, Value{}, true)}, {pin("points", PinType::PointCloud)}, {},
        "Concatenates every linked cloud in link order (ids are renumbered).",
        [](const Node& node, const std::vector<Value>& values, std::vector<Value>& outputs,
           EvalContext&) -> Result<void> {
            const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
            const Inputs in(*info, values);
            spatial::PointCloud merged;
            bool first = true;
            for (const Value& value : in.multi("points")) {
                const spatial::PointCloud& cloud = cloudOf(value);
                if (first) {
                    merged = cloud;
                    first = false;
                } else {
                    spatial::mergeClouds(merged, cloud);
                }
            }
            merged.renumberIndices();
            outputs[0] = std::move(merged);
            return {};
        });
}

// ---- attributes ----------------------------------------------------------------------------------

void registerAttributes(NodeRegistry& registry) {
    add(registry, "attributes/op", "Attribute Op", NodeCategory::Attributes,
        {pin("points", PinType::PointCloud)}, {pin("points", PinType::PointCloud)},
        {pe("kind", 0,
            {"set", "add", "multiply", "remap", "clamp", "normalize", "smooth", "noise", "randomize", "lerp",
             "fit", "threshold", "compare"}),
         pb("enabled", true),
         ps("target", "density"),
         ps("source"),
         ps("secondSource"),
         ps("positionAttribute", "position"),
         pcol("value", glm::vec4(0.0f)),
         pf("amount", 1.0f, 0.0f, 1.0f, true),
         pf("inMin", 0.0f, -10.0f, 10.0f, true),
         pf("inMax", 1.0f, -10.0f, 10.0f, true),
         pf("outMin", 0.0f, -10.0f, 10.0f, true),
         pf("outMax", 1.0f, -10.0f, 10.0f, true),
         pb("clamp", true),
         pf("scale", 1.0f, 0.01f, 5.0f, true),
         pv3("offset", glm::vec3(0.0f), true),
         pcol("range", glm::vec4(1.0f)),
         pi("seed", 1, 0.0f, 999.0f),
         pi("radius", 1, 0.0f, 16.0f),
         pe("compare", 3, compareChoices()),
         ps("idAttribute", "id")},
        "Applies one attribute operation to the cloud's columns.",
        [](const Node& node, const std::vector<Value>& values, std::vector<Value>& outputs,
           EvalContext&) -> Result<void> {
            const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
            const Inputs in(*info, values);
            const Params p(node);
            spatial::PointCloud cloud = cloudOf(in.single("points"));
            spatial::AttributeOp op;
            if (const auto kind = spatial::attributeOpKindFromName(p.label("kind")); kind) {
                op.kind = *kind;
            }
            readB(p, "enabled", op.enabled);
            readS(p, "target", op.target);
            readS(p, "source", op.source);
            readS(p, "secondSource", op.secondSource);
            readS(p, "positionAttribute", op.positionAttribute);
            readV4(p, "value", op.value);
            readF(p, "amount", op.amount);
            readF(p, "inMin", op.inMin);
            readF(p, "inMax", op.inMax);
            readF(p, "outMin", op.outMin);
            readF(p, "outMax", op.outMax);
            readB(p, "clamp", op.clamp);
            readF(p, "scale", op.scale);
            readV3(p, "offset", op.offset);
            readV4(p, "range", op.range);
            readU32(p, "seed", op.seed);
            readI(p, "radius", op.radius);
            op.compare = p.enumOf("compare");
            readS(p, "idAttribute", op.idAttribute);
            if (auto ok = spatial::applyAttributeOp(cloud.attributes, op); !ok) {
                return std::unexpected(ok.error());
            }
            outputs[0] = std::move(cloud);
            return {};
        });

    add(registry, "attributes/stats", "Attribute Stats", NodeCategory::Attributes,
        {pin("points", PinType::PointCloud)},
        {pin("min", PinType::Vec3), pin("max", PinType::Vec3), pin("mean", PinType::Vec3),
         pin("count", PinType::Int)},
        {ps("attribute", "position")}, "Per-component minimum, maximum and mean of one attribute column.",
        [](const Node& node, const std::vector<Value>& values, std::vector<Value>& outputs,
           EvalContext&) -> Result<void> {
            const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
            const Inputs in(*info, values);
            const Params p(node);
            const spatial::PointCloud& cloud = cloudOf(in.single("points"));
            const std::string name = p.s("attribute");
            const spatial::AttributeBuffer* buffer = cloud.attributes.find(name);
            if (buffer == nullptr) {
                return fail("attribute '{}' not found", name);
            }
            const spatial::AttributeStats stats = spatial::attributeStats(*buffer);
            outputs[0] = glm::vec3(stats.min);
            outputs[1] = glm::vec3(stats.max);
            outputs[2] = glm::vec3(stats.mean);
            outputs[3] = static_cast<int>(cloud.count());
            return {};
        });
}

// ---- fields ----------------------------------------------------------------------------------------

std::vector<ParamInfo> commonFieldParams() {
    return {pb("enabled", true),
            pe("space", 0, {"world", "local"}),
            pv3("position", glm::vec3(0.0f)),
            pv3("rotation", glm::vec3(0.0f)),
            pv3("scale", glm::vec3(1.0f)),
            pf("strength", 1.0f, -5.0f, 5.0f),
            pb("invert", false),
            pe("falloffKind", 0,
               {"none", "linear", "smoothstep", "smooth", "easeIn", "easeOut", "easeInOut", "exponential",
                "customCurve", "noiseModulated"}),
            pf("falloffInner", 0.0f, 0.0f, 50.0f),
            pf("falloffOuter", 10.0f, 0.0f, 50.0f),
            pf("falloffExponent", 2.0f, 0.1f, 8.0f),
            pf("speed", 0.0f, -5.0f, 5.0f),
            pf("phase", 0.0f, -6.2832f, 6.2832f),
            pv3("axis", glm::vec3(0.0f, 1.0f, 0.0f)),
            pv3("point", glm::vec3(0.0f)),
            pf("radius", 10.0f, 0.0f, 50.0f),
            pf("length", 10.0f, 0.0f, 50.0f),
            pv3("size", glm::vec3(5.0f)),
            pf("softness", 0.5f, 0.0f, 5.0f),
            pf("frequency", 0.2f, 0.0f, 2.0f),
            pi("seed", 7, 0.0f, 999.0f),
            ps("reference")};
}

spatial::FieldSpec fieldFromParams(const Node& node) {
    const Params p(node);
    spatial::FieldSpec field;
    field.name = node.name;
    if (const auto kind = spatial::fieldKindFromName(p.label("kind")); kind) {
        field.kind = *kind;
    }
    readB(p, "enabled", field.enabled);
    if (p.has("space")) {
        if (const auto space = spatial::fieldSpaceFromName(p.label("space")); space) {
            field.space = *space;
        }
    }
    readV3(p, "position", field.position);
    readV3(p, "rotation", field.rotationDegrees);
    readV3(p, "scale", field.scale);
    readF(p, "strength", field.strength);
    readB(p, "invert", field.invert);
    if (p.has("falloffKind")) {
        if (const auto falloff = spatial::falloffKindFromName(p.label("falloffKind")); falloff) {
            field.falloff.kind = *falloff;
        }
    }
    readF(p, "falloffInner", field.falloff.inner);
    readF(p, "falloffOuter", field.falloff.outer);
    readF(p, "falloffExponent", field.falloff.exponent);
    readF(p, "speed", field.speed);
    readF(p, "phase", field.phase);
    readV3(p, "axis", field.axis);
    readV3(p, "point", field.point);
    readF(p, "radius", field.radius);
    readF(p, "length", field.length);
    readV3(p, "size", field.size);
    readF(p, "softness", field.softness);
    readF(p, "frequency", field.frequency);
    readU32(p, "seed", field.seed);
    readF(p, "spiralBias", field.spiralBias);
    readV4(p, "colorA", field.colorA);
    readV4(p, "colorB", field.colorB);
    readF(p, "amplitude", field.amplitude);
    readF(p, "wavelength", field.wavelength);
    readF(p, "waveSpeed", field.waveSpeed);
    readF(p, "waveWidth", field.waveWidth);
    readF(p, "waveOrigin", field.waveOrigin);
    readF(p, "mix", field.mix);
    readS(p, "reference", field.reference);
    if (p.has("waveGeometry")) {
        if (const auto geometry = spatial::waveGeometryFromName(p.label("waveGeometry")); geometry) {
            field.waveGeometry = *geometry;
        }
    }
    if (p.has("waveShape")) {
        if (const auto shape = spatial::waveShapeFromName(p.label("waveShape")); shape) {
            field.waveShape = *shape;
        }
    }
    if (p.has("combine")) {
        if (const auto combine = spatial::fieldCombineFromName(p.label("combine")); combine) {
            field.combine = *combine;
        }
    }
    return field;
}

void addFieldNode(NodeRegistry& registry, const char* type, const char* label, std::vector<std::string> kinds,
                  int defaultKind, std::vector<ParamInfo> extra, const char* description) {
    std::vector<ParamInfo> params{pe("kind", defaultKind, std::move(kinds))};
    append(params, commonFieldParams());
    append(params, std::move(extra));
    add(registry, type, label, NodeCategory::Fields, {}, {pin("field", PinType::Field)}, std::move(params),
        description,
        [](const Node& node, const std::vector<Value>&, std::vector<Value>& outputs,
           EvalContext&) -> Result<void> {
            spatial::FieldSpec field = fieldFromParams(node);
            if (auto ok = field.validate(); !ok) {
                return std::unexpected(ok.error());
            }
            outputs[0] = std::move(field);
            return {};
        });
}

void registerFields(NodeRegistry& registry) {
    addFieldNode(registry, "fields/scalar", "Scalar Field",
                 {"constant", "linearGradient", "radial", "box", "sphere", "plane", "noise", "voronoi",
                  "distance", "sdfDistance"},
                 2, {}, "A scalar field named after the node.");
    addFieldNode(registry, "fields/vector", "Vector Field",
                 {"direction", "radialVector", "attractor", "repulsor", "vortex", "curlNoise", "spiral"}, 1,
                 {pf("spiralBias", 0.5f, -2.0f, 2.0f)}, "A vector field named after the node.");
    addFieldNode(registry, "fields/color", "Colour Field",
                 {"constantColor", "gradient", "radialGradient", "noiseColor", "positionColor"}, 1,
                 {pcol("colorA", glm::vec4(1.0f)), pcol("colorB", glm::vec4(0.0f, 0.0f, 0.0f, 1.0f))},
                 "A colour field named after the node.");
    addFieldNode(registry, "fields/wave", "Wave Field", {"wave", "waveVector"}, 0,
                 {pe("waveGeometry", 1, {"planar", "radial", "spherical", "cylindrical"}),
                  pe("waveShape", 0, {"sine", "pulse", "triangle"}), pf("amplitude", 1.0f, -5.0f, 5.0f),
                  pf("wavelength", 4.0f, 0.1f, 20.0f), pf("waveSpeed", 4.0f, -20.0f, 20.0f),
                  pf("waveWidth", 6.0f, 0.0f, 20.0f), pf("waveOrigin", 0.0f, -20.0f, 20.0f)},
                 "A travelling wave field named after the node.");

    std::vector<ParamInfo> compoundParams{pe("kind", 0, {"compound"})};
    append(compoundParams, commonFieldParams());
    append(compoundParams, {pe("combine", 0, {"add", "multiply", "max", "min", "mix", "average"}),
                            pf("mix", 0.5f, 0.0f, 1.0f)});
    add(registry, "fields/compound", "Compound Field", NodeCategory::Fields,
        {pin("fields", PinType::Field, Value{}, true)}, {pin("field", PinType::Field)},
        std::move(compoundParams), "Combines the linked fields by name (they must be emitted too).",
        [](const Node& node, const std::vector<Value>& values, std::vector<Value>& outputs,
           EvalContext&) -> Result<void> {
            const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
            const Inputs in(*info, values);
            spatial::FieldSpec field = fieldFromParams(node);
            field.kind = spatial::FieldKind::Compound;
            for (const Value& value : in.multi("fields")) {
                if (const spatial::FieldSpec* child = std::get_if<spatial::FieldSpec>(&value);
                    child != nullptr) {
                    field.children.push_back(child->name);
                }
            }
            if (field.children.size() > static_cast<std::size_t>(spatial::kMaxCompoundChildren)) {
                field.children.resize(static_cast<std::size_t>(spatial::kMaxCompoundChildren));
            }
            if (auto ok = field.validate(); !ok) {
                return std::unexpected(ok.error());
            }
            outputs[0] = std::move(field);
            return {};
        });

    add(registry, "fields/sample", "Sample Field", NodeCategory::Fields,
        {pin("field", PinType::Field), pin("position", PinType::Vec3, glm::vec3(0.0f))},
        {pin("scalar", PinType::Float), pin("vector", PinType::Vec3), pin("color", PinType::Color)}, {},
        "Samples the linked field on the CPU at the evaluation time.",
        [](const Node& node, const std::vector<Value>& values, std::vector<Value>& outputs,
           EvalContext& ctx) -> Result<void> {
            const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
            const Inputs in(*info, values);
            const spatial::FieldSpec* field = std::get_if<spatial::FieldSpec>(&in.single("field"));
            if (field == nullptr) {
                return fail("no field linked");
            }
            const glm::vec3 position = in.v3("position");
            spatial::FieldSet set;
            set.fields.push_back(*field);
            outputs[0] = spatial::sampleScalar(*field, position, ctx.time, &set);
            outputs[1] = spatial::sampleVector(*field, position, ctx.time, &set);
            outputs[2] = spatial::sampleColor(*field, position, ctx.time, &set);
            return {};
        });
}

// ---- effectors -------------------------------------------------------------------------------------

void registerEffectors(NodeRegistry& registry) {
    add(registry, "effectors/effector", "Effector", NodeCategory::Effectors,
        {pin("points", PinType::PointCloud), pin("field", PinType::Field)},
        {pin("points", PinType::PointCloud)},
        {pe("op", 0,
            {"positionOffset", "scale", "rotation", "velocity", "color", "emission", "density", "attribute"}),
         pe("blend", 0, {"add", "multiply", "replace", "min", "max", "mix"}), pb("enabled", true),
         pf("strength", 1.0f, -5.0f, 5.0f), pf("weight", 1.0f, 0.0f, 1.0f),
         pv3("axis", glm::vec3(0.0f, 1.0f, 0.0f)), pv3("scaleAxis", glm::vec3(1.0f)), ps("target")},
        "Applies field x operation to the cloud on the CPU (static shaping; live GPU effectors are "
        "declared on output/procedural).",
        [](const Node& node, const std::vector<Value>& values, std::vector<Value>& outputs,
           EvalContext& ctx) -> Result<void> {
            const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
            const Inputs in(*info, values);
            const Params p(node);
            spatial::PointCloud cloud = cloudOf(in.single("points"));
            const spatial::FieldSpec* field = std::get_if<spatial::FieldSpec>(&in.single("field"));
            if (field == nullptr) {
                return fail("no field linked");
            }
            spatial::Effector effector;
            effector.field = field->name;
            if (const auto op = spatial::effectorOpFromName(p.label("op")); op) {
                effector.op = *op;
            }
            if (const auto blend = spatial::effectorBlendFromName(p.label("blend")); blend) {
                effector.blend = *blend;
            }
            readB(p, "enabled", effector.enabled);
            readF(p, "strength", effector.strength);
            readF(p, "weight", effector.weight);
            readV3(p, "axis", effector.axis);
            readV3(p, "scaleAxis", effector.scaleAxis);
            readS(p, "target", effector.target);
            spatial::FieldSet set;
            set.fields.push_back(*field);
            spatial::applyEffectors(cloud, std::span<const spatial::Effector>(&effector, 1), set, ctx.time);
            outputs[0] = std::move(cloud);
            return {};
        });
}

// ---- deformers --------------------------------------------------------------------------------------

void addDeformer(NodeRegistry& registry, const char* type, const char* label, scene::DeformerKind kind,
                 std::vector<PinInfo> inputs, std::vector<ParamInfo> extra, const char* description) {
    std::vector<ParamInfo> params{pb("enabled", true),
                                  pf("amount", 0.5f, -3.0f, 3.0f),
                                  pe("space", 0, {"local", "world"}),
                                  pf("speed", 0.0f, -5.0f, 5.0f),
                                  pf("phase", 0.0f, -6.2832f, 6.2832f),
                                  pv3("axis", glm::vec3(0.0f, 1.0f, 0.0f)),
                                  pv3("center", glm::vec3(0.0f)),
                                  pf("falloff", 0.0f, 0.0f, 10.0f)};
    append(params, std::move(extra));
    add(registry, type, label, NodeCategory::Deformers, std::move(inputs), {pin("deformer", PinType::Any)},
        std::move(params), description,
        [kind](const Node& node, const std::vector<Value>& values, std::vector<Value>& outputs,
               EvalContext&) -> Result<void> {
            const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
            const Inputs in(*info, values);
            const Params p(node);
            scene::Deformer d;
            d.kind = kind;
            readB(p, "enabled", d.enabled);
            readF(p, "amount", d.amount);
            if (p.has("space")) {
                if (const auto space = p.label("space") == "world" ? std::optional(scene::DeformSpace::World)
                                                                   : std::optional(scene::DeformSpace::Local);
                    space) {
                    d.space = *space;
                }
            }
            readF(p, "speed", d.speed);
            readF(p, "phase", d.phase);
            readV3(p, "axis", d.axis);
            readV3(p, "center", d.center);
            readF(p, "falloff", d.falloff);
            readF(p, "frequency", d.frequency);
            readV3(p, "displacementAxis", d.displacementAxis);
            readF(p, "scale", d.scale);
            readU32(p, "seed", d.seed);
            readV3(p, "axisMask", d.axisMask);
            readI(p, "pattern", d.pattern);
            readB(p, "alongNormal", d.alongNormal);
            readF(p, "pathOffset", d.pathOffset);
            readF(p, "pathScale", d.pathScale);
            readF(p, "pathRoll", d.pathRoll);
            readS(p, "field", d.field);
            readS(p, "spline", d.spline);
            if (const spatial::FieldSpec* field = std::get_if<spatial::FieldSpec>(&in.single("field"));
                field != nullptr) {
                d.field = field->name;
            }
            if (const spatial::Spline* spline = std::get_if<spatial::Spline>(&in.single("spline"));
                spline != nullptr) {
                d.spline = spline->name;
            }
            outputs[0] = deformerToJson(d).dump();
            return {};
        });
}

void registerDeformers(NodeRegistry& registry) {
    addDeformer(registry, "deformers/bend", "Bend", scene::DeformerKind::Bend, {},
                {pv3("displacementAxis", glm::vec3(1.0f, 0.0f, 0.0f))},
                "Barr bend of `amount` radians per unit along `axis`.");
    addDeformer(registry, "deformers/twist", "Twist", scene::DeformerKind::Twist, {}, {},
                "Rotation about `axis` proportional to the coordinate along it.");
    addDeformer(registry, "deformers/sine", "Sine", scene::DeformerKind::Sine, {},
                {pf("frequency", 1.0f, 0.0f, 10.0f), pv3("displacementAxis", glm::vec3(1.0f, 0.0f, 0.0f))},
                "Sine wave displacement along `displacementAxis`.");
    addDeformer(
        registry, "deformers/noise", "Noise", scene::DeformerKind::Noise, {},
        {pf("scale", 1.0f, 0.05f, 5.0f), pi("seed", 1, 0.0f, 999.0f), pv3("axisMask", glm::vec3(1.0f))},
        "Value-noise displacement per axis.");
    addDeformer(registry, "deformers/displacement", "Displacement", scene::DeformerKind::Displacement, {},
                {pf("scale", 1.0f, 0.05f, 5.0f), pi("seed", 1, 0.0f, 999.0f), pi("pattern", 0, 0.0f, 4.0f)},
                "Displacement along the vertex normal from a pattern source.");
    addDeformer(registry, "deformers/field", "Field Deform", scene::DeformerKind::Field,
                {pin("field", PinType::Field)}, {pb("alongNormal", true), ps("field")},
                "Displacement by a named field (link the field, and emit it with output/field).");
    addDeformer(registry, "deformers/path", "Path Deform", scene::DeformerKind::Path,
                {pin("spline", PinType::Spline)},
                {ps("spline"), pf("pathOffset", 0.0f, -50.0f, 50.0f), pf("pathScale", 0.0f, 0.0f, 10.0f),
                 pf("pathRoll", 0.0f, -6.2832f, 6.2832f)},
                "Curve deform along a named spline (link the spline, and emit it with output/spline).");
}

// ---- sdf ----------------------------------------------------------------------------------------------

void addSdfCombination(NodeRegistry& registry, const char* type, const char* label, spatial::SdfNodeKind kind,
                       bool smooth, const char* description) {
    std::vector<ParamInfo> params;
    if (smooth) {
        params.push_back(pf("smooth", 0.5f, 0.0f, 2.0f, true));
    }
    add(registry, type, label, NodeCategory::Sdf, {pin("sdf", PinType::Sdf, Value{}, true)},
        {pin("sdf", PinType::Sdf)}, std::move(params), description,
        [kind](const Node& node, const std::vector<Value>& values, std::vector<Value>& outputs,
               EvalContext&) -> Result<void> {
            const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
            const Inputs in(*info, values);
            const Params p(node);
            SdfValue result;
            result.tree.root.kind = kind;
            readF(p, "smooth", result.tree.root.smooth);
            for (const Value& value : in.multi("sdf")) {
                if (const SdfValue* child = std::get_if<SdfValue>(&value); child != nullptr) {
                    result.tree.root.children.push_back(child->tree.root);
                }
            }
            if (result.tree.root.children.empty()) {
                return fail("no sdf inputs linked");
            }
            if (result.tree.root.children.size() == 1) {
                result.tree.root = result.tree.root.children.front();
            }
            outputs[0] = std::move(result);
            return {};
        });
}

void addSdfUnary(NodeRegistry& registry, const char* type, const char* label, spatial::SdfNodeKind kind,
                 std::vector<ParamInfo> params, const char* description) {
    add(registry, type, label, NodeCategory::Sdf, {pin("sdf", PinType::Sdf)}, {pin("sdf", PinType::Sdf)},
        std::move(params), description,
        [kind](const Node& node, const std::vector<Value>& values, std::vector<Value>& outputs,
               EvalContext&) -> Result<void> {
            const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
            const Inputs in(*info, values);
            const Params p(node);
            const SdfValue* child = std::get_if<SdfValue>(&in.single("sdf"));
            if (child == nullptr) {
                return fail("no sdf input linked");
            }
            SdfValue result;
            spatial::SdfNode& n = result.tree.root;
            n.kind = kind;
            readF(p, "amount", n.amount);
            readV3(p, "axis", n.axis);
            readV3(p, "size", n.size);
            readI(p, "count", n.count);
            readF(p, "frequency", n.frequency);
            readF(p, "speed", n.speed);
            readU32(p, "seed", n.seed);
            readS(p, "reference", n.reference);
            if (const spatial::FieldSpec* field = std::get_if<spatial::FieldSpec>(&in.single("field"));
                field != nullptr) {
                n.reference = field->name;
            }
            n.children.push_back(child->tree.root);
            outputs[0] = std::move(result);
            return {};
        });
}

void registerSdf(NodeRegistry& registry) {
    addSdfCombination(registry, "sdf/union", "Union", spatial::SdfNodeKind::Union, false,
                      "Boolean union (min).");
    addSdfCombination(registry, "sdf/intersection", "Intersection", spatial::SdfNodeKind::Intersection, false,
                      "Boolean intersection (max).");
    addSdfCombination(registry, "sdf/difference", "Difference", spatial::SdfNodeKind::Difference, false,
                      "First input minus the rest.");
    addSdfCombination(registry, "sdf/smoothUnion", "Smooth Union", spatial::SdfNodeKind::SmoothUnion, true,
                      "Polynomial smooth union.");
    addSdfCombination(registry, "sdf/smoothIntersection", "Smooth Intersection",
                      spatial::SdfNodeKind::SmoothIntersection, true, "Polynomial smooth intersection.");
    addSdfCombination(registry, "sdf/smoothDifference", "Smooth Difference",
                      spatial::SdfNodeKind::SmoothDifference, true, "Polynomial smooth difference.");

    add(registry, "sdf/transform", "SDF Transform", NodeCategory::Sdf, {pin("sdf", PinType::Sdf)},
        {pin("sdf", PinType::Sdf)},
        {pv3("translation", glm::vec3(0.0f), true), pv3("rotation", glm::vec3(0.0f), true),
         pf("scale", 1.0f, 0.1f, 5.0f, true)},
        "Translate x rotate x scale of the child (identity levels are dropped).",
        [](const Node& node, const std::vector<Value>& values, std::vector<Value>& outputs,
           EvalContext&) -> Result<void> {
            const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
            const Inputs in(*info, values);
            const Params p(node);
            const SdfValue* child = std::get_if<SdfValue>(&in.single("sdf"));
            if (child == nullptr) {
                return fail("no sdf input linked");
            }
            spatial::SdfNode current = child->tree.root;
            const float scale = p.f("scale");
            if (scale != 1.0f) {
                spatial::SdfNode node2;
                node2.kind = spatial::SdfNodeKind::Scale;
                node2.scale = scale;
                node2.children.push_back(std::move(current));
                current = std::move(node2);
            }
            const glm::vec3 rotation = p.v3("rotation");
            if (rotation != glm::vec3(0.0f)) {
                spatial::SdfNode node2;
                node2.kind = spatial::SdfNodeKind::Rotate;
                node2.rotationDegrees = rotation;
                node2.children.push_back(std::move(current));
                current = std::move(node2);
            }
            const glm::vec3 translation = p.v3("translation");
            if (translation != glm::vec3(0.0f)) {
                spatial::SdfNode node2;
                node2.kind = spatial::SdfNodeKind::Translate;
                node2.translation = translation;
                node2.children.push_back(std::move(current));
                current = std::move(node2);
            }
            SdfValue result;
            result.tree.root = std::move(current);
            outputs[0] = std::move(result);
            return {};
        });

    addSdfUnary(registry, "sdf/twist", "SDF Twist", spatial::SdfNodeKind::Twist,
                {pf("amount", 0.5f, -3.0f, 3.0f, true)}, "Twists the child about Y.");
    addSdfUnary(registry, "sdf/bend", "SDF Bend", spatial::SdfNodeKind::Bend,
                {pf("amount", 0.5f, -3.0f, 3.0f, true)}, "Bends the child about Z.");
    addSdfUnary(registry, "sdf/repeat", "SDF Repeat", spatial::SdfNodeKind::Repeat,
                {pv3("size", glm::vec3(4.0f), true), pi("count", 0, 0.0f, 16.0f)},
                "Repeats the child on a grid (size 0 on an axis = no repeat).");
    addSdfUnary(registry, "sdf/polarRepeat", "SDF Polar Repeat", spatial::SdfNodeKind::PolarRepeat,
                {pi("count", 6, 1.0f, 32.0f)}, "Repeats the child `count` times about Y.");
    addSdfUnary(registry, "sdf/mirror", "SDF Mirror", spatial::SdfNodeKind::Mirror,
                {pv3("size", glm::vec3(1.0f, 0.0f, 0.0f), true)},
                "Mirrors the child on the axes where size > 0.");
    add(registry, "sdf/displace", "SDF Displace", NodeCategory::Sdf,
        {pin("sdf", PinType::Sdf), pin("field", PinType::Field)}, {pin("sdf", PinType::Sdf)},
        {pe("mode", 0, {"noise", "voronoi", "wave", "field"}), pf("amount", 0.1f, -2.0f, 2.0f, true),
         pf("frequency", 1.0f, 0.01f, 10.0f, true), pf("speed", 0.0f, -5.0f, 5.0f),
         pi("seed", 1, 0.0f, 999.0f), pv3("axis", glm::vec3(0.0f, 1.0f, 0.0f), true), ps("reference")},
        "Adds a noise, voronoi, wave or field displacement to the child's distance.",
        [](const Node& node, const std::vector<Value>& values, std::vector<Value>& outputs,
           EvalContext&) -> Result<void> {
            const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
            const Inputs in(*info, values);
            const Params p(node);
            const SdfValue* child = std::get_if<SdfValue>(&in.single("sdf"));
            if (child == nullptr) {
                return fail("no sdf input linked");
            }
            SdfValue result;
            spatial::SdfNode& n = result.tree.root;
            const std::string mode = p.label("mode");
            n.kind = mode == "voronoi" ? spatial::SdfNodeKind::DisplaceVoronoi
                     : mode == "wave"  ? spatial::SdfNodeKind::DisplaceWave
                     : mode == "field" ? spatial::SdfNodeKind::DisplaceField
                                       : spatial::SdfNodeKind::DisplaceNoise;
            readF(p, "amount", n.amount);
            readF(p, "frequency", n.frequency);
            readF(p, "speed", n.speed);
            readU32(p, "seed", n.seed);
            readV3(p, "axis", n.axis);
            readS(p, "reference", n.reference);
            if (const spatial::FieldSpec* field = std::get_if<spatial::FieldSpec>(&in.single("field"));
                field != nullptr) {
                n.reference = field->name;
            }
            n.children.push_back(child->tree.root);
            outputs[0] = std::move(result);
            return {};
        });
}

// ---- materials ------------------------------------------------------------------------------------------

void registerMaterials(NodeRegistry& registry) {
    add(registry, "materials/material", "Material", NodeCategory::Materials, {},
        {pin("material", PinType::Material)},
        {pcol("baseColor", glm::vec4(0.75f, 0.2f, 0.9f, 1.0f)), pf("opacity", 1.0f, 0.0f, 1.0f),
         pcol("emissiveColor", glm::vec4(0.9f, 0.45f, 1.0f, 1.0f)), pf("emissiveIntensity", 0.0f, 0.0f, 8.0f),
         pf("roughness", 0.35f, 0.0f, 1.0f), pf("metallic", 0.0f, 0.0f, 1.0f), pb("doubleSided", false),
         pb("unlit", false), ps("program")},
        "A PBR material (optionally naming a procedural material program).",
        [](const Node& node, const std::vector<Value>&, std::vector<Value>& outputs,
           EvalContext&) -> Result<void> {
            const Params p(node);
            MaterialValue value;
            const glm::vec4 baseColor = p.v4("baseColor");
            const glm::vec4 emissiveColor = p.v4("emissiveColor");
            value.material.baseColor = glm::vec3(baseColor);
            value.material.opacity = p.f("opacity");
            value.material.emissiveColor = glm::vec3(emissiveColor);
            value.material.emissiveIntensity = p.f("emissiveIntensity");
            value.material.roughness = p.f("roughness");
            value.material.metallic = p.f("metallic");
            value.material.doubleSided = p.b("doubleSided");
            value.material.unlit = p.b("unlit");
            value.material.program = p.s("program");
            outputs[0] = std::move(value);
            return {};
        });

    add(registry, "materials/program", "Material Program", NodeCategory::Materials,
        {pin("material", PinType::Material)}, {pin("material", PinType::Material)},
        {ps("name"), ps("program", "{}")},
        "Attaches a procedural material program (JSON in `program`, ADR-030) to a material.",
        [](const Node& node, const std::vector<Value>& values, std::vector<Value>& outputs,
           EvalContext&) -> Result<void> {
            const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
            const Inputs in(*info, values);
            const Params p(node);
            MaterialValue value;
            if (const MaterialValue* upstream = std::get_if<MaterialValue>(&in.single("material"));
                upstream != nullptr) {
                value = *upstream;
            }
            const std::string text = p.s("program");
            json parsed = json::object();
            if (!text.empty()) {
                parsed = json::parse(text, nullptr, false);
                if (parsed.is_discarded()) {
                    return fail("'program' is not valid JSON");
                }
            }
            auto program = scene::MaterialProgram::fromJson(parsed);
            if (!program) {
                return std::unexpected(program.error());
            }
            const std::string name = p.s("name");
            program->name = name.empty() ? node.name : name;
            value.material.program = program->name;
            value.program = std::move(*program);
            outputs[0] = std::move(value);
            return {};
        });

    add(registry, "materials/fromColor", "Material From Colour", NodeCategory::Materials,
        {pin("color", PinType::Color, glm::vec4(1.0f)), pin("emissive", PinType::Float, 0.0f)},
        {pin("material", PinType::Material)},
        {pf("roughness", 0.35f, 0.0f, 1.0f), pf("metallic", 0.0f, 0.0f, 1.0f)},
        "Builds a material from a colour value (handy downstream of the math nodes).",
        [](const Node& node, const std::vector<Value>& values, std::vector<Value>& outputs,
           EvalContext&) -> Result<void> {
            const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
            const Inputs in(*info, values);
            const Params p(node);
            MaterialValue value;
            const glm::vec4 color = in.v4("color", glm::vec4(1.0f));
            value.material.baseColor = glm::vec3(color);
            value.material.opacity = color.a;
            value.material.emissiveColor = glm::vec3(color);
            value.material.emissiveIntensity = in.f("emissive", 0.0f);
            value.material.roughness = p.f("roughness");
            value.material.metallic = p.f("metallic");
            outputs[0] = std::move(value);
            return {};
        });
}

// ---- particles and volumes -------------------------------------------------------------------------------

void registerParticlesAndVolumes(NodeRegistry& registry) {
    add(registry, "particles/system", "Particle System", NodeCategory::Particles,
        {pin("field", PinType::Field), pin("spline", PinType::Spline)},
        {pin("particles", PinType::Particles)},
        {pb("enabled", true),
         pi("capacity", 65536, 1024.0f, 262144.0f),
         pi("seed", 1, 0.0f, 999.0f),
         pe("shape", 1, {"point", "sphere", "disc", "box", "spline"}),
         pv3("position", glm::vec3(0.0f, 1.0f, 0.0f)),
         pv3("extent", glm::vec3(0.5f)),
         pf("spawnRate", 2000.0f, 0.0f, 20000.0f),
         pf("lifetimeMin", 1.0f, 0.0f, 10.0f),
         pf("lifetimeMax", 3.0f, 0.0f, 10.0f),
         pv3("direction", glm::vec3(0.0f, 1.0f, 0.0f)),
         pf("spread", 0.5f, 0.0f, 1.0f),
         pf("speedMin", 0.5f, 0.0f, 10.0f),
         pf("speedMax", 2.0f, 0.0f, 10.0f),
         pv3("gravity", glm::vec3(0.0f, -0.5f, 0.0f)),
         pf("drag", 0.2f, 0.0f, 5.0f),
         pf("turbulence", 0.8f, 0.0f, 5.0f),
         pf("turbulenceScale", 1.0f, 0.01f, 5.0f),
         pf("turbulenceSpeed", 0.3f, 0.0f, 5.0f),
         pf("sizeStart", 0.04f, 0.0f, 1.0f),
         pf("sizeEnd", 0.0f, 0.0f, 1.0f),
         pcol("colorStart", glm::vec4(1.0f, 0.6f, 0.2f, 1.0f)),
         pcol("colorEnd", glm::vec4(0.4f, 0.1f, 1.0f, 0.0f)),
         pf("emissive", 4.0f, 0.0f, 20.0f),
         pe("blend", 0, {"additive", "alpha"}),
         pf("softness", 0.2f, 0.0f, 2.0f),
         pe("fieldMode", 0, {"force", "velocity", "turbulence", "kill"}),
         pf("fieldStrength", 1.0f, -5.0f, 5.0f)},
        "A GPU particle system; a linked field becomes a field force, a linked spline the emitter shape.",
        [](const Node& node, const std::vector<Value>& values, std::vector<Value>& outputs,
           EvalContext&) -> Result<void> {
            const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
            const Inputs in(*info, values);
            const Params p(node);
            ParticlesValue value;
            scene::ParticleSystem& s = value.system;
            s.name = node.name;
            readB(p, "enabled", s.enabled);
            s.capacity = static_cast<std::uint32_t>(std::max(1, p.i("capacity")));
            readU32(p, "seed", s.seed);
            const std::string shape = p.label("shape");
            s.shape = shape == "point"    ? scene::EmitterShape::Point
                      : shape == "disc"   ? scene::EmitterShape::Disc
                      : shape == "box"    ? scene::EmitterShape::Box
                      : shape == "spline" ? scene::EmitterShape::Spline
                                          : scene::EmitterShape::Sphere;
            readV3(p, "position", s.position);
            readV3(p, "extent", s.extent);
            readF(p, "spawnRate", s.spawnRate);
            readF(p, "lifetimeMin", s.lifetimeMin);
            readF(p, "lifetimeMax", s.lifetimeMax);
            readV3(p, "direction", s.direction);
            readF(p, "spread", s.spread);
            readF(p, "speedMin", s.speedMin);
            readF(p, "speedMax", s.speedMax);
            readV3(p, "gravity", s.gravity);
            readF(p, "drag", s.drag);
            readF(p, "turbulence", s.turbulence);
            readF(p, "turbulenceScale", s.turbulenceScale);
            readF(p, "turbulenceSpeed", s.turbulenceSpeed);
            readF(p, "sizeStart", s.sizeStart);
            readF(p, "sizeEnd", s.sizeEnd);
            readV4(p, "colorStart", s.colorStart);
            readV4(p, "colorEnd", s.colorEnd);
            readF(p, "emissive", s.emissive);
            s.blend =
                p.label("blend") == "alpha" ? scene::ParticleBlend::Alpha : scene::ParticleBlend::Additive;
            readF(p, "softness", s.softness);
            if (const spatial::Spline* spline = std::get_if<spatial::Spline>(&in.single("spline"));
                spline != nullptr) {
                s.spline = spline->name;
                s.shape = scene::EmitterShape::Spline;
            }
            if (const spatial::FieldSpec* field = std::get_if<spatial::FieldSpec>(&in.single("field"));
                field != nullptr) {
                scene::FieldForce force;
                force.field = field->name;
                const std::string mode = p.label("fieldMode");
                force.mode = mode == "velocity"     ? scene::FieldForceMode::Velocity
                             : mode == "turbulence" ? scene::FieldForceMode::Turbulence
                             : mode == "kill"       ? scene::FieldForceMode::Kill
                                                    : scene::FieldForceMode::Force;
                force.strength = p.f("fieldStrength");
                s.fieldForces.push_back(force);
            }
            outputs[0] = std::move(value);
            return {};
        });

    add(registry, "volumes/fog", "Fog Volume", NodeCategory::Volumes, {pin("field", PinType::Field)},
        {pin("volume", PinType::Volume)}, {pf("density", 1.0f, 0.0f, 5.0f), ps("field")},
        "A fog volume whose density comes from a scalar field (ADR-032).",
        [](const Node& node, const std::vector<Value>& values, std::vector<Value>& outputs,
           EvalContext&) -> Result<void> {
            const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
            const Inputs in(*info, values);
            const Params p(node);
            VolumeValue value;
            value.density = p.f("density");
            value.densityField = p.s("field");
            if (const spatial::FieldSpec* field = std::get_if<spatial::FieldSpec>(&in.single("field"));
                field != nullptr) {
                value.densityField = field->name;
            }
            outputs[0] = std::move(value);
            return {};
        });
}

// ---- audio and time --------------------------------------------------------------------------------------

void addSignalNode(NodeRegistry& registry, const char* type, const char* label, const char* defaultSignal,
                   const char* description) {
    add(registry, type, label,
        std::string(type).starts_with("audio/") ? NodeCategory::Audio : NodeCategory::Time, {},
        {pin("value", PinType::Float)},
        {ps("signal", defaultSignal), ps("target"), pf("amount", 1.0f, -2.0f, 2.0f),
         pe("op", 0, {"add", "multiply", "replace", "min", "max"}),
         pe("polarity", 0, {"unipolar", "bipolar"}), pi("component", -1, -1.0f, 3.0f),
         pf("value", 0.0f, 0.0f, 1.0f)},
        description,
        [](const Node& node, const std::vector<Value>&, std::vector<Value>& outputs,
           EvalContext&) -> Result<void> {
            const Params p(node);
            outputs[0] = p.f("value");
            return {};
        });
}

void registerAudioAndTime(NodeRegistry& registry) {
    addSignalNode(
        registry, "audio/signal", "Audio Signal", "audio.bass",
        "A signal bus source; link it into an Output node's parameter pin to emit a modulation route.");
    addSignalNode(
        registry, "time/beatPhase", "Beat Phase", "beat.phase",
        "A beat-clock signal; link it into an Output node's parameter pin to emit a modulation route.");

    add(registry, "time/time", "Time", NodeCategory::Time, {}, {pin("time", PinType::Float)},
        {pf("scale", 1.0f, 0.0f, 4.0f), pf("offset", 0.0f, -10.0f, 10.0f)},
        "The evaluation time in seconds (times `scale` plus `offset`). Static: the graph is not "
        "re-evaluated per frame.",
        [](const Node& node, const std::vector<Value>&, std::vector<Value>& outputs,
           EvalContext& ctx) -> Result<void> {
            const Params p(node);
            outputs[0] = static_cast<float>(ctx.time) * p.f("scale") + p.f("offset");
            return {};
        });
}

// ---- math ------------------------------------------------------------------------------------------------

enum class MathOp : std::uint8_t {
    Add,
    Subtract,
    Multiply,
    Divide,
    Min,
    Max,
    Mix,
    Clamp,
    Remap,
    Abs,
    Sin,
    Cos,
    Pow,
    Floor,
    Fract,
    Smoothstep,
    Length,
    Normalize,
    Dot,
    Cross
};

Num numberInput(const Inputs& in, const Params& p, const char* name) {
    if (in.has(name)) {
        return detail::numberOf(in.single(name));
    }
    return Num{glm::vec4(p.has(name) ? p.f(name) : 0.0f, 0.0f, 0.0f, 0.0f), 1};
}

glm::vec4 broadcast(const Num& n) {
    return n.components == 1 ? glm::vec4(n.v.x) : n.v;
}

Num combine(const Num& a, const Num& b, float (*fn)(float, float)) {
    Num out;
    out.components = std::max(a.components, b.components);
    const glm::vec4 va = broadcast(a);
    const glm::vec4 vb = broadcast(b);
    for (int i = 0; i < 4; ++i) {
        out.v[i] = fn(va[i], vb[i]);
    }
    return out;
}

Num mapComponents(const Num& a, float (*fn)(float)) {
    Num out = a;
    for (int i = 0; i < 4; ++i) {
        out.v[i] = fn(a.v[i]);
    }
    return out;
}

float addF(float a, float b) {
    return a + b;
}
float subF(float a, float b) {
    return a - b;
}
float mulF(float a, float b) {
    return a * b;
}
float divF(float a, float b) {
    return b == 0.0f ? 0.0f : a / b;
}
float minF(float a, float b) {
    return std::min(a, b);
}
float maxF(float a, float b) {
    return std::max(a, b);
}
float powF(float a, float b) {
    return std::pow(std::max(a, 0.0f), b);
}
float absF(float a) {
    return std::abs(a);
}
float sinF(float a) {
    return std::sin(a);
}
float cosF(float a) {
    return std::cos(a);
}
float floorF(float a) {
    return std::floor(a);
}
float fractF(float a) {
    return a - std::floor(a);
}

void addMathNode(NodeRegistry& registry, const char* type, const char* label, MathOp op,
                 std::vector<PinInfo> inputs, std::vector<PinInfo> outputs, std::vector<ParamInfo> params,
                 const char* description) {
    add(registry, type, label, NodeCategory::Math, std::move(inputs), std::move(outputs), std::move(params),
        description,
        [op](const Node& node, const std::vector<Value>& values, std::vector<Value>& outs,
             EvalContext&) -> Result<void> {
            const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
            const Inputs in(*info, values);
            const Params p(node);
            switch (op) {
            case MathOp::Add:
                outs[0] =
                    detail::numberValue(combine(numberInput(in, p, "a"), numberInput(in, p, "b"), &addF));
                break;
            case MathOp::Subtract:
                outs[0] =
                    detail::numberValue(combine(numberInput(in, p, "a"), numberInput(in, p, "b"), &subF));
                break;
            case MathOp::Multiply:
                outs[0] =
                    detail::numberValue(combine(numberInput(in, p, "a"), numberInput(in, p, "b"), &mulF));
                break;
            case MathOp::Divide:
                outs[0] =
                    detail::numberValue(combine(numberInput(in, p, "a"), numberInput(in, p, "b"), &divF));
                break;
            case MathOp::Min:
                outs[0] =
                    detail::numberValue(combine(numberInput(in, p, "a"), numberInput(in, p, "b"), &minF));
                break;
            case MathOp::Max:
                outs[0] =
                    detail::numberValue(combine(numberInput(in, p, "a"), numberInput(in, p, "b"), &maxF));
                break;
            case MathOp::Pow:
                outs[0] =
                    detail::numberValue(combine(numberInput(in, p, "a"), numberInput(in, p, "b"), &powF));
                break;
            case MathOp::Abs:
                outs[0] = detail::numberValue(mapComponents(numberInput(in, p, "a"), &absF));
                break;
            case MathOp::Sin:
                outs[0] = detail::numberValue(mapComponents(numberInput(in, p, "a"), &sinF));
                break;
            case MathOp::Cos:
                outs[0] = detail::numberValue(mapComponents(numberInput(in, p, "a"), &cosF));
                break;
            case MathOp::Floor:
                outs[0] = detail::numberValue(mapComponents(numberInput(in, p, "a"), &floorF));
                break;
            case MathOp::Fract:
                outs[0] = detail::numberValue(mapComponents(numberInput(in, p, "a"), &fractF));
                break;
            case MathOp::Mix: {
                const Num a = numberInput(in, p, "a");
                const Num b = numberInput(in, p, "b");
                const float t = numberInput(in, p, "t").v.x;
                Num out;
                out.components = std::max(a.components, b.components);
                out.v = glm::mix(broadcast(a), broadcast(b), t);
                outs[0] = detail::numberValue(out);
                break;
            }
            case MathOp::Clamp: {
                const Num value = numberInput(in, p, "value");
                const Num lo = numberInput(in, p, "min");
                const Num hi = numberInput(in, p, "max");
                Num out = value;
                out.v = glm::clamp(broadcast(value), broadcast(lo), broadcast(hi));
                outs[0] = detail::numberValue(out);
                break;
            }
            case MathOp::Remap: {
                const Num value = numberInput(in, p, "value");
                const float inMin = p.f("inMin");
                const float inMax = p.f("inMax");
                const float outMin = p.f("outMin");
                const float outMax = p.f("outMax");
                const float span = inMax - inMin;
                Num out = value;
                for (int i = 0; i < 4; ++i) {
                    float t = span == 0.0f ? 0.0f : (value.v[i] - inMin) / span;
                    if (p.b("clamp")) {
                        t = std::clamp(t, 0.0f, 1.0f);
                    }
                    out.v[i] = outMin + t * (outMax - outMin);
                }
                outs[0] = detail::numberValue(out);
                break;
            }
            case MathOp::Smoothstep: {
                const Num edge0 = numberInput(in, p, "edge0");
                const Num edge1 = numberInput(in, p, "edge1");
                const Num value = numberInput(in, p, "value");
                Num out = value;
                for (int i = 0; i < 4; ++i) {
                    out.v[i] = glm::smoothstep(broadcast(edge0)[i], broadcast(edge1)[i], value.v[i]);
                }
                outs[0] = detail::numberValue(out);
                break;
            }
            case MathOp::Length: {
                const Num a = numberInput(in, p, "a");
                outs[0] = glm::length(glm::vec3(a.v));
                break;
            }
            case MathOp::Normalize: {
                const Num a = numberInput(in, p, "a");
                const glm::vec3 v = glm::vec3(a.v);
                const float len = glm::length(v);
                outs[0] = len > 1e-8f ? v / len : glm::vec3(0.0f);
                break;
            }
            case MathOp::Dot: {
                const Num a = numberInput(in, p, "a");
                const Num b = numberInput(in, p, "b");
                outs[0] = glm::dot(glm::vec3(broadcast(a)), glm::vec3(broadcast(b)));
                break;
            }
            case MathOp::Cross: {
                const Num a = numberInput(in, p, "a");
                const Num b = numberInput(in, p, "b");
                outs[0] = glm::cross(glm::vec3(broadcast(a)), glm::vec3(broadcast(b)));
                break;
            }
            }
            return {};
        });
}

void registerMath(NodeRegistry& registry) {
    const auto binary = [&](const char* type, const char* label, MathOp op, float defaultB,
                            const char* description) {
        addMathNode(registry, type, label, op, {pin("a", PinType::Any), pin("b", PinType::Any)},
                    {pin("result", PinType::Any)},
                    {pf("a", 0.0f, -10.0f, 10.0f), pf("b", defaultB, -10.0f, 10.0f)}, description);
    };
    binary("math/add", "Add", MathOp::Add, 0.0f, "a + b (componentwise, scalars broadcast).");
    binary("math/subtract", "Subtract", MathOp::Subtract, 0.0f, "a - b.");
    binary("math/multiply", "Multiply", MathOp::Multiply, 1.0f, "a * b.");
    binary("math/divide", "Divide", MathOp::Divide, 1.0f, "a / b (0 when b is 0).");
    binary("math/min", "Min", MathOp::Min, 0.0f, "min(a, b).");
    binary("math/max", "Max", MathOp::Max, 0.0f, "max(a, b).");
    binary("math/pow", "Power", MathOp::Pow, 2.0f, "pow(max(a, 0), b).");

    const auto unary = [&](const char* type, const char* label, MathOp op, PinType outType,
                           const char* description) {
        addMathNode(registry, type, label, op, {pin("a", PinType::Any)}, {pin("result", outType)},
                    {pf("a", 0.0f, -10.0f, 10.0f)}, description);
    };
    unary("math/abs", "Abs", MathOp::Abs, PinType::Any, "|a| componentwise.");
    unary("math/sin", "Sine", MathOp::Sin, PinType::Any, "sin(a) componentwise (radians).");
    unary("math/cos", "Cosine", MathOp::Cos, PinType::Any, "cos(a) componentwise (radians).");
    unary("math/floor", "Floor", MathOp::Floor, PinType::Any, "floor(a) componentwise.");
    unary("math/fract", "Fract", MathOp::Fract, PinType::Any, "a - floor(a) componentwise.");
    unary("math/length", "Length", MathOp::Length, PinType::Float, "Length of the vector.");
    unary("math/normalize", "Normalize", MathOp::Normalize, PinType::Vec3,
          "The unit vector (zero when degenerate).");

    addMathNode(registry, "math/mix", "Mix", MathOp::Mix,
                {pin("a", PinType::Any), pin("b", PinType::Any), pin("t", PinType::Float)},
                {pin("result", PinType::Any)},
                {pf("a", 0.0f, -10.0f, 10.0f), pf("b", 1.0f, -10.0f, 10.0f), pf("t", 0.5f, 0.0f, 1.0f)},
                "mix(a, b, t).");
    addMathNode(
        registry, "math/clamp", "Clamp", MathOp::Clamp,
        {pin("value", PinType::Any), pin("min", PinType::Any), pin("max", PinType::Any)},
        {pin("result", PinType::Any)},
        {pf("value", 0.0f, -10.0f, 10.0f), pf("min", 0.0f, -10.0f, 10.0f), pf("max", 1.0f, -10.0f, 10.0f)},
        "clamp(value, min, max).");
    addMathNode(registry, "math/remap", "Remap", MathOp::Remap, {pin("value", PinType::Any)},
                {pin("result", PinType::Any)},
                {pf("value", 0.0f, -10.0f, 10.0f), pf("inMin", 0.0f, -10.0f, 10.0f),
                 pf("inMax", 1.0f, -10.0f, 10.0f), pf("outMin", 0.0f, -10.0f, 10.0f),
                 pf("outMax", 1.0f, -10.0f, 10.0f), pb("clamp", true, false)},
                "Maps value from [inMin, inMax] to [outMin, outMax].");
    addMathNode(registry, "math/smoothstep", "Smoothstep", MathOp::Smoothstep,
                {pin("edge0", PinType::Any), pin("edge1", PinType::Any), pin("value", PinType::Any)},
                {pin("result", PinType::Any)},
                {pf("edge0", 0.0f, -10.0f, 10.0f), pf("edge1", 1.0f, -10.0f, 10.0f),
                 pf("value", 0.5f, -10.0f, 10.0f)},
                "smoothstep(edge0, edge1, value).");
    addMathNode(registry, "math/dot", "Dot", MathOp::Dot, {pin("a", PinType::Any), pin("b", PinType::Any)},
                {pin("result", PinType::Float)}, {pf("a", 0.0f, -10.0f, 10.0f), pf("b", 0.0f, -10.0f, 10.0f)},
                "dot(a, b) over the first three components.");
    addMathNode(registry, "math/cross", "Cross", MathOp::Cross,
                {pin("a", PinType::Any), pin("b", PinType::Any)}, {pin("result", PinType::Vec3)},
                {pf("a", 0.0f, -10.0f, 10.0f), pf("b", 0.0f, -10.0f, 10.0f)}, "cross(a, b).");

    add(registry, "math/vec3", "Vector", NodeCategory::Math,
        {pin("x", PinType::Float), pin("y", PinType::Float), pin("z", PinType::Float)},
        {pin("result", PinType::Vec3)},
        {pf("x", 0.0f, -10.0f, 10.0f), pf("y", 0.0f, -10.0f, 10.0f), pf("z", 0.0f, -10.0f, 10.0f)},
        "Builds a vec3 from three floats.",
        [](const Node& node, const std::vector<Value>& values, std::vector<Value>& outputs,
           EvalContext&) -> Result<void> {
            const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
            const Inputs in(*info, values);
            const Params p(node);
            outputs[0] = glm::vec3(numberInput(in, p, "x").v.x, numberInput(in, p, "y").v.x,
                                   numberInput(in, p, "z").v.x);
            return {};
        });

    add(registry, "math/split", "Split", NodeCategory::Math, {pin("value", PinType::Any)},
        {pin("x", PinType::Float), pin("y", PinType::Float), pin("z", PinType::Float),
         pin("w", PinType::Float)},
        {}, "Splits a vector into its components.",
        [](const Node& node, const std::vector<Value>& values, std::vector<Value>& outputs,
           EvalContext&) -> Result<void> {
            const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
            const Inputs in(*info, values);
            const Num n = detail::numberOf(in.single("value"));
            outputs[0] = n.v.x;
            outputs[1] = n.v.y;
            outputs[2] = n.v.z;
            outputs[3] = n.v.w;
            return {};
        });

    add(registry, "math/constant", "Constant", NodeCategory::Math, {}, {pin("value", PinType::Float)},
        {pf("value", 1.0f, -10.0f, 10.0f)}, "A constant float.",
        [](const Node& node, const std::vector<Value>&, std::vector<Value>& outputs,
           EvalContext&) -> Result<void> {
            outputs[0] = Params(node).f("value");
            return {};
        });

    add(registry, "math/color", "Colour", NodeCategory::Math, {}, {pin("color", PinType::Color)},
        {pcol("color", glm::vec4(1.0f))}, "A constant colour (linear rgba).",
        [](const Node& node, const std::vector<Value>&, std::vector<Value>& outputs,
           EvalContext&) -> Result<void> {
            outputs[0] = Params(node).v4("color");
            return {};
        });

    add(registry, "math/random", "Random", NodeCategory::Math, {pin("index", PinType::Int)},
        {pin("value", PinType::Float)},
        {pi("seed", 1, 0.0f, 999.0f), pi("index", 0, 0.0f, 1024.0f), pf("min", 0.0f, -10.0f, 10.0f),
         pf("max", 1.0f, -10.0f, 10.0f), pi("channel", 0, 0.0f, 16.0f)},
        "A deterministic random in [min, max] from (seed, index, channel).",
        [](const Node& node, const std::vector<Value>& values, std::vector<Value>& outputs,
           EvalContext&) -> Result<void> {
            const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
            const Inputs in(*info, values);
            const Params p(node);
            const int index = in.has("index") ? in.i("index") : p.i("index");
            const float random = noise::hashIndex(
                p.u32("seed"), static_cast<std::uint32_t>(std::max(0, index)), p.u32("channel"));
            outputs[0] = p.f("min") + random * (p.f("max") - p.f("min"));
            return {};
        });
}

// ---- logic ------------------------------------------------------------------------------------------------

void registerLogic(NodeRegistry& registry) {
    add(registry, "logic/select", "Select", NodeCategory::Logic,
        {pin("condition", PinType::Bool), pin("a", PinType::Any), pin("b", PinType::Any)},
        {pin("result", PinType::Any)}, {pb("condition", true, false)},
        "a when the condition is true, else b.",
        [](const Node& node, const std::vector<Value>& values, std::vector<Value>& outputs,
           EvalContext&) -> Result<void> {
            const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
            const Inputs in(*info, values);
            const Params p(node);
            const bool condition = in.has("condition") ? in.b("condition") : p.b("condition");
            outputs[0] = condition ? in.single("a") : in.single("b");
            return {};
        });

    add(registry, "logic/compare", "Compare", NodeCategory::Logic,
        {pin("a", PinType::Any), pin("b", PinType::Any)}, {pin("result", PinType::Bool)},
        {pf("a", 0.0f, -10.0f, 10.0f), pf("b", 0.0f, -10.0f, 10.0f), pe("compare", 3, compareChoices())},
        "Compares the first components of a and b.",
        [](const Node& node, const std::vector<Value>& values, std::vector<Value>& outputs,
           EvalContext&) -> Result<void> {
            const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
            const Inputs in(*info, values);
            const Params p(node);
            const float a = numberInput(in, p, "a").v.x;
            const float b = numberInput(in, p, "b").v.x;
            switch (p.enumOf("compare")) {
            case 0:
                outputs[0] = a < b;
                break;
            case 1:
                outputs[0] = a <= b;
                break;
            case 2:
                outputs[0] = a == b;
                break;
            case 4:
                outputs[0] = a > b;
                break;
            case 5:
                outputs[0] = a != b;
                break;
            default:
                outputs[0] = a >= b;
                break;
            }
            return {};
        });

    const auto boolOp = [&](const char* type, const char* label, int mode, const char* description) {
        add(registry, type, label, NodeCategory::Logic, {pin("a", PinType::Bool), pin("b", PinType::Bool)},
            {pin("result", PinType::Bool)}, {pb("a", false, false), pb("b", false, false)}, description,
            [mode](const Node& node, const std::vector<Value>& values, std::vector<Value>& outputs,
                   EvalContext&) -> Result<void> {
                const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
                const Inputs in(*info, values);
                const Params p(node);
                const bool a = in.has("a") ? in.b("a") : p.b("a");
                const bool b = in.has("b") ? in.b("b") : p.b("b");
                outputs[0] = mode == 0 ? (a && b) : (a || b);
                return {};
            });
    };
    boolOp("logic/and", "And", 0, "a and b.");
    boolOp("logic/or", "Or", 1, "a or b.");

    add(registry, "logic/not", "Not", NodeCategory::Logic, {pin("a", PinType::Bool)},
        {pin("result", PinType::Bool)}, {pb("a", false, false)}, "The negation of a.",
        [](const Node& node, const std::vector<Value>& values, std::vector<Value>& outputs,
           EvalContext&) -> Result<void> {
            const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
            const Inputs in(*info, values);
            const Params p(node);
            outputs[0] = !(in.has("a") ? in.b("a") : p.b("a"));
            return {};
        });

    add(registry, "logic/switch", "Switch", NodeCategory::Logic,
        {pin("index", PinType::Int), pin("values", PinType::Any, Value{}, true)},
        {pin("result", PinType::Any)}, {pi("index", 0, 0.0f, 8.0f, false)},
        "Passes through the linked value at `index` (link order).",
        [](const Node& node, const std::vector<Value>& values, std::vector<Value>& outputs,
           EvalContext&) -> Result<void> {
            const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
            const Inputs in(*info, values);
            const Params p(node);
            const std::span<const Value> choices = in.multi("values");
            if (choices.empty()) {
                return fail("no values linked");
            }
            const int index = in.has("index") ? in.i("index") : p.i("index");
            outputs[0] =
                choices[static_cast<std::size_t>(std::clamp(index, 0, static_cast<int>(choices.size()) - 1))];
            return {};
        });
}

// ---- output
// ------------------------------------------------------------------------------------------------

struct ParamPin {
    const char* pin;
    const char* path; // parameter sub-path under "<kind>/<name>/"
};

bool isSignalType(const std::string& type) {
    return type == "audio/signal" || type == "time/beatPhase";
}

// Emits a ModRoute for every direct link from a signal node into one of this node's pins and
// returns the pins it covered (their static value is left to the emitted object).
std::vector<std::string> emitSignalRoutes(const Node& node, EvalContext& ctx, const std::string& prefix,
                                          const std::vector<ParamPin>& pins) {
    std::vector<std::string> driven;
    if (ctx.graph == nullptr || ctx.output == nullptr) {
        return driven;
    }
    for (const Link& link : ctx.graph->links) {
        if (link.toNode != node.name) {
            continue;
        }
        const Node* source = ctx.graph->findNode(link.fromNode);
        if (source == nullptr || !isSignalType(source->type) || !source->enabled) {
            continue;
        }
        const Params sp(*source);
        std::string path = sp.s("target");
        if (path.empty()) {
            for (const ParamPin& entry : pins) {
                if (link.toPin == entry.pin) {
                    path = entry.path;
                    break;
                }
            }
        }
        if (path.empty()) {
            ctx.output->warnings.push_back(fmt::format(
                "node '{}': signal '{}' linked into '{}' has no parameter target (set the signal's `target`)",
                node.name, source->name, link.toPin));
            continue;
        }
        params::ModRoute route;
        route.source = sp.s("signal");
        route.target = prefix + path;
        route.component = sp.i("component");
        route.amount = sp.f("amount");
        route.op = static_cast<params::ModOp>(std::clamp(sp.enumOf("op"), 0, 4));
        route.polarity = sp.enumOf("polarity") == 1 ? params::Polarity::Bipolar : params::Polarity::Unipolar;
        ctx.output->routes.push_back(std::move(route));
        driven.push_back(link.toPin);
    }
    return driven;
}

bool drivenBySignal(const std::vector<std::string>& driven, const char* pin) {
    return std::find(driven.begin(), driven.end(), pin) != driven.end();
}

void emitField(GraphOutput& output, spatial::FieldSpec field) {
    for (spatial::FieldSpec& existing : output.fields) {
        if (existing.name == field.name) {
            existing = std::move(field);
            return;
        }
    }
    output.fields.push_back(std::move(field));
}

void emitProgram(GraphOutput& output, scene::MaterialProgram program) {
    for (scene::MaterialProgram& existing : output.materialPrograms) {
        if (existing.name == program.name) {
            existing = std::move(program);
            return;
        }
    }
    output.materialPrograms.push_back(std::move(program));
}

// One Place rule per point (bounded); the axiom branches into all of them.
scene::Grammar grammarFromCloud(const spatial::PointCloud& cloud, std::size_t limit) {
    scene::Grammar grammar;
    grammar.axiom = "root";
    grammar.maxDepth = 2;
    const std::size_t count = std::min(cloud.count(), limit);
    grammar.maxInstances = static_cast<int>(std::max<std::size_t>(count, 1));
    scene::GrammarRule root;
    root.name = "root";
    root.op = scene::GrammarOp::Branch;
    const auto positions = cloud.positions();
    const auto rotations = cloud.rotations();
    const auto scales = cloud.scales();
    std::vector<scene::GrammarRule> places;
    places.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        scene::GrammarRule place;
        place.name = "p" + std::to_string(i);
        place.op = scene::GrammarOp::Place;
        place.pre.position = positions[i];
        place.pre.rotation = glm::quat(rotations[i].w, rotations[i].x, rotations[i].y, rotations[i].z);
        place.pre.scale = scales[i];
        root.children.push_back(place.name);
        places.push_back(std::move(place));
    }
    grammar.rules.push_back(std::move(root));
    for (scene::GrammarRule& place : places) {
        grammar.rules.push_back(std::move(place));
    }
    return grammar;
}

void registerOutputs(NodeRegistry& registry) {
    add(registry, "output/procedural", "Procedural Output", NodeCategory::Output,
        {pin("source", PinType::Mesh), pin("points", PinType::PointCloud), pin("spec", PinType::String),
         pin("material", PinType::Material), pin("emissive", PinType::Float),
         pin("roughness", PinType::Float), pin("metallic", PinType::Float), pin("hueShift", PinType::Float),
         pin("radius", PinType::Float), pin("emissiveFieldAmount", PinType::Float),
         pin("deformers", PinType::Any, Value{}, true), pin("fields", PinType::Field, Value{}, true),
         pin("modulation", PinType::Float, Value{}, true)},
        {},
        {ps("name"), pb("visible", true, false), pv3("position", glm::vec3(0.0f)),
         pv3("rotation", glm::vec3(0.0f)), pv3("scale", glm::vec3(1.0f)), pi("seed", 12345, 0.0f, 1000.0f),
         pv3("randomPosition", glm::vec3(0.0f), true), pv3("randomRotation", glm::vec3(0.0f), true),
         pv3("randomScale", glm::vec3(0.0f), true), pf("randomUniformScale", 0.0f, 0.0f, 1.0f, true),
         pf("hueShift", 0.0f, 0.0f, 1.0f), pf("hueGradient", 0.0f, -1.0f, 1.0f),
         pf("valueRandom", 0.0f, 0.0f, 1.0f), pf("emissiveRandom", 0.0f, 0.0f, 1.0f),
         pf("emissiveGradient", 0.0f, 0.0f, 4.0f), ps("extraLane"), ps("emissiveField"),
         pf("emissiveFieldAmount", 0.0f, 0.0f, 10.0f), ps("effectors", "[]")},
        "Emits a scene::ProceduralGeometry: source mesh, distribution (from a `spec`, else a grammar "
        "built from `points`), deformer stack, material, GPU effectors and modulation routes.",
        [](const Node& node, const std::vector<Value>& values, std::vector<Value>&,
           EvalContext& ctx) -> Result<void> {
            const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
            const Inputs in(*info, values);
            const Params p(node);
            scene::ProceduralGeometry g;
            const std::string name = p.s("name");
            g.name = name.empty() ? node.name : name;
            g.visible = p.b("visible");
            const std::string prefix = "procedural/" + g.name + "/";
            const std::vector<std::string> driven =
                emitSignalRoutes(node, ctx, prefix,
                                 {{"emissive", "material/emissive"},
                                  {"roughness", "material/roughness"},
                                  {"metallic", "material/metallic"},
                                  {"hueShift", "materialVariation/hueShift"},
                                  {"radius", "distribution/radius"},
                                  {"emissiveFieldAmount", "emissiveFieldAmount"}});

            if (const MeshValue* mesh = std::get_if<MeshValue>(&in.single("source")); mesh != nullptr) {
                g.source = mesh->source;
                g.sourceTransform = mesh->transform;
            }
            if (in.has("spec")) {
                json parsed = json::parse(in.s("spec"), nullptr, false);
                if (parsed.is_discarded()) {
                    return fail("'spec' is not valid JSON");
                }
                auto distribution = distributionFromJson(parsed);
                if (!distribution) {
                    return std::unexpected(distribution.error());
                }
                g.distribution = *distribution;
            } else if (in.has("points")) {
                const spatial::PointCloud& cloud = cloudOf(in.single("points"));
                if (cloud.count() > kMaxGrammarPlacements) {
                    ctx.output->warnings.push_back(
                        fmt::format("node '{}': {} points exceed the {} placement limit; the rest is dropped",
                                    node.name, cloud.count(), kMaxGrammarPlacements));
                }
                g.distribution.kind = scene::DistributionKind::Grammar;
                // The count is the expansion size (at least 1: Distribution::validate rejects 0).
                g.distribution.count =
                    std::max(1, static_cast<int>(std::min(cloud.count(), kMaxGrammarPlacements)));
                g.grammar = grammarFromCloud(cloud, kMaxGrammarPlacements);
            } else {
                ctx.output->warnings.push_back(fmt::format(
                    "node '{}': neither 'spec' nor 'points' is linked; using the default distribution",
                    node.name));
            }
            g.distributionTransform = transformFromParams(p, "position", "rotation", "scale");
            g.variation.seed = p.u32("seed");
            g.variation.randomPosition = p.v3("randomPosition");
            g.variation.randomRotation = p.v3("randomRotation");
            g.variation.randomScale = p.v3("randomScale");
            g.variation.randomUniformScale = p.f("randomUniformScale");
            g.materialVariation.hueShift = p.f("hueShift");
            g.materialVariation.hueGradient = p.f("hueGradient");
            g.materialVariation.valueRandom = p.f("valueRandom");
            g.materialVariation.emissiveRandom = p.f("emissiveRandom");
            g.materialVariation.emissiveGradient = p.f("emissiveGradient");
            g.extraLane = p.s("extraLane");
            g.emissiveField = p.s("emissiveField");
            g.emissiveFieldAmount = p.f("emissiveFieldAmount");

            for (const Value& value : in.multi("deformers")) {
                const std::string text = detail::asString(value);
                if (text.empty()) {
                    continue;
                }
                json parsed = json::parse(text, nullptr, false);
                if (parsed.is_discarded()) {
                    return fail("a deformer input is not valid JSON");
                }
                auto deformer = deformerFromJson(parsed);
                if (!deformer) {
                    return std::unexpected(deformer.error());
                }
                if (g.deformers.size() >= static_cast<std::size_t>(scene::kMaxDeformers)) {
                    ctx.output->warnings.push_back(
                        fmt::format("node '{}': more than {} deformers; the rest is dropped", node.name,
                                    scene::kMaxDeformers));
                    break;
                }
                g.deformers.push_back(std::move(*deformer));
            }

            if (const MaterialValue* material = std::get_if<MaterialValue>(&in.single("material"));
                material != nullptr) {
                g.material = material->material;
                if (material->program) {
                    emitProgram(*ctx.output, *material->program);
                }
            }

            // Live GPU effectors: the multi `fields` input names them, the `effectors` parameter
            // carries op/blend/strength per field (entry i belongs to linked field i).
            std::vector<std::string> fieldNames;
            for (const Value& value : in.multi("fields")) {
                if (const spatial::FieldSpec* field = std::get_if<spatial::FieldSpec>(&value);
                    field != nullptr) {
                    fieldNames.push_back(field->name);
                    emitField(*ctx.output, *field);
                }
            }
            if (!fieldNames.empty()) {
                json effectors = json::parse(p.s("effectors"), nullptr, false);
                if (effectors.is_discarded() || !effectors.is_array()) {
                    effectors = json::array();
                }
                for (std::size_t i = 0; i < fieldNames.size(); ++i) {
                    if (g.effectors.size() >= static_cast<std::size_t>(scene::kMaxEffectors)) {
                        ctx.output->warnings.push_back(
                            fmt::format("node '{}': more than {} effectors; the rest is dropped", node.name,
                                        scene::kMaxEffectors));
                        break;
                    }
                    spatial::Effector effector;
                    effector.field = fieldNames[i];
                    if (i < effectors.size() && effectors.at(i).is_object()) {
                        const json& entry = effectors.at(i);
                        if (const auto op =
                                spatial::effectorOpFromName(stringOf(entry, "op", "positionOffset"));
                            op) {
                            effector.op = *op;
                        }
                        if (const auto blend =
                                spatial::effectorBlendFromName(stringOf(entry, "blend", "add"));
                            blend) {
                            effector.blend = *blend;
                        }
                        effector.enabled = boolOf(entry, "enabled", true);
                        effector.strength = floatOf(entry, "strength", 1.0f);
                        effector.weight = floatOf(entry, "weight", 1.0f);
                        effector.axis = vec3Of(entry, "axis", effector.axis);
                        effector.scaleAxis = vec3Of(entry, "scaleAxis", effector.scaleAxis);
                        effector.target = stringOf(entry, "target");
                    }
                    g.effectors.push_back(std::move(effector));
                }
            }

            // Parameter pins: a linked value that is not a signal sets the emitted value.
            if (in.has("emissive") && !drivenBySignal(driven, "emissive")) {
                g.material.emissiveIntensity = in.f("emissive");
            }
            if (in.has("roughness") && !drivenBySignal(driven, "roughness")) {
                g.material.roughness = in.f("roughness");
            }
            if (in.has("metallic") && !drivenBySignal(driven, "metallic")) {
                g.material.metallic = in.f("metallic");
            }
            if (in.has("hueShift") && !drivenBySignal(driven, "hueShift")) {
                g.materialVariation.hueShift = in.f("hueShift");
            }
            if (in.has("radius") && !drivenBySignal(driven, "radius")) {
                g.distribution.radius = in.f("radius");
            }
            if (in.has("emissiveFieldAmount") && !drivenBySignal(driven, "emissiveFieldAmount")) {
                g.emissiveFieldAmount = in.f("emissiveFieldAmount");
            }

            if (auto ok = g.validate(); !ok) {
                return std::unexpected(ok.error());
            }
            g.rebuild();
            ctx.output->procedurals.push_back(std::move(g));
            return {};
        });

    add(registry, "output/field", "Field Output", NodeCategory::Output,
        {pin("field", PinType::Field), pin("strength", PinType::Float), pin("frequency", PinType::Float),
         pin("radius", PinType::Float), pin("speed", PinType::Float), pin("amplitude", PinType::Float),
         pin("modulation", PinType::Float, Value{}, true)},
        {}, {ps("name")},
        "Emits the linked field into the scene (keeping its own name unless `name` is set).",
        [](const Node& node, const std::vector<Value>& values, std::vector<Value>&,
           EvalContext& ctx) -> Result<void> {
            const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
            const Inputs in(*info, values);
            const Params p(node);
            const spatial::FieldSpec* linked = std::get_if<spatial::FieldSpec>(&in.single("field"));
            if (linked == nullptr) {
                return fail("no field linked");
            }
            spatial::FieldSpec field = *linked;
            if (const std::string name = p.s("name"); !name.empty()) {
                field.name = name;
            }
            const std::vector<std::string> driven = emitSignalRoutes(node, ctx, "field/" + field.name + "/",
                                                                     {{"strength", "strength"},
                                                                      {"frequency", "frequency"},
                                                                      {"radius", "radius"},
                                                                      {"speed", "speed"},
                                                                      {"amplitude", "amplitude"}});
            if (in.has("strength") && !drivenBySignal(driven, "strength")) {
                field.strength = in.f("strength");
            }
            if (in.has("frequency") && !drivenBySignal(driven, "frequency")) {
                field.frequency = in.f("frequency");
            }
            if (in.has("radius") && !drivenBySignal(driven, "radius")) {
                field.radius = in.f("radius");
            }
            if (in.has("speed") && !drivenBySignal(driven, "speed")) {
                field.speed = in.f("speed");
            }
            if (in.has("amplitude") && !drivenBySignal(driven, "amplitude")) {
                field.amplitude = in.f("amplitude");
            }
            emitField(*ctx.output, std::move(field));
            return {};
        });

    add(registry, "output/spline", "Spline Output", NodeCategory::Output,
        {pin("spline", PinType::Spline), pin("radius", PinType::Float), pin("turns", PinType::Float),
         pin("height", PinType::Float), pin("noiseAmount", PinType::Float),
         pin("modulation", PinType::Float, Value{}, true)},
        {}, {ps("name")}, "Emits the linked spline into the scene.",
        [](const Node& node, const std::vector<Value>& values, std::vector<Value>&,
           EvalContext& ctx) -> Result<void> {
            const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
            const Inputs in(*info, values);
            const Params p(node);
            const spatial::Spline* linked = std::get_if<spatial::Spline>(&in.single("spline"));
            if (linked == nullptr) {
                return fail("no spline linked");
            }
            spatial::Spline spline = *linked;
            if (const std::string name = p.s("name"); !name.empty()) {
                spline.name = name;
            }
            const std::vector<std::string> driven = emitSignalRoutes(node, ctx, "spline/" + spline.name + "/",
                                                                     {{"radius", "radius"},
                                                                      {"turns", "turns"},
                                                                      {"height", "height"},
                                                                      {"noiseAmount", "noiseAmount"}});
            if (in.has("radius") && !drivenBySignal(driven, "radius")) {
                spline.radius = in.f("radius");
            }
            if (in.has("turns") && !drivenBySignal(driven, "turns")) {
                spline.turns = in.f("turns");
            }
            if (in.has("height") && !drivenBySignal(driven, "height")) {
                spline.height = in.f("height");
            }
            if (in.has("noiseAmount") && !drivenBySignal(driven, "noiseAmount")) {
                spline.noiseAmount = in.f("noiseAmount");
            }
            ctx.output->splines.push_back(std::move(spline));
            return {};
        });

    add(registry, "output/sdf", "SDF Output", NodeCategory::Output,
        {pin("sdf", PinType::Sdf), pin("material", PinType::Material), pin("emissive", PinType::Float),
         pin("modulation", PinType::Float, Value{}, true)},
        {},
        {ps("name"), pb("visible", true, false), pv3("position", glm::vec3(0.0f)),
         pv3("rotation", glm::vec3(0.0f)), pv3("scale", glm::vec3(1.0f)),
         pe("renderMode", 0, {"raymarch", "mesh"}), pv3("boundsMin", glm::vec3(-5.0f), true),
         pv3("boundsMax", glm::vec3(5.0f), true), pi("resolution", 48, 8.0f, 128.0f)},
        "Emits a scene::SdfObject from the linked SDF tree.",
        [](const Node& node, const std::vector<Value>& values, std::vector<Value>&,
           EvalContext& ctx) -> Result<void> {
            const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
            const Inputs in(*info, values);
            const Params p(node);
            const SdfValue* tree = std::get_if<SdfValue>(&in.single("sdf"));
            if (tree == nullptr) {
                return fail("no sdf linked");
            }
            scene::SdfObject object;
            const std::string name = p.s("name");
            object.name = name.empty() ? node.name : name;
            object.tree = tree->tree;
            object.visible = p.b("visible");
            object.transform = transformFromParams(p, "position", "rotation", "scale");
            object.renderMode =
                p.label("renderMode") == "mesh" ? scene::SdfRenderMode::Mesh : scene::SdfRenderMode::Raymarch;
            object.boundsMin = p.v3("boundsMin");
            object.boundsMax = p.v3("boundsMax");
            object.resolution = p.i("resolution");
            const std::vector<std::string> driven =
                emitSignalRoutes(node, ctx, "sdf/" + object.name + "/", {{"emissive", "material/emissive"}});
            if (const MaterialValue* material = std::get_if<MaterialValue>(&in.single("material"));
                material != nullptr) {
                object.material = material->material;
                if (material->program) {
                    emitProgram(*ctx.output, *material->program);
                }
            }
            if (in.has("emissive") && !drivenBySignal(driven, "emissive")) {
                object.material.emissiveIntensity = in.f("emissive");
            }
            if (auto ok = object.tree.validate(); !ok) {
                return std::unexpected(ok.error());
            }
            ctx.output->sdfs.push_back(std::move(object));
            return {};
        });

    add(registry, "output/particles", "Particles Output", NodeCategory::Output,
        {pin("particles", PinType::Particles), pin("spawnRate", PinType::Float),
         pin("emissive", PinType::Float), pin("size", PinType::Float),
         pin("modulation", PinType::Float, Value{}, true)},
        {}, {ps("name")}, "Emits the linked particle system into the scene.",
        [](const Node& node, const std::vector<Value>& values, std::vector<Value>&,
           EvalContext& ctx) -> Result<void> {
            const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
            const Inputs in(*info, values);
            const Params p(node);
            const ParticlesValue* linked = std::get_if<ParticlesValue>(&in.single("particles"));
            if (linked == nullptr) {
                return fail("no particle system linked");
            }
            scene::ParticleSystem system = linked->system;
            const std::string name = p.s("name");
            if (!name.empty()) {
                system.name = name;
            }
            const std::vector<std::string> driven =
                emitSignalRoutes(node, ctx, "particles/" + system.name + "/",
                                 {{"spawnRate", "spawnRate"}, {"emissive", "emissive"}, {"size", "size"}});
            if (in.has("spawnRate") && !drivenBySignal(driven, "spawnRate")) {
                system.spawnRate = in.f("spawnRate");
            }
            if (in.has("emissive") && !drivenBySignal(driven, "emissive")) {
                system.emissive = in.f("emissive");
            }
            if (in.has("size") && !drivenBySignal(driven, "size")) {
                system.sizeStart = in.f("size");
            }
            ctx.output->particles.push_back(std::move(system));
            return {};
        });

    add(registry, "output/material", "Material Output", NodeCategory::Output,
        {pin("material", PinType::Material), pin("modulation", PinType::Float, Value{}, true)}, {},
        {ps("name")}, "Emits the linked material's procedural program into the scene.",
        [](const Node& node, const std::vector<Value>& values, std::vector<Value>&,
           EvalContext& ctx) -> Result<void> {
            const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
            const Inputs in(*info, values);
            const Params p(node);
            const MaterialValue* material = std::get_if<MaterialValue>(&in.single("material"));
            if (material == nullptr) {
                return fail("no material linked");
            }
            if (!material->program) {
                return fail("the linked material has no program (use materials/program)");
            }
            scene::MaterialProgram program = *material->program;
            if (const std::string name = p.s("name"); !name.empty()) {
                program.name = name;
            }
            emitSignalRoutes(node, ctx, "material/" + program.name + "/", {});
            emitProgram(*ctx.output, std::move(program));
            return {};
        });

    add(registry, "output/volume", "Volume Output", NodeCategory::Output,
        {pin("volume", PinType::Volume), pin("field", PinType::Field),
         pin("modulation", PinType::Float, Value{}, true)},
        {}, {ps("name"), pf("density", 1.0f, 0.0f, 5.0f)},
        "Emits a volume's density field (GraphOutput has no volume list yet; the field is emitted so the "
        "renderer can bind it).",
        [](const Node& node, const std::vector<Value>& values, std::vector<Value>&,
           EvalContext& ctx) -> Result<void> {
            const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
            const Inputs in(*info, values);
            const Params p(node);
            std::string fieldName;
            if (const VolumeValue* volume = std::get_if<VolumeValue>(&in.single("volume"));
                volume != nullptr) {
                fieldName = volume->densityField;
            }
            if (const spatial::FieldSpec* field = std::get_if<spatial::FieldSpec>(&in.single("field"));
                field != nullptr) {
                spatial::FieldSpec copy = *field;
                copy.strength *= p.f("density");
                fieldName = copy.name;
                emitField(*ctx.output, std::move(copy));
            }
            if (fieldName.empty()) {
                return fail("no volume or field linked");
            }
            emitSignalRoutes(node, ctx, "field/" + fieldName + "/", {});
            return {};
        });
}

// ---- subgraphs
// ---------------------------------------------------------------------------------------------

void registerSubgraphs(NodeRegistry& registry) {
    add(registry, "subgraph/instance", "Subgraph", NodeCategory::Subgraph, {}, {}, {},
        "Instantiates a graph file: its exposed parameters are this node's params, its emissions are "
        "forwarded with names prefixed \"<instance>_\".",
        [](const Node& node, const std::vector<Value>&, std::vector<Value>&,
           EvalContext& ctx) -> Result<void> {
            if (ctx.depth + 1 > Graph::kMaxSubgraphDepth) {
                return {}; // recursion bottoms out silently at the depth limit
            }
            if (node.subgraph.empty()) {
                return fail("no graph file set");
            }
            const std::filesystem::path path = detail::resolveGraphFile(node.subgraph);
            if (path.empty()) {
                return fail("graph file '{}' not found", node.subgraph);
            }
            const std::filesystem::path previousDirectory = detail::graphSourceDirectory();
            auto child = Graph::loadFile(path);
            if (!child) {
                detail::setGraphSourceDirectory(previousDirectory);
                return std::unexpected(child.error());
            }
            for (const ExposedParam& exposed : child->exposed) {
                const auto it = node.params.find(exposed.name);
                if (it == node.params.end()) {
                    continue;
                }
                if (Node* inner = child->findNode(exposed.node); inner != nullptr) {
                    inner->params[exposed.param] = *it;
                }
            }
            child->markAllDirty();
            GraphOutput childOutput;
            detail::setGraphSourceDirectory(path.parent_path());
            const auto result = child->evaluate(childOutput, ctx.time, ctx.depth + 1);
            detail::setGraphSourceDirectory(previousDirectory);
            if (!result) {
                return std::unexpected(result.error());
            }
            detail::prefixOutputNames(childOutput, node.name + "_");
            GraphOutput& out = *ctx.output;
            for (scene::ProceduralGeometry& value : childOutput.procedurals) {
                out.procedurals.push_back(std::move(value));
            }
            for (spatial::FieldSpec& value : childOutput.fields) {
                emitField(out, std::move(value));
            }
            for (spatial::Spline& value : childOutput.splines) {
                out.splines.push_back(std::move(value));
            }
            for (scene::SdfObject& value : childOutput.sdfs) {
                out.sdfs.push_back(std::move(value));
            }
            for (scene::MaterialProgram& value : childOutput.materialPrograms) {
                emitProgram(out, std::move(value));
            }
            for (scene::ParticleSystem& value : childOutput.particles) {
                out.particles.push_back(std::move(value));
            }
            for (params::ModRoute& value : childOutput.routes) {
                out.routes.push_back(std::move(value));
            }
            for (std::string& value : childOutput.warnings) {
                out.warnings.push_back(std::move(value));
            }
            return {};
        });
}

} // namespace

void registerBuiltinNodes(NodeRegistry& registry) {
    registerGenerators(registry);
    registerDistributions(registry);
    registerPointOps(registry);
    registerAttributes(registry);
    registerFields(registry);
    registerEffectors(registry);
    registerDeformers(registry);
    registerSdf(registry);
    registerMaterials(registry);
    registerParticlesAndVolumes(registry);
    registerAudioAndTime(registry);
    registerMath(registry);
    registerLogic(registry);
    registerOutputs(registry);
    registerSubgraphs(registry);
}

} // namespace avgen::graph
