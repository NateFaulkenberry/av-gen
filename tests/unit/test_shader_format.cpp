#include "shaders/shader_format.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace avgen;
using namespace avgen::shaders;
using Catch::Matchers::ContainsSubstring;

namespace {

// The header example from shader_format.hpp, verbatim.
const std::string kExampleHeader = R"(/*{
  "DESCRIPTION": "Plasma",
  "INPUTS": [
    {"NAME": "speed", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 10.0, "LABEL": "Speed"},
    {"NAME": "tint",  "TYPE": "color", "DEFAULT": [1.0, 0.5, 0.2, 1.0]},
    {"NAME": "center","TYPE": "point2D", "DEFAULT": [0.5, 0.5]},
    {"NAME": "invert","TYPE": "bool", "DEFAULT": false},
    {"NAME": "steps", "TYPE": "long", "DEFAULT": 4, "MIN": 1, "MAX": 16},
    {"NAME": "flash", "TYPE": "event"}
  ],
  "PASSES": [
    {"TARGET": "feedback", "PERSISTENT": true, "FLOAT": true, "WIDTH": "$WIDTH/2", "HEIGHT": "$HEIGHT/2"},
    {}
  ]
}*/)";

const std::string kBody = R"(
fn mainImage(uv: vec2<f32>, fragCoord: vec2<f32>) -> vec4<f32> {
    return vec4<f32>(uv, 0.0, 1.0);
}
)";

std::string headerWithInputs(const std::string& inputsJson) {
    return "/*{ \"INPUTS\": [" + inputsJson + "] }*/" + kBody;
}

std::string headerWithPasses(const std::string& passesJson) {
    return "/*{ \"PASSES\": [" + passesJson + "] }*/" + kBody;
}

std::vector<float> floats(const std::vector<float>& v) {
    return v;
}

float readF32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    float v = 0.0f;
    std::memcpy(&v, bytes.data() + offset, sizeof(v));
    return v;
}

std::uint32_t readU32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    std::uint32_t v = 0;
    std::memcpy(&v, bytes.data() + offset, sizeof(v));
    return v;
}

std::int32_t readI32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    std::int32_t v = 0;
    std::memcpy(&v, bytes.data() + offset, sizeof(v));
    return v;
}

std::size_t countLines(const std::string& text) {
    std::size_t n = 0;
    for (const char c : text) {
        if (c == '\n') {
            ++n;
        }
    }
    return n;
}

} // namespace

