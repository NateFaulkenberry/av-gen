#include "scene/material_compiler.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <string_view>

namespace avgen::scene {
namespace {

// The WGSL function of each op kind, in MaterialOpKind order (the wire format, so the order is fixed).
constexpr std::array<std::string_view, 36> kKindFunctions{
    "matKInput",         "matKConstant",      "matKGradient",        "matKNoise",       "matKVoronoi",
    "matKFresnel",       "matKRamp",          "matKRemap",           "matKMultiply",    "matKAdd",
    "matKMix",           "matKMixby",         "matKPower",           "matKSmoothstep",  "matKThreshold",
    "matKHueshift",      "matKSaturate",      "matKPalette",         "matKField",       "matKTriplanar",
    "matKWorldProject",  "matKObjectProject", "matKHeightBlend",     "matKDetailNormal", "matKCurvatureMask",
    "matKEdgeWear",      "matKDecalBox",      "matKAnisotropy",      "matKRoughnessFilter", "matKMicroDetail",
    "matKSwizzle",       "matKVoronoiEdge",   "matKQuantize",        "matKOklabRamp",
    "matKTextureTriplanar", "matKTextureTriplanarNormal", // ADR-1642
};
static_assert(static_cast<std::size_t>(scene::MaterialOpKind::TextureTriplanarNormal) + 1 == kKindFunctions.size(),
              "a new op kind needs its matK function here and in shaders/material.wgsl");

constexpr int kMaxOps = scene::kMaxMaterialOps;
constexpr int kMaxLayers = scene::kMaxMaterialLayers;

bool inRange(int r) { return r >= 0 && r < scene::kMaterialRegisters; }

std::string reg(int r) { return fmt::format("r{}", r); }

void mix(std::uint64_t& h, std::uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
}

// The ops [first, first + count) as the interpreter's materialRunOps runs them: stop at the op table's end, skip an op
// with any register out of range.
void emitOps(std::string& out, const scene::MaterialProgramGpu& p, int first, int count) {
    for (int i = 0; i < std::min(count, kMaxOps); ++i) {
        const int oi = first + i;
        if (oi >= kMaxOps) {
            break;
        }
        const scene::MaterialOpGpu& op = p.ops[static_cast<std::size_t>(oi)];
        const glm::ivec4 r = op.registers;
        if (!inRange(r.x) || !inRange(r.y) || !inRange(r.z) || !inRange(r.w)) {
            continue;
        }
        if (op.kind == static_cast<std::uint32_t>(scene::MaterialOpKind::Input)) {
            out += fmt::format("    {} = materialInputValue({}u, local);\n", reg(r.x), op.input);
        } else if (op.kind < kKindFunctions.size()) {
            out += fmt::format("    {} = {}(pi, {}u, local, {}, {}, {}, fields);\n", reg(r.x), kKindFunctions[op.kind],
                               oi, reg(r.y), reg(r.z), reg(r.w));
        } else {
            out += fmt::format("    {} = vec4<f32>(0.0);\n", reg(r.x));
        }
    }
}

// ADR-1642: whether the program has a texture op the pre-pass samples.
bool usesTexture(const scene::MaterialProgramGpu& p) {
    for (int k = 0; k < scene::kMaxMaterialTextureOps; ++k) {
        if (p.textureOps[k] >= 0) {
            return true;
        }
    }
    return false;
}

bool usesField(const scene::MaterialProgramGpu& p) {
    for (const scene::MaterialOpGpu& op : p.ops) {
        if (op.kind == static_cast<std::uint32_t>(scene::MaterialOpKind::Field)) {
            return true;
        }
    }
    return false;
}

} // namespace

std::uint64_t materialStructureKey(std::span<const scene::MaterialProgramGpu> programs) {
    std::uint64_t h = 0xA5A5'1493'0000'0001ull;
    mix(h, programs.size());
    for (const scene::MaterialProgramGpu& p : programs) {
        for (int c = 0; c < 4; ++c) {
            mix(h, static_cast<std::uint32_t>(p.outputs[c]));
            mix(h, static_cast<std::uint32_t>(p.aux[c]));
        }
        for (int c = 0; c < 3; ++c) {
            mix(h, static_cast<std::uint32_t>(p.opacityCountPad[c]));
        }
        mix(h, usesTexture(p) ? 1u : 0u); // ADR-1642: the pre-pass is emitted or not
        for (const scene::MaterialLayerGpu& layer : p.layers) {
            for (int c = 0; c < 4; ++c) {
                mix(h, static_cast<std::uint32_t>(layer.outputs[c]));
                mix(h, static_cast<std::uint32_t>(layer.aux[c]));
                mix(h, static_cast<std::uint32_t>(layer.range[c]));
            }
        }
        for (const scene::MaterialOpGpu& op : p.ops) {
            mix(h, op.kind);
            mix(h, op.input);
            for (int c = 0; c < 4; ++c) {
                mix(h, static_cast<std::uint32_t>(op.registers[c]));
            }
        }
    }
    return h == 0 ? 1 : h;
}

std::string compileMaterialProgramWgsl(const scene::MaterialProgramGpu& p, int slot) {
    std::string out;
    out += fmt::format("fn matCompiled{}(ctx: MaterialContext, base: MaterialResult) -> MaterialResult {{\n", slot);
    out += fmt::format("    let pi = {}u;\n", slot);
    out += "    var r0 = vec4<f32>(0.0); var r1 = vec4<f32>(0.0); var r2 = vec4<f32>(0.0); var r3 = vec4<f32>(0.0);\n";
    out += "    var r4 = vec4<f32>(0.0); var r5 = vec4<f32>(0.0); var r6 = vec4<f32>(0.0); var r7 = vec4<f32>(0.0);\n";
    out += "    var local = ctx;\n";
    const std::string fieldsExpr = usesField(p) ? "materialFieldPrepass(pi, ctx.worldPosition)" : "matFieldsZero()";
    if (usesTexture(p)) { // ADR-1642: only where the pipeline samples program textures (MAT_PROGRAM_TEXTURES)
        out += fmt::format("    var fields = {};\n", fieldsExpr);
        out += "    if (MAT_PROGRAM_TEXTURES) { fields = materialTexturePrepass(pi, ctx, fields); }\n";
    } else {
        out += fmt::format("    let fields = {};\n", fieldsExpr);
    }
    emitOps(out, p, 0, std::max(p.opacityCountPad.y, 0));
    out += "    var result = base;\n";
    const auto emitIf = [&out](int r, const std::string& assignment) {
        if (inRange(r)) {
            out += fmt::format(fmt::runtime(assignment), reg(r));
        }
    };
    emitIf(p.outputs.x, "    result.baseColor = max({}.xyz, vec3<f32>(0.0));\n");
    emitIf(p.outputs.y, "    result.metallic = saturate({}.x);\n");
    emitIf(p.outputs.z, "    result.roughness = saturate({}.x);\n");
    emitIf(p.outputs.w, "    result.emission = {}.xyz * materialPrograms.programs[pi].emissionIntensityPad.x;\n");
    emitIf(p.opacityCountPad.x, "    result.opacity = saturate({}.x);\n");
    emitIf(p.aux.x, "    result.normal = matSafeNormalize({}.xyz, vec3<f32>(0.0, 0.0, 1.0));\n");
    emitIf(p.aux.y, "    result.occlusion = saturate({}.x);\n");
    emitIf(p.aux.z, "    result.height = {}.x;\n");

    const int layerCount = std::min(std::max(p.opacityCountPad.z, 0), kMaxLayers);
    for (int li = 0; li < layerCount; ++li) {
        const scene::MaterialLayerGpu& layer = p.layers[static_cast<std::size_t>(li)];
        out += fmt::format("    // layer {}\n    {{\n", li);
        out += fmt::format("    let layerParams = materialPrograms.programs[pi].layers[{}].params;\n", li);
        out += "    local.height = result.height;\n";
        emitOps(out, p, std::max(layer.range.x, 0), std::max(layer.range.y, 0));
        out += "    var mask = 1.0;\n";
        emitIf(layer.aux.z, "    mask = saturate({}.x);\n");
        out += "    var layerHeight = 0.0;\n";
        emitIf(layer.aux.w, "    layerHeight = {}.x;\n");
        out += "    let t = matHeightBlend(result.height, layerHeight, mask, layerParams.y);\n";
        emitIf(layer.outputs.x, "    result.baseColor = mix(result.baseColor, max({}.xyz, vec3<f32>(0.0)), t);\n");
        emitIf(layer.outputs.y, "    result.metallic = mix(result.metallic, saturate({}.x), t);\n");
        emitIf(layer.outputs.z, "    result.roughness = mix(result.roughness, saturate({}.x), t);\n");
        emitIf(layer.outputs.w, "    result.emission = mix(result.emission, {}.xyz * layerParams.x, t);\n");
        if (inRange(layer.aux.x)) {
            out += fmt::format("    let layerNormal = matSafeNormalize({}.xyz, vec3<f32>(0.0, 0.0, 1.0));\n",
                               reg(layer.aux.x));
            out += "    result.normal = matSafeNormalize(mix(result.normal, layerNormal, t), vec3<f32>(0.0, 0.0, 1.0));\n";
        }
        emitIf(layer.aux.y, "    result.occlusion = mix(result.occlusion, saturate({}.x), t);\n");
        out += "    result.height = mix(result.height, layerHeight, t);\n";
        out += "    }\n";
    }
    out += "    return result;\n}\n\n";
    return out;
}

std::string compiledMaterialLibraryWgsl(std::span<const scene::MaterialProgramGpu> programs) {
    std::string out = "// ADR-1493: compiled by scene/material_compiler.cpp for this scene's programs.\n";
    for (std::size_t i = 0; i < programs.size(); ++i) {
        out += compileMaterialProgramWgsl(programs[i], static_cast<int>(i));
    }
    out += "fn evaluateMaterialProgramFast(programIndex: i32, ctx: MaterialContext, base: MaterialResult) -> "
           "MaterialResult {\n";
    out += "    switch (programIndex) {\n";
    for (std::size_t i = 0; i < programs.size(); ++i) {
        out += fmt::format("        case {}: {{ return matCompiled{}(ctx, base); }}\n", i, i);
    }
    out += "        default: { return evaluateMaterialProgram(programIndex, ctx, base); }\n";
    out += "    }\n}\n";
    return out;
}

std::string spliceCompiledMaterials(const std::string& source, const std::string& library) {
    constexpr std::string_view kBegin = "// @@material-compiled-begin";
    constexpr std::string_view kEnd = "// @@material-compiled-end";
    const auto b = source.find(kBegin);
    const auto e = source.find(kEnd);
    if (b == std::string::npos || e == std::string::npos || e < b) {
        return {};
    }
    return source.substr(0, b) + library + source.substr(e + kEnd.size());
}

} // namespace avgen::scene
