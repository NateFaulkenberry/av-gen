#include "scene/material_program.hpp"

#include "core/color.hpp"
#include "core/noise.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace avgen;
using namespace avgen::scene;
using Catch::Matchers::WithinAbs;

namespace {

double d(float v) {
    return static_cast<double>(v);
}

void checkVec3(const glm::vec3& v, const glm::vec3& expected, double tol = 1e-5) {
    CHECK_THAT(d(v.x), WithinAbs(d(expected.x), tol));
    CHECK_THAT(d(v.y), WithinAbs(d(expected.y), tol));
    CHECK_THAT(d(v.z), WithinAbs(d(expected.z), tol));
}

void checkVec4(const glm::vec4& v, const glm::vec4& expected, double tol = 1e-5) {
    CHECK_THAT(d(v.x), WithinAbs(d(expected.x), tol));
    CHECK_THAT(d(v.y), WithinAbs(d(expected.y), tol));
    CHECK_THAT(d(v.z), WithinAbs(d(expected.z), tol));
    CHECK_THAT(d(v.w), WithinAbs(d(expected.w), tol));
}

MaterialOp makeOp(MaterialOpKind kind, int dst, int srcA = 0, int srcB = 0, int srcC = 0) {
    MaterialOp op;
    op.kind = kind;
    op.dst = dst;
    op.srcA = srcA;
    op.srcB = srcB;
    op.srcC = srcC;
    return op;
}

MaterialOp constantOp(int dst, const glm::vec4& k) {
    MaterialOp op = makeOp(MaterialOpKind::Constant, dst);
    op.constant = k;
    return op;
}

MaterialOp inputOp(int dst, MaterialInput input) {
    MaterialOp op = makeOp(MaterialOpKind::Input, dst);
    op.input = input;
    return op;
}

MaterialResult run(const MaterialProgram& program, const MaterialContext& ctx = {},
                   const MaterialResult& base = {}) {
    REQUIRE(program.validate().has_value());
    return evaluateMaterialProgram(program, ctx, base);
}

// Evaluates a single op after `setup` ops and returns its destination register.
glm::vec4 evalOp(std::vector<MaterialOp> setup, const MaterialOp& op, const MaterialContext& ctx = {}) {
    MaterialProgram p;
    p.ops = std::move(setup);
    p.ops.push_back(op);
    return run(p, ctx).registers[static_cast<std::size_t>(op.dst)];
}

MaterialContext richContext() {
    MaterialContext ctx;
    ctx.worldPosition = {1.0f, 2.0f, 3.0f};
    ctx.localPosition = {0.1f, 0.2f, 0.3f};
    ctx.normal = {0.0f, 0.0f, 1.0f};
    ctx.uv = {0.25f, 0.75f};
    ctx.objectId = 7.0f;
    ctx.instanceIndex = 0.5f;
    ctx.instanceId = 42.0f;
    ctx.instanceRandom = {0.1f, 0.2f, 0.3f, 0.4f};
    ctx.instanceColor = {0.9f, 0.8f, 0.7f, 0.6f};
    ctx.instanceEmissive = {0.5f, 0.4f, 0.3f, 0.2f};
    ctx.time = 12.5f;
    ctx.audio = {0.6f, 0.7f, 0.8f, 0.9f};
    ctx.audioBands = {0.11f, 0.22f, 0.33f, 0.44f};
    ctx.beat = {0.25f, 1.0f, 0.5f, 0.125f};
    ctx.viewDirection = {0.0f, 0.0f, 1.0f};
    ctx.depth = 3.5f;
    return ctx;
}

struct TestFields : MaterialFieldSampler {
    [[nodiscard]] glm::vec4 sample(std::string_view field, const glm::vec3& p) const override {
        if (field == "heat") {
            return glm::vec4(p.x + p.y + p.z); // scalar field broadcast
        }
        if (field == "wind") {
            return glm::vec4(p, 0.0f);
        }
        return glm::vec4(0.0f);
    }
};

} // namespace

// ---- names ------------------------------------------------------------------------------------

TEST_CASE("Material op kinds and inputs have JSON names that round trip", "[material]") {
    const MaterialOpKind kinds[] = {
        MaterialOpKind::Input,    MaterialOpKind::Constant,   MaterialOpKind::Gradient,  MaterialOpKind::Noise,
        MaterialOpKind::Voronoi,  MaterialOpKind::Fresnel,    MaterialOpKind::Ramp,      MaterialOpKind::Remap,
        MaterialOpKind::Multiply, MaterialOpKind::Add,        MaterialOpKind::Mix,       MaterialOpKind::MixBy,
        MaterialOpKind::Power,    MaterialOpKind::Smoothstep, MaterialOpKind::Threshold, MaterialOpKind::HueShift,
        MaterialOpKind::Saturate, MaterialOpKind::Palette,    MaterialOpKind::Field,
    };
    for (const MaterialOpKind kind : kinds) {
        const auto back = materialOpKindFromName(materialOpKindName(kind));
        REQUIRE(back.has_value());
        CHECK(*back == kind);
    }
    CHECK(std::string(materialOpKindName(MaterialOpKind::MixBy)) == "mixBy");
    CHECK(std::string(materialOpKindName(MaterialOpKind::HueShift)) == "hueShift");
    CHECK_FALSE(materialOpKindFromName("HueShift").has_value());
    CHECK_FALSE(materialOpKindFromName("").has_value());

    const MaterialInput inputs[] = {
        MaterialInput::WorldPosition, MaterialInput::LocalPosition, MaterialInput::Normal,
        MaterialInput::Uv,            MaterialInput::ObjectId,      MaterialInput::InstanceIndex,
        MaterialInput::InstanceId,    MaterialInput::InstanceRandom, MaterialInput::InstanceColor,
        MaterialInput::InstanceEmissive, MaterialInput::Time,       MaterialInput::Audio,
        MaterialInput::AudioBands,    MaterialInput::BeatPhase,     MaterialInput::ViewDirection,
        MaterialInput::Depth,
    };
    for (const MaterialInput input : inputs) {
        const auto back = materialInputFromName(materialInputName(input));
        REQUIRE(back.has_value());
        CHECK(*back == input);
    }
    CHECK(std::string(materialInputName(MaterialInput::BeatPhase)) == "beatPhase");
    CHECK(std::string(materialInputName(MaterialInput::AudioBands)) == "audioBands");
    CHECK_FALSE(materialInputFromName("position").has_value());
}

// ---- inputs -----------------------------------------------------------------------------------