TEST_CASE("parseShaderSource reads the documented example header", "[shaders]") {
    const auto parsed = parseShaderSource(kExampleHeader + kBody, "plasma");
    REQUIRE(parsed.has_value());
    const ShaderDescription& d = parsed->description;

    CHECK(d.name == "plasma"); // no NAME key: falls back to the argument
    CHECK(d.description == "Plasma");
    CHECK(d.credit.empty());
    CHECK(d.categories.empty());
    CHECK(parsed->header.front() == '{');
    CHECK(parsed->header.back() == '}');
    CHECK(parsed->body == kBody);

    REQUIRE(d.inputs.size() == 6);

    CHECK(d.inputs[0].name == "speed");
    CHECK(d.inputs[0].type == InputType::Float);
    CHECK(d.inputs[0].label == "Speed");
    CHECK(d.inputs[0].defaultValue == floats({1.0f}));
    CHECK(d.inputs[0].minValue == floats({0.0f}));
    CHECK(d.inputs[0].maxValue == floats({10.0f}));

    CHECK(d.inputs[1].name == "tint");
    CHECK(d.inputs[1].type == InputType::Color);
    CHECK(d.inputs[1].label == "tint"); // label defaults to the name
    CHECK(d.inputs[1].defaultValue == floats({1.0f, 0.5f, 0.2f, 1.0f}));
    CHECK(d.inputs[1].minValue == floats({0.0f, 0.0f, 0.0f, 0.0f}));
    CHECK(d.inputs[1].maxValue == floats({1.0f, 1.0f, 1.0f, 1.0f}));

    CHECK(d.inputs[2].name == "center");
    CHECK(d.inputs[2].type == InputType::Point2D);
    CHECK(d.inputs[2].defaultValue == floats({0.5f, 0.5f}));
    CHECK(d.inputs[2].minValue == floats({0.0f, 0.0f}));
    CHECK(d.inputs[2].maxValue == floats({1.0f, 1.0f}));

    CHECK(d.inputs[3].name == "invert");
    CHECK(d.inputs[3].type == InputType::Bool);
    CHECK(d.inputs[3].defaultValue == floats({0.0f}));
    CHECK(d.inputs[3].minValue == floats({0.0f}));
    CHECK(d.inputs[3].maxValue == floats({1.0f}));

    CHECK(d.inputs[4].name == "steps");
    CHECK(d.inputs[4].type == InputType::Long);
    CHECK(d.inputs[4].defaultValue == floats({4.0f}));
    CHECK(d.inputs[4].minValue == floats({1.0f}));
    CHECK(d.inputs[4].maxValue == floats({16.0f}));

    CHECK(d.inputs[5].name == "flash");
    CHECK(d.inputs[5].type == InputType::Event);
    CHECK(d.inputs[5].defaultValue == floats({0.0f}));

    REQUIRE(d.passes.size() == 2);
    CHECK(d.passes[0].target == "feedback");
    CHECK(d.passes[0].persistent);
    CHECK(d.passes[0].floatFormat);
    CHECK(d.passes[0].widthExpr == "$WIDTH/2");
    CHECK(d.passes[0].heightExpr == "$HEIGHT/2");
    CHECK(d.passes[1].target.empty()); // the explicit {} output pass
    CHECK_FALSE(d.passes[1].persistent);
    CHECK_FALSE(d.passes[1].floatFormat);
    CHECK(d.passes[1].widthExpr == "$WIDTH");
    CHECK(d.passes[1].heightExpr == "$HEIGHT");

    // The header spans 15 lines; the body begins right after "}*/" on line 15.
    CHECK(parsed->bodyLineOffset == countLines(kExampleHeader) + 1);
    CHECK(parsed->bodyLineOffset == 15);
}

TEST_CASE("parseShaderSource reads NAME, CREDIT and CATEGORIES", "[shaders]") {
    const std::string source = R"(/*{
  "NAME": "Fancy",
  "CREDIT": "someone",
  "CATEGORIES": ["Generator", "Audio Reactive"],
  "INPUTS": [{"NAME": "amount", "TYPE": "float"}]
}*/)" + kBody;
    const auto parsed = parseShaderSource(source, "file_stem");
    REQUIRE(parsed.has_value());
    CHECK(parsed->description.name == "Fancy");
    CHECK(parsed->description.credit == "someone");
    CHECK(parsed->description.categories == std::vector<std::string>{"Generator", "Audio Reactive"});
    REQUIRE(parsed->description.inputs.size() == 1);
    // Missing DEFAULT/MIN/MAX use the float type defaults.
    CHECK(parsed->description.inputs[0].defaultValue == floats({0.0f}));
    CHECK(parsed->description.inputs[0].minValue == floats({0.0f}));
    CHECK(parsed->description.inputs[0].maxValue == floats({1.0f}));
    // No PASSES: one default output pass.
    REQUIRE(parsed->description.passes.size() == 1);
    CHECK(parsed->description.passes[0].target.empty());
}

TEST_CASE("parseShaderSource appends an output pass when the last pass has a target", "[shaders]") {
    const auto parsed = parseShaderSource(
        headerWithPasses(R"({"TARGET": "buf", "WIDTH": 256, "HEIGHT": "$HEIGHT * 0.5"})"), "x");
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->description.passes.size() == 2);
    CHECK(parsed->description.passes[0].target == "buf");
    CHECK(parsed->description.passes[0].widthExpr == "256");
    CHECK(parsed->description.passes[0].heightExpr == "$HEIGHT * 0.5");
    CHECK(parsed->description.passes[1].target.empty());
}

TEST_CASE("parseShaderSource rejects an output pass that is not last", "[shaders]") {
    const auto parsed = parseShaderSource(headerWithPasses(R"({}, {"TARGET": "buf"})"), "x");
    REQUIRE_FALSE(parsed.has_value());
    CHECK_THAT(parsed.error().message, ContainsSubstring("not the last pass"));
}

