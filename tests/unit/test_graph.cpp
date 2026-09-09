#include "graph/graph.hpp"
#include "scene/procedural.hpp"
#include "spatial/point_cloud.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <glm/glm.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using namespace avgen;
using namespace avgen::graph;
using Catch::Matchers::WithinAbs;
using json = nlohmann::json;

namespace {

double d(float v) {
    return static_cast<double>(v);
}

std::filesystem::path tempDir(const std::string& name) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("avgen_graph_" + name);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

std::string joinWarnings(const GraphOutput& output) {
    std::string joined;
    for (const std::string& warning : output.warnings) {
        joined += warning;
        joined += "; ";
    }
    return joined;
}

void writeFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path);
    out << text;
}

// A stable JSON view of an evaluation, used by the determinism test.
json outputToJson(const GraphOutput& output) {
    json root = json::object();
    json procedurals = json::array();
    for (const scene::ProceduralGeometry& g : output.procedurals) {
        procedurals.push_back(g.toJson());
    }
    root["procedurals"] = std::move(procedurals);
    json fields = json::array();
    for (const spatial::FieldSpec& f : output.fields) {
        fields.push_back(f.toJson());
    }
    root["fields"] = std::move(fields);
    json splines = json::array();
    for (const spatial::Spline& s : output.splines) {
        splines.push_back(s.toJson());
    }
    root["splines"] = std::move(splines);
    json routes = json::array();
    for (const params::ModRoute& r : output.routes) {
        routes.push_back(json{{"source", r.source}, {"target", r.target}, {"amount", r.amount}});
    }
    root["routes"] = std::move(routes);
    root["warnings"] = output.warnings;
    return root;
}

// A node type that counts how often each node of it was evaluated (dirty-tracking test).
std::map<std::string, int>& evaluationCounts() {
    static std::map<std::string, int> counts;
    return counts;
}

void registerCounterType() {
    static bool once = [] {
        NodeTypeInfo info;
        info.type = "test/counter";
        info.label = "Counter";
        info.description = "Counts evaluations (test only).";
        info.category = NodeCategory::Math;
        info.inputs.push_back(PinInfo{"in", PinType::Float, 0.0f, false});
        info.outputs.push_back(PinInfo{"out", PinType::Float, 0.0f, false});
        ParamInfo value;
        value.name = "value";
        value.type = PinType::Float;
        value.defaultValue = 1.0f;
        info.params.push_back(value);
        NodeRegistry::instance().add(std::move(info),
                                     [](const Node& node, const std::vector<Value>& inputs,
                                        std::vector<Value>& outputs, EvalContext&) -> Result<void> {
                                         ++evaluationCounts()[node.name];
                                         const float in = inputs.empty() ? 0.0f : std::get<float>(inputs[0]);
                                         outputs[0] = in + node.params.value("value", 0.0f);
                                         return {};
                                     });
        return true;
    }();
    (void)once;
}

// A minimal working graph: cylinder -> radial ring -> procedural output.
Graph makeRingGraph(int count, float radius) {
    Graph graph;
    graph.name = "ring";
    // Node pointers are invalidated by later addNode calls, so each node is set up immediately.
    {
        Node* source = graph.addNode("generators/primitive", "column").value();
        source->params["kind"] = "cylinder";
        source->params["radius"] = 0.5f;
        source->params["height"] = 4.0f;
    }
    {
        Node* ring = graph.addNode("distributions/radial", "radial").value();
        ring->params["count"] = count;
        ring->params["radius"] = radius;
    }
    {
        Node* out = graph.addNode("output/procedural", "columns").value();
        out->params["name"] = "columns";
    }
    REQUIRE(graph.connect("column", "mesh", "columns", "source"));
    REQUIRE(graph.connect("radial", "spec", "columns", "spec"));
    return graph;
}

} // namespace

// ---- registry ---------------------------------------------------------------------------------

