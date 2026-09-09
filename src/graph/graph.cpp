// Procedural graph (ADR-028): the graph data structure, its typed pins, structural hashing and
// incremental evaluation, the JSON format and the graph library scanner. The node types
// themselves live in graph/builtin_nodes.cpp.
//
// Conventions chosen here (the header fixes the shape; these are the remaining choices):
//
// * Node names are unique within a graph and are derived from the leaf of the type ("radial" for
//   "distributions/radial"), suffixed with a number when taken ("radial2", "radial3", ...). They
//   never contain '.' or '/' (the link format "node.pin" and parameter paths depend on it);
//   offending characters are replaced with '_'.
// * addNode() fills the node's params with the type's declared defaults, so a saved graph is
//   explicit and the editor has values to show. It returns a pointer into `nodes`, which a later
//   addNode()/removeNode() invalidates: set a new node up before adding the next one, or address
//   nodes by name with findNode().
// * connect() refuses unknown nodes/pins, incompatible types and cycles. A link into a
//   non-multi input pin replaces whatever was linked there; a multi pin keeps every link and
//   passes the values in link order.
// * The value a link carries is converted to the destination pin's type when the compatibility
//   rule implies a conversion (Int -> Float, Float -> Vec3 broadcast); Any passes values through
//   unchanged.
// * Evaluator inputs are flattened in input-pin order: a single pin contributes exactly one
//   value (the linked value, or the pin's default when unlinked), a multi pin contributes its
//   linked values followed by one std::monostate terminator (graph::detail::Inputs decodes it).
// * A node's structural hash covers its type, name, enabled flag, subgraph reference, the whole
//   parameter block and the hashes of its upstream nodes in link order. The name is part of it
//   because emitted objects are named after their node.
// * A node re-evaluates when it is dirty or when its hash changed since the last evaluation;
//   because upstream hashes are folded in, a change re-runs the node and everything downstream.
//   Output nodes always run: evaluation is incremental, emission is always complete.
// * A disabled node does not run; its outputs are its pins' defaults, so downstream nodes see
//   empty geometry instead of stale data.
// * Evaluator failures are collected in GraphOutput::warnings ("node 'x' (type): message") with
//   the node's outputs reset to their defaults; evaluation continues with the rest of the graph.
// * Time-dependent nodes (time/*, fields/sample, effectors/effector) are evaluated at the time
//   passed to evaluate(); the graph is not re-evaluated per frame, so call markAllDirty() before
//   evaluating at a new time when the cached values must follow it.

#include "graph/graph.hpp"

