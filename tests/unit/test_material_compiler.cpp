// ADR-1493: material programs compiled to straight-line WGSL -- the text, the structure key and the splice. No GPU;
// tests/rendering/test_material_compiler_gpu.cpp renders the compiled programs against the interpreter.

#include "scene/material_compiler.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace avgen;
using namespace avgen::scene;

namespace {

scene::MaterialOp op(scene::MaterialOpKind kind, int dst, int a = 0, int b = 0, int c = 0) {
    scene::MaterialOp o;
    o.kind = kind;
    o.dst = dst;
    o.srcA = a;
    o.srcB = b;
    o.srcC = c;
    return o;
}

scene::MaterialProgram program() {
    scene::MaterialProgram p;
    p.name = "p";
    scene::MaterialOp in = op(scene::MaterialOpKind::Input, 0);
    in.input = scene::MaterialInput::Normal;
    p.ops.push_back(in);
    p.ops.push_back(op(scene::MaterialOpKind::Ramp, 1, 0));
    p.ops.push_back(op(scene::MaterialOpKind::Multiply, 2, 1, 0));
    p.baseColorRegister = 2;
    p.roughnessRegister = 1;
    return p;
}

scene::MaterialProgramGpu pack(const scene::MaterialProgram& p) {
    return scene::packMaterialProgramWithSlots(p, {});
}

bool has(const std::string& text, const std::string& what) { return text.find(what) != std::string::npos; }

} // namespace

TEST_CASE("A compiled program calls each op's function on named registers (ADR-1493)", "[material][adr1493]") {
    const std::string wgsl = compileMaterialProgramWgsl(pack(program()), 3);
    CHECK(has(wgsl, "fn matCompiled3(ctx: MaterialContext, base: MaterialResult) -> MaterialResult {"));
    CHECK(has(wgsl, "let pi = 3u;"));
    CHECK(has(wgsl, "r0 = materialInputValue(2u, local);")); // the input is structure: a literal
    CHECK(has(wgsl, "r1 = matKRamp(pi, 1u, local, r0, r0, r0, fields);"));
    CHECK(has(wgsl, "r2 = matKMultiply(pi, 2u, local, r1, r0, r0, fields);"));
    CHECK(has(wgsl, "result.baseColor = max(r2.xyz, vec3<f32>(0.0));"));
    CHECK(has(wgsl, "result.roughness = saturate(r1.x);"));
    CHECK_FALSE(has(wgsl, "result.metallic")); // unwritten outputs keep the material's own value
    CHECK_FALSE(has(wgsl, "materialFieldPrepass")); // no Field op, no field samples
}

TEST_CASE("A compiled program skips what the interpreter skips and runs its layers (ADR-1493)", "[material][adr1493]") {
    scene::MaterialProgram p = program();
    p.ops.push_back(op(scene::MaterialOpKind::Add, 9, 0, 0)); // a register out of range: the interpreter skips it
    scene::MaterialLayer layer;
    layer.ops.push_back(op(scene::MaterialOpKind::Constant, 4));
    layer.maskRegister = 4;
    layer.emissionRegister = 4;
    p.layers.push_back(layer);
    scene::MaterialOp field = op(scene::MaterialOpKind::Field, 5);
    field.field = "f";
    p.ops.push_back(field);
    const scene::MaterialProgramGpu g = scene::packMaterialProgramWithSlots(p, {{"f", 2}});
    const std::string wgsl = compileMaterialProgramWgsl(g, 0);
    CHECK_FALSE(has(wgsl, "r9"));
    CHECK(has(wgsl, "let fields = materialFieldPrepass(pi, ctx.worldPosition);"));
    CHECK(has(wgsl, "matKField(pi,"));
    CHECK(has(wgsl, "// layer 0"));
    CHECK(has(wgsl, "local.height = result.height;"));
    CHECK(has(wgsl, "mask = saturate(r4.x);"));
    CHECK(has(wgsl, "result.emission = mix(result.emission, r4.xyz * layerParams.x, t);"));
}

TEST_CASE("The structure key follows the structure and ignores what a route moves (ADR-1493)", "[material][adr1493]") {
    std::vector<scene::MaterialProgramGpu> a{pack(program())};
    const std::uint64_t key = materialStructureKey(a);
    CHECK(key != 0);
    std::vector<scene::MaterialProgramGpu> moved = a;
    moved[0].ops[1].constant = glm::vec4(0.3f, 0.2f, 0.1f, 1.0f); // a route on a constant
    moved[0].ops[1].value = 7.0f;
    moved[0].emissionIntensityPad = glm::vec4(4.0f, 0.5f, 2.0f, 90.0f); // emission intensity and the gate's numbers
    moved[0].ops[1].constant2 = glm::vec4(0.9f);                       // a palette binding writes these
    moved[0].ops[1].constant3 = glm::vec4(0.1f);
    moved[0].ops[1].constant4 = glm::vec4(0.5f);
    moved[0].ops[1].seed = 99;
    moved[0].ops[1].fieldSlot = 7;
    moved[0].fieldSlots = glm::ivec4(3, -1, -1, -1); // a field set that moved its slots
    moved[0].opacityCountPad.w = 2;                  // the gate's lane
    moved[0].layers[0].params = glm::vec4(2.0f, 0.4f, 0.0f, 0.0f);
    moved[0].flags = glm::ivec4(3, 0, 0, 0);
    // Nothing a palette, a MIDI CC or a route writes is structure, so none of it rebuilds a variant.
    CHECK(materialStructureKey(moved) == key);
    std::vector<scene::MaterialProgramGpu> rekind = a;
    rekind[0].ops[2].kind = static_cast<std::uint32_t>(scene::MaterialOpKind::Add);
    CHECK(materialStructureKey(rekind) != key);
    std::vector<scene::MaterialProgramGpu> rewired = a;
    rewired[0].ops[2].registers.y = 0;
    CHECK(materialStructureKey(rewired) != key);
    std::vector<scene::MaterialProgramGpu> more = a;
    more.push_back(a[0]);
    CHECK(materialStructureKey(more) != key);
}

TEST_CASE("The library dispatches by slot with the interpreter as its default, spliced between the markers (ADR-1493)",
          "[material][adr1493]") {
    const std::vector<scene::MaterialProgramGpu> programs{pack(program()), pack(program())};
    const std::string library = compiledMaterialLibraryWgsl(programs);
    CHECK(has(library, "fn matCompiled0("));
    CHECK(has(library, "fn matCompiled1("));
    CHECK(has(library, "case 1: { return matCompiled1(ctx, base); }"));
    CHECK(has(library, "default: { return evaluateMaterialProgram(programIndex, ctx, base); }"));
    const std::string source = "a\n// @@material-compiled-begin\nold\n// @@material-compiled-end\nb\n";
    const std::string spliced = spliceCompiledMaterials(source, "NEW\n");
    CHECK(spliced == "a\nNEW\n\nb\n");
    CHECK(spliceCompiledMaterials("no markers", "NEW").empty());
}