TEST_CASE("Node registry: every type has a category, a label and consistent pins", "[graph]") {
    NodeRegistry& registry = NodeRegistry::instance();
    const std::vector<const NodeTypeInfo*> types = registry.types();
    CHECK(types.size() > 90);

    for (const NodeTypeInfo* info : types) {
        INFO("node type " << info->type);
        CHECK_FALSE(info->type.empty());
        CHECK_FALSE(info->label.empty());
        CHECK_FALSE(info->description.empty());
        // The type is "<category>/<leaf>" for every built-in type.
        const std::size_t slash = info->type.find('/');
        REQUIRE(slash != std::string::npos);
        CHECK(registry.evaluator(info->type) != nullptr);
        CHECK(registry.find(info->type) == info);
        std::vector<std::string> names;
        for (const PinInfo& p : info->inputs) {
            CHECK_FALSE(p.name.empty());
            CHECK(std::find(names.begin(), names.end(), p.name) == names.end());
            names.push_back(p.name);
            // A pin's default must be empty or match its type.
            if (!std::holds_alternative<std::monostate>(p.defaultValue)) {
                CHECK(pinTypesCompatible(valueType(p.defaultValue), p.type));
            }
        }
        names.clear();
        for (const PinInfo& p : info->outputs) {
            CHECK_FALSE(p.name.empty());
            CHECK(std::find(names.begin(), names.end(), p.name) == names.end());
            names.push_back(p.name);
            CHECK_FALSE(p.multi); // only inputs may be multi
        }
        names.clear();
        for (const ParamInfo& p : info->params) {
            CHECK_FALSE(p.name.empty());
            CHECK(std::find(names.begin(), names.end(), p.name) == names.end());
            names.push_back(p.name);
            if (!p.choices.empty()) {
                CHECK(p.type == PinType::Int);
            }
        }
        // Output nodes emit; they have no output pins.
        if (info->category == NodeCategory::Output || info->category == NodeCategory::Subgraph) {
            CHECK(info->outputs.empty());
        } else {
            CHECK_FALSE(info->outputs.empty());
        }
    }
    // Every category name round trips and the expected families exist.
    CHECK(std::string(nodeCategoryName(NodeCategory::Generators)) == "generators");
    CHECK(std::string(nodeCategoryName(NodeCategory::Distributions)) == "distributions");
    CHECK(std::string(nodeCategoryName(NodeCategory::Output)) == "output");
    CHECK(std::string(nodeCategoryName(NodeCategory::Subgraph)) == "subgraph");
    CHECK_FALSE(registry.byCategory(NodeCategory::Generators).empty());
    CHECK(registry.byCategory(NodeCategory::Output).size() == 7);
    CHECK(registry.byCategory(NodeCategory::Deformers).size() == 7);
    CHECK(registry.byCategory(NodeCategory::Distributions).size() == 5);
}

TEST_CASE("Pin types: names round trip and the compatibility table holds", "[graph]") {
    for (int i = 0; i <= static_cast<int>(PinType::Any); ++i) {
        const auto type = static_cast<PinType>(i);
        const auto parsed = pinTypeFromName(pinTypeName(type));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == type);
        CHECK(pinTypesCompatible(type, type));
        CHECK(pinTypesCompatible(type, PinType::Any));
        CHECK(pinTypesCompatible(PinType::Any, type));
    }
    CHECK(std::string(pinTypeName(PinType::PointCloud)) == "pointCloud");
    CHECK(pinTypesCompatible(PinType::Int, PinType::Float));
    CHECK(pinTypesCompatible(PinType::Float, PinType::Vec3));
    CHECK_FALSE(pinTypesCompatible(PinType::Float, PinType::Int));
    CHECK_FALSE(pinTypesCompatible(PinType::Vec3, PinType::Float));
    CHECK_FALSE(pinTypesCompatible(PinType::PointCloud, PinType::Field));
    CHECK_FALSE(pinTypesCompatible(PinType::Mesh, PinType::Sdf));
    CHECK_FALSE(pinTypeFromName("nonsense").has_value());

    CHECK(valueType(Value{1.0f}) == PinType::Float);
    CHECK(valueType(Value{2}) == PinType::Int);
    CHECK(valueType(Value{true}) == PinType::Bool);
    CHECK(valueType(Value{glm::vec3(1.0f)}) == PinType::Vec3);
    CHECK(valueType(Value{glm::vec4(1.0f)}) == PinType::Color);
    CHECK(valueType(Value{std::string("x")}) == PinType::String);
    CHECK(valueType(Value{spatial::PointCloud()}) == PinType::PointCloud);
    CHECK(valueType(Value{spatial::FieldSpec()}) == PinType::Field);
    CHECK(valueType(Value{}) == PinType::Any);
}

// ---- structure --------------------------------------------------------------------------------

TEST_CASE("addNode: names come from the type leaf and are made unique", "[graph]") {
    Graph graph;
    CHECK(graph.addNode("distributions/radial").value()->name == "radial");
    CHECK(graph.addNode("distributions/radial").value()->name == "radial2");
    CHECK(graph.addNode("distributions/radial").value()->name == "radial3");
    CHECK(graph.addNode("math/add", "sum").value()->name == "sum");
    CHECK(graph.addNode("math/add", "sum").value()->name == "sum2");
    CHECK(graph.addNode("math/add", "a.b/c").value()->name == "a_b_c");
    CHECK_FALSE(graph.addNode("nope/missing").has_value());
    // Parameters are filled with the type's defaults.
    const Node* radial = graph.findNode("radial");
    REQUIRE(radial != nullptr);
    CHECK(radial->params.at("count").get<int>() == 32);
    CHECK(radial->params.contains("radius"));
    CHECK(graph.validate());
}