TEST_CASE("Input op: every input kind with its vec4 layout", "[material]") {
    const MaterialContext ctx = richContext();
    checkVec4(evalOp({}, inputOp(0, MaterialInput::WorldPosition), ctx), {1.0f, 2.0f, 3.0f, 1.0f});
    checkVec4(evalOp({}, inputOp(0, MaterialInput::LocalPosition), ctx), {0.1f, 0.2f, 0.3f, 1.0f});
    checkVec4(evalOp({}, inputOp(0, MaterialInput::Normal), ctx), {0.0f, 0.0f, 1.0f, 0.0f});
    checkVec4(evalOp({}, inputOp(0, MaterialInput::Uv), ctx), {0.25f, 0.75f, 0.0f, 0.0f});
    checkVec4(evalOp({}, inputOp(0, MaterialInput::ObjectId), ctx), glm::vec4(7.0f));
    checkVec4(evalOp({}, inputOp(0, MaterialInput::InstanceIndex), ctx), glm::vec4(0.5f));
    checkVec4(evalOp({}, inputOp(0, MaterialInput::InstanceId), ctx), glm::vec4(42.0f));
    checkVec4(evalOp({}, inputOp(0, MaterialInput::InstanceRandom), ctx), {0.1f, 0.2f, 0.3f, 0.4f});
    checkVec4(evalOp({}, inputOp(0, MaterialInput::InstanceColor), ctx), {0.9f, 0.8f, 0.7f, 0.6f});
    checkVec4(evalOp({}, inputOp(0, MaterialInput::InstanceEmissive), ctx), {0.5f, 0.4f, 0.3f, 0.2f});
    checkVec4(evalOp({}, inputOp(0, MaterialInput::Time), ctx), glm::vec4(12.5f));
    checkVec4(evalOp({}, inputOp(0, MaterialInput::Audio), ctx), {0.6f, 0.7f, 0.8f, 0.9f});
    checkVec4(evalOp({}, inputOp(0, MaterialInput::AudioBands), ctx), {0.11f, 0.22f, 0.33f, 0.44f});
    checkVec4(evalOp({}, inputOp(0, MaterialInput::BeatPhase), ctx), {0.25f, 1.0f, 0.5f, 0.125f});
    checkVec4(evalOp({}, inputOp(0, MaterialInput::ViewDirection), ctx), {0.0f, 0.0f, 1.0f, 0.0f});
    checkVec4(evalOp({}, inputOp(0, MaterialInput::Depth), ctx), glm::vec4(3.5f));
    // Any destination register works and the others stay zero.
    MaterialProgram p;
    p.ops = {inputOp(5, MaterialInput::Time)};
    const MaterialResult r = run(p, ctx);
    checkVec4(r.registers[5], glm::vec4(12.5f));
    for (std::size_t i = 0; i < 8; ++i) {
        if (i != 5) {
            checkVec4(r.registers[i], glm::vec4(0.0f));
        }
    }
}

// ---- ops --------------------------------------------------------------------------------------

TEST_CASE("Constant and Gradient ops", "[material]") {
    checkVec4(evalOp({}, constantOp(3, {1.0f, 2.0f, 3.0f, 4.0f})), {1.0f, 2.0f, 3.0f, 4.0f});

    MaterialContext ctx;
    ctx.worldPosition = {0.0f, 0.5f, 0.0f};
    MaterialOp g = makeOp(MaterialOpKind::Gradient, 1, 0);
    g.constant = {0.0f, 1.0f, 0.0f, 0.5f}; // axis y, bias 0.5
    g.value = 0.5f;                        // scale
    // saturate(dot((0, 0.5, 0), (0, 1, 0)) * 0.5 + 0.5) = 0.75
    checkVec4(evalOp({inputOp(0, MaterialInput::WorldPosition)}, g, ctx), glm::vec4(0.75f));
    ctx.worldPosition = {0.0f, 5.0f, 0.0f};
    checkVec4(evalOp({inputOp(0, MaterialInput::WorldPosition)}, g, ctx), glm::vec4(1.0f)); // saturated
    ctx.worldPosition = {0.0f, -5.0f, 0.0f};
    checkVec4(evalOp({inputOp(0, MaterialInput::WorldPosition)}, g, ctx), glm::vec4(0.0f));
    // Only the xyz of the axis is used (the position's w = 1 does not leak in).
    ctx.worldPosition = {0.0f, 0.0f, 0.0f};
    checkVec4(evalOp({inputOp(0, MaterialInput::WorldPosition)}, g, ctx), glm::vec4(0.5f));
}

TEST_CASE("Noise and Voronoi ops match core/noise", "[material]") {
    MaterialContext ctx;
    ctx.worldPosition = {0.3f, -1.2f, 2.5f};
    MaterialOp n = makeOp(MaterialOpKind::Noise, 1, 0);
    n.value = 2.0f;
    n.constant = {5.0f, 6.0f, 7.0f, 99.0f}; // w ignored
    n.seed = 11;
    const glm::vec3 p = ctx.worldPosition * 2.0f + glm::vec3(5.0f, 6.0f, 7.0f);
    const float expectedNoise = noise::fbm3(p, 11);
    checkVec4(evalOp({inputOp(0, MaterialInput::WorldPosition)}, n, ctx), glm::vec4(expectedNoise), 1e-6);
    CHECK(expectedNoise >= 0.0f);
    CHECK(expectedNoise <= 1.0f);
    n.seed = 12;
    CHECK(evalOp({inputOp(0, MaterialInput::WorldPosition)}, n, ctx).x != expectedNoise);

    MaterialOp v = makeOp(MaterialOpKind::Voronoi, 2, 0);
    v.value = 2.0f;
    v.constant = {5.0f, 6.0f, 7.0f, 0.0f};
    v.seed = 11;
    const float expectedVoronoi = noise::voronoiF1(p, 11);
    checkVec4(evalOp({inputOp(0, MaterialInput::WorldPosition)}, v, ctx), glm::vec4(expectedVoronoi), 1e-6);
    CHECK(expectedVoronoi >= 0.0f);
}

TEST_CASE("Fresnel op: pow(1 - saturate(dot(N, V)), value)", "[material]") {
    MaterialContext ctx;
    ctx.normal = {0.0f, 0.0f, 1.0f};
    MaterialOp f = makeOp(MaterialOpKind::Fresnel, 0);
    f.value = 3.0f;
    ctx.viewDirection = {0.0f, 0.0f, 1.0f};
    checkVec4(evalOp({}, f, ctx), glm::vec4(0.0f));
    ctx.viewDirection = {1.0f, 0.0f, 0.0f};
    checkVec4(evalOp({}, f, ctx), glm::vec4(1.0f));
    ctx.viewDirection = {0.0f, 0.0f, -1.0f}; // back-facing: dot clamps to 0
    checkVec4(evalOp({}, f, ctx), glm::vec4(1.0f));
    ctx.viewDirection = glm::normalize(glm::vec3(std::sqrt(3.0f), 0.0f, 1.0f)); // 60 degrees
    checkVec4(evalOp({}, f, ctx), glm::vec4(0.125f), 1e-5);
    f.value = 1.0f;
    checkVec4(evalOp({}, f, ctx), glm::vec4(0.5f), 1e-5);
}

