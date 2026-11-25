// ADR-2150: the engine's published contracts are generated from the C++ that decides them, and these
// tests fail when the checked-in artefacts drift from it. Ids follow the Phase 0 plan's table
// (T1 catalogue, T2 schema keywords, T4 output-schema ratchet, T5 breaking changes, T6 MCP names,
// E1 errors, H1 no hand-written tool counts).

#include "ai/tool_api.hpp"
#include "ai/tool_context.hpp"
#include "api/describe.hpp"
#include "api/version.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>

namespace {

using nlohmann::json;
namespace api = avgen::api;
namespace ai = avgen::ai;

const std::filesystem::path kRoot(AVGEN_SOURCE_DIR);

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

json readJson(const std::filesystem::path& path) { return json::parse(readFile(path)); }

// The keywords ai::validateAgainstSchema enforces, plus `description` (documentation). A schema that
// uses anything else would be published as a constraint the engine silently does not check.
const std::set<std::string> kValidatorKeywords = {"type",    "properties", "required", "additionalProperties",
                                                  "enum",    "minimum",    "maximum",  "minItems",
                                                  "maxItems", "items",     "description"};

void checkSchema(const json& schema, const std::string& where, std::vector<std::string>& problems) {
    if (!schema.is_object()) {
        problems.push_back(where + ": a schema must be an object");
        return;
    }
    for (const auto& [k, v] : schema.items()) {
        if (!kValidatorKeywords.contains(k)) {
            problems.push_back(where + ": keyword '" + k + "' is not enforced by the engine's validator");
        }
    }
    if (const auto p = schema.find("properties"); p != schema.end()) {
        for (const auto& [k, v] : p->items()) {
            checkSchema(v, where + ".properties." + k, problems);
        }
        if (const auto r = schema.find("required"); r != schema.end()) {
            for (const json& name : *r) {
                if (!p->contains(name.get<std::string>())) {
                    problems.push_back(where + ": required '" + name.get<std::string>() + "' is not a property");
                }
            }
        }
    } else if (schema.contains("required")) {
        problems.push_back(where + ": 'required' without 'properties'");
    }
    if (const auto i = schema.find("items"); i != schema.end()) {
        checkSchema(*i, where + ".items", problems);
    }
}

} // namespace

TEST_CASE("T1: the committed catalogue and API docs are what the engine generates", "[api][catalogue]") {
    const ai::ToolRegistry registry = api::makeCatalogueRegistry();
    for (const api::Artefact& a : api::generatedArtefacts(registry)) {
        INFO(a.relativePath.generic_string() << " is stale: run tools/gen_api.sh and commit the result");
        CHECK(readFile(kRoot / a.relativePath) == a.contents);
    }
}

TEST_CASE("The catalogue lists every tool once, with its annotations, class and MCP name", "[api][catalogue]") {
    const ai::ToolRegistry registry = api::makeCatalogueRegistry();
    const json catalogue = api::toolCatalogue(registry);
    CHECK(catalogue.at("format") == "avgen-tool-catalogue");
    CHECK(catalogue.at("api") == api::kApiLevel);
    REQUIRE(catalogue.at("tools").size() == registry.size());
    // The baseline the Phase 0 plan counted at 414d47790; a floor, so adding tools never fails this.
    CHECK(registry.size() >= 76);
    const std::set<std::string> classes = {"read", "instant", "rebind", "structural", "session", "job"};
    for (const json& t : catalogue.at("tools")) {
        INFO(t.at("name").get<std::string>());
        CHECK(classes.contains(t.at("annotations").at("executionClass").get<std::string>()));
        CHECK(t.at("annotations").at("permissions").is_array());
        CHECK(t.at("since").get<int>() >= 1);
        CHECK(t.at("since").get<int>() <= api::kApiLevel);
        CHECK_FALSE(t.at("description").get<std::string>().empty());
    }
}

