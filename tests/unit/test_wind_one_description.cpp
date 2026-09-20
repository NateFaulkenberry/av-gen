// ADR-370, as a property of the shader tree rather than of a comment.
//
// "A leaf and the branch it fell from are reading one description of the air." That was a comment
// above a hand-maintained second copy of `windSampleAt` in `shaders/particles.wgsl` called
// `particleWindAt`, which had already lost `WindSample::phase` and which nothing tied to the
// original. ADR-385: a stated reason is not evidence. This is the evidence.
//
// The GPU half of the pair (`tests/rendering/test_wind_parity_gpu.cpp`) shows that the one
// remaining transliteration agrees with `core/wind.cpp` numerically, including `phase`. This half
// shows that it is the ONLY one: that every shader which samples the wind reaches the same
// function, byte for byte, and that no module in the tree carries a second copy of the arithmetic.
// Neither half is sufficient alone -- a correct function nobody calls, and a shared call site into
// a wrong function, are both green under one test and red under the pair.
//
// This needs no device, which is the point: the property is about text the compiler will be handed,
// and a rule that can only be checked with a GPU in the room is a rule nobody checks.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// AVGEN_SHADER_SOURCE_DIR is a PUBLIC definition of `avgen_gpu`, which the CPU suite does not
// link; AVGEN_SOURCE_DIR comes from `avgen_core` and reaches the same directory.
fs::path shaderDir() { return fs::path(AVGEN_SOURCE_DIR) / "shaders"; }