TEST_CASE("Ramp op: three stops at 0, 0.5, 1 mixed linearly", "[material]") {
    MaterialOp r = makeOp(MaterialOpKind::Ramp, 1, 0);
    r.constant = {1.0f, 0.0f, 0.0f, 1.0f};
    r.constant2 = {0.0f, 1.0f, 0.0f, 1.0f};
    r.constant3 = {0.0f, 0.0f, 1.0f, 0.0f};
    checkVec4(evalOp({constantOp(0, glm::vec4(0.0f))}, r), {1.0f, 0.0f, 0.0f, 1.0f});
    checkVec4(evalOp({constantOp(0, glm::vec4(0.5f))}, r), {0.0f, 1.0f, 0.0f, 1.0f});
    checkVec4(evalOp({constantOp(0, glm::vec4(1.0f))}, r), {0.0f, 0.0f, 1.0f, 0.0f});
    checkVec4(evalOp({constantOp(0, glm::vec4(0.25f))}, r), {0.5f, 0.5f, 0.0f, 1.0f}, 1e-6);
    checkVec4(evalOp({constantOp(0, glm::vec4(0.75f))}, r), {0.0f, 0.5f, 0.5f, 0.5f}, 1e-6);
    // t = a.x clamped; only x is read.
    checkVec4(evalOp({constantOp(0, {-2.0f, 9.0f, 9.0f, 9.0f})}, r), {1.0f, 0.0f, 0.0f, 1.0f});
    checkVec4(evalOp({constantOp(0, {3.0f, 0.0f, 0.0f, 0.0f})}, r), {0.0f, 0.0f, 1.0f, 0.0f});
}

TEST_CASE("Remap op with and without clamping", "[material]") {
    MaterialOp m = makeOp(MaterialOpKind::Remap, 1, 0);
    m.constant = {-1.0f, 1.0f, 0.0f, 10.0f}; // [-1, 1] -> [0, 10]
    m.value = 0.0f;                          // no clamp
    checkVec4(evalOp({constantOp(0, {-1.0f, 0.0f, 1.0f, 2.0f})}, m), {0.0f, 5.0f, 10.0f, 15.0f}, 1e-5);
    m.value = 1.0f; // clamp
    checkVec4(evalOp({constantOp(0, {-1.0f, 0.0f, 1.0f, 2.0f})}, m), {0.0f, 5.0f, 10.0f, 10.0f}, 1e-5);
    checkVec4(evalOp({constantOp(0, glm::vec4(-3.0f))}, m), glm::vec4(0.0f), 1e-5);
    // Inverted output range clamps to [min, max] of (k.z, k.w).
    m.constant = {0.0f, 1.0f, 1.0f, 0.0f};
    checkVec4(evalOp({constantOp(0, {0.25f, 2.0f, -1.0f, 0.0f})}, m), {0.75f, 0.0f, 1.0f, 1.0f}, 1e-5);
    // Degenerate input range: out = k.z.
    m.constant = {0.5f, 0.5f, 3.0f, 4.0f};
    checkVec4(evalOp({constantOp(0, glm::vec4(0.9f))}, m), glm::vec4(3.0f), 1e-5);
}

TEST_CASE("Multiply, Add, Mix and MixBy ops", "[material]") {
    const std::vector<MaterialOp> setup = {constantOp(0, {1.0f, 2.0f, 3.0f, 4.0f}),
                                           constantOp(1, {10.0f, 20.0f, 30.0f, 40.0f}),
                                           constantOp(2, {0.25f, 0.0f, 0.0f, 0.0f})};
    checkVec4(evalOp(setup, makeOp(MaterialOpKind::Multiply, 3, 0, 1)), {10.0f, 40.0f, 90.0f, 160.0f});
    checkVec4(evalOp(setup, makeOp(MaterialOpKind::Add, 3, 0, 1)), {11.0f, 22.0f, 33.0f, 44.0f});
    MaterialOp mix = makeOp(MaterialOpKind::Mix, 3, 0, 1);
    mix.value = 0.5f;
    checkVec4(evalOp(setup, mix), {5.5f, 11.0f, 16.5f, 22.0f}, 1e-5);
    mix.value = 0.0f;
    checkVec4(evalOp(setup, mix), {1.0f, 2.0f, 3.0f, 4.0f});
    mix.value = 1.0f;
    checkVec4(evalOp(setup, mix), {10.0f, 20.0f, 30.0f, 40.0f});
    // MixBy reads the factor from srcC.x (not clamped).
    checkVec4(evalOp(setup, makeOp(MaterialOpKind::MixBy, 3, 0, 1, 2)), {3.25f, 6.5f, 9.75f, 13.0f}, 1e-5);
    // A register can be its own source and destination.
    checkVec4(evalOp(setup, makeOp(MaterialOpKind::Add, 0, 0, 0)), {2.0f, 4.0f, 6.0f, 8.0f});
}

TEST_CASE("Power, Smoothstep and Threshold ops", "[material]") {
    MaterialOp pw = makeOp(MaterialOpKind::Power, 1, 0);
    pw.value = 2.0f;
    checkVec4(evalOp({constantOp(0, {0.5f, 2.0f, -3.0f, 1.0f})}, pw), {0.25f, 4.0f, 0.0f, 1.0f}, 1e-6);
    pw.value = 0.5f;
    checkVec4(evalOp({constantOp(0, {4.0f, 0.0f, -1.0f, 9.0f})}, pw), {2.0f, 0.0f, 0.0f, 3.0f}, 1e-6);

    MaterialOp ss = makeOp(MaterialOpKind::Smoothstep, 1, 0);
    ss.constant = {0.2f, 0.6f, 0.0f, 0.0f};
    checkVec4(evalOp({constantOp(0, {0.0f, 0.2f, 0.4f, 1.0f})}, ss), {0.0f, 0.0f, 0.5f, 1.0f}, 1e-6);
    // t = 0.25 -> 0.25^2 * (3 - 0.5) = 0.15625
    checkVec4(evalOp({constantOp(0, glm::vec4(0.3f))}, ss), glm::vec4(0.15625f), 1e-6);
    ss.constant = {0.5f, 0.5f, 0.0f, 0.0f}; // degenerate edges -> step
    checkVec4(evalOp({constantOp(0, {0.4f, 0.5f, 0.6f, 0.0f})}, ss), {0.0f, 1.0f, 1.0f, 0.0f});

    MaterialOp th = makeOp(MaterialOpKind::Threshold, 1, 0);
    th.value = 0.5f;
    checkVec4(evalOp({constantOp(0, {0.49f, 0.5f, 0.51f, -1.0f})}, th), {0.0f, 1.0f, 1.0f, 0.0f});
}