TEST_CASE("connect: refuses unknown pins, type mismatches and cycles", "[graph]") {
    Graph graph;
    graph.addNode("generators/primitive", "mesh");
    graph.addNode("distributions/radial", "ring");
    graph.addNode("output/procedural", "out");
    graph.addNode("math/add", "sum");
    graph.addNode("math/multiply", "product");

    CHECK_FALSE(graph.connect("mesh", "mesh", "missing", "source"));
    CHECK_FALSE(graph.connect("missing", "mesh", "out", "source"));
    CHECK_FALSE(graph.connect("mesh", "nope", "out", "source"));
    CHECK_FALSE(graph.connect("mesh", "mesh", "out", "nope"));
    // mesh -> spec is a Mesh into a String pin.
    CHECK_FALSE(graph.connect("mesh", "mesh", "out", "spec"));
    // A point cloud is not a field.
    CHECK_FALSE(graph.connect("ring", "points", "out", "fields"));

    REQUIRE(graph.connect("mesh", "mesh", "out", "source"));
    REQUIRE(graph.connect("ring", "spec", "out", "spec"));
    CHECK(graph.links.size() == 2);
    // Re-linking the same pin replaces the link (single input pins).
    REQUIRE(graph.connect("ring", "spec", "out", "spec"));
    CHECK(graph.links.size() == 2);

    REQUIRE(graph.connect("sum", "result", "product", "a"));
    CHECK(graph.wouldCycle("product", "sum"));
    CHECK_FALSE(graph.connect("product", "result", "sum", "a"));
    CHECK_FALSE(graph.connect("sum", "result", "sum", "b")); // self link
    CHECK(graph.links.size() == 3);

    CHECK(graph.disconnect("out", "spec"));
    CHECK_FALSE(graph.disconnect("out", "spec"));
    CHECK(graph.links.size() == 2);
    CHECK(graph.validate());

    // Multi pins keep every link, in link order.
    Graph merge;
    merge.addNode("distributions/radial", "a");
    merge.addNode("distributions/linear", "b");
    merge.addNode("points/merge", "merge");
    REQUIRE(merge.connect("a", "points", "merge", "points"));
    REQUIRE(merge.connect("b", "points", "merge", "points"));
    CHECK(merge.links.size() == 2);
}

TEST_CASE("topologicalOrder: dependencies first, empty on a cycle", "[graph]") {
    Graph graph;
    graph.addNode("math/constant", "c");
    graph.addNode("math/add", "add");
    graph.addNode("math/multiply", "mul");
    REQUIRE(graph.connect("c", "value", "add", "a"));
    REQUIRE(graph.connect("add", "result", "mul", "a"));

    const std::vector<const Node*> order = graph.topologicalOrder();
    REQUIRE(order.size() == 3);
    std::map<std::string, std::size_t> position;
    for (std::size_t i = 0; i < order.size(); ++i) {
        position[order[i]->name] = i;
    }
    CHECK(position["c"] < position["add"]);
    CHECK(position["add"] < position["mul"]);

    // Forcing a cycle into the link list is caught by topologicalOrder and validate().
    graph.links.push_back(Link{"mul", "result", "c", "value"});
    CHECK(graph.topologicalOrder().empty());
    CHECK_FALSE(graph.validate());
}

// ---- evaluation -------------------------------------------------------------------------------

TEST_CASE("A primitive + radial distribution + output emits one procedural object", "[graph]") {
    Graph graph = makeRingGraph(24, 5.0f);
    GraphOutput output;
    REQUIRE(graph.evaluate(output));
    CHECK(output.warnings.empty());
    REQUIRE(output.procedurals.size() == 1);
    const scene::ProceduralGeometry& g = output.procedurals.front();
    CHECK(g.name == "columns");
    CHECK(g.source.kind == scene::PrimitiveKind::Cylinder);
    CHECK_THAT(d(g.source.height), WithinAbs(4.0, 1e-6));
    CHECK(g.distribution.kind == scene::DistributionKind::Radial);
    CHECK(g.distribution.count == 24);
    CHECK_THAT(d(g.distribution.radius), WithinAbs(5.0, 1e-6));
    CHECK(g.instances.size() == 24);
    // Instance 0 sits on +X at the radius (the radial convention of scene/procedural.cpp).
    CHECK_THAT(d(g.instances.front().position.x), WithinAbs(5.0, 1e-4));
    CHECK(output.fields.empty());
    CHECK(output.routes.empty());
}