TEST_CASE("parseShaderSource accepts a header-less body", "[shaders]") {
    const auto parsed = parseShaderSource(kBody, "plain");
    REQUIRE(parsed.has_value());
    CHECK(parsed->description.name == "plain");
    CHECK(parsed->description.inputs.empty());
    REQUIRE(parsed->description.passes.size() == 1);
    CHECK(parsed->description.passes[0].target.empty());
    CHECK(parsed->body == kBody);
    CHECK(parsed->header.empty());
    CHECK(parsed->bodyLineOffset == 1);
}

TEST_CASE("parseShaderSource skips leading whitespace and // comments before the header", "[shaders]") {
    const std::string source = "\n// a comment\n   // another\n\n" + kExampleHeader + kBody;
    const auto parsed = parseShaderSource(source, "x");
    REQUIRE(parsed.has_value());
    CHECK(parsed->description.inputs.size() == 6);
    CHECK(parsed->bodyLineOffset == 4 + 15);
}

TEST_CASE("parseShaderSource rejects a body without mainImage", "[shaders]") {
    const auto parsed = parseShaderSource(kExampleHeader + "\nfn other() -> f32 { return 1.0; }\n", "x");
    REQUIRE_FALSE(parsed.has_value());
    CHECK_THAT(parsed.error().message, ContainsSubstring("mainImage"));
    CHECK_THAT(parsed.error().message, ContainsSubstring("contract"));

    // A prefix match is not a definition.
    const auto prefixed = parseShaderSource("fn mainImageHelper() -> f32 { return 1.0; }\n", "x");
    CHECK_FALSE(prefixed.has_value());
}

TEST_CASE("parseShaderSource rejects invalid JSON with the shader name", "[shaders]") {
    const auto parsed = parseShaderSource("/*{ \"INPUTS\": [ }*/" + kBody, "broken_header");
    REQUIRE_FALSE(parsed.has_value());
    CHECK_THAT(parsed.error().message, ContainsSubstring("broken_header"));
    CHECK_THAT(parsed.error().message, ContainsSubstring("invalid JSON header"));

    const auto unterminated = parseShaderSource("/*{ \"INPUTS\": [] }\n" + kBody, "no_end");
    REQUIRE_FALSE(unterminated.has_value());
    CHECK_THAT(unterminated.error().message, ContainsSubstring("no_end"));
}

TEST_CASE("parseShaderSource validates input and target identifiers", "[shaders]") {
    SECTION("bad identifier characters") {
        const auto r = parseShaderSource(headerWithInputs(R"({"NAME": "my-speed", "TYPE": "float"})"), "x");
        REQUIRE_FALSE(r.has_value());
        CHECK_THAT(r.error().message, ContainsSubstring("my-speed"));
    }
    SECTION("leading digit") {
        const auto r = parseShaderSource(headerWithInputs(R"({"NAME": "1st", "TYPE": "float"})"), "x");
        CHECK_FALSE(r.has_value());
    }
    SECTION("WGSL keyword") {
        for (const char* keyword : {"fn", "var", "let", "struct", "sys", "std", "inputs"}) {
            const auto r = parseShaderSource(
                headerWithInputs(std::string("{\"NAME\": \"") + keyword + "\", \"TYPE\": \"float\"}"), "x");
            REQUIRE_FALSE(r.has_value());
            CHECK_THAT(r.error().message, ContainsSubstring(keyword));
        }
    }
    SECTION("duplicate input names") {
        const auto r = parseShaderSource(
            headerWithInputs(R"({"NAME": "a", "TYPE": "float"}, {"NAME": "a", "TYPE": "bool"})"), "x");
        REQUIRE_FALSE(r.has_value());
        CHECK_THAT(r.error().message, ContainsSubstring("duplicate input name 'a'"));
    }
    SECTION("duplicate targets") {
        const auto r = parseShaderSource(headerWithPasses(R"({"TARGET": "t"}, {"TARGET": "t"}, {})"), "x");
        REQUIRE_FALSE(r.has_value());
        CHECK_THAT(r.error().message, ContainsSubstring("duplicate pass target 't'"));
    }
    SECTION("target keyword") {
        const auto r = parseShaderSource(headerWithPasses(R"({"TARGET": "inputImage"}, {})"), "x");
        CHECK_FALSE(r.has_value());
    }
    SECTION("missing NAME or TYPE") {
        CHECK_FALSE(parseShaderSource(headerWithInputs(R"({"TYPE": "float"})"), "x").has_value());
        CHECK_FALSE(parseShaderSource(headerWithInputs(R"({"NAME": "a"})"), "x").has_value());
        CHECK_FALSE(
            parseShaderSource(headerWithInputs(R"({"NAME": "a", "TYPE": "image"})"), "x").has_value());
        CHECK_FALSE(
            parseShaderSource(headerWithInputs(R"({"NAME": "a", "TYPE": "Float"})"), "x").has_value());
    }
}