#include "core/log.hpp"
#include "graph/detail.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <deque>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace avgen::graph {
namespace {

using nlohmann::json;

struct PinTypeName {
    PinType type;
    const char* name;
};
constexpr std::array<PinTypeName, 17> kPinTypeNames{{
    {PinType::Float, "float"},
    {PinType::Vec2, "vec2"},
    {PinType::Vec3, "vec3"},
    {PinType::Color, "color"},
    {PinType::Transform, "transform"},
    {PinType::PointCloud, "pointCloud"},
    {PinType::Spline, "spline"},
    {PinType::Field, "field"},
    {PinType::Mesh, "mesh"},
    {PinType::Sdf, "sdf"},
    {PinType::Material, "material"},
    {PinType::Particles, "particles"},
    {PinType::Volume, "volume"},
    {PinType::Int, "int"},
    {PinType::Bool, "bool"},
    {PinType::String, "string"},
    {PinType::Any, "any"},
}};

constexpr std::array<const char*, 18> kCategoryNames{
    "generators", "distributions", "spatial", "points",    "attributes", "fields",
    "effectors",  "deformers",     "sdf",     "materials", "particles",  "volumes",
    "audio",      "time",          "math",    "logic",     "output",     "subgraph"};

// Replaces the characters the link format and parameter paths cannot carry.
std::string sanitiseName(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (const char c : name) {
        out.push_back((c == '.' || c == '/' || c == ' ') ? '_' : c);
    }
    return out;
}

const NodeTypeInfo* typeOf(const Node& node) {
    return NodeRegistry::instance().find(node.type);
}

const PinInfo* findPin(const std::vector<PinInfo>& pins, std::string_view name) {
    for (const PinInfo& pin : pins) {
        if (pin.name == name) {
            return &pin;
        }
    }
    return nullptr;
}

int pinIndex(const std::vector<PinInfo>& pins, std::string_view name) {
    for (std::size_t i = 0; i < pins.size(); ++i) {
        if (pins[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// The value carried by a link, converted to the destination pin's type where the compatibility
// rule implies a conversion.
Value convertValue(const Value& value, PinType target) {
    switch (target) {
    case PinType::Float:
        if (const int* i = std::get_if<int>(&value)) {
            return static_cast<float>(*i);
        }
        return value;
    case PinType::Vec3:
        if (const float* f = std::get_if<float>(&value)) {
            return glm::vec3(*f);
        }
        if (const int* i = std::get_if<int>(&value)) {
            return glm::vec3(static_cast<float>(*i));
        }
        return value;
    default:
        return value;
    }
}

} // namespace

const char* pinTypeName(PinType type) {
    for (const PinTypeName& entry : kPinTypeNames) {
        if (entry.type == type) {
            return entry.name;
        }
    }
    return "any";
}

std::optional<PinType> pinTypeFromName(std::string_view name) {
    for (const PinTypeName& entry : kPinTypeNames) {
        if (name == entry.name) {
            return entry.type;
        }
    }
    return std::nullopt;
}

bool pinTypesCompatible(PinType from, PinType to) {
    if (from == to || from == PinType::Any || to == PinType::Any) {
        return true;
    }
    if (from == PinType::Int && to == PinType::Float) {
        return true;
    }
    if (from == PinType::Float && to == PinType::Vec3) {
        return true;
    }
    return false;
}

PinType valueType(const Value& v) {
    return std::visit(
        [](const auto& held) {
            using T = std::decay_t<decltype(held)>;
            if constexpr (std::is_same_v<T, float>) {
                return PinType::Float;
            } else if constexpr (std::is_same_v<T, glm::vec2>) {
                return PinType::Vec2;
            } else if constexpr (std::is_same_v<T, glm::vec3>) {
                return PinType::Vec3;
            } else if constexpr (std::is_same_v<T, glm::vec4>) {
                return PinType::Color;
            } else if constexpr (std::is_same_v<T, scene::Transform>) {
                return PinType::Transform;
            } else if constexpr (std::is_same_v<T, spatial::PointCloud>) {
                return PinType::PointCloud;
            } else if constexpr (std::is_same_v<T, spatial::Spline>) {
                return PinType::Spline;
            } else if constexpr (std::is_same_v<T, spatial::FieldSpec>) {
                return PinType::Field;
            } else if constexpr (std::is_same_v<T, MeshValue>) {
                return PinType::Mesh;
            } else if constexpr (std::is_same_v<T, SdfValue>) {
                return PinType::Sdf;
            } else if constexpr (std::is_same_v<T, MaterialValue>) {
                return PinType::Material;
            } else if constexpr (std::is_same_v<T, ParticlesValue>) {
                return PinType::Particles;
            } else if constexpr (std::is_same_v<T, VolumeValue>) {
                return PinType::Volume;
            } else if constexpr (std::is_same_v<T, int>) {
                return PinType::Int;
            } else if constexpr (std::is_same_v<T, bool>) {
                return PinType::Bool;
            } else if constexpr (std::is_same_v<T, std::string>) {
                return PinType::String;
            } else {
                return PinType::Any;
            }
        },
        v);
}

const char* nodeCategoryName(NodeCategory c) {
    const auto index = static_cast<std::size_t>(c);
    return index < kCategoryNames.size() ? kCategoryNames[index] : "generators";
}

// ================================================================================================
// Registry
// ================================================================================================

NodeRegistry& NodeRegistry::instance() {
    static NodeRegistry registry;
    static const bool once = [] {
        registerBuiltinNodes(registry);
        return true;
    }();
    (void)once;
    return registry;
}

void NodeRegistry::add(NodeTypeInfo info, NodeEvaluator evaluator) {
    const std::string type = info.type;
    infos_.insert_or_assign(type, std::move(info));
    evaluators_.insert_or_assign(type, std::move(evaluator));
}

const NodeTypeInfo* NodeRegistry::find(std::string_view type) const {
    const auto it = infos_.find(type);
    return it == infos_.end() ? nullptr : &it->second;
}

const NodeEvaluator* NodeRegistry::evaluator(std::string_view type) const {
    const auto it = evaluators_.find(type);
    return it == evaluators_.end() ? nullptr : &it->second;
}

std::vector<const NodeTypeInfo*> NodeRegistry::types() const {
    std::vector<const NodeTypeInfo*> out;
    out.reserve(infos_.size());
    for (const auto& [type, info] : infos_) {
        out.push_back(&info);
    }
    std::sort(out.begin(), out.end(), [](const NodeTypeInfo* a, const NodeTypeInfo* b) {
        if (a->category != b->category) {
            return a->category < b->category;
        }
        if (a->label != b->label) {
            return a->label < b->label;
        }
        return a->type < b->type;
    });
    return out;
}

std::vector<const NodeTypeInfo*> NodeRegistry::byCategory(NodeCategory c) const {
    std::vector<const NodeTypeInfo*> out;
    for (const NodeTypeInfo* info : types()) {
        if (info->category == c) {
            out.push_back(info);
        }
    }
    return out;
}

void GraphOutput::clear() {
    procedurals.clear();
    fields.clear();
    splines.clear();
    sdfs.clear();
    materialPrograms.clear();
    particles.clear();
    routes.clear();
    warnings.clear();
}

// ================================================================================================
// Graph structure
// ================================================================================================

Node* Graph::findNode(std::string_view nodeName) {
    for (Node& node : nodes) {
        if (node.name == nodeName) {
            return &node;
        }
    }
    return nullptr;
}

const Node* Graph::findNode(std::string_view nodeName) const {
    for (const Node& node : nodes) {
        if (node.name == nodeName) {
            return &node;
        }
    }
    return nullptr;
}

Result<Node*> Graph::addNode(std::string type, std::string nodeName, glm::vec2 position) {
    const NodeTypeInfo* info = NodeRegistry::instance().find(type);
    if (info == nullptr) {
        return fail("unknown node type '{}'", type);
    }
    std::string base = sanitiseName(nodeName);
    if (base.empty()) {
        const std::size_t slash = type.rfind('/');
        base = sanitiseName(slash == std::string::npos ? type : type.substr(slash + 1));
    }
    if (base.empty()) {
        base = "node";
    }
    std::string unique = base;
    for (int suffix = 2; findNode(unique) != nullptr; ++suffix) {
        unique = base + std::to_string(suffix);
    }

    Node node;
    node.name = std::move(unique);
    node.type = std::move(type);
    node.position = position;
    for (const ParamInfo& param : info->params) {
        node.params[param.name] = detail::valueToJson(param.defaultValue);
    }
    node.dirty = true;
    nodes.push_back(std::move(node));
    return &nodes.back();
}

bool Graph::removeNode(std::string_view nodeName) {
    const auto it =
        std::find_if(nodes.begin(), nodes.end(), [&](const Node& n) { return n.name == nodeName; });
    if (it == nodes.end()) {
        return false;
    }
    nodes.erase(it);
    std::erase_if(links,
                  [&](const Link& link) { return link.fromNode == nodeName || link.toNode == nodeName; });
    markAllDirty();
    return true;
}

bool Graph::wouldCycle(std::string_view fromNode, std::string_view toNode) const {
    if (fromNode == toNode) {
        return true;
    }
    // A link fromNode -> toNode closes a cycle when fromNode is already reachable from toNode.
    std::vector<std::string> stack{std::string(toNode)};
    std::vector<std::string> seen;
    while (!stack.empty()) {
        const std::string current = std::move(stack.back());
        stack.pop_back();
        if (std::find(seen.begin(), seen.end(), current) != seen.end()) {
            continue;
        }
        seen.push_back(current);
        for (const Link& link : links) {
            if (link.fromNode != current) {
                continue;
            }
            if (link.toNode == fromNode) {
                return true;
            }
            stack.push_back(link.toNode);
        }
    }
    return false;
}

Result<void> Graph::connect(std::string_view fromNode, std::string_view fromPin, std::string_view toNode,
                            std::string_view toPin) {
    const Node* source = findNode(fromNode);
    if (source == nullptr) {
        return fail("unknown node '{}'", fromNode);
    }
    Node* target = findNode(toNode);
    if (target == nullptr) {
        return fail("unknown node '{}'", toNode);
    }
    const NodeTypeInfo* sourceInfo = typeOf(*source);
    const NodeTypeInfo* targetInfo = typeOf(*target);
    if (sourceInfo == nullptr) {
        return fail("node '{}' has unknown type '{}'", source->name, source->type);
    }
    if (targetInfo == nullptr) {
        return fail("node '{}' has unknown type '{}'", target->name, target->type);
    }
    const PinInfo* out = findPin(sourceInfo->outputs, fromPin);
    if (out == nullptr) {
        return fail("node '{}' ({}) has no output pin '{}'", source->name, source->type, fromPin);
    }
    const PinInfo* in = findPin(targetInfo->inputs, toPin);
    if (in == nullptr) {
        return fail("node '{}' ({}) has no input pin '{}'", target->name, target->type, toPin);
    }
    if (!pinTypesCompatible(out->type, in->type)) {
        return fail("cannot link {}.{} ({}) to {}.{} ({}): incompatible types", source->name, out->name,
                    pinTypeName(out->type), target->name, in->name, pinTypeName(in->type));
    }
    if (wouldCycle(fromNode, toNode)) {
        return fail("linking {}.{} to {}.{} would create a cycle", source->name, out->name, target->name,
                    in->name);
    }
    for (const Link& link : links) {
        if (link.fromNode == fromNode && link.fromPin == fromPin && link.toNode == toNode &&
            link.toPin == toPin) {
            return {}; // already linked
        }
    }
    if (!in->multi) {
        std::erase_if(links, [&](const Link& link) { return link.toNode == toNode && link.toPin == toPin; });
    }
    links.push_back(
        Link{std::string(fromNode), std::string(fromPin), std::string(toNode), std::string(toPin)});
    target->dirty = true;
    return {};
}

bool Graph::disconnect(std::string_view toNode, std::string_view toPin) {
    const std::size_t removed =
        std::erase_if(links, [&](const Link& link) { return link.toNode == toNode && link.toPin == toPin; });
    if (removed == 0) {
        return false;
    }
    if (Node* target = findNode(toNode); target != nullptr) {
        target->dirty = true;
    }
    return true;
}

Result<void> Graph::validate() const {
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const Node& node = nodes[i];
        if (node.name.empty()) {
            return fail("node {} has an empty name", i + 1);
        }
        if (node.name.find('.') != std::string::npos) {
            return fail("node name '{}' must not contain '.'", node.name);
        }
        for (std::size_t j = i + 1; j < nodes.size(); ++j) {
            if (nodes[j].name == node.name) {
                return fail("duplicate node name '{}'", node.name);
            }
        }
        const NodeTypeInfo* info = typeOf(node);
        if (info == nullptr) {
            return fail("node '{}' has unknown type '{}'", node.name, node.type);
        }
        if (info->category == NodeCategory::Subgraph && node.subgraph.empty()) {
            return fail("subgraph node '{}' has no graph file", node.name);
        }
    }
    for (const Link& link : links) {
        const Node* source = findNode(link.fromNode);
        const Node* target = findNode(link.toNode);
        if (source == nullptr) {
            return fail("link from unknown node '{}'", link.fromNode);
        }
        if (target == nullptr) {
            return fail("link to unknown node '{}'", link.toNode);
        }
        const NodeTypeInfo* sourceInfo = typeOf(*source);
        const NodeTypeInfo* targetInfo = typeOf(*target);
        const PinInfo* out = sourceInfo != nullptr ? findPin(sourceInfo->outputs, link.fromPin) : nullptr;
        const PinInfo* in = targetInfo != nullptr ? findPin(targetInfo->inputs, link.toPin) : nullptr;
        if (out == nullptr) {
            return fail("node '{}' has no output pin '{}'", link.fromNode, link.fromPin);
        }
        if (in == nullptr) {
            return fail("node '{}' has no input pin '{}'", link.toNode, link.toPin);
        }
        if (!pinTypesCompatible(out->type, in->type)) {
            return fail("link {}.{} -> {}.{} is a type mismatch ({} to {})", link.fromNode, link.fromPin,
                        link.toNode, link.toPin, pinTypeName(out->type), pinTypeName(in->type));
        }
    }
    for (const ExposedParam& entry : exposed) {
        const Node* node = findNode(entry.node);
        if (node == nullptr) {
            return fail("exposed parameter '{}' refers to unknown node '{}'", entry.name, entry.node);
        }
        const NodeTypeInfo* info = typeOf(*node);
        if (info != nullptr) {
            const auto it = std::find_if(info->params.begin(), info->params.end(),
                                         [&](const ParamInfo& p) { return p.name == entry.param; });
            if (it == info->params.end()) {
                return fail("exposed parameter '{}': node '{}' ({}) has no parameter '{}'", entry.name,
                            node->name, node->type, entry.param);
            }
        }
    }
    if (!nodes.empty() && topologicalOrder().empty()) {
        return fail("graph '{}' contains a cycle", name);
    }
    return {};
}

std::vector<const Node*> Graph::topologicalOrder() const {
    const std::size_t count = nodes.size();
    std::unordered_map<std::string, std::size_t> indexByName;
    indexByName.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        indexByName.emplace(nodes[i].name, i);
    }
    std::vector<int> indegree(count, 0);
    std::vector<std::vector<std::size_t>> outgoing(count);
    for (const Link& link : links) {
        const auto from = indexByName.find(link.fromNode);
        const auto to = indexByName.find(link.toNode);
        if (from == indexByName.end() || to == indexByName.end()) {
            continue; // dangling links are reported by validate()
        }
        outgoing[from->second].push_back(to->second);
        ++indegree[to->second];
    }
    std::deque<std::size_t> ready;
    for (std::size_t i = 0; i < count; ++i) {
        if (indegree[i] == 0) {
            ready.push_back(i);
        }
    }
    std::vector<const Node*> order;
    order.reserve(count);
    while (!ready.empty()) {
        const std::size_t index = ready.front();
        ready.pop_front();
        order.push_back(&nodes[index]);
        for (const std::size_t next : outgoing[index]) {
            if (--indegree[next] == 0) {
                ready.push_back(next);
            }
        }
    }
    if (order.size() != count) {
        return {};
    }
    return order;
}

// ================================================================================================
// Hashing and evaluation
// ================================================================================================

namespace {

std::uint64_t hashNodeMemo(const Graph& graph, const Node& node,
                           std::unordered_map<std::string, std::uint64_t>& memo, int depth) {
    if (const auto it = memo.find(node.name); it != memo.end()) {
        return it->second;
    }
    if (depth > 64) {
        return 0; // cycle guard; validate() reports the cycle
    }
    detail::Fnv f;
    f.str(node.type);
    f.str(node.name);
    f.boolean(node.enabled);
    f.str(node.subgraph);
    f.str(node.params.dump());
    const NodeTypeInfo* info = NodeRegistry::instance().find(node.type);
    if (info != nullptr) {
        for (const PinInfo& pin : info->inputs) {
            f.str(pin.name);
            for (const Link& link : graph.links) {
                if (link.toNode != node.name || link.toPin != pin.name) {
                    continue;
                }
                const Node* upstream = graph.findNode(link.fromNode);
                if (upstream == nullptr) {
                    f.u64(0);
                    continue;
                }
                f.str(link.fromPin);
                f.u64(hashNodeMemo(graph, *upstream, memo, depth + 1));
            }
        }
    }
    const std::uint64_t value = f.value();
    memo.emplace(node.name, value);
    return value;
}

// Gathers the evaluator inputs of one node (see the file header for the flattened encoding).
std::vector<Value> gatherInputs(const Graph& graph, const Node& node, const NodeTypeInfo& info) {
    std::vector<Value> values;
    for (const PinInfo& pin : info.inputs) {
        std::size_t linked = 0;
        for (const Link& link : graph.links) {
            if (link.toNode != node.name || link.toPin != pin.name) {
                continue;
            }
            const Node* upstream = graph.findNode(link.fromNode);
            Value value;
            if (upstream != nullptr) {
                const NodeTypeInfo* upstreamInfo = NodeRegistry::instance().find(upstream->type);
                if (upstreamInfo != nullptr) {
                    const int index = pinIndex(upstreamInfo->outputs, link.fromPin);
                    if (index >= 0 && static_cast<std::size_t>(index) < upstream->outputs.size()) {
                        value = upstream->outputs[static_cast<std::size_t>(index)];
                    }
                }
            }
            values.push_back(convertValue(value, pin.type));
            ++linked;
            if (!pin.multi) {
                break;
            }
        }
        if (pin.multi) {
            values.emplace_back(std::monostate{});
        } else if (linked == 0) {
            values.push_back(pin.defaultValue);
        }
    }
    return values;
}

void resetOutputs(Node& node, const NodeTypeInfo& info) {
    node.outputs.assign(info.outputs.size(), Value{});
    for (std::size_t i = 0; i < info.outputs.size(); ++i) {
        node.outputs[i] = info.outputs[i].defaultValue;
    }
}

} // namespace

std::uint64_t Graph::nodeHash(const Node& node) const {
    std::unordered_map<std::string, std::uint64_t> memo;
    return hashNodeMemo(*this, node, memo, 0);
}

Result<void> Graph::evaluate(GraphOutput& output, double time, int depth) {
    if (depth > kMaxSubgraphDepth) {
        return fail("subgraph nesting deeper than {} levels", kMaxSubgraphDepth);
    }
    if (auto ok = validate(); !ok) {
        return std::unexpected(ok.error());
    }
    output.clear();

    const std::vector<const Node*> order = topologicalOrder();
    std::vector<std::size_t> indices;
    indices.reserve(order.size());
    for (const Node* node : order) {
        indices.push_back(static_cast<std::size_t>(node - nodes.data()));
    }

    NodeRegistry& registry = NodeRegistry::instance();
    EvalContext ctx;
    ctx.graph = this;
    ctx.graphName = name;
    ctx.time = time;
    ctx.depth = depth;
    ctx.output = &output;

    std::unordered_map<std::string, std::uint64_t> memo;
    for (const std::size_t index : indices) {
        Node& node = nodes[index];
        const NodeTypeInfo* info = registry.find(node.type);
        if (info == nullptr) {
            continue; // validate() already rejected this
        }
        const std::uint64_t hash = hashNodeMemo(*this, node, memo, 0);
        // Output and subgraph nodes emit into the GraphOutput, so they always run: evaluation is
        // incremental, emission is always complete.
        const bool isOutput =
            info->category == NodeCategory::Output || info->category == NodeCategory::Subgraph;
        const bool cached =
            !node.dirty && hash == node.lastHash && node.outputs.size() == info->outputs.size();
        node.lastHash = hash;
        node.dirty = false;
        if (cached && !isOutput) {
            continue;
        }
        if (!node.enabled) {
            resetOutputs(node, *info);
            continue;
        }
        const NodeEvaluator* evaluator = registry.evaluator(node.type);
        if (evaluator == nullptr || !*evaluator) {
            resetOutputs(node, *info);
            output.warnings.push_back(fmt::format("node '{}' ({}): no evaluator", node.name, node.type));
            continue;
        }
        const std::vector<Value> inputs = gatherInputs(*this, node, *info);
        resetOutputs(node, *info);
        if (auto ok = (*evaluator)(node, inputs, node.outputs, ctx); !ok) {
            resetOutputs(node, *info);
            output.warnings.push_back(
                fmt::format("node '{}' ({}): {}", node.name, node.type, ok.error().message));
        }
        if (node.outputs.size() != info->outputs.size()) {
            node.outputs.resize(info->outputs.size());
        }
    }
    return {};
}

void Graph::markAllDirty() {
    for (Node& node : nodes) {
        node.dirty = true;
        node.lastHash = 0;
    }
}

std::size_t Graph::dirtyCount() const {
    return static_cast<std::size_t>(
        std::count_if(nodes.begin(), nodes.end(), [](const Node& n) { return n.dirty; }));
}

// ================================================================================================
// JSON
// ================================================================================================

json Graph::toJson() const {
    json root = json::object();
    root["format"] = kFormatName;
    root["version"] = kFormatVersion;
    root["name"] = name;
    root["description"] = description;
    json nodeArray = json::array();
    for (const Node& node : nodes) {
        json j = json::object();
        j["name"] = node.name;
        j["type"] = node.type;
        j["position"] = json::array({node.position.x, node.position.y});
        j["params"] = node.params;
        j["subgraph"] = node.subgraph;
        j["enabled"] = node.enabled;
        nodeArray.push_back(std::move(j));
    }
    root["nodes"] = std::move(nodeArray);
    json linkArray = json::array();
    for (const Link& link : links) {
        json j = json::object();
        j["from"] = link.fromNode + "." + link.fromPin;
        j["to"] = link.toNode + "." + link.toPin;
        linkArray.push_back(std::move(j));
    }
    root["links"] = std::move(linkArray);
    json exposedArray = json::array();
    for (const ExposedParam& param : exposed) {
        json j = json::object();
        j["name"] = param.name;
        j["node"] = param.node;
        j["param"] = param.param;
        exposedArray.push_back(std::move(j));
    }
    root["exposed"] = std::move(exposedArray);
    return root;
}

Result<Graph> Graph::fromJson(const json& j) {
    if (!j.is_object()) {
        return fail("graph must be a JSON object");
    }
    if (const auto it = j.find("format"); it != j.end()) {
        if (!it->is_string() || it->get<std::string>() != kFormatName) {
            return fail("not an {} file", kFormatName);
        }
    } else {
        return fail("missing 'format' (expected '{}')", kFormatName);
    }
    if (const auto it = j.find("version"); it != j.end()) {
        if (!it->is_number_integer()) {
            return fail("'version' must be an integer");
        }
        if (it->get<int>() > kFormatVersion) {
            return fail("graph version {} is newer than {}", it->get<int>(), kFormatVersion);
        }
    }
    Graph graph;
    if (const auto it = j.find("name"); it != j.end() && it->is_string()) {
        graph.name = it->get<std::string>();
    }
    if (const auto it = j.find("description"); it != j.end() && it->is_string()) {
        graph.description = it->get<std::string>();
    }
    if (const auto it = j.find("nodes"); it != j.end()) {
        if (!it->is_array()) {
            return fail("'nodes' must be an array");
        }
        for (const json& n : *it) {
            if (!n.is_object()) {
                return fail("each node must be an object");
            }
            Node node;
            if (const auto name = n.find("name"); name != n.end() && name->is_string()) {
                node.name = name->get<std::string>();
            } else {
                return fail("a node has no 'name'");
            }
            if (const auto type = n.find("type"); type != n.end() && type->is_string()) {
                node.type = type->get<std::string>();
            } else {
                return fail("node '{}' has no 'type'", node.name);
            }
            if (const auto pos = n.find("position"); pos != n.end()) {
                if (!pos->is_array() || pos->size() != 2) {
                    return fail("node '{}': 'position' must be [x, y]", node.name);
                }
                node.position = glm::vec2(pos->at(0).get<float>(), pos->at(1).get<float>());
            }
            if (const auto params = n.find("params"); params != n.end()) {
                if (!params->is_object()) {
                    return fail("node '{}': 'params' must be an object", node.name);
                }
                node.params = *params;
            }
            if (const auto sub = n.find("subgraph"); sub != n.end() && sub->is_string()) {
                node.subgraph = sub->get<std::string>();
            }
            if (const auto enabled = n.find("enabled"); enabled != n.end()) {
                if (!enabled->is_boolean()) {
                    return fail("node '{}': 'enabled' must be a boolean", node.name);
                }
                node.enabled = enabled->get<bool>();
            }
            node.dirty = true;
            graph.nodes.push_back(std::move(node));
        }
    }
    if (const auto it = j.find("links"); it != j.end()) {
        if (!it->is_array()) {
            return fail("'links' must be an array");
        }
        for (const json& l : *it) {
            if (!l.is_object() || !l.contains("from") || !l.contains("to") || !l.at("from").is_string() ||
                !l.at("to").is_string()) {
                return fail("each link must be {{\"from\": \"node.pin\", \"to\": \"node.pin\"}}");
            }
            const std::string from = l.at("from").get<std::string>();
            const std::string to = l.at("to").get<std::string>();
            const std::size_t fromDot = from.find('.');
            const std::size_t toDot = to.find('.');
            if (fromDot == std::string::npos || toDot == std::string::npos) {
                return fail("link '{}' -> '{}': endpoints must be \"node.pin\"", from, to);
            }
            graph.links.push_back(Link{from.substr(0, fromDot), from.substr(fromDot + 1), to.substr(0, toDot),
                                       to.substr(toDot + 1)});
        }
    }
    if (const auto it = j.find("exposed"); it != j.end()) {
        if (!it->is_array()) {
            return fail("'exposed' must be an array");
        }
        for (const json& e : *it) {
            if (!e.is_object() || !e.contains("name") || !e.contains("node") || !e.contains("param")) {
                return fail("each exposed parameter needs 'name', 'node' and 'param'");
            }
            graph.exposed.push_back(ExposedParam{e.at("name").get<std::string>(),
                                                 e.at("node").get<std::string>(),
                                                 e.at("param").get<std::string>()});
        }
    }
    if (auto ok = graph.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    return graph;
}

Result<void> Graph::saveFile(const std::filesystem::path& path) const {
    std::ofstream out(path);
    if (!out) {
        return fail("cannot write graph file '{}'", path.string());
    }
    out << toJson().dump(2) << '\n';
    if (!out) {
        return fail("failed writing graph file '{}'", path.string());
    }
    return {};
}

Result<Graph> Graph::loadFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return fail("cannot read graph file '{}'", path.string());
    }
    json parsed;
    try {
        in >> parsed;
    } catch (const json::exception& e) {
        return fail("graph file '{}': {}", path.string(), e.what());
    }
    auto graph = fromJson(parsed);
    if (!graph) {
        return fail("graph file '{}': {}", path.string(), graph.error().message);
    }
    detail::setGraphSourceDirectory(path.parent_path());
    return graph;
}

// ================================================================================================
// Library
// ================================================================================================

std::vector<LibraryEntry> scanGraphLibrary(const std::vector<std::filesystem::path>& dirs) {
    detail::setGraphLibraryDirectories(dirs);
    std::vector<LibraryEntry> entries;
    for (const std::filesystem::path& dir : dirs) {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) {
            continue;
        }
        for (const std::filesystem::directory_entry& file : std::filesystem::directory_iterator(dir, ec)) {
            if (!file.is_regular_file()) {
                continue;
            }
            const std::string filename = file.path().filename().string();
            if (!filename.ends_with(".graph.json")) {
                continue;
            }
            LibraryEntry entry;
            entry.path = file.path();
            entry.name = filename.substr(0, filename.size() - std::string(".graph.json").size());
            std::ifstream in(file.path());
            if (in) {
                json parsed;
                try {
                    in >> parsed;
                    if (parsed.is_object()) {
                        if (const auto it = parsed.find("name"); it != parsed.end() && it->is_string()) {
                            entry.name = it->get<std::string>();
                        }
                        if (const auto it = parsed.find("category"); it != parsed.end() && it->is_string()) {
                            entry.category = it->get<std::string>();
                        }
                        if (const auto it = parsed.find("description");
                            it != parsed.end() && it->is_string()) {
                            entry.description = it->get<std::string>();
                        }
                    }
                } catch (const json::exception& e) {
                    log::warn("graph library: '{}': {}", file.path().string(), e.what());
                    continue;
                }
            }
            // Thumbnail: the same stem with .png ("x.graph.json" -> "x.graph.png", or "x.png").
            const std::string stem = filename.substr(0, filename.size() - std::string(".json").size());
            const std::string base = filename.substr(0, filename.size() - std::string(".graph.json").size());
            for (const std::string& candidate : {stem + ".png", base + ".png"}) {
                const std::filesystem::path thumbnail = dir / candidate;
                if (std::filesystem::exists(thumbnail, ec)) {
                    entry.thumbnail = thumbnail;
                    break;
                }
            }
            entries.push_back(std::move(entry));
        }
    }
    std::sort(entries.begin(), entries.end(), [](const LibraryEntry& a, const LibraryEntry& b) {
        if (a.category != b.category) {
            return a.category < b.category;
        }
        if (a.name != b.name) {
            return a.name < b.name;
        }
        return a.path < b.path;
    });
    return entries;
}

} // namespace avgen::graph