TEST_CASE("Math nodes compute the expected numbers", "[graph]") {
    Graph graph;
    auto* a = graph.addNode("math/constant", "a").value();
    a->params["value"] = 3.0f;
    auto* b = graph.addNode("math/constant", "b").value();
    b->params["value"] = 4.0f;
    graph.addNode("math/add", "sum");
    auto* mul = graph.addNode("math/multiply", "mul").value();
    REQUIRE(graph.connect("a", "value", "sum", "a"));
    REQUIRE(graph.connect("b", "value", "sum", "b"));
    REQUIRE(graph.connect("sum", "result", "mul", "a"));
    mul->params["b"] = 2.0f;

    auto* vec = graph.addNode("math/vec3", "vec").value();
    vec->params["x"] = 3.0f;
    vec->params["y"] = 0.0f;
    vec->params["z"] = 4.0f;
    graph.addNode("math/length", "len");
    REQUIRE(graph.connect("vec", "result", "len", "a"));
    auto* remap = graph.addNode("math/remap", "remap").value();
    remap->params["value"] = 0.5f;
    remap->params["outMin"] = 10.0f;
    remap->params["outMax"] = 20.0f;
    auto* clamp = graph.addNode("math/clamp", "clamp").value();
    clamp->params["value"] = 5.0f;
    clamp->params["min"] = 0.0f;
    clamp->params["max"] = 1.0f;
    auto* mix = graph.addNode("math/mix", "mix").value();
    mix->params["a"] = 0.0f;
    mix->params["b"] = 10.0f;
    mix->params["t"] = 0.25f;
    auto* random = graph.addNode("math/random", "random").value();
    random->params["seed"] = 7;
    random->params["index"] = 3;

    GraphOutput output;
    REQUIRE(graph.evaluate(output));
    const auto floatOf = [&](const char* name, std::size_t index) {
        const Node* node = graph.findNode(name);
        REQUIRE(node != nullptr);
        REQUIRE(node->outputs.size() > index);
        return std::get<float>(node->outputs[index]);
    };
    CHECK_THAT(d(floatOf("sum", 0)), WithinAbs(7.0, 1e-6));
    CHECK_THAT(d(floatOf("mul", 0)), WithinAbs(14.0, 1e-6));
    CHECK_THAT(d(floatOf("len", 0)), WithinAbs(5.0, 1e-6));
    CHECK_THAT(d(floatOf("remap", 0)), WithinAbs(15.0, 1e-6));
    CHECK_THAT(d(floatOf("clamp", 0)), WithinAbs(1.0, 1e-6));
    CHECK_THAT(d(floatOf("mix", 0)), WithinAbs(2.5, 1e-6));
    const float random1 = floatOf("random", 0);
    CHECK(random1 >= 0.0f);
    CHECK(random1 <= 1.0f);

    // Vector maths keeps the wider type; a float broadcasts into it.
    Graph vectors;
    auto* v = vectors.addNode("math/vec3", "v").value();
    v->params["x"] = 1.0f;
    v->params["y"] = 2.0f;
    v->params["z"] = 3.0f;
    auto* scale = vectors.addNode("math/multiply", "scale").value();
    scale->params["b"] = 2.0f;
    REQUIRE(vectors.connect("v", "result", "scale", "a"));
    GraphOutput vectorOutput;
    REQUIRE(vectors.evaluate(vectorOutput));
    const Value& scaled = vectors.findNode("scale")->outputs.at(0);
    REQUIRE(std::holds_alternative<glm::vec3>(scaled));
    CHECK_THAT(d(std::get<glm::vec3>(scaled).z), WithinAbs(6.0, 1e-6));
}

TEST_CASE("A field node emits a field of the right kind, named after its node", "[graph]") {
    Graph graph;
    auto* field = graph.addNode("fields/scalar", "pulse").value();
    field->params["kind"] = "noise";
    field->params["strength"] = 2.0f;
    field->params["frequency"] = 0.5f;
    graph.addNode("output/field", "fieldOut");
    REQUIRE(graph.connect("pulse", "field", "fieldOut", "field"));

    GraphOutput output;
    REQUIRE(graph.evaluate(output));
    CHECK(output.warnings.empty());
    REQUIRE(output.fields.size() == 1);
    CHECK(output.fields.front().name == "pulse");
    CHECK(output.fields.front().kind == spatial::FieldKind::Noise);
    CHECK(output.fields.front().type() == spatial::FieldType::Scalar);
    CHECK_THAT(d(output.fields.front().strength), WithinAbs(2.0, 1e-6));

    // A vector field keeps its own kind family.
    Graph vectors;
    auto* vectorField = vectors.addNode("fields/vector", "swirl").value();
    vectorField->params["kind"] = "vortex";
    vectors.addNode("output/field", "fieldOut");
    REQUIRE(vectors.connect("swirl", "field", "fieldOut", "field"));
    GraphOutput vectorOutput;
    REQUIRE(vectors.evaluate(vectorOutput));
    REQUIRE(vectorOutput.fields.size() == 1);
    CHECK(vectorOutput.fields.front().kind == spatial::FieldKind::Vortex);
    CHECK(vectorOutput.fields.front().type() == spatial::FieldType::Vector);
}