TEST_CASE("parseShaderSource rejects component-count mismatches", "[shaders]") {
    const auto color =
        parseShaderSource(headerWithInputs(R"({"NAME": "c", "TYPE": "color", "DEFAULT": [1.0, 0.5]})"), "x");
    REQUIRE_FALSE(color.has_value());
    CHECK_THAT(color.error().message, ContainsSubstring("expected 4"));

    const auto point =
        parseShaderSource(headerWithInputs(R"({"NAME": "p", "TYPE": "point2D", "MAX": [1, 2, 3]})"), "x");
    REQUIRE_FALSE(point.has_value());
    CHECK_THAT(point.error().message, ContainsSubstring("expected 2"));

    const auto scalarArray =
        parseShaderSource(headerWithInputs(R"({"NAME": "f", "TYPE": "float", "DEFAULT": [1, 2]})"), "x");
    CHECK_FALSE(scalarArray.has_value());

    const auto text =
        parseShaderSource(headerWithInputs(R"({"NAME": "f", "TYPE": "float", "DEFAULT": "one"})"), "x");
    CHECK_FALSE(text.has_value());

    // A scalar is broadcast; lower-case point2d is accepted.
    const auto broadcast =
        parseShaderSource(headerWithInputs(R"({"NAME": "p", "TYPE": "point2d", "MAX": 4})"), "x");
    REQUIRE(broadcast.has_value());
    CHECK(broadcast->description.inputs[0].type == InputType::Point2D);
    CHECK(broadcast->description.inputs[0].maxValue == floats({4.0f, 4.0f}));
}

TEST_CASE("InputDesc reports WGSL sizes, alignments and types", "[shaders]") {
    const auto check = [](InputType type, std::size_t count, std::size_t size, std::size_t align,
                          const char* wgsl) {
        const InputDesc desc{.type = type};
        CHECK(desc.componentCount() == count);
        CHECK(desc.byteSize() == size);
        CHECK(desc.alignment() == align);
        CHECK(std::string(desc.wgslType()) == wgsl);
    };
    check(InputType::Float, 1, 4, 4, "f32");
    check(InputType::Long, 1, 4, 4, "i32");
    check(InputType::Bool, 1, 4, 4, "u32");
    check(InputType::Color, 4, 16, 16, "vec4<f32>");
    check(InputType::Point2D, 2, 8, 8, "vec2<f32>");
    check(InputType::Event, 1, 4, 4, "f32");
}

TEST_CASE("computeInputsLayout follows WGSL uniform rules", "[shaders]") {
    ShaderDescription d;
    d.inputs = {
        InputDesc{.name = "a", .type = InputType::Float}, InputDesc{.name = "b", .type = InputType::Color},
        InputDesc{.name = "c", .type = InputType::Point2D}, InputDesc{.name = "d", .type = InputType::Bool},
        InputDesc{.name = "e", .type = InputType::Float}};
    const InputsLayout layout = computeInputsLayout(d);
    CHECK(layout.offsets == std::vector<std::size_t>{0, 16, 32, 40, 44});
    CHECK(layout.size == 48);

    const InputsLayout empty = computeInputsLayout(ShaderDescription{});
    CHECK(empty.offsets.empty());
    CHECK(empty.size == 16);

    // A single float still rounds up to 16.
    ShaderDescription one;
    one.inputs = {InputDesc{.name = "a", .type = InputType::Float}};
    CHECK(computeInputsLayout(one).size == 16);

    // The documented example: f32, vec4, vec2, u32, i32, f32.
    const auto example = parseShaderSource(kExampleHeader + kBody, "plasma");
    REQUIRE(example.has_value());
    const InputsLayout exampleLayout = computeInputsLayout(example->description);
    CHECK(exampleLayout.offsets == std::vector<std::size_t>{0, 16, 32, 40, 44, 48});
    CHECK(exampleLayout.size == 64);
}