TEST_CASE("HueShift and Saturate ops go through core/color and keep w", "[material]") {
    const glm::vec4 in{0.7f, 0.3f, 0.2f, 0.9f};
    MaterialOp hs = makeOp(MaterialOpKind::HueShift, 2, 0, 1);
    hs.value = 0.2f;
    // srcB = r1 = zero: the shift is exactly `value`.
    glm::vec4 out = evalOp({constantOp(0, in)}, hs);
    checkVec3(glm::vec3(out), color::hueShift(glm::vec3(in), 0.2f), 1e-6);
    CHECK_THAT(d(out.w), WithinAbs(0.9, 1e-6));
    // srcB.x adds to the shift.
    out = evalOp({constantOp(0, in), constantOp(1, {0.3f, 5.0f, 5.0f, 5.0f})}, hs);
    checkVec3(glm::vec3(out), color::hueShift(glm::vec3(in), 0.5f), 1e-6);
    // 1 turn is the identity.
    hs.value = 1.0f;
    out = evalOp({constantOp(0, in)}, hs);
    checkVec3(glm::vec3(out), glm::vec3(in), 1e-4);

    MaterialOp sat = makeOp(MaterialOpKind::Saturate, 2, 0);
    sat.value = 0.0f;
    out = evalOp({constantOp(0, in)}, sat);
    checkVec3(glm::vec3(out), color::saturate(glm::vec3(in), 0.0f), 1e-6);
    CHECK_THAT(d(out.x), WithinAbs(d(out.y), 1e-4)); // grey
    CHECK_THAT(d(out.w), WithinAbs(0.9, 1e-6));
    sat.value = 1.0f;
    out = evalOp({constantOp(0, in)}, sat);
    checkVec3(glm::vec3(out), glm::vec3(in), 1e-4);
}