TEST_CASE("The effector node applies its field to the cloud on the CPU", "[graph]") {
    Graph graph;
    auto* points = graph.addNode("generators/points", "points").value();
    points->params["count"] = 8;
    auto* field = graph.addNode("fields/vector", "push").value();
    field->params["kind"] = "direction";
    field->params["axis"] = json::array({1.0f, 0.0f, 0.0f});
    field->params["strength"] = 2.0f;
    auto* effector = graph.addNode("effectors/effector", "effector").value();
    effector->params["op"] = "positionOffset";
    effector->params["strength"] = 1.0f;
    REQUIRE(graph.connect("points", "points", "effector", "points"));
    REQUIRE(graph.connect("push", "field", "effector", "field"));

    GraphOutput output;
    REQUIRE(graph.evaluate(output));
    CHECK(output.warnings.empty());
    const Node* node = graph.findNode("effector");
    REQUIRE(node != nullptr);
    REQUIRE(node->outputs.size() == 1);
    const auto* cloud = std::get_if<spatial::PointCloud>(&node->outputs.front());
    REQUIRE(cloud != nullptr);
    REQUIRE(cloud->count() == 8);
    CHECK_THAT(d(cloud->positions()[0].x), WithinAbs(2.0, 1e-5));
    CHECK_THAT(d(cloud->positions()[7].x), WithinAbs(2.0, 1e-5));
    CHECK_THAT(d(cloud->positions()[0].y), WithinAbs(0.0, 1e-5));
}

TEST_CASE("Dirty tracking: only the changed node and its downstream re-evaluate", "[graph]") {
    registerCounterType();
    evaluationCounts().clear();

    Graph graph;
    graph.addNode("test/counter", "first");
    graph.addNode("test/counter", "second");
    graph.addNode("test/counter", "sibling");
    REQUIRE(graph.connect("first", "out", "second", "in"));

    CHECK(graph.dirtyCount() == 3);
    GraphOutput output;
    REQUIRE(graph.evaluate(output, 0.0));
    CHECK(graph.dirtyCount() == 0);
    CHECK(evaluationCounts()["first"] == 1);
    CHECK(evaluationCounts()["second"] == 1);
    CHECK(evaluationCounts()["sibling"] == 1);

    // Nothing changed: nothing re-runs.
    REQUIRE(graph.evaluate(output, 0.0));
    CHECK(evaluationCounts()["first"] == 1);
    CHECK(evaluationCounts()["second"] == 1);
    CHECK(evaluationCounts()["sibling"] == 1);

    // Changing the upstream node re-runs it and its downstream, but not the sibling.
    graph.findNode("first")->params["value"] = 5.0f;
    REQUIRE(graph.evaluate(output, 0.0));
    CHECK(evaluationCounts()["first"] == 2);
    CHECK(evaluationCounts()["second"] == 2);
    CHECK(evaluationCounts()["sibling"] == 1);
    CHECK_THAT(d(std::get<float>(graph.findNode("second")->outputs.at(0))), WithinAbs(6.0, 1e-6));

    // Changing the downstream node re-runs only it.
    graph.findNode("second")->params["value"] = 2.0f;
    REQUIRE(graph.evaluate(output, 0.0));
    CHECK(evaluationCounts()["first"] == 2);
    CHECK(evaluationCounts()["second"] == 3);
    CHECK(evaluationCounts()["sibling"] == 1);

    // markAllDirty re-runs everything.
    graph.markAllDirty();
    CHECK(graph.dirtyCount() == 3);
    REQUIRE(graph.evaluate(output, 0.0));
    CHECK(evaluationCounts()["first"] == 3);
    CHECK(evaluationCounts()["second"] == 4);
    CHECK(evaluationCounts()["sibling"] == 2);

    // A node hash covers the type, the name and the parameters plus the upstream hashes.
    const std::uint64_t before = graph.nodeHash(*graph.findNode("second"));
    graph.findNode("first")->params["value"] = 9.0f;
    CHECK(graph.nodeHash(*graph.findNode("second")) != before);
}

