// ADR-2243: every member of scene::Scene and every field of scene::Material has a row in the path tracer's coverage
// table, and the table names nothing that does not exist. Read from the source on disk: a new scene feature fails
// here until somebody has decided what the tracer does with it -- which is how clouds, material programs and effect
// lights came to be dropped from a Neon Dreams trace without a word.

#include "pathtrace/coverage.hpp"
#include "pathtrace/snapshot.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace avgen;

namespace {

std::string readSource(const char* relative) {
    std::ifstream in(std::filesystem::path(AVGEN_SOURCE_DIR) / relative);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// The data members of `struct <name> {` at four-space indent, in order, up to `private:` or the closing brace.
// Methods (anything with a parenthesis), comments, nested struct bodies and attributes are skipped; a nested struct
// declared together with a member (`} portal;`) contributes that member.
std::vector<std::string> membersOf(const std::string& source, const std::string& name) {
    std::vector<std::string> out;
    const std::string open = "struct " + name + " {\n";
    const auto start = source.find(open);
    if (start == std::string::npos) return out;
    std::istringstream lines(source.substr(start + open.size()));
    std::string line;
    const std::regex member(R"(^    [A-Za-z_][\w:<>, ]*[\w>]\s+([A-Za-z_]\w*)\s*(\{[^;]*\}|=[^;]*)?;)");
    const std::regex nestedClose(R"(^    \}\s*([A-Za-z_]\w*);)");
    while (std::getline(lines, line)) {
        if (line.rfind("};", 0) == 0 || line.rfind("private:", 0) == 0) break;
        std::smatch m;
        if (std::regex_search(line, m, nestedClose)) {
            out.push_back(m[1]);
            continue;
        }
        if (line.rfind("    //", 0) == 0 || line.rfind("    [[", 0) == 0 || line.rfind("     ", 0) == 0) continue;
        // A method has a parenthesis before any initialiser; `identity = mintSceneIdentity();` is still a member.
        // Only the declaration counts, not a trailing comment (which may well hold a parenthesis).
        const std::string decl = line.substr(0, line.find(';'));
        const auto paren = decl.find('(');
        const auto init = decl.find_first_of("={");
        if (paren != std::string::npos && (init == std::string::npos || paren < init)) continue;
        if (std::regex_search(line, m, member)) out.push_back(m[1]);
    }
    return out;
}

void requireSameNames(const std::vector<std::string>& declared, std::span<const pathtrace::CoverageRow> rows) {
    std::set<std::string> table;
    for (const auto& r : rows) table.insert(std::string(r.name));
    std::set<std::string> source(declared.begin(), declared.end());
    for (const auto& d : source) {
        INFO("declared but has no coverage row: " << d);
        CHECK(table.count(d) == 1);
    }
    for (const auto& t : table) {
        INFO("coverage row names no declared member: " << t);
        CHECK(source.count(t) == 1);
    }
}

} // namespace

TEST_CASE("every scene::Scene member has a path-tracer coverage row", "[unit][pathtrace][coverage]") {
    const auto declared = membersOf(readSource("src/scene/scene.hpp"), "Scene");
    // The control: a parser that found nothing, or stopped early, would pass the comparison vacuously.
    REQUIRE(declared.size() >= 40);
    REQUIRE(std::find(declared.begin(), declared.end(), "clouds") != declared.end());
    REQUIRE(std::find(declared.begin(), declared.end(), "portal") != declared.end());
    REQUIRE(std::find(declared.begin(), declared.end(), "identity") != declared.end());
    requireSameNames(declared, pathtrace::sceneCoverage());
}

TEST_CASE("every scene::Material field has a path-tracer coverage row", "[unit][pathtrace][coverage]") {
    const auto declared = membersOf(readSource("src/scene/scene_types.hpp"), "Material");
    REQUIRE(declared.size() >= 20);
    REQUIRE(std::find(declared.begin(), declared.end(), "program") != declared.end());
    REQUIRE(std::find(declared.begin(), declared.end(), "regions") != declared.end());
    requireSameNames(declared, pathtrace::materialCoverage());
}

TEST_CASE("the capability report names what Neon Dreams lost silently", "[unit][pathtrace][coverage]") {
    scene::Scene s;
    const scene::MeshId cube = s.addMesh(scene::makeCube(1.0f));
    scene::Entity& e = s.addEntity("pane", cube);
    e.material.program = "windows";
    e.material.normalTexture.texture = 0;
    s.clouds.enabled = true;
    s.clouds.sea.enabled = true;
    s.stars.on = true;
    s.pulses.count = 2;
    s.entityFx.lights.count = 3;

    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(s);
    const auto find = [&](std::string_view f) -> const pathtrace::Capability* {
        for (const auto& c : snap.capabilities.entries) {
            if (c.feature == f) return &c;
        }
        return nullptr;
    };
    for (const char* feature : {"clouds", "material program", "normal map", "stars", "shockwave pulse", "effect light"}) {
        INFO(feature);
        const pathtrace::Capability* c = find(feature);
        REQUIRE(c != nullptr);
        CHECK(c->support == pathtrace::Support::Unsupported);
        CHECK(c->count > 0);
    }
    CHECK(find("effect light")->count == 3);

    // And a plain scene reports none of them: the rows are about this frame's content.
    scene::Scene plain;
    plain.addEntity("cube", plain.addMesh(scene::makeCube(1.0f)));
    const pathtrace::Snapshot quiet = pathtrace::buildSnapshot(plain);
    for (const auto& c : quiet.capabilities.entries) {
        INFO(c.feature);
        CHECK(c.support != pathtrace::Support::Unsupported);
    }
}
