#pragma once

// Procedural graph (ADR-028): an authoring layer whose evaluation EMITS the flat scene data the
// engine already renders (procedural objects, fields, splines, SDF objects, material programs,
// particle systems, routes). The runtime never sees the graph; it is evaluated when it changes
// (dirty nodes only, keyed by structural hashes) and re-emits into a `GraphOutput` that the
// composition installs as nodes. Typed pins; links between different types are refused.
//
// Node model: every node has a `type` (from the registry), named input pins and output pins with
// a PinType, and a parameter block (`params`, a JSON object of values that are also exposed as
// engine parameters "graph/<graphName>/<node>/<param>" for the non-structural ones). Node types
// are described by `NodeTypeInfo` (category, pins, parameter descriptors) and implemented by a
// `NodeEvaluator` (a pure function of the resolved inputs + params → outputs). Subgraphs are
// graph files instantiated as a node; their exposed parameters become the instance's params.

#include "core/error.hpp"
#include "params/modulation.hpp"
#include "scene/material_program.hpp"
#include "scene/particles.hpp"
#include "scene/procedural.hpp"
#include "scene/sdf_object.hpp"
#include "spatial/field.hpp"
#include "spatial/point_cloud.hpp"
#include "spatial/spline.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace avgen::graph {

enum class PinType : std::uint8_t {
    Float, Vec2, Vec3, Color, Transform, PointCloud, Spline, Field, Mesh, Sdf, Material, Particles, Volume, Int, Bool,
    String, Any
};
[[nodiscard]] const char* pinTypeName(PinType type);
[[nodiscard]] std::optional<PinType> pinTypeFromName(std::string_view name);
[[nodiscard]] bool pinTypesCompatible(PinType from, PinType to); // equal, or Any on either side, Int->Float, Float->Vec3 (broadcast)

// Values that travel along links.
struct MeshValue {
    scene::SourceSpec source;            // a primitive or a procedural reference (kept symbolic)
    scene::Transform transform;
};
struct MaterialValue {
    scene::Material material;
    std::optional<scene::MaterialProgram> program;
};
struct SdfValue {
    spatial::SdfTree tree;
};
struct ParticlesValue {
    scene::ParticleSystem system;
};
struct VolumeValue {
    std::string densityField;            // name of a scalar field (ADR-032)
    float density = 1.0f;
};
using Value = std::variant<std::monostate, float, glm::vec2, glm::vec3, glm::vec4, scene::Transform, spatial::PointCloud,
                           spatial::Spline, spatial::FieldSpec, MeshValue, SdfValue, MaterialValue, ParticlesValue,
                           VolumeValue, int, bool, std::string>;
[[nodiscard]] PinType valueType(const Value& v);

struct PinInfo {
    std::string name;
    PinType type = PinType::Float;
    Value defaultValue;                  // used when the input is unlinked and not given by params
    bool multi = false;                  // input accepts several links (values collected in link order)
};
struct ParamInfo {
    std::string name;
    PinType type = PinType::Float;       // Float/Int/Bool/Vec2/Vec3/Color/String
    Value defaultValue;
    float min = -1e9f, max = 1e9f, softMin = 0.0f, softMax = 1.0f;
    bool structural = false;             // changes re-evaluate (counts, kinds); else per-frame parameters
    std::vector<std::string> choices;    // enum-like Int params (labels)
};
enum class NodeCategory : std::uint8_t {
    Generators, Distributions, Spatial, Points, Attributes, Fields, Effectors, Deformers, Sdf, Materials, Particles,
    Volumes, Audio, Time, Math, Logic, Output, Subgraph
};
[[nodiscard]] const char* nodeCategoryName(NodeCategory c);

struct NodeTypeInfo {
    std::string type;                    // "generators/primitive", "fields/radial", "output/procedural", …
    std::string label;
    NodeCategory category = NodeCategory::Generators;
    std::vector<PinInfo> inputs;
    std::vector<PinInfo> outputs;
    std::vector<ParamInfo> params;
    std::string description;
};

struct Node {
    std::string name;                    // unique in the graph
    std::string type;
    glm::vec2 position{0.0f};            // editor placement
    nlohmann::json params = nlohmann::json::object(); // parameter values by name
    std::string subgraph;                // Subgraph nodes: file path / library name
    bool enabled = true;
    // Runtime
    std::uint64_t lastHash = 0;
    std::vector<Value> outputs;          // by output pin index
    bool dirty = true;
};
struct Link {
    std::string fromNode;
    std::string fromPin;
    std::string toNode;
    std::string toPin;
};