std::string readFile(const fs::path& p) {
    std::ifstream in(p);
    REQUIRE(in.good());
    std::stringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

// The `#include "file.wgsl"` convention of gpu/shader_library.cpp, and deliberately the same
// non-de-duplicating behaviour: a file included twice appears twice here too, because that is what
// the compiler would be handed and a duplicate definition is one of the things this test is for.
std::string resolve(const fs::path& file, int depth = 0) {
    REQUIRE(depth < 8);
    std::istringstream in(readFile(file));
    std::ostringstream out;
    std::string line;
    while (std::getline(in, line)) {
        const auto pos = line.find("#include");
        if (pos != std::string::npos && line.find_first_not_of(" \t") == pos) {
            const auto q1 = line.find('"', pos);
            const auto q2 = q1 == std::string::npos ? std::string::npos : line.find('"', q1 + 1);
            REQUIRE(q1 != std::string::npos);
            REQUIRE(q2 != std::string::npos);
            out << resolve(shaderDir() / line.substr(q1 + 1, q2 - q1 - 1), depth + 1) << '\n';
        } else {
            out << line << '\n';
        }
    }
    return out.str();
}

std::size_t countOf(const std::string& haystack, const std::string& needle) {
    std::size_t n = 0;
    for (std::size_t at = haystack.find(needle); at != std::string::npos;
         at = haystack.find(needle, at + needle.size())) {
        ++n;
    }
    return n;
}

// The body of a top-level `fn <name>`, by brace matching. Empty when the function is not there.
std::string functionBody(const std::string& source, const std::string& name) {
    const auto at = source.find("fn " + name + "(");
    if (at == std::string::npos) {
        return {};
    }
    const auto open = source.find('{', at);
    if (open == std::string::npos) {
        return {};
    }
    int depth = 0;
    for (std::size_t i = open; i < source.size(); ++i) {
        if (source[i] == '{') {
            ++depth;
        } else if (source[i] == '}') {
            if (--depth == 0) {
                return source.substr(open, i - open + 1);
            }
        }
    }
    return {};
}

// The regional wave's coefficients. Distinctive enough that it appears nowhere else in the tree,
// and early enough in `windSampleFrom` that any transliteration of the field would have to contain
// it. A fourth hand-written copy is a second occurrence of this string, whatever it is called.
constexpr const char* kFieldMarker = "0.94 * a + 0.34 * c";

std::vector<fs::path> everyShader() {
    std::vector<fs::path> out;
    for (const auto& e : fs::directory_iterator(shaderDir())) {
        if (e.path().extension() == ".wgsl") {
            out.push_back(e.path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

TEST_CASE("the wind field is transliterated exactly once in the whole shader tree", "[wind][shaders]") {
    const std::vector<fs::path> shaders = everyShader();
    REQUIRE(shaders.size() > 20); // the tree was found, rather than an empty directory iterated

    std::vector<std::string> carriers;
    for (const fs::path& p : shaders) {
        const std::string source = resolve(p);
        const std::size_t copies = countOf(source, kFieldMarker);
        INFO(p.filename().string() << " contains " << copies << " copies of the field arithmetic");
        // Zero for a shader that does not sample the wind; one for a shader that does. Two means
        // either a second hand-written copy -- the defect this test exists for -- or an include
        // pulled in twice, which the directive does not de-duplicate and which is a compile error
        // waiting for whoever adds the next one.
        CHECK(copies <= 1);
        if (copies == 1) {
            carriers.push_back(p.filename().string());
        }
    }

    // The neither-ran guard, and it is not a formality: `CHECK(copies <= 1)` passes trivially over a
    // tree in which the marker has been renamed, or in which the resolver silently returned nothing.
    INFO("shaders carrying the field: " << carriers.size());
    CHECK(carriers.size() >= 4);
}

TEST_CASE("every shader that samples the wind reaches the same function", "[wind][shaders]") {
    // The two ends of ADR-370's sentence: the tree that bends (`procedural.wgsl` and every mesh
    // draw, through common.wgsl -> wind.wgsl) and the leaf that falls from it (`particles.wgsl`,
    // which has no frame group and includes `wind_field.wgsl` directly).
    const std::string branch = resolve(shaderDir() / "procedural.wgsl");
    const std::string leaf = resolve(shaderDir() / "particles.wgsl");

    const std::string branchBody = functionBody(branch, "windSampleFrom");
    const std::string leafBody = functionBody(leaf, "windSampleFrom");

    // Not empty, and not a stub. Byte-identity between two things that are both absent is the
    // classic way this kind of test passes without testing anything (ADR-182), and the specific
    // member the deleted transliteration had dropped is named here on purpose: if `phase` ever
    // leaves the shared function, this fails rather than quietly agreeing about four numbers.
    INFO("windSampleFrom body length: branch " << branchBody.size() << ", leaf " << leafBody.size());
    REQUIRE(branchBody.size() > 600);
    REQUIRE(leafBody.find("out.phase") != std::string::npos);
    REQUIRE(leafBody.find("out.strength") != std::string::npos);
    REQUIRE(leafBody.find("out.gust") != std::string::npos);
    REQUIRE(leafBody.find("out.direction") != std::string::npos);

    CHECK(branchBody == leafBody);

    // ...and the comparison can fail. Perturb one arm by a single character and the same comparator
    // has to reject it, or "they are identical" means only that the comparator is blind.
    std::string perturbed = leafBody;
    const std::string term = "0.7 * a + 0.71 * c";
    REQUIRE(perturbed.find(term) != std::string::npos);
    perturbed.replace(perturbed.find(term), term.size(), "0.7 * a + 0.72 * c");
    CHECK(branchBody != perturbed);

    // Each arm defines it once. Two definitions in one module is a WGSL compile error, so this is
    // really a guard on the include graph: `common.wgsl` includes `wind.wgsl`, `wind.wgsl` includes
    // `wind_field.wgsl`, and the directive does not de-duplicate.
    CHECK(countOf(branch, "fn windSampleFrom(") == 1);
    CHECK(countOf(leaf, "fn windSampleFrom(") == 1);

    // The removed copy, named, so that restoring it is a test failure and not a review comment.
    CHECK(countOf(leaf, "fn particleWindAt(") == 0);
}