TEST_CASE("Execution classes and permissions say what each tool does", "[api][catalogue]") {
    const ai::ToolRegistry registry = api::makeCatalogueRegistry();
    const auto cls = [&](std::string_view name) {
        const ai::Tool* t = registry.find(name);
        REQUIRE(t != nullptr);
        return std::string(ai::executionClassName(t->definition.annotations.effectiveExecutionClass()));
    };
    const auto perms = [&](std::string_view name) {
        const ai::Tool* t = registry.find(name);
        REQUIRE(t != nullptr);
        return t->definition.annotations.permissions;
    };
    // Derived from the annotations.
    CHECK(cls("parameter.get") == "read");
    CHECK(cls("parameter.set") == "instant");
    CHECK(cls("camera.set") == "instant");
    CHECK(cls("project.save") == "session");
    CHECK(cls("asset.import") == "session");
    CHECK(cls("sequencer.set_playhead") == "session"); // a seek
    // The stated exceptions.
    for (const char* t : {"scene.create_node", "scene.delete_node", "scene.set_parent", "world.generate",
                          "project.restore_snapshot", "field.create"}) {
        CHECK(cls(t) == "structural");
    }
    for (const char* t : {"modulation.create", "modulation.remove", "sequencer.add_keyframe", "sequencer.remove_track",
                          "sequence.add_marker", "sequence.add_shot", "sequence.add_overlay"}) {
        CHECK(cls(t) == "rebind");
    }
    for (const char* t : {"performance.profile_scene", "performance.optimize_scene", "performance.benchmark_before_after",
                          "performance.apply_low_risk_optimizations", "director.evaluate"}) {
        CHECK(cls(t) == "job");
        CHECK(perms(t) == std::vector<std::string>{"subprocess"});
    }
    CHECK(perms("asset.import") == std::vector<std::string>{"read", "write"});
    CHECK(perms("project.create") == std::vector<std::string>{"write"});
    CHECK(perms("project.open") == std::vector<std::string>{"read"});
    CHECK(perms("scene.create_node") == std::vector<std::string>{"read"});
    CHECK(perms("parameter.set").empty());
}

TEST_CASE("Registration refuses a class that disagrees with the annotations, or an unknown permission",
          "[api][catalogue]") {
    const auto tool = [](ai::ToolAnnotations a) {
        ai::Tool t;
        t.definition.name = "test.tool";
        t.definition.annotations = std::move(a);
        t.execute = [](const json&, ai::ToolContext&) { return ai::ToolResult::ok(json::object()); };
        return t;
    };
    ai::ToolAnnotations write;
    write.mutatesProject = true;
    ai::ToolAnnotations read;
    read.readOnly = true;
    {
        ai::ToolRegistry r;
        ai::ToolAnnotations a = write;
        a.executionClass = ai::ExecutionClass::Read; // a "read" that writes
        CHECK_THROWS(r.add(tool(a)));
    }
    {
        ai::ToolRegistry r;
        ai::ToolAnnotations a = read;
        a.executionClass = ai::ExecutionClass::Structural; // a structural change that changes nothing
        CHECK_THROWS(r.add(tool(a)));
    }
    {
        ai::ToolRegistry r;
        ai::ToolAnnotations a = read;
        a.executionClass = ai::ExecutionClass::Job; // a read-only job (a render of a scratch copy) is fine
        CHECK_NOTHROW(r.add(tool(a)));
    }
    {
        ai::ToolRegistry r;
        ai::ToolAnnotations a = read;
        a.permissions = {"network"};
        CHECK_THROWS(r.add(tool(a)));
    }
}

TEST_CASE("T2: every tool schema uses only keywords the engine enforces", "[api][catalogue]") {
    const json catalogue = api::toolCatalogue(api::makeCatalogueRegistry());
    std::vector<std::string> problems;
    for (const json& t : catalogue.at("tools")) {
        const std::string name = t.at("name").get<std::string>();
        checkSchema(t.at("inputSchema"), name + ".inputSchema", problems);
        if (t.contains("outputSchema")) {
            checkSchema(t.at("outputSchema"), name + ".outputSchema", problems);
        }
    }
    for (const std::string& p : problems) {
        UNSCOPED_INFO(p);
    }
    CHECK(problems.empty());
}