TEST_CASE("Palette op: cosine palette of a.x + value, w = 1", "[material]") {
    MaterialOp pal = makeOp(MaterialOpKind::Palette, 1, 0);
    pal.constant = {0.5f, 0.5f, 0.5f, 0.0f};
    pal.constant2 = {0.5f, 0.5f, 0.5f, 0.0f};
    pal.constant3 = {1.0f, 1.0f, 1.0f, 0.0f};
    pal.constant4 = {0.0f, 0.33f, 0.67f, 0.0f};
    pal.value = 0.1f;
    const color::CosinePalette reference{{0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {1.0f, 1.0f, 1.0f},
                                         {0.0f, 0.33f, 0.67f}};
    const glm::vec4 out = evalOp({constantOp(0, {0.25f, 9.0f, 9.0f, 9.0f})}, pal);
    checkVec3(glm::vec3(out), reference.sample(0.35f), 1e-6);
    CHECK_THAT(d(out.w), WithinAbs(1.0, 1e-6));
    // t = 0 with d = 0: a + b = 1 on the first channel.
    pal.value = 0.0f;
    CHECK_THAT(d(evalOp({constantOp(0, glm::vec4(0.0f))}, pal).x), WithinAbs(1.0, 1e-6));
}

TEST_CASE("Field op samples by name at the world position; no sampler gives zeros", "[material]") {
    TestFields fields;
    MaterialContext ctx;
    ctx.worldPosition = {1.0f, 2.0f, 3.0f};
    ctx.fields = &fields;
    MaterialOp heat = makeOp(MaterialOpKind::Field, 0);
    heat.field = "heat";
    checkVec4(evalOp({}, heat, ctx), glm::vec4(6.0f));
    MaterialOp wind = makeOp(MaterialOpKind::Field, 0);
    wind.field = "wind";
    checkVec4(evalOp({}, wind, ctx), {1.0f, 2.0f, 3.0f, 0.0f});
    MaterialOp unknown = makeOp(MaterialOpKind::Field, 0);
    unknown.field = "nope";
    checkVec4(evalOp({}, unknown, ctx), glm::vec4(0.0f));
    ctx.fields = nullptr;
    checkVec4(evalOp({}, heat, ctx), glm::vec4(0.0f));
}

TEST_CASE("Disabled ops are skipped and registers start at zero", "[material]") {
    MaterialProgram p;
    p.ops = {constantOp(0, glm::vec4(1.0f)), constantOp(1, glm::vec4(2.0f)), makeOp(MaterialOpKind::Add, 2, 0, 1)};
    p.ops[1].enabled = false;
    const MaterialResult r = run(p);
    checkVec4(r.registers[0], glm::vec4(1.0f));
    checkVec4(r.registers[1], glm::vec4(0.0f));
    checkVec4(r.registers[2], glm::vec4(1.0f));
    // Registers do not persist between evaluations (the base's registers are ignored).
    MaterialResult base;
    base.registers[3] = glm::vec4(9.0f);
    checkVec4(run(p, {}, base).registers[3], glm::vec4(0.0f));
    // An empty program leaves the base untouched.
    MaterialProgram empty;
    base.baseColor = {0.1f, 0.2f, 0.3f};
    base.roughness = 0.7f;
    const MaterialResult r2 = run(empty, {}, base);
    checkVec3(r2.baseColor, {0.1f, 0.2f, 0.3f});
    CHECK(r2.roughness == 0.7f);
}

// ---- outputs ----------------------------------------------------------------------------------

TEST_CASE("Outputs: register mapping, -1 keeps the base, clamping", "[material]") {
    MaterialProgram p;
    p.ops = {constantOp(0, {0.2f, -0.5f, 1.5f, 0.0f}), constantOp(1, {1.7f, 0.0f, 0.0f, 0.0f}),
             constantOp(2, {-0.3f, 0.0f, 0.0f, 0.0f}), constantOp(3, {2.0f, 1.0f, 0.5f, 0.0f}),
             constantOp(4, {0.25f, 9.0f, 9.0f, 9.0f})};
    MaterialResult base;
    base.baseColor = {0.9f, 0.9f, 0.9f};
    base.metallic = 0.33f;
    base.roughness = 0.44f;
    base.emission = {0.1f, 0.1f, 0.1f};
    base.opacity = 0.55f;

    SECTION("all outputs from registers") {
        p.baseColorRegister = 0;
        p.metallicRegister = 1;
        p.roughnessRegister = 2;
        p.emissionRegister = 3;
        p.emissionIntensity = 2.0f;
        p.opacityRegister = 4;
        const MaterialResult r = run(p, {}, base);
        checkVec3(r.baseColor, {0.2f, 0.0f, 1.5f}); // negatives clamped, > 1 allowed
        CHECK(r.metallic == 1.0f);                   // clamped to [0, 1]
        CHECK(r.roughness == 0.0f);
        checkVec3(r.emission, {4.0f, 2.0f, 1.0f});  // rgb × intensity
        CHECK(r.opacity == 0.25f);
    }
    SECTION("-1 keeps the base values") {
        p.roughnessRegister = 2;
        const MaterialResult r = run(p, {}, base);
        checkVec3(r.baseColor, {0.9f, 0.9f, 0.9f});
        CHECK(r.metallic == 0.33f);
        CHECK(r.roughness == 0.0f);
        checkVec3(r.emission, {0.1f, 0.1f, 0.1f});
        CHECK(r.opacity == 0.55f);
    }
    SECTION("an unwritten register outputs zero") {
        p.baseColorRegister = 7;
        p.metallicRegister = 7;
        const MaterialResult r = run(p, {}, base);
        checkVec3(r.baseColor, {0.0f, 0.0f, 0.0f});
        CHECK(r.metallic == 0.0f);
    }
}

// ---- validation -------------------------------------------------------------------------------

TEST_CASE("validate: op count, register ranges, output ranges, field names", "[material]") {
    MaterialProgram p;
    p.name = "m";
    CHECK(p.validate().has_value());
    p.ops.assign(static_cast<std::size_t>(kMaxMaterialOps), constantOp(0, glm::vec4(1.0f)));
    CHECK(p.validate().has_value());
    p.ops.push_back(constantOp(0, glm::vec4(1.0f)));
    CHECK_FALSE(p.validate().has_value());
    p.ops.clear();

    for (int bad : {-1, kMaterialRegisters}) {
        p.ops = {makeOp(MaterialOpKind::Add, bad, 0, 0)};
        CHECK_FALSE(p.validate().has_value());
        p.ops = {makeOp(MaterialOpKind::Add, 0, bad, 0)};
        CHECK_FALSE(p.validate().has_value());
        p.ops = {makeOp(MaterialOpKind::Add, 0, 0, bad)};
        CHECK_FALSE(p.validate().has_value());
        p.ops = {makeOp(MaterialOpKind::MixBy, 0, 0, 0, bad)};
        CHECK_FALSE(p.validate().has_value());
    }
    p.ops = {makeOp(MaterialOpKind::Add, 7, 7, 7, 7)};
    CHECK(p.validate().has_value());

    p.ops = {makeOp(MaterialOpKind::Field, 0)};
    const auto err = p.validate();
    REQUIRE_FALSE(err.has_value());
    CHECK(err.error().message.find("field") != std::string::npos);
    p.ops[0].field = "heat";
    CHECK(p.validate().has_value());

    p.ops.clear();
    p.baseColorRegister = -1;
    CHECK(p.validate().has_value());
    p.baseColorRegister = 7;
    CHECK(p.validate().has_value());
    p.baseColorRegister = 8;
    CHECK_FALSE(p.validate().has_value());
    p.baseColorRegister = -2;
    CHECK_FALSE(p.validate().has_value());
    p.baseColorRegister = 0;
    p.metallicRegister = 8;
    CHECK_FALSE(p.validate().has_value());
    p.metallicRegister = 0;
    p.roughnessRegister = -3;
    CHECK_FALSE(p.validate().has_value());
    p.roughnessRegister = 0;
    p.emissionRegister = 9;
    CHECK_FALSE(p.validate().has_value());
    p.emissionRegister = 0;
    p.opacityRegister = 8;
    CHECK_FALSE(p.validate().has_value());
    p.opacityRegister = 0;
    CHECK(p.validate().has_value());
}

// ---- JSON -------------------------------------------------------------------------------------

TEST_CASE("JSON round trip writes kind + non-default members only", "[material]") {
    MaterialProgram p;
    p.name = "alien";
    MaterialOp in = inputOp(0, MaterialInput::Normal);
    MaterialOp n = makeOp(MaterialOpKind::Noise, 1, 0);
    n.value = 3.5f;
    n.constant = {1.0f, 2.0f, 3.0f, 4.0f};
    n.seed = 77;
    MaterialOp pal = makeOp(MaterialOpKind::Palette, 2, 1);
    pal.constant2 = {0.1f, 0.2f, 0.3f, 0.0f};
    pal.constant3 = {1.0f, 1.0f, 0.5f, 0.0f};
    pal.constant4 = {0.0f, 0.1f, 0.2f, 0.0f};
    pal.enabled = false;
    MaterialOp fld = makeOp(MaterialOpKind::Field, 3);
    fld.field = "heat";
    MaterialOp mixBy = makeOp(MaterialOpKind::MixBy, 4, 2, 3, 1);
    p.ops = {in, n, pal, fld, mixBy};
    p.baseColorRegister = 4;
    p.roughnessRegister = 1;
    p.emissionRegister = 2;
    p.emissionIntensity = 2.5f;
    p.opacityRegister = 3;

    const nlohmann::json j = p.toJson();
    CHECK(j.at("name") == "alien");
    REQUIRE(j.at("ops").is_array());
    REQUIRE(j.at("ops").size() == 5);
    const nlohmann::json& j0 = j.at("ops").at(0);
    CHECK(j0.at("kind") == "input");
    CHECK(j0.at("input") == "normal");
    CHECK_FALSE(j0.contains("dst"));
    CHECK_FALSE(j0.contains("value"));
    CHECK_FALSE(j0.contains("constant"));
    CHECK_FALSE(j0.contains("enabled"));
    CHECK_FALSE(j0.contains("seed"));
    CHECK_FALSE(j0.contains("field"));
    const nlohmann::json& j1 = j.at("ops").at(1);
    CHECK(j1.at("kind") == "noise");
    CHECK(j1.at("dst") == 1);
    CHECK(j1.at("seed") == 77);
    CHECK(j1.at("constant").size() == 4);
    CHECK_FALSE(j1.contains("srcA"));
    CHECK_FALSE(j1.contains("input"));
    const nlohmann::json& j2 = j.at("ops").at(2);
    CHECK(j2.at("kind") == "palette");
    CHECK(j2.at("enabled") == false);
    CHECK_FALSE(j2.contains("constant"));
    CHECK(j2.contains("constant2"));
    CHECK(j.at("ops").at(3).at("field") == "heat");
    CHECK(j.at("ops").at(4).at("kind") == "mixBy");
    CHECK(j.at("ops").at(4).at("srcC") == 1);
    CHECK(j.at("baseColor") == 4);
    CHECK(j.at("metallic") == -1);
    CHECK(j.at("roughness") == 1);
    CHECK(j.at("emission") == 2);
    CHECK(j.at("opacity") == 3);
    CHECK_THAT(d(j.at("emissionIntensity").get<float>()), WithinAbs(2.5, 1e-6));

    auto back = MaterialProgram::fromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->name == "alien");
    REQUIRE(back->ops.size() == 5);
    CHECK(back->ops[0].kind == MaterialOpKind::Input);
    CHECK(back->ops[0].input == MaterialInput::Normal);
    CHECK(back->ops[1].kind == MaterialOpKind::Noise);
    CHECK(back->ops[1].seed == 77);
    CHECK(back->ops[1].value == 3.5f);
    CHECK(back->ops[1].constant == glm::vec4(1.0f, 2.0f, 3.0f, 4.0f));
    CHECK_FALSE(back->ops[2].enabled);
    CHECK(back->ops[2].constant3 == glm::vec4(1.0f, 1.0f, 0.5f, 0.0f));
    CHECK(back->ops[3].field == "heat");
    CHECK(back->ops[4].srcC == 1);
    CHECK(back->emissionIntensity == 2.5f);
    CHECK(back->opacityRegister == 3);
    CHECK(back->structuralHash() == p.structuralHash());
    CHECK(back->toJson() == j);

    // Errors.
    CHECK_FALSE(MaterialProgram::fromJson(nlohmann::json::array()).has_value());
    CHECK_FALSE(MaterialProgram::fromJson({{"ops", 3}}).has_value());
    CHECK_FALSE(MaterialProgram::fromJson({{"ops", {{{"kind", "bogus"}}}}}).has_value());
    CHECK_FALSE(MaterialProgram::fromJson({{"ops", {{{"kind", "input"}, {"input", "bogus"}}}}}).has_value());
    CHECK_FALSE(MaterialProgram::fromJson({{"ops", {{{"kind", "noise"}, {"constant", {1, 2, 3}}}}}}).has_value());
    CHECK_FALSE(MaterialProgram::fromJson({{"ops", {{{"kind", "noise"}, {"dst", 8}}}}}).has_value());
    CHECK_FALSE(MaterialProgram::fromJson({{"ops", {{{"kind", "field"}}}}}).has_value());
    CHECK_FALSE(MaterialProgram::fromJson({{"baseColor", 8}}).has_value());
    CHECK_FALSE(MaterialProgram::fromJson({{"baseColor", "r0"}}).has_value());
    CHECK_FALSE(MaterialProgram::fromJson({{"ops", {{{"kind", "noise"}, {"seed", -1}}}}}).has_value());
    // Minimal.
    auto minimal = MaterialProgram::fromJson(nlohmann::json::object());
    REQUIRE(minimal.has_value());
    CHECK(minimal->name == "material");
    CHECK(minimal->ops.empty());
    CHECK(minimal->baseColorRegister == -1);
}

