// ADR-1561: the procedural vertex stage calls the field evaluator from few places.
//
// The Metal compiler inlines a whole copy of a function at every call site. fields.wgsl's evaluator (every field
// kind) is large, and procedural.wgsl used to call it from each deformer branch of each of the four chain
// evaluations a lit vertex makes, plus the point path and the fiber pulls -- about twenty copies. A cold compile of
// the lit procedural pipeline took 126 s on an M2 Max, and the depth pipeline 88 s; the owner's first launch after
// a shader change sat on a blank window for minutes. After the restructure they compile in 23 s and 19 s.
//
// A compile-time budget cannot be tested without a GPU and minutes of wall time, so this guards the shape that
// makes it cheap: the number of call sites of the large functions, counted in the source text. A new call site of
// one of them is not wrong, but it costs a copy of the evaluator in every procedural pipeline -- route it through the
// existing one (fieldDeformerSample, deformRun, fiberPull) or raise the number here knowing what it costs.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

namespace {

#if defined(AVGEN_SHADER_SOURCE_DIR)
std::filesystem::path shaderDir() { return std::filesystem::path(AVGEN_SHADER_SOURCE_DIR); }
#else
std::filesystem::path shaderDir() { return std::filesystem::path(AVGEN_SOURCE_DIR) / "shaders"; }
#endif

std::string readStripped(const std::filesystem::path& path) {
    std::ifstream in(path);
    REQUIRE(in.good());
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text.compare(i, 2, "//") == 0) {
            while (i < text.size() && text[i] != '\n') {
                ++i;
            }
        }
        if (i < text.size()) {
            out += text[i];
        }
    }
    return out;
}

// Calls of `name(` that are not its declaration (`fn name(`).
int callSites(const std::string& text, const std::string& name) {
    const std::regex call("(^|[^A-Za-z0-9_])" + name + "\\(");
    int n = 0;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), call); it != std::sregex_iterator(); ++it) {
        const std::size_t name0 = static_cast<std::size_t>(it->position(0) + it->length(1));
        if (name0 >= 3 && text.compare(name0 - 3, 3, "fn ") == 0) {
            continue;
        }
        ++n;
    }
    return n;
}

} // namespace

TEST_CASE("the procedural shader calls the field evaluator and the deformer chain from few places",
          "[adr1561][shaders]") {
    const std::string proc = readStripped(shaderDir() / "procedural.wgsl");
    // One vector and one scalar call in fieldDeformerSample, one vector call in fiberPull, and the fragment
    // stage's emissive field.
    CHECK(callSites(proc, "fieldVector") == 2);
    CHECK(callSites(proc, "fieldScalar") == 2);
    // The deformer chain: deformChain from the vertex's point loop and the wire stage, deformWorld from the point
    // path's loop and the fiber vertex, deformRun from those two wrappers only.
    CHECK(callSites(proc, "deformRun") == 2);
    CHECK(callSites(proc, "deformChain") == 2);
    CHECK(callSites(proc, "deformWorld") == 2);
    CHECK(callSites(proc, "fiberPull") == 1);
    CHECK(callSites(proc, "fiberTurn") == 1);
    // The field deformers' arithmetic never samples on its own.
    CHECK(callSites(proc, "fieldDeformerSample") == 1);
}