TEST_CASE("packInputs writes typed components at their offsets", "[shaders]") {
    ShaderDescription d;
    d.inputs = {
        InputDesc{.name = "a", .type = InputType::Float},   InputDesc{.name = "b", .type = InputType::Color},
        InputDesc{.name = "c", .type = InputType::Point2D}, InputDesc{.name = "d", .type = InputType::Bool},
        InputDesc{.name = "e", .type = InputType::Long},    InputDesc{.name = "f", .type = InputType::Event}};
    const InputsLayout layout = computeInputsLayout(d);
    REQUIRE(layout.offsets == std::vector<std::size_t>{0, 16, 32, 40, 44, 48});
    REQUIRE(layout.size == 64);

    std::vector<std::uint8_t> bytes(3, 0xFF); // resized by packInputs
    packInputs(d, layout, {{1.5f}, {0.1f, 0.2f, 0.3f, 0.4f}, {0.25f, 0.75f}, {0.7f}, {3.6f}, {1.0f}}, bytes);
    REQUIRE(bytes.size() == 64);
    CHECK(readF32(bytes, 0) == 1.5f);
    CHECK(readF32(bytes, 16) == 0.1f);
    CHECK(readF32(bytes, 20) == 0.2f);
    CHECK(readF32(bytes, 24) == 0.3f);
    CHECK(readF32(bytes, 28) == 0.4f);
    CHECK(readF32(bytes, 32) == 0.25f);
    CHECK(readF32(bytes, 36) == 0.75f);
    CHECK(readU32(bytes, 40) == 1u);
    CHECK(readI32(bytes, 44) == 4);
    CHECK(readF32(bytes, 48) == 1.0f);
    // Padding between the float and the color is zeroed.
    CHECK(readU32(bytes, 4) == 0u);
    CHECK(readU32(bytes, 60) == 0u);

    SECTION("bool threshold and long rounding") {
        packInputs(d, layout, {{0.0f}, {}, {}, {0.49f}, {-2.5f}, {}}, bytes);
        CHECK(readU32(bytes, 40) == 0u);
        CHECK(readI32(bytes, 44) == -3); // std::round: half away from zero
    }
    SECTION("missing and short value vectors are zeros") {
        packInputs(d, layout, {{2.0f}, {0.9f}}, bytes);
        CHECK(readF32(bytes, 0) == 2.0f);
        CHECK(readF32(bytes, 16) == 0.9f);
        CHECK(readF32(bytes, 20) == 0.0f);
        CHECK(readF32(bytes, 32) == 0.0f);
        CHECK(readU32(bytes, 40) == 0u);
        CHECK(readI32(bytes, 44) == 0);
    }
}