// ---- hashing ----------------------------------------------------------------------------------

TEST_CASE("structuralHash is stable and sensitive to every member", "[material]") {
    MaterialProgram p;
    p.name = "h";
    MaterialOp n = makeOp(MaterialOpKind::Noise, 1, 0);
    n.seed = 3;
    p.ops = {inputOp(0, MaterialInput::WorldPosition), n};
    p.roughnessRegister = 1;
    const std::uint64_t h = p.structuralHash();
    CHECK(h == p.structuralHash());
    CHECK(h == MaterialProgram(p).structuralHash());

    const auto differs = [&](auto mutate) {
        MaterialProgram q = p;
        mutate(q);
        return q.structuralHash() != h;
    };
    CHECK(differs([](MaterialProgram& q) { q.name = "other"; }));
    CHECK(differs([](MaterialProgram& q) { q.ops[1].kind = MaterialOpKind::Voronoi; }));
    CHECK(differs([](MaterialProgram& q) { q.ops[1].enabled = false; }));
    CHECK(differs([](MaterialProgram& q) { q.ops[1].dst = 2; }));
    CHECK(differs([](MaterialProgram& q) { q.ops[1].srcA = 2; }));
    CHECK(differs([](MaterialProgram& q) { q.ops[1].srcB = 2; }));
    CHECK(differs([](MaterialProgram& q) { q.ops[1].srcC = 2; }));
    CHECK(differs([](MaterialProgram& q) { q.ops[1].value = 2.0f; }));
    CHECK(differs([](MaterialProgram& q) { q.ops[1].constant.x = 1.0f; }));
    CHECK(differs([](MaterialProgram& q) { q.ops[1].constant2.w = 1.0f; }));
    CHECK(differs([](MaterialProgram& q) { q.ops[1].constant3.y = 1.0f; }));
    CHECK(differs([](MaterialProgram& q) { q.ops[1].constant4.z = 1.0f; }));
    CHECK(differs([](MaterialProgram& q) { q.ops[1].seed = 4; }));
    CHECK(differs([](MaterialProgram& q) { q.ops[0].input = MaterialInput::LocalPosition; }));
    CHECK(differs([](MaterialProgram& q) { q.ops[1].field = "x"; }));
    CHECK(differs([](MaterialProgram& q) { q.ops.pop_back(); }));
    CHECK(differs([](MaterialProgram& q) { std::swap(q.ops[0], q.ops[1]); }));
    CHECK(differs([](MaterialProgram& q) { q.baseColorRegister = 1; }));
    CHECK(differs([](MaterialProgram& q) { q.metallicRegister = 1; }));
    CHECK(differs([](MaterialProgram& q) { q.roughnessRegister = -1; }));
    CHECK(differs([](MaterialProgram& q) { q.emissionRegister = 1; }));
    CHECK(differs([](MaterialProgram& q) { q.emissionIntensity = 2.0f; }));
    CHECK(differs([](MaterialProgram& q) { q.opacityRegister = 1; }));
    // -0 and +0 hash alike.
    MaterialProgram z = p;
    z.ops[1].constant.x = -0.0f;
    CHECK(z.structuralHash() == h);
}

// ---- packing ----------------------------------------------------------------------------------

TEST_CASE("packMaterialProgram: layout, disabled ops skipped, field slots", "[material]") {
    CHECK(sizeof(MaterialOpGpu) == 112);
    CHECK(offsetof(MaterialOpGpu, registers) == 16);
    CHECK(offsetof(MaterialOpGpu, valuePad) == 32);
    CHECK(offsetof(MaterialOpGpu, constant) == 48);
    CHECK(offsetof(MaterialOpGpu, constant4) == 96);
    CHECK(offsetof(MaterialProgramGpu, ops) == 48);

    MaterialProgram p;
    MaterialOp in = inputOp(0, MaterialInput::Uv);
    MaterialOp off = constantOp(5, glm::vec4(9.0f));
    off.enabled = false;
    MaterialOp heat = makeOp(MaterialOpKind::Field, 1);
    heat.field = "heat";
    MaterialOp missing = makeOp(MaterialOpKind::Field, 2);
    missing.field = "missing";
    MaterialOp n = makeOp(MaterialOpKind::Noise, 3, 1, 2, 0);
    n.value = 4.0f;
    n.seed = 21;
    n.constant = {1.0f, 2.0f, 3.0f, 4.0f};
    n.constant2 = {5.0f, 6.0f, 7.0f, 8.0f};
    n.constant3 = {9.0f, 10.0f, 11.0f, 12.0f};
    n.constant4 = {13.0f, 14.0f, 15.0f, 16.0f};
    n.field = "ignored"; // non-Field ops never resolve a slot
    p.ops = {in, off, heat, missing, n};
    p.baseColorRegister = 3;
    p.metallicRegister = -1;
    p.roughnessRegister = 1;
    p.emissionRegister = 2;
    p.emissionIntensity = 1.5f;
    p.opacityRegister = 0;

    const std::vector<std::pair<std::string, int>> slots = {{"wind", 0}, {"heat", 2}};
    const MaterialProgramGpu gpu = packMaterialProgramWithSlots(p, slots);
    CHECK(gpu.outputs == glm::ivec4(3, -1, 1, 2));
    CHECK(gpu.opacityCountPad == glm::ivec4(0, 4, 0, 0));
    CHECK(gpu.emissionIntensityPad == glm::vec4(1.5f, 0.0f, 0.0f, 0.0f));

    CHECK(gpu.ops[0].kind == static_cast<std::uint32_t>(MaterialOpKind::Input));
    CHECK(gpu.ops[0].input == static_cast<std::uint32_t>(MaterialInput::Uv));
    CHECK(gpu.ops[0].fieldSlot == -1);
    CHECK(gpu.ops[0].registers == glm::ivec4(0, 0, 0, 0));
    CHECK(gpu.ops[0].valuePad == glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));

    CHECK(gpu.ops[1].kind == static_cast<std::uint32_t>(MaterialOpKind::Field));
    CHECK(gpu.ops[1].fieldSlot == 2);
    CHECK(gpu.ops[1].registers == glm::ivec4(1, 0, 0, 0));
    CHECK(gpu.ops[2].kind == static_cast<std::uint32_t>(MaterialOpKind::Field));
    CHECK(gpu.ops[2].fieldSlot == -1);

    CHECK(gpu.ops[3].kind == static_cast<std::uint32_t>(MaterialOpKind::Noise));
    CHECK(gpu.ops[3].seed == 21);
    CHECK(gpu.ops[3].fieldSlot == -1);
    CHECK(gpu.ops[3].registers == glm::ivec4(3, 1, 2, 0));
    CHECK(gpu.ops[3].valuePad == glm::vec4(4.0f, 0.0f, 0.0f, 0.0f));
    CHECK(gpu.ops[3].constant == glm::vec4(1.0f, 2.0f, 3.0f, 4.0f));
    CHECK(gpu.ops[3].constant2 == glm::vec4(5.0f, 6.0f, 7.0f, 8.0f));
    CHECK(gpu.ops[3].constant3 == glm::vec4(9.0f, 10.0f, 11.0f, 12.0f));
    CHECK(gpu.ops[3].constant4 == glm::vec4(13.0f, 14.0f, 15.0f, 16.0f));
    // Unused slots are zero.
    for (std::size_t i = 4; i < static_cast<std::size_t>(kMaxMaterialOps); ++i) {
        CHECK(gpu.ops[i].kind == 0);
        CHECK(gpu.ops[i].registers == glm::ivec4(0));
        CHECK(gpu.ops[i].constant == glm::vec4(0.0f));
    }

    // The template resolves slots through the callback.
    const MaterialProgramGpu viaFn = packMaterialProgram(p, [](const std::string& name) {
        return name == "heat" ? 5 : -1;
    });
    CHECK(viaFn.ops[1].fieldSlot == 5);
    CHECK(viaFn.ops[2].fieldSlot == -1);
    CHECK(std::memcmp(&viaFn.outputs, &gpu.outputs, sizeof(glm::ivec4)) == 0);
}