TEST_CASE("A disabled node evaluates to its pins' defaults and its evaluator does not run", "[graph]") {
    registerCounterType();
    evaluationCounts().clear();
    Graph graph;
    graph.addNode("test/counter", "off");
    graph.findNode("off")->enabled = false;
    GraphOutput output;
    REQUIRE(graph.evaluate(output));
    CHECK(evaluationCounts()["off"] == 0);
    CHECK_THAT(d(std::get<float>(graph.findNode("off")->outputs.at(0))), WithinAbs(0.0, 1e-6));
}

TEST_CASE("An evaluator failure becomes a warning and leaves the rest of the graph running", "[graph]") {
    Graph graph;
    graph.addNode("output/field", "fieldOut"); // nothing linked: fails
    auto* constant = graph.addNode("math/constant", "c").value();
    constant->params["value"] = 2.0f;
    GraphOutput output;
    REQUIRE(graph.evaluate(output));
    REQUIRE(output.warnings.size() == 1);
    CHECK(output.warnings.front().find("fieldOut") != std::string::npos);
    CHECK(output.fields.empty());
    CHECK_THAT(d(std::get<float>(graph.findNode("c")->outputs.at(0))), WithinAbs(2.0, 1e-6));
}

// ---- serialisation ----------------------------------------------------------------------------

TEST_CASE("JSON round trip and file save/load", "[graph]") {
    Graph graph = makeRingGraph(12, 3.0f);
    graph.description = "a ring of columns";
    graph.exposed.push_back(ExposedParam{"count", "radial", "count"});

    const json j = graph.toJson();
    CHECK(j.at("format").get<std::string>() == Graph::kFormatName);
    CHECK(j.at("version").get<int>() == Graph::kFormatVersion);
    CHECK(j.at("nodes").size() == 3);
    CHECK(j.at("links").size() == 2);
    CHECK(j.at("links").at(0).at("from").get<std::string>() == "column.mesh");
    CHECK(j.at("links").at(0).at("to").get<std::string>() == "columns.source");
    CHECK(j.at("exposed").at(0).at("name").get<std::string>() == "count");

    const auto restored = Graph::fromJson(j);
    REQUIRE(restored);
    CHECK(restored->name == graph.name);
    CHECK(restored->description == graph.description);
    REQUIRE(restored->nodes.size() == graph.nodes.size());
    CHECK(restored->nodes[1].params == graph.nodes[1].params);
    CHECK(restored->links.size() == graph.links.size());
    CHECK(restored->exposed.size() == 1);
    CHECK(restored->toJson() == j);

    const std::filesystem::path dir = tempDir("io");
    const std::filesystem::path path = dir / "ring.graph.json";
    REQUIRE(graph.saveFile(path));
    auto loaded = Graph::loadFile(path);
    REQUIRE(loaded);
    CHECK(loaded->toJson() == j);
    GraphOutput output;
    REQUIRE(loaded->evaluate(output));
    REQUIRE(output.procedurals.size() == 1);
    CHECK(output.procedurals.front().distribution.count == 12);

    // Malformed files are rejected with a message, not a crash.
    writeFile(dir / "bad.graph.json", "{ not json");
    CHECK_FALSE(Graph::loadFile(dir / "bad.graph.json"));
    writeFile(dir / "wrong.graph.json", R"({"format": "something-else", "version": 1})");
    CHECK_FALSE(Graph::loadFile(dir / "wrong.graph.json"));
    writeFile(dir / "unknown.graph.json",
              R"({"format": "avgen-graph", "version": 1, "nodes": [{"name": "x", "type": "nope/none"}]})");
    CHECK_FALSE(Graph::loadFile(dir / "unknown.graph.json"));
    CHECK_FALSE(Graph::loadFile(dir / "missing.graph.json"));
    std::filesystem::remove_all(dir);
}

TEST_CASE("Evaluation is deterministic: two runs emit identical JSON", "[graph]") {
    Graph graph = makeRingGraph(16, 4.0f);
    auto* twist = graph.addNode("deformers/twist", "twist").value();
    twist->params["amount"] = 0.4f;
    REQUIRE(graph.connect("twist", "deformer", "columns", "deformers"));
    auto* field = graph.addNode("fields/scalar", "glow").value();
    field->params["kind"] = "radial";
    graph.addNode("output/field", "fieldOut");
    REQUIRE(graph.connect("glow", "field", "fieldOut", "field"));

    GraphOutput first;
    REQUIRE(graph.evaluate(first, 1.5));
    Graph second = graph;
    second.markAllDirty();
    GraphOutput secondOutput;
    REQUIRE(second.evaluate(secondOutput, 1.5));
    CHECK(outputToJson(first) == outputToJson(secondOutput));

    // The same graph rebuilt from its JSON emits the same thing.
    auto reloaded = Graph::fromJson(graph.toJson());
    REQUIRE(reloaded);
    GraphOutput third;
    REQUIRE(reloaded->evaluate(third, 1.5));
    CHECK(outputToJson(third) == outputToJson(first));
    REQUIRE(first.procedurals.size() == 1);
    CHECK(first.procedurals.front().deformers.size() == 1);
    CHECK(first.procedurals.front().deformers.front().kind == scene::DeformerKind::Twist);
    CHECK(first.fields.size() == 1);
}

