#pragma once

// User shader contract (milestone 0.4, ADR-006 / ADR-014). A user shader is a WGSL file that
// starts with an ISF-style JSON header inside a block comment, followed by a body that defines
//
//     fn mainImage(uv: vec2<f32>, fragCoord: vec2<f32>) -> vec4<f32>
//
// The engine generates everything else: standard uniforms, the INPUTS uniform struct, texture
// bindings for the input image, pass targets and the audio spectrum, the vertex stage and the
// fragment entry point. Header example:
//
// /*{
//   "DESCRIPTION": "Plasma",
//   "INPUTS": [
//     {"NAME": "speed", "TYPE": "float", "DEFAULT": 1.0, "MIN": 0.0, "MAX": 10.0, "LABEL": "Speed"},
//     {"NAME": "tint",  "TYPE": "color", "DEFAULT": [1.0, 0.5, 0.2, 1.0]},
//     {"NAME": "center","TYPE": "point2D", "DEFAULT": [0.5, 0.5]},
//     {"NAME": "invert","TYPE": "bool", "DEFAULT": false},
//     {"NAME": "steps", "TYPE": "long", "DEFAULT": 4, "MIN": 1, "MAX": 16},
//     {"NAME": "flash", "TYPE": "event"}
//   ],
//   "PASSES": [
//     {"TARGET": "feedback", "PERSISTENT": true, "FLOAT": true, "WIDTH": "$WIDTH/2", "HEIGHT": "$HEIGHT/2"},
//     {}
//   ]
// }*/
//
// Generated WGSL (available to the body):
//   struct Std { time, timeDelta, frameIndex, passIndex: f32; renderSize, passSize: vec2<f32>;
//                audio: vec4<f32> (rms, bass, mid, treble); audio2: vec4<f32> (lowMid, highMid, onset, beatPhase);
//                beat: vec4<f32> (bpm, beatCount, barPhase, progress) }  @group(0) @binding(0) var<uniform> sys: Std;
//   struct Inputs { <one field per INPUT, in declared order> }             @group(0) @binding(1) var<uniform> inputs: Inputs;
//   @group(0) @binding(2) var linearSampler: sampler;   (clamp, linear, mipless)
//   @group(0) @binding(3) var inputImage: texture_2d<f32>;   (post stage: the scene; else 1x1 black)
//   @group(0) @binding(4) var audioSpectrum: texture_2d<f32>; (binCount x 1: r = log spectrum 0..1, g = linear magnitude)
//   @group(0) @binding(5 + i) var <TARGET i name>: texture_2d<f32>;   (every named pass target; persistent ones hold last frame)
// INPUT types map to: float -> f32, long -> i32, bool -> u32 (0/1), color -> vec4<f32>,
// point2D -> vec2<f32>, event -> f32 (1 for one frame). Layout follows WGSL uniform rules.

#include "core/error.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace avgen::shaders {

enum class InputType : std::uint8_t { Float, Long, Bool, Color, Point2D, Event };

struct InputDesc {
    std::string name;
    InputType type = InputType::Float;
    std::string label;                   // defaults to name
    std::vector<float> defaultValue;     // component count per type (1, 1, 1, 4, 2, 1)
    std::vector<float> minValue;         // empty = type default (float 0, long 0, point2D 0, color 0)
    std::vector<float> maxValue;         // empty = type default (float 1, long 10, point2D 1, color 1)
    [[nodiscard]] std::size_t componentCount() const;
    [[nodiscard]] std::size_t byteSize() const;   // WGSL size of the field
    [[nodiscard]] std::size_t alignment() const;  // WGSL alignment of the field
    [[nodiscard]] const char* wgslType() const;
};

struct PassDesc {
    std::string target;          // empty = the layer's output
    bool persistent = false;     // target keeps its contents across frames (feedback)
    bool floatFormat = false;    // RGBA16Float instead of RGBA8Unorm
    std::string widthExpr = "$WIDTH";   // "$WIDTH", "$HEIGHT", numbers, and "/", "*" with one operand
    std::string heightExpr = "$HEIGHT";
};

struct ShaderDescription {
    std::string name;            // from the file stem or "NAME"
    std::string description;
    std::string credit;
    std::vector<std::string> categories;
    std::vector<InputDesc> inputs;
    std::vector<PassDesc> passes; // at least one (a default output pass is added when empty)
};

struct ParsedShader {
    ShaderDescription description;
    std::string body;            // WGSL after the header
    std::string header;          // raw JSON text (for diagnostics)
    std::size_t bodyLineOffset = 0; // line number where the body starts (1-based line of first body line)
};

// Parses the header (a leading /*{ ... }*/ block; leading whitespace and // comments allowed).
// A file with no header is accepted as a shader with no inputs and one output pass.
Result<ParsedShader> parseShaderSource(const std::string& source, const std::string& name);
Result<ParsedShader> parseShaderFile(const std::filesystem::path& path);

// Uniform layout of the Inputs struct (WGSL rules: each field aligned to its alignment; struct
// size rounded up to 16). Offsets are in bytes.
struct InputsLayout {
    std::vector<std::size_t> offsets;
    std::size_t size = 16; // never 0 so an empty struct still binds
};
InputsLayout computeInputsLayout(const ShaderDescription& description);

// Writes component values (one vector per input, in order, sized by componentCount) into `out`
// using the layout. bool/event are written as u32/f32 respectively. `out` is resized.
void packInputs(const ShaderDescription& description, const InputsLayout& layout,
                const std::vector<std::vector<float>>& values, std::vector<std::uint8_t>& out);

// Generates the complete WGSL module for one pass: standard declarations, the Inputs struct,
// bindings for every target of `description`, the user body, and the entry points
// `vs_main` (fullscreen triangle) and `fs_main` (calls mainImage; flips uv so 0,0 is top-left).
// Every generated declaration precedes the body and the prologue ends with the line
// `// ---- user body ----`; its first line reads `... N prologue lines; body starts at line N+1`,
// so a compiler line L maps to body line L - N and to source line bodyLineOffset + (L - N) - 1.
std::string generateModuleSource(const ShaderDescription& description, const std::string& body);

// Resolves a pass size expression against the layer size. Errors on malformed expressions.
Result<std::uint32_t> evaluateSizeExpression(const std::string& expr, std::uint32_t width, std::uint32_t height);

// Standard uniform block mirrored in C++ (size 96, matches the generated `Std` struct).
struct StdUniforms {
    float time = 0.0f;
    float timeDelta = 0.0f;
    float frameIndex = 0.0f;
    float passIndex = 0.0f;
    float renderSize[2] = {1.0f, 1.0f};
    float passSize[2] = {1.0f, 1.0f};
    float audio[4] = {0, 0, 0, 0};
    float audio2[4] = {0, 0, 0, 0};
    float beat[4] = {0, 0, 0, 0};
    float pad[4] = {0, 0, 0, 0};
};
static_assert(sizeof(StdUniforms) == 96);

const char* inputTypeName(InputType type);
Result<InputType> inputTypeFromName(const std::string& name);

} // namespace avgen::shaders