// ---- showcase ---------------------------------------------------------------------------------

TEST_CASE("Showcase program: height ramp base colour, noise roughness, fresnel emission", "[material]") {
    MaterialProgram p;
    p.name = "showcase";
    MaterialOp pos = inputOp(0, MaterialInput::WorldPosition);
    MaterialOp height = makeOp(MaterialOpKind::Gradient, 1, 0);
    height.constant = {0.0f, 1.0f, 0.0f, 0.5f}; // y in [-1, 1] -> [0, 1]
    height.value = 0.5f;
    MaterialOp ramp = makeOp(MaterialOpKind::Ramp, 2, 1);
    ramp.constant = {0.05f, 0.02f, 0.1f, 1.0f};
    ramp.constant2 = {0.6f, 0.2f, 0.7f, 1.0f};
    ramp.constant3 = {1.0f, 0.9f, 0.6f, 1.0f};
    MaterialOp noiseOp = makeOp(MaterialOpKind::Noise, 3, 0);
    noiseOp.value = 3.0f;
    noiseOp.seed = 5;
    MaterialOp rough = makeOp(MaterialOpKind::Remap, 4, 3);
    rough.constant = {0.0f, 1.0f, 0.2f, 0.9f};
    rough.value = 1.0f;
    MaterialOp fresnel = makeOp(MaterialOpKind::Fresnel, 5);
    fresnel.value = 4.0f;
    MaterialOp glow = constantOp(6, {0.3f, 0.8f, 1.0f, 1.0f});
    MaterialOp emission = makeOp(MaterialOpKind::Multiply, 7, 5, 6);
    p.ops = {pos, height, ramp, noiseOp, rough, fresnel, glow, emission};
    p.baseColorRegister = 2;
    p.roughnessRegister = 4;
    p.emissionRegister = 7;
    p.emissionIntensity = 3.0f;
    REQUIRE(p.validate().has_value());
    REQUIRE(p.ops.size() == 8);

    MaterialContext ctx;
    ctx.normal = {0.0f, 0.0f, 1.0f};
    for (float y = -1.0f; y <= 1.0f; y += 0.25f) {
        ctx.worldPosition = {0.3f, y, -0.7f};
        for (float grazing = 0.0f; grazing <= 1.0f; grazing += 0.5f) {
            ctx.viewDirection = glm::normalize(glm::vec3(grazing, 0.0f, 1.0f - grazing + 1e-3f));
            const MaterialResult r = run(p, ctx);
            const float t = y * 0.5f + 0.5f;
            const glm::vec4 expectedBase = t < 0.5f ? glm::mix(ramp.constant, ramp.constant2, t * 2.0f)
                                                    : glm::mix(ramp.constant2, ramp.constant3, (t - 0.5f) * 2.0f);
            checkVec3(r.baseColor, glm::vec3(expectedBase), 1e-5);
            CHECK(r.roughness >= 0.2f);
            CHECK(r.roughness <= 0.9f);
            const float fr = std::pow(1.0f - glm::clamp(ctx.viewDirection.z, 0.0f, 1.0f), 4.0f);
            checkVec3(r.emission, glm::vec3(0.3f, 0.8f, 1.0f) * fr * 3.0f, 1e-4);
            CHECK(r.emission.x >= 0.0f);
            CHECK(r.emission.z <= 3.0f);
            CHECK(r.metallic == 0.0f);   // untouched base
            CHECK(r.opacity == 1.0f);
        }
    }
    // The bottom is dark, the top is bright.
    ctx.worldPosition = {0.0f, -1.0f, 0.0f};
    const float lumBottom = color::luminance(run(p, ctx).baseColor);
    ctx.worldPosition = {0.0f, 1.0f, 0.0f};
    const float lumTop = color::luminance(run(p, ctx).baseColor);
    CHECK(lumBottom < 0.1f);
    CHECK(lumTop > 0.8f);
    // Roughness varies with position (noise is not constant).
    ctx.worldPosition = {0.0f, 0.0f, 0.0f};
    const float r0 = run(p, ctx).roughness;
    ctx.worldPosition = {0.37f, 0.11f, 0.83f};
    const float r1 = run(p, ctx).roughness;
    CHECK(r0 != r1);
    // Survives JSON and packs to 8 ops.
    auto back = MaterialProgram::fromJson(p.toJson());
    REQUIRE(back.has_value());
    CHECK(back->structuralHash() == p.structuralHash());
    CHECK(packMaterialProgramWithSlots(p, {}).opacityCountPad.y == 8);
}

// ---- documented examples ----------------------------------------------------------------------