TEST_CASE("generateModuleSource emits the binding contract in order", "[shaders]") {
    const auto parsed = parseShaderSource(kExampleHeader + kBody, "plasma");
    REQUIRE(parsed.has_value());
    const std::string module = generateModuleSource(parsed->description, parsed->body);

    // Declarations appear in binding order and before the marker; the body follows the marker.
    const std::vector<std::string> expected = {
        "struct Std {",
        "    time: f32,",
        "    timeDelta: f32,",
        "    frameIndex: f32,",
        "    passIndex: f32,",
        "    renderSize: vec2<f32>,",
        "    passSize: vec2<f32>,",
        "    audio: vec4<f32>,",
        "    audio2: vec4<f32>,",
        "    beat: vec4<f32>,",
        "    pad: vec4<f32>,",
        "@group(0) @binding(0) var<uniform> sys: Std;",
        "struct Inputs {",
        "    speed: f32,",
        "    tint: vec4<f32>,",
        "    center: vec2<f32>,",
        "    invert: u32,",
        "    steps: i32,",
        "    flash: f32,",
        "@group(0) @binding(1) var<uniform> inputs: Inputs;",
        "@group(0) @binding(2) var linearSampler: sampler;",
        "@group(0) @binding(3) var inputImage: texture_2d<f32>;",
        "@group(0) @binding(4) var audioSpectrum: texture_2d<f32>;",
        "@group(0) @binding(5) var feedback: texture_2d<f32>;",
        "// ---- user body ----",
        "fn mainImage(uv: vec2<f32>, fragCoord: vec2<f32>) -> vec4<f32> {",
        "@vertex fn vs_main(@builtin(vertex_index) i: u32) -> VsOut",
        "@fragment fn fs_main(in: VsOut) -> @location(0) vec4<f32>",
        "return mainImage(in.uv, in.uv * sys.passSize);",
    };
    std::size_t cursor = 0;
    for (const std::string& needle : expected) {
        INFO("looking for: " << needle);
        const std::size_t at = module.find(needle, cursor);
        REQUIRE(at != std::string::npos);
        cursor = at + needle.size();
    }
    CHECK(module.find("_pad0") == std::string::npos);
    CHECK(module.find("@binding(6)") == std::string::npos);
    CHECK_THAT(module, ContainsSubstring("1.0 - (p.y * 0.5 + 0.5)")); // uv y flipped

    // The prologue comment states the body's first line, and the marker sits on the line before.
    const std::string firstLine = module.substr(0, module.find('\n'));
    CHECK_THAT(firstLine, ContainsSubstring("body starts at line"));
    const std::size_t marker = module.find("// ---- user body ----\n");
    REQUIRE(marker != std::string::npos);
    const std::size_t prologueLines = countLines(module.substr(0, marker)) + 1;
    CHECK_THAT(firstLine, ContainsSubstring(std::to_string(prologueLines) + " prologue lines"));
    CHECK_THAT(firstLine, ContainsSubstring("body starts at line " + std::to_string(prologueLines + 1)));
    // 1 comment + 12 (Std) + 1 + 8 (Inputs) + 1 + 4 bindings + 1 target + marker = 28.
    CHECK(prologueLines == 28);
    // The body begins immediately after the marker line, verbatim.
    CHECK(module.compare(marker + std::strlen("// ---- user body ----\n"), parsed->body.size(),
                         parsed->body) == 0);
}

TEST_CASE("generateModuleSource pads an empty Inputs struct and numbers targets", "[shaders]") {
    ShaderDescription d;
    d.name = "multi";
    d.passes = {PassDesc{.target = "a"}, PassDesc{.target = "b"}, PassDesc{}};
    const std::string module = generateModuleSource(
        d, "fn mainImage(uv: vec2<f32>, fragCoord: vec2<f32>) -> vec4<f32> { return vec4<f32>(1.0); }");
    CHECK_THAT(module, ContainsSubstring("struct Inputs {\n    _pad0: vec4<f32>,\n};"));
    CHECK_THAT(module, ContainsSubstring("@group(0) @binding(5) var a: texture_2d<f32>;\n@group(0) "
                                         "@binding(6) var b: texture_2d<f32>;\n// ---- user body ----\n"));
    // A body without a trailing newline still gets the entry points on their own lines.
    CHECK_THAT(module, ContainsSubstring("return vec4<f32>(1.0); }\n// ---- entry points ----\n"));
}