TEST_CASE("T4: tools without an output schema may only become fewer", "[api][catalogue]") {
    const json ratchet = readJson(kRoot / "schemas/tools-without-output-schema.json");
    const json& listed = ratchet.at("tools");
    const ai::ToolRegistry registry = api::makeCatalogueRegistry();
    for (const ai::Tool* t : registry.all()) {
        if (t->definition.outputSchema.is_null()) {
            INFO(t->definition.name << " has no outputSchema and is not in schemas/tools-without-output-schema.json; "
                                       "give it one, or list it there with the reason");
            CHECK(listed.contains(t->definition.name));
        }
    }
    for (const auto& [name, reason] : listed.items()) {
        INFO(name);
        const ai::Tool* t = registry.find(name);
        INFO("listed in schemas/tools-without-output-schema.json but " << (t ? "now has an outputSchema" : "no longer exists")
                                                                       << ": remove it from the list");
        CHECK((t != nullptr && t->definition.outputSchema.is_null()));
        CHECK_FALSE(reason.get<std::string>().empty());
    }
}

TEST_CASE("T5: a breaking catalogue change raises the API level", "[api][catalogue]") {
    const json baseline = readJson(kRoot / "schemas/tool-catalogue-baseline.json");
    const json catalogue = api::toolCatalogue(api::makeCatalogueRegistry());
    const std::vector<std::string> breaks = api::breakingChanges(baseline, catalogue);
    if (api::kApiLevel == baseline.at("api").get<int>()) {
        for (const std::string& b : breaks) {
            UNSCOPED_INFO(b);
        }
        INFO("a breaking change needs avgen::api::kApiLevel raised and tools/gen_api.sh --baseline in the same commit");
        CHECK(breaks.empty());
    } else {
        INFO("kApiLevel was raised: rewrite the baseline with tools/gen_api.sh --baseline");
        CHECK(api::kApiLevel == baseline.at("api").get<int>() + 1);
    }
}

TEST_CASE("T5: the breaking-change detector sees each kind of break, and not an addition", "[api][catalogue]") {
    const json before = api::toolCatalogue(api::makeCatalogueRegistry());
    const json baseline = api::apiBaseline(before);
    CHECK(api::breakingChanges(baseline, before).empty());

    const auto findTool = [](json& cat, const std::string& name) -> json& {
        for (json& t : cat.at("tools")) {
            if (t.at("name") == name) {
                return t;
            }
        }
        FAIL("no tool " << name);
        throw std::logic_error("unreachable");
    };
    {
        json added = before;
        findTool(added, "parameter.set")["inputSchema"]["properties"]["note"] = {{"type", "string"}};
        findTool(added, "parameter.set")["outputSchema"] = {{"type", "object"},
                                                             {"properties", {{"path", {{"type", "string"}}}}}};
        CHECK(api::breakingChanges(baseline, added).empty()); // an optional input and a new output are additive
        const json withOutput = api::apiBaseline(added);
        json lost = added;
        findTool(lost, "parameter.set").erase("outputSchema");
        CHECK(api::breakingChanges(withOutput, lost).size() == 1); // an output field removed
    }
    {
        json removed = before;
        auto& tools = removed.at("tools");
        tools.erase(std::remove_if(tools.begin(), tools.end(), [](const json& t) { return t.at("name") == "camera.set"; }),
                    tools.end());
        CHECK(api::breakingChanges(baseline, removed).size() == 1);
    }
    {
        json required = before;
        json& in = findTool(required, "parameter.get")["inputSchema"];
        in["properties"]["extra"] = {{"type", "string"}};
        in["required"].push_back("extra");
        CHECK(api::breakingChanges(baseline, required).size() == 1);
    }
    {
        json narrowed = before;
        json& props = findTool(narrowed, "parameter.get")["inputSchema"]["properties"];
        REQUIRE_FALSE(props.empty());
        json& first = props.begin().value();
        REQUIRE(first.value("type", std::string{}) != "boolean");
        first["type"] = "boolean"; // an existing input's type changed
        CHECK(api::breakingChanges(baseline, narrowed).size() == 1);
    }
}