// ---- signals ----------------------------------------------------------------------------------

TEST_CASE("An audio signal linked into an output parameter pin emits a route", "[graph]") {
    Graph graph = makeRingGraph(8, 2.0f);
    auto* signal = graph.addNode("audio/signal", "bass").value();
    signal->params["signal"] = "audio.bass";
    signal->params["amount"] = 0.75f;
    REQUIRE(graph.connect("bass", "value", "columns", "emissive"));

    GraphOutput output;
    REQUIRE(graph.evaluate(output));
    REQUIRE(output.routes.size() == 1);
    CHECK(output.routes.front().source == "audio.bass");
    CHECK(output.routes.front().target == "procedural/columns/material/emissive");
    CHECK_THAT(d(output.routes.front().amount), WithinAbs(0.75, 1e-6));
    CHECK(output.routes.front().op == params::ModOp::Add);

    // An explicit target on the signal wins over the pin's default mapping.
    signal->params["target"] = "distribution/radius";
    signal->params["op"] = "multiply";
    graph.markAllDirty();
    GraphOutput retargeted;
    REQUIRE(graph.evaluate(retargeted, 0.0));
    REQUIRE(retargeted.routes.size() == 1);
    CHECK(retargeted.routes.front().target == "procedural/columns/distribution/radius");
    CHECK(retargeted.routes.front().op == params::ModOp::Multiply);

    // A beat signal into a field output targets the field's parameters.
    Graph fields;
    fields.addNode("fields/scalar", "glow");
    fields.addNode("output/field", "fieldOut");
    auto* beat = fields.addNode("time/beatPhase", "beat").value();
    beat->params["signal"] = "beat.phase";
    REQUIRE(fields.connect("glow", "field", "fieldOut", "field"));
    REQUIRE(fields.connect("beat", "value", "fieldOut", "strength"));
    GraphOutput fieldOutput;
    REQUIRE(fields.evaluate(fieldOutput));
    REQUIRE(fieldOutput.routes.size() == 1);
    CHECK(fieldOutput.routes.front().source == "beat.phase");
    CHECK(fieldOutput.routes.front().target == "field/glow/strength");
}

// ---- subgraphs --------------------------------------------------------------------------------

TEST_CASE("A subgraph instance applies exposed parameters and prefixes the emitted names", "[graph]") {
    const std::filesystem::path dir = tempDir("subgraph");
    Graph child = makeRingGraph(8, 2.0f);
    child.name = "ring";
    child.exposed.push_back(ExposedParam{"count", "radial", "count"});
    child.exposed.push_back(ExposedParam{"radius", "radial", "radius"});
    REQUIRE(child.saveFile(dir / "ring.graph.json"));

    Graph parent;
    parent.name = "parent";
    auto* instance = parent.addNode("subgraph/instance", "tower").value();
    instance->subgraph = "ring.graph.json";
    instance->params["count"] = 20;
    instance->params["radius"] = 9.0f;
    REQUIRE(parent.saveFile(dir / "parent.graph.json"));

    auto loaded = Graph::loadFile(dir / "parent.graph.json");
    REQUIRE(loaded);
    GraphOutput output;
    REQUIRE(loaded->evaluate(output));
    INFO(joinWarnings(output));
    CHECK(output.warnings.empty());
    REQUIRE(output.procedurals.size() == 1);
    CHECK(output.procedurals.front().name == "tower_columns");
    CHECK(output.procedurals.front().distribution.count == 20);
    CHECK_THAT(d(output.procedurals.front().distribution.radius), WithinAbs(9.0, 1e-6));

    // A missing file is a warning, not a crash.
    loaded->findNode("tower")->subgraph = "nothing-here.graph.json";
    loaded->markAllDirty();
    GraphOutput missing;
    REQUIRE(loaded->evaluate(missing));
    CHECK(missing.procedurals.empty());
    REQUIRE(missing.warnings.size() == 1);
    CHECK(missing.warnings.front().find("not found") != std::string::npos);

    // A self-referencing subgraph expands until the depth limit instead of recursing forever.
    Graph recursive;
    recursive.name = "recursive";
    auto* self = recursive.addNode("subgraph/instance", "self").value();
    self->subgraph = "recursive.graph.json";
    auto* inner = recursive.addNode("distributions/radial", "ring").value();
    inner->params["count"] = 3;
    recursive.addNode("generators/primitive", "column");
    recursive.addNode("output/procedural", "columns");
    REQUIRE(recursive.connect("column", "mesh", "columns", "source"));
    REQUIRE(recursive.connect("ring", "spec", "columns", "spec"));
    REQUIRE(recursive.saveFile(dir / "recursive.graph.json"));
    auto recursiveLoaded = Graph::loadFile(dir / "recursive.graph.json");
    REQUIRE(recursiveLoaded);
    GraphOutput recursiveOutput;
    REQUIRE(recursiveLoaded->evaluate(recursiveOutput));
    CHECK(recursiveOutput.warnings.empty());
    // One object per level: the graph itself plus one per nested expansion, each prefixed again.
    CHECK(recursiveOutput.procedurals.size() == Graph::kMaxSubgraphDepth + 1);
    CHECK(recursiveOutput.procedurals.back().name == "columns");
    CHECK(recursiveOutput.procedurals.front().name == "self_self_self_self_columns");
    std::filesystem::remove_all(dir);
}