TEST_CASE("Std struct field order matches StdUniforms", "[shaders]") {
    // Offsets of the C++ mirror must match the WGSL layout: 4 f32, 2 vec2, 4 vec4 = 96 bytes.
    static_assert(offsetof(StdUniforms, time) == 0);
    static_assert(offsetof(StdUniforms, timeDelta) == 4);
    static_assert(offsetof(StdUniforms, frameIndex) == 8);
    static_assert(offsetof(StdUniforms, passIndex) == 12);
    static_assert(offsetof(StdUniforms, renderSize) == 16);
    static_assert(offsetof(StdUniforms, passSize) == 24);
    static_assert(offsetof(StdUniforms, audio) == 32);
    static_assert(offsetof(StdUniforms, audio2) == 48);
    static_assert(offsetof(StdUniforms, beat) == 64);
    static_assert(offsetof(StdUniforms, pad) == 80);
    static_assert(sizeof(StdUniforms) == 96);

    const std::string module = generateModuleSource(ShaderDescription{}, kBody);
    const std::size_t structStart = module.find("struct Std {");
    const std::size_t structEnd = module.find("};", structStart);
    REQUIRE(structStart != std::string::npos);
    REQUIRE(structEnd != std::string::npos);
    const std::string stdStruct = module.substr(structStart, structEnd - structStart);
    const std::vector<std::string> fields = {
        "time: f32",        "timeDelta: f32",        "frameIndex: f32",
        "passIndex: f32",   "renderSize: vec2<f32>", "passSize: vec2<f32>",
        "audio: vec4<f32>", "audio2: vec4<f32>",     "beat: vec4<f32>",
        "pad: vec4<f32>"};
    std::size_t cursor = 0;
    for (const std::string& field : fields) {
        const std::size_t at = stdStruct.find(field, cursor);
        REQUIRE(at != std::string::npos);
        cursor = at + field.size();
    }
    CHECK(countLines(stdStruct) == fields.size() + 1);
}

TEST_CASE("evaluateSizeExpression handles literals, variables and one operator", "[shaders]") {
    CHECK(evaluateSizeExpression("$WIDTH/2", 1280, 720).value() == 640);
    CHECK(evaluateSizeExpression("$HEIGHT*0.5", 1280, 720).value() == 360);
    CHECK(evaluateSizeExpression("$HEIGHT * 0.5", 1280, 720).value() == 360);
    CHECK(evaluateSizeExpression(" $WIDTH ", 1280, 720).value() == 1280);
    CHECK(evaluateSizeExpression("$HEIGHT", 1280, 720).value() == 720);
    CHECK(evaluateSizeExpression("256", 1280, 720).value() == 256);
    CHECK(evaluateSizeExpression("2 * $WIDTH", 1280, 720).value() == 2560);
    CHECK(evaluateSizeExpression("$WIDTH / 3", 1000, 720).value() == 333);
    CHECK(evaluateSizeExpression("$WIDTH / 1.5", 1001, 720).value() == 667); // rounded, not truncated

    SECTION("clamping") {
        CHECK(evaluateSizeExpression("$WIDTH / 10000", 1280, 720).value() == 1);
        CHECK(evaluateSizeExpression("0", 1280, 720).value() == 1);
        CHECK(evaluateSizeExpression("$WIDTH * 100", 1280, 720).value() == 16384);
        CHECK(evaluateSizeExpression("99999", 1280, 720).value() == 16384);
    }
    SECTION("malformed") {
        for (const char* bad : {"", "   ", "$DEPTH", "$WIDTH/", "/2", "$WIDTH + 2", "$WIDTH - 2", "width/2",
                                "$WIDTH/2/2", "abc", "1.2.3", "$WIDTH/0", "-256", "$WIDTH / -2"}) {
            INFO("expression: '" << bad << "'");
            CHECK_FALSE(evaluateSizeExpression(bad, 1280, 720).has_value());
        }
    }
    SECTION("malformed pass sizes are rejected at parse time") {
        const auto r =
            parseShaderSource(headerWithPasses(R"({"TARGET": "t", "WIDTH": "$WIDTH + 1"}, {})"), "x");
        REQUIRE_FALSE(r.has_value());
        CHECK_THAT(r.error().message, ContainsSubstring("WIDTH"));
    }
}