TEST_CASE("T6: every tool has a unique MCP name that MCP clients accept", "[api][catalogue]") {
    const json catalogue = api::toolCatalogue(api::makeCatalogueRegistry());
    const std::regex pattern("^[a-zA-Z0-9_-]{1,64}$");
    std::set<std::string> seen;
    for (const json& t : catalogue.at("tools")) {
        const std::string mcp = t.at("mcpName").get<std::string>();
        INFO(t.at("name").get<std::string>() << " -> " << mcp);
        CHECK(std::regex_match(mcp, pattern));
        CHECK(seen.insert(mcp).second);
    }
    CHECK(seen.size() == catalogue.at("tools").size());
}

TEST_CASE("E1: every error code has a name and a meaning, and the list is closed", "[api][catalogue]") {
    const json errors = api::errorCatalogue();
    std::set<std::string> names;
    for (const json& c : errors.at("codes")) {
        const std::string code = c.at("code").get<std::string>();
        CHECK_FALSE(code.empty());
        CHECK(c.at("meaning").get<std::string>().size() > 20);
        CHECK(names.insert(code).second);
    }
    CHECK(names.size() == std::size(ai::kAllToolErrorCodes));
    // The wire name is what ToolResult::toJson puts in `error.code`.
    const json wire = ai::ToolResult::failure(ai::ToolErrorCode::NotFound, "x").toJson();
    CHECK(names.contains(wire.at("error").at("code").get<std::string>()));
}

TEST_CASE("H1: the reference docs do not hard-code a tool count", "[api][catalogue][hygiene]") {
    // A count written by hand went stale twice (architecture.md said 34 while the build had 76). The
    // living reference docs link to the generated catalogue instead. Dated reports and ADRs record the
    // count of their day and are history, so they are not checked.
    const std::regex count(R"(\b[0-9]+ (engine |AI |registered |named )?tools\b)");
    std::vector<std::filesystem::path> files = {kRoot / "docs/architecture.md", kRoot / "docs/ai-control-plane.md"};
    for (const auto& e : std::filesystem::recursive_directory_iterator(kRoot / "docs/help")) {
        if (e.path().extension() == ".md") {
            files.push_back(e.path());
        }
    }
    for (const auto& f : files) {
        INFO(f.generic_string());
        CHECK_FALSE(std::regex_search(readFile(f), count));
    }
    CHECK(readFile(kRoot / "docs/ai-control-plane.md").find("(api/tools.md)") != std::string::npos);
}

TEST_CASE("The describe command writes the same artefacts, with no GPU, window or engine", "[api][catalogue]") {
    // The generator is in avgen_core, which cannot link gpu::Context, so this path is GPU-free by
    // construction; the test runs it end to end into a scratch folder.
    const std::filesystem::path out = std::filesystem::temp_directory_path() / "avgen-describe-test";
    std::filesystem::remove_all(out);
    std::string a0 = "avgen", a1 = "--describe", a2 = "all", a3 = "--out", a4 = out.string();
    char* argv[] = {a0.data(), a1.data(), a2.data(), a3.data(), a4.data()};
    REQUIRE(api::runDescribeCommand(5, argv) == 0);
    for (const api::Artefact& a : api::generatedArtefacts(api::makeCatalogueRegistry())) {
        INFO(a.relativePath.generic_string());
        CHECK(readFile(out / a.relativePath) == a.contents);
    }
    std::string b2 = "nonsense";
    char* bad[] = {a0.data(), a1.data(), b2.data()};
    CHECK(api::runDescribeCommand(3, bad) == 2);
    std::filesystem::remove_all(out);
}

TEST_CASE("One version identity", "[api][version]") {
    const api::VersionInfo& v = api::version();
    CHECK(v.engine == api::kEngineVersion);
    CHECK(v.api == api::kApiLevel);
    CHECK(v.projectFormat == 4);
    CHECK(v.sceneFormat == 1);
    CHECK_FALSE(v.revision.empty());
    CHECK(api::versionLine().find(api::kEngineVersion) != std::string::npos);
    const json doc = api::versionDocument();
    CHECK(doc.at("format") == "avgen-version");
    CHECK(doc.at("api") == api::kApiLevel);
}