// ---- library ----------------------------------------------------------------------------------

TEST_CASE("scanGraphLibrary lists graph files with their metadata and thumbnails", "[graph]") {
    const std::filesystem::path dir = tempDir("library");
    const std::filesystem::path other = tempDir("library2");
    writeFile(dir / "b.graph.json",
              R"({"format": "avgen-graph", "version": 1, "name": "Beta", "category": "structures",
                  "description": "second", "nodes": [], "links": []})");
    writeFile(dir / "a.graph.json",
              R"({"format": "avgen-graph", "version": 1, "name": "Alpha", "category": "structures",
                  "description": "first", "nodes": [], "links": []})");
    writeFile(dir / "a.graph.png", "not really a png");
    writeFile(dir / "notes.txt", "ignored");
    writeFile(other / "c.graph.json",
              R"({"format": "avgen-graph", "version": 1, "name": "Gamma", "category": "abstract",
                  "description": "third", "nodes": [], "links": []})");

    const std::vector<LibraryEntry> entries = scanGraphLibrary({dir, other});
    REQUIRE(entries.size() == 3);
    CHECK(entries[0].name == "Gamma"); // category "abstract" sorts first
    CHECK(entries[0].category == "abstract");
    CHECK(entries[1].name == "Alpha");
    CHECK(entries[1].description == "first");
    CHECK(entries[1].thumbnail == dir / "a.graph.png");
    CHECK(entries[2].name == "Beta");
    CHECK(entries[2].thumbnail.empty());
    CHECK(entries[2].path == dir / "b.graph.json");

    CHECK(scanGraphLibrary({dir / "missing"}).empty());
    std::filesystem::remove_all(dir);
    std::filesystem::remove_all(other);
}

// ---- examples ---------------------------------------------------------------------------------

TEST_CASE("The example graphs load, validate and evaluate", "[graph]") {
    const std::filesystem::path dir = std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "graphs";
    REQUIRE(std::filesystem::is_directory(dir));
    const std::vector<LibraryEntry> entries = scanGraphLibrary({dir});
    CHECK(entries.size() == 3);

    for (const LibraryEntry& entry : entries) {
        INFO("graph " << entry.path.string());
        auto graph = Graph::loadFile(entry.path);
        REQUIRE(graph);
        CHECK(graph->validate());
        CHECK_FALSE(graph->exposed.empty());
        GraphOutput output;
        REQUIRE(graph->evaluate(output));
        INFO(joinWarnings(output));
        CHECK(output.warnings.empty());
        REQUIRE(output.procedurals.size() == 1);
        CHECK_FALSE(output.procedurals.front().name.empty());
    }

    auto columns = Graph::loadFile(dir / "radial-columns.graph.json");
    REQUIRE(columns);
    GraphOutput output;
    REQUIRE(columns->evaluate(output));
    const scene::ProceduralGeometry& g = output.procedurals.front();
    CHECK(g.distribution.count == 16);
    CHECK(g.instances.size() == 16);
    CHECK(g.source.kind == scene::PrimitiveKind::Cylinder);

    auto rings = Graph::loadFile(dir / "ring-stack.graph.json");
    REQUIRE(rings);
    GraphOutput stack;
    REQUIRE(rings->evaluate(stack));
    REQUIRE(stack.procedurals.size() == 1);
    // The stack has no Distribution: it is emitted as a grammar of placements (root + one per point).
    CHECK(stack.procedurals.front().distribution.kind == scene::DistributionKind::Grammar);
    CHECK(stack.procedurals.front().grammar.rules.size() == 12 * 6 + 1);
}