TEST_CASE("bodyLineOffset counts header lines", "[shaders]") {
    SECTION("body on the line after the header") {
        const std::string source =
            "/*{\n  \"INPUTS\": []\n}*/\n" + std::string("fn mainImage(uv: vec2<f32>, fragCoord: vec2<f32>) "
                                                         "-> vec4<f32> { return vec4<f32>(0.0); }\n");
        const auto parsed = parseShaderSource(source, "x");
        REQUIRE(parsed.has_value());
        // The body starts right after "}*/" on line 3 (its first line is the empty remainder).
        CHECK(parsed->bodyLineOffset == 3);
        CHECK(parsed->body.front() == '\n');
    }
    SECTION("single-line header") {
        const auto parsed = parseShaderSource("/*{}*/ fn mainImage(uv: vec2<f32>, fragCoord: vec2<f32>) -> "
                                              "vec4<f32> { return vec4<f32>(0.0); }",
                                              "x");
        REQUIRE(parsed.has_value());
        CHECK(parsed->bodyLineOffset == 1);
    }
    SECTION("ten-line header") {
        std::string header = "/*{\n  \"INPUTS\": [\n";
        for (int i = 0; i < 6; ++i) {
            header += "    {\"NAME\": \"v" + std::to_string(i) + "\", \"TYPE\": \"float\"}" +
                      (i < 5 ? ",\n" : "\n");
        }
        header += "  ]\n}*/";
        REQUIRE(countLines(header) == 9);
        const auto parsed = parseShaderSource(header + kBody, "x");
        REQUIRE(parsed.has_value());
        CHECK(parsed->bodyLineOffset == 10);
        CHECK(parsed->description.inputs.size() == 6);
    }
}

TEST_CASE("parseShaderFile reads a file and names it by its stem", "[shaders]") {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "avgen_test_shader_format_swirl.wgsl";
    {
        std::ofstream out(path, std::ios::binary);
        REQUIRE(out.good());
        out << kExampleHeader << kBody;
    }
    const auto parsed = parseShaderFile(path);
    std::filesystem::remove(path);
    REQUIRE(parsed.has_value());
    CHECK(parsed->description.name == "avgen_test_shader_format_swirl");
    CHECK(parsed->description.inputs.size() == 6);
    CHECK(parsed->description.passes.size() == 2);

    const auto missing = parseShaderFile(std::filesystem::temp_directory_path() /
                                         "avgen_test_shader_format_does_not_exist.wgsl");
    REQUIRE_FALSE(missing.has_value());
    CHECK_THAT(missing.error().message, ContainsSubstring("does_not_exist"));
}

TEST_CASE("input type names round-trip", "[shaders]") {
    for (const InputType type : {InputType::Float, InputType::Long, InputType::Bool, InputType::Color,
                                 InputType::Point2D, InputType::Event}) {
        const auto back = inputTypeFromName(inputTypeName(type));
        REQUIRE(back.has_value());
        CHECK(*back == type);
    }
    CHECK(std::string(inputTypeName(InputType::Point2D)) == "point2D");
    CHECK(inputTypeFromName("point2d").value() == InputType::Point2D);
    CHECK_FALSE(inputTypeFromName("POINT2D").has_value());
    CHECK_FALSE(inputTypeFromName("image").has_value());
    CHECK_FALSE(inputTypeFromName("").has_value());
}

TEST_CASE("the bundled example shaders satisfy the contract", "[shaders]") {
    // tests/unit/<this file> -> repository root -> shaders/examples.
    const std::filesystem::path examples =
        std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() / "shaders" / "examples";
    if (!std::filesystem::is_directory(examples)) {
        SKIP("shaders/examples not found next to the test sources");
    }

    const auto plasma = parseShaderFile(examples / "plasma.wgsl");
    REQUIRE(plasma.has_value());
    CHECK(plasma->description.name == "Plasma");
    CHECK(plasma->description.inputs.size() == 4);
    CHECK(plasma->description.passes.size() == 1);
    CHECK_THAT(generateModuleSource(plasma->description, plasma->body),
               ContainsSubstring("tint: vec4<f32>,"));

    const auto feedback = parseShaderFile(examples / "feedback.wgsl");
    REQUIRE(feedback.has_value());
    CHECK(feedback->description.inputs.size() == 5);
    REQUIRE(feedback->description.passes.size() == 2);
    CHECK(feedback->description.passes[0].target == "trail");
    CHECK(feedback->description.passes[0].persistent);
    CHECK(feedback->description.passes[0].floatFormat);
    CHECK(evaluateSizeExpression(feedback->description.passes[0].widthExpr, 1920, 1080).value() == 960);
    CHECK(feedback->description.passes[1].target.empty());
    CHECK_THAT(generateModuleSource(feedback->description, feedback->body),
               ContainsSubstring("@group(0) @binding(5) var trail: texture_2d<f32>;"));
}