// What an evaluation emits. Names are the emitting node's name (unique) unless the node sets one.
struct GraphOutput {
    std::vector<scene::ProceduralGeometry> procedurals;
    std::vector<spatial::FieldSpec> fields;
    std::vector<spatial::Spline> splines;
    std::vector<scene::SdfObject> sdfs;
    std::vector<scene::MaterialProgram> materialPrograms;
    std::vector<scene::ParticleSystem> particles;
    std::vector<params::ModRoute> routes;   // e.g. audio inputs wired to emitted parameters
    std::vector<std::string> warnings;
    void clear();
};

// Evaluation context (per graph evaluation).
struct EvalContext {
    const struct Graph* graph = nullptr;
    std::string graphName;               // parameter prefix "graph/<graphName>/"
    double time = 0.0;
    int depth = 0;                       // subgraph nesting
    GraphOutput* output = nullptr;
};
using NodeEvaluator = std::function<Result<void>(const Node& node, const std::vector<Value>& inputs,
                                                 std::vector<Value>& outputs, EvalContext& ctx)>;

class NodeRegistry {
public:
    static NodeRegistry& instance();     // the built-in types are registered on first use
    void add(NodeTypeInfo info, NodeEvaluator evaluator);
    [[nodiscard]] const NodeTypeInfo* find(std::string_view type) const;
    [[nodiscard]] const NodeEvaluator* evaluator(std::string_view type) const;
    [[nodiscard]] std::vector<const NodeTypeInfo*> types() const;     // sorted by category, label
    [[nodiscard]] std::vector<const NodeTypeInfo*> byCategory(NodeCategory c) const;

private:
    std::map<std::string, NodeTypeInfo, std::less<>> infos_;
    std::map<std::string, NodeEvaluator, std::less<>> evaluators_;
};
void registerBuiltinNodes(NodeRegistry& registry); // idempotent

struct ExposedParam {                    // subgraph interface
    std::string name;                    // exposed name
    std::string node;                    // inner node
    std::string param;                   // inner param
};

struct Graph {
    std::string name = "graph";
    std::vector<Node> nodes;
    std::vector<Link> links;
    std::vector<ExposedParam> exposed;   // when used as a subgraph
    std::string description;

    [[nodiscard]] Node* findNode(std::string_view name);
    [[nodiscard]] const Node* findNode(std::string_view name) const;
    Result<Node*> addNode(std::string type, std::string name = {}, glm::vec2 position = {});
    bool removeNode(std::string_view name);              // removes its links
    // Refuses unknown nodes/pins, type mismatches and cycles.
    Result<void> connect(std::string_view fromNode, std::string_view fromPin, std::string_view toNode, std::string_view toPin);
    bool disconnect(std::string_view toNode, std::string_view toPin);
    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] std::vector<const Node*> topologicalOrder() const; // empty on cycles
    [[nodiscard]] bool wouldCycle(std::string_view fromNode, std::string_view toNode) const;

    // Structural hash of a node = type + structural params + hashes of its upstream nodes; a node
    // re-evaluates when it changed. `evaluate` runs dirty nodes in topological order and rebuilds
    // `output` from every Output node (emission is always complete, evaluation is incremental).
    [[nodiscard]] std::uint64_t nodeHash(const Node& node) const;
    Result<void> evaluate(GraphOutput& output, double time = 0.0, int depth = 0);
    void markAllDirty();
    [[nodiscard]] std::size_t dirtyCount() const;

    [[nodiscard]] nlohmann::json toJson() const;
    static Result<Graph> fromJson(const nlohmann::json& j);
    [[nodiscard]] Result<void> saveFile(const std::filesystem::path& path) const;
    static Result<Graph> loadFile(const std::filesystem::path& path);
    static constexpr const char* kFormatName = "avgen-graph";
    static constexpr int kFormatVersion = 1;
    static constexpr int kMaxSubgraphDepth = 4;
};

// Library of reusable graphs (subgraphs and presets): JSON files under one or more directories,
// with metadata (name, category, description, thumbnail path when generated).
struct LibraryEntry {
    std::string name;
    std::string category;
    std::string description;
    std::filesystem::path path;
    std::filesystem::path thumbnail;     // may be empty
};
[[nodiscard]] std::vector<LibraryEntry> scanGraphLibrary(const std::vector<std::filesystem::path>& dirs);

} // namespace avgen::graph