TEST_CASE("docs/procedural-materials.md example programs parse, validate and evaluate sanely", "[material]") {
    const char* alienMetal = R"({ "name": "alienMetal",
  "ops": [
    { "kind": "input",    "input": "worldPosition", "dst": 0 },
    { "kind": "constant", "dst": 1, "constant": [0.04, 0.05, 0.07, 1] },
    { "kind": "voronoi",  "dst": 2, "srcA": 0, "value": 3.0, "seed": 9 },
    { "kind": "remap",    "dst": 3, "srcA": 2, "constant": [0, 0.8, 0.15, 0.7], "value": 1 },
    { "kind": "constant", "dst": 4, "constant": [1, 1, 1, 1] },
    { "kind": "fresnel",  "dst": 5, "value": 4.0 },
    { "kind": "constant", "dst": 6, "constant": [0.2, 0.9, 0.6, 1] },
    { "kind": "multiply", "dst": 7, "srcA": 5, "srcB": 6 } ],
  "baseColor": 1, "metallic": 4, "roughness": 3, "emission": 7, "emissionIntensity": 2.5, "opacity": -1 })";
    const char* emissiveGlass = R"({ "name": "emissiveGlass",
  "ops": [
    { "kind": "constant", "dst": 0, "constant": [0.6, 0.85, 1.0, 1] },
    { "kind": "fresnel",  "dst": 1, "value": 2.0 },
    { "kind": "remap",    "dst": 2, "srcA": 1, "constant": [0, 1, 0.15, 0.95], "value": 1 },
    { "kind": "multiply", "dst": 3, "srcA": 0, "srcB": 1 },
    { "kind": "constant", "dst": 4, "constant": [0.05, 0, 0, 0] },
    { "kind": "constant", "dst": 5, "constant": [0, 0, 0, 0] } ],
  "baseColor": 0, "metallic": 5, "roughness": 4, "emission": 3, "emissionIntensity": 1.5, "opacity": 2 })";
    const char* bioluminescent = R"({ "name": "bioluminescent",
  "ops": [
    { "kind": "input",    "input": "worldPosition", "dst": 0 },
    { "kind": "input",    "input": "time",          "dst": 1 },
    { "kind": "input",    "input": "audio",         "dst": 2 },
    { "kind": "constant", "dst": 3, "constant": [0, 0, 0.15, 0] },
    { "kind": "multiply", "dst": 3, "srcA": 3, "srcB": 1 },
    { "kind": "add",      "dst": 3, "srcA": 0, "srcB": 3 },
    { "kind": "noise",    "dst": 4, "srcA": 3, "value": 2.0, "seed": 4 },
    { "kind": "smoothstep", "dst": 5, "srcA": 4, "constant": [0.35, 0.7, 0, 0] },
    { "kind": "palette",  "dst": 6, "srcA": 4, "value": 0.1,
      "constant": [0.2, 0.4, 0.5, 0], "constant2": [0.2, 0.4, 0.5, 0],
      "constant3": [1, 1, 0.5, 0], "constant4": [0, 0.15, 0.2, 0] },
    { "kind": "multiply", "dst": 6, "srcA": 6, "srcB": 5 },
    { "kind": "gradient", "dst": 7, "srcA": 2, "constant": [1, 0, 0, 0.2], "value": 1.6 },
    { "kind": "multiply", "dst": 6, "srcA": 6, "srcB": 7 },
    { "kind": "constant", "dst": 1, "constant": [0.02, 0.03, 0.05, 1] } ],
  "baseColor": 1, "metallic": -1, "roughness": -1, "emission": 6, "emissionIntensity": 4.0, "opacity": -1 })";

    MaterialContext ctx;
    ctx.worldPosition = {0.4f, 1.3f, -2.2f};
    ctx.normal = {0.0f, 1.0f, 0.0f};
    ctx.viewDirection = glm::normalize(glm::vec3(0.8f, 0.4f, 0.0f)); // grazing
    ctx.time = 3.0f;
    ctx.audio = {0.5f, 0.2f, 0.2f, 0.1f};

    auto metal = MaterialProgram::fromJson(nlohmann::json::parse(alienMetal));
    REQUIRE(metal.has_value());
    const MaterialResult m = run(*metal, ctx);
    checkVec3(m.baseColor, {0.04f, 0.05f, 0.07f}, 1e-6);
    CHECK(m.metallic == 1.0f);
    CHECK(m.roughness >= 0.15f);
    CHECK(m.roughness <= 0.7f);
    const float rim = std::pow(1.0f - ctx.viewDirection.y, 4.0f);
    checkVec3(m.emission, glm::vec3(0.2f, 0.9f, 0.6f) * rim * 2.5f, 1e-5);
    CHECK(m.opacity == 1.0f);

    auto glass = MaterialProgram::fromJson(nlohmann::json::parse(emissiveGlass));
    REQUIRE(glass.has_value());
    const MaterialResult g = run(*glass, ctx);
    checkVec3(g.baseColor, {0.6f, 0.85f, 1.0f}, 1e-6);
    CHECK(g.metallic == 0.0f);
    CHECK_THAT(d(g.roughness), WithinAbs(0.05, 1e-6));
    const float fr = std::pow(1.0f - ctx.viewDirection.y, 2.0f);
    CHECK_THAT(d(g.opacity), WithinAbs(d(0.15f + fr * 0.8f), 1e-5));
    checkVec3(g.emission, glm::vec3(0.6f, 0.85f, 1.0f) * fr * 1.5f, 1e-5);
    ctx.viewDirection = {0.0f, 1.0f, 0.0f}; // head-on: least opaque, no glow
    const MaterialResult g2 = run(*glass, ctx);
    CHECK_THAT(d(g2.opacity), WithinAbs(0.15, 1e-6));
    checkVec3(g2.emission, {0.0f, 0.0f, 0.0f});

    auto bio = MaterialProgram::fromJson(nlohmann::json::parse(bioluminescent));
    REQUIRE(bio.has_value());
    CHECK(bio->ops.size() == 13);
    const MaterialResult b = run(*bio, ctx);
    checkVec3(b.baseColor, {0.02f, 0.03f, 0.05f}, 1e-6);
    CHECK(b.roughness == 0.5f); // base kept
    CHECK(b.emission.x >= 0.0f);
    CHECK(b.emission.y >= 0.0f);
    CHECK(b.emission.z >= 0.0f);
    CHECK(b.emission.x <= 4.0f);
    // Louder audio → brighter: the gradient broadcasts saturate(rms * 1.6 + 0.2), 0.2 → 1.0 here.
    ctx.audio.x = 0.0f;
    const MaterialResult quiet = run(*bio, ctx);
    ctx.audio.x = 1.0f;
    const MaterialResult loud = run(*bio, ctx);
    const float base = run(*bio, ctx).registers[5].x; // smoothstep mask at this position
    if (base > 0.0f) {
        CHECK(color::luminance(loud.emission) > color::luminance(quiet.emission));
        checkVec3(loud.emission, quiet.emission * 5.0f, 1e-5);
    }
    // Time drifts the pattern.
    ctx.time = 30.0f;
    const MaterialResult later = run(*bio, ctx);
    CHECK(later.registers[4].x != loud.registers[4].x);
}
