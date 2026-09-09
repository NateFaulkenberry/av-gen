#include "spatial/effector.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace avgen;
using namespace avgen::spatial;
using Catch::Matchers::WithinAbs;

namespace {

double d(float v) {
    return static_cast<double>(v);
}

void checkVec(const glm::vec3& v, const glm::vec3& expected, double tol = 1e-5) {
    CHECK_THAT(d(v.x), WithinAbs(d(expected.x), tol));
    CHECK_THAT(d(v.y), WithinAbs(d(expected.y), tol));
    CHECK_THAT(d(v.z), WithinAbs(d(expected.z), tol));
}

FieldSpec make(FieldKind kind, const char* name) {
    FieldSpec f;
    f.name = name;
    f.kind = kind;
    f.falloff.kind = FalloffKind::None;
    return f;
}

// "up": Direction +Y; "one": Constant 1; "half": Constant 0.5; "red": ConstantColor red;
// "bump": Radial radius 2 at world (10, 0, 0); "local": Local-space Direction +X.
FieldSet fields() {
    FieldSet set;
    set.fields.push_back(make(FieldKind::Direction, "up"));
    set.fields.push_back(make(FieldKind::Constant, "one"));
    FieldSpec half = make(FieldKind::Constant, "half");
    half.strength = 0.5f;
    set.fields.push_back(half);
    FieldSpec red = make(FieldKind::ConstantColor, "red");
    red.colorA = {1.0f, 0.0f, 0.0f, 1.0f};
    set.fields.push_back(red);
    FieldSpec bump = make(FieldKind::Radial, "bump");
    bump.radius = 2.0f;
    bump.position = {10.0f, 0.0f, 0.0f};
    set.fields.push_back(bump);
    FieldSpec local = make(FieldKind::Direction, "local");
    local.space = FieldSpace::Local;
    local.axis = {1.0f, 0.0f, 0.0f};
    set.fields.push_back(local);
    FieldSpec off = make(FieldKind::Constant, "off");
    off.enabled = false;
    set.fields.push_back(off);
    return set;
}

PointCloud cloud2() {
    PointCloud c(2);
    c.positions()[0] = {1.0f, 2.0f, 3.0f};
    c.positions()[1] = {-1.0f, 0.0f, 0.0f};
    c.scales()[0] = {2.0f, 2.0f, 2.0f};
    c.colors()[0] = {0.0f, 0.0f, 1.0f, 0.25f};
    c.emissives()[0] = {2.0f, 2.0f, 2.0f};
    c.densities()[0] = 0.5f;
    c.velocities()[0] = {0.0f, 0.0f, 1.0f};
    return c;
}

Effector eff(const char* field, EffectorOp op, float strength = 1.0f, EffectorBlend blend = EffectorBlend::Add) {
    Effector e;
    e.field = field;
    e.op = op;
    e.strength = strength;
    e.blend = blend;
    return e;
}

glm::quat quatOf(const glm::vec4& r) {
    return glm::quat(r.w, r.x, r.y, r.z);
}

} // namespace

TEST_CASE("Effector names round trip", "[spatial][effectors]") {
    for (const auto op : {EffectorOp::PositionOffset, EffectorOp::Scale, EffectorOp::Rotation, EffectorOp::Velocity,
                          EffectorOp::Color, EffectorOp::Emission, EffectorOp::Density, EffectorOp::Attribute}) {
        CHECK(effectorOpFromName(effectorOpName(op)) == op);
    }
    CHECK(effectorOpName(EffectorOp::PositionOffset) == std::string("positionOffset"));
    for (const auto b : {EffectorBlend::Add, EffectorBlend::Multiply, EffectorBlend::Replace, EffectorBlend::Min,
                         EffectorBlend::Max, EffectorBlend::Mix}) {
        CHECK(effectorBlendFromName(effectorBlendName(b)) == b);
    }
    CHECK_FALSE(effectorBlendFromName("Add").has_value());
}

TEST_CASE("PositionOffset and Velocity effectors with every blend", "[spatial][effectors]") {
    const FieldSet set = fields();
    PointCloud c = cloud2();
    const std::vector<Effector> add{eff("up", EffectorOp::PositionOffset, 2.0f)};
    CHECK(applyEffectors(c, add, set, 0.0) == 1);
    checkVec(c.positions()[0], {1.0f, 4.0f, 3.0f});
    checkVec(c.positions()[1], {-1.0f, 2.0f, 0.0f});

    // Scalar fields act along the effector axis.
    c = cloud2();
    Effector s = eff("half", EffectorOp::PositionOffset, 2.0f);
    s.axis = {1.0f, 0.0f, 0.0f};
    CHECK(applyEffectors(c, std::vector<Effector>{s}, set, 0.0) == 1);
    checkVec(c.positions()[0], {2.0f, 2.0f, 3.0f});

    c = cloud2();
    CHECK(applyEffectors(c, std::vector<Effector>{eff("up", EffectorOp::PositionOffset, 2.0f, EffectorBlend::Replace)}, set, 0.0) == 1);
    checkVec(c.positions()[0], {0.0f, 2.0f, 0.0f});
    c = cloud2();
    CHECK(applyEffectors(c, std::vector<Effector>{eff("up", EffectorOp::PositionOffset, 2.0f, EffectorBlend::Multiply)}, set, 0.0) == 1);
    checkVec(c.positions()[0], {0.0f, 4.0f, 0.0f});
    c = cloud2();
    CHECK(applyEffectors(c, std::vector<Effector>{eff("up", EffectorOp::PositionOffset, 2.0f, EffectorBlend::Min)}, set, 0.0) == 1);
    checkVec(c.positions()[0], {1.0f, 2.0f, 3.0f});
    c = cloud2();
    CHECK(applyEffectors(c, std::vector<Effector>{eff("up", EffectorOp::PositionOffset, 2.0f, EffectorBlend::Max)}, set, 0.0) == 1);
    checkVec(c.positions()[0], {1.0f, 4.0f, 3.0f});
    c = cloud2();
    Effector mix = eff("up", EffectorOp::PositionOffset, 2.0f, EffectorBlend::Mix);
    mix.weight = 0.25f;
    CHECK(applyEffectors(c, std::vector<Effector>{mix}, set, 0.0) == 1);
    checkVec(c.positions()[0], {1.0f, 2.5f, 3.0f});

    c = cloud2();
    CHECK(applyEffectors(c, std::vector<Effector>{eff("up", EffectorOp::Velocity, 3.0f)}, set, 0.0) == 1);
    checkVec(c.velocities()[0], {0.0f, 3.0f, 1.0f});
    checkVec(c.positions()[0], {1.0f, 2.0f, 3.0f});

    // Skipped: disabled effector, unknown field, disabled field.
    c = cloud2();
    Effector disabled = eff("up", EffectorOp::PositionOffset);
    disabled.enabled = false;
    CHECK(applyEffectors(c, std::vector<Effector>{disabled, eff("nope", EffectorOp::PositionOffset), eff("off", EffectorOp::PositionOffset)},
                         set, 0.0) == 0);
    checkVec(c.positions()[0], {1.0f, 2.0f, 3.0f});
}

TEST_CASE("Scale, Emission and Density effectors", "[spatial][effectors]") {
    const FieldSet set = fields();
    PointCloud c = cloud2();
    Effector sc = eff("one", EffectorOp::Scale, 0.5f);
    sc.scaleAxis = {1.0f, 0.0f, 1.0f};
    CHECK(applyEffectors(c, std::vector<Effector>{sc}, set, 0.0) == 1);
    checkVec(c.scales()[0], {3.0f, 2.0f, 3.0f});
    checkVec(c.scales()[1], {1.5f, 1.0f, 1.5f});
    c = cloud2();
    sc.blend = EffectorBlend::Replace;
    CHECK(applyEffectors(c, std::vector<Effector>{sc}, set, 0.0) == 1);
    checkVec(c.scales()[0], {0.5f, 0.0f, 0.5f});
    c = cloud2();
    sc.blend = EffectorBlend::Multiply;
    CHECK(applyEffectors(c, std::vector<Effector>{sc}, set, 0.0) == 1);
    checkVec(c.scales()[0], {1.0f, 0.0f, 1.0f});
    c = cloud2();
    sc.blend = EffectorBlend::Mix;
    sc.weight = 0.5f;
    CHECK(applyEffectors(c, std::vector<Effector>{sc}, set, 0.0) == 1);
    checkVec(c.scales()[0], {2.5f, 2.0f, 2.5f});

    c = cloud2();
    CHECK(applyEffectors(c, std::vector<Effector>{eff("one", EffectorOp::Emission, 1.0f)}, set, 0.0) == 1);
    checkVec(c.emissives()[0], {4.0f, 4.0f, 4.0f});
    checkVec(c.emissives()[1], {2.0f, 2.0f, 2.0f});
    c = cloud2();
    CHECK(applyEffectors(c, std::vector<Effector>{eff("half", EffectorOp::Emission, 2.0f, EffectorBlend::Replace)}, set, 0.0) == 1);
    checkVec(c.emissives()[0], {1.0f, 1.0f, 1.0f});

    c = cloud2();
    CHECK(applyEffectors(c, std::vector<Effector>{eff("half", EffectorOp::Density, 1.0f)}, set, 0.0) == 1);
    CHECK(c.densities()[0] == 0.25f);
    CHECK(c.densities()[1] == 0.5f);
    c = cloud2();
    CHECK(applyEffectors(c, std::vector<Effector>{eff("half", EffectorOp::Density, 2.0f, EffectorBlend::Replace)}, set, 0.0) == 1);
    CHECK(c.densities()[0] == 1.0f);
    c = cloud2();
    CHECK(applyEffectors(c, std::vector<Effector>{eff("one", EffectorOp::Density, 0.1f, EffectorBlend::Max)}, set, 0.0) == 1);
    CHECK(c.densities()[0] == 0.5f);
    CHECK(c.densities()[1] == 1.0f);
}

TEST_CASE("Rotation and Color effectors", "[spatial][effectors]") {
    const FieldSet set = fields();
    PointCloud c = cloud2();
    Effector rot = eff("one", EffectorOp::Rotation, glm::half_pi<float>());
    rot.axis = {0.0f, 1.0f, 0.0f};
    CHECK(applyEffectors(c, std::vector<Effector>{rot}, set, 0.0) == 1);
    checkVec(quatOf(c.rotations()[0]) * glm::vec3(1.0f, 0.0f, 0.0f), {0.0f, 0.0f, -1.0f});
    // Premultiplied: a second application makes a half turn.
    CHECK(applyEffectors(c, std::vector<Effector>{rot}, set, 0.0) == 1);
    checkVec(quatOf(c.rotations()[0]) * glm::vec3(1.0f, 0.0f, 0.0f), {-1.0f, 0.0f, 0.0f});
    // Vector fields rotate about the sampled direction by |v| * strength.
    c = cloud2();
    CHECK(applyEffectors(c, std::vector<Effector>{eff("up", EffectorOp::Rotation, glm::half_pi<float>())}, set, 0.0) == 1);
    checkVec(quatOf(c.rotations()[0]) * glm::vec3(1.0f, 0.0f, 0.0f), {0.0f, 0.0f, -1.0f});
    // Replace ignores the existing rotation; Mix slerps.
    rot.blend = EffectorBlend::Replace;
    CHECK(applyEffectors(c, std::vector<Effector>{rot}, set, 0.0) == 1);
    checkVec(quatOf(c.rotations()[0]) * glm::vec3(1.0f, 0.0f, 0.0f), {0.0f, 0.0f, -1.0f});
    c = cloud2();
    rot.blend = EffectorBlend::Mix;
    rot.weight = 0.5f;
    CHECK(applyEffectors(c, std::vector<Effector>{rot}, set, 0.0) == 1);
    const glm::vec3 quarter = quatOf(c.rotations()[0]) * glm::vec3(1.0f, 0.0f, 0.0f);
    const float s = std::sqrt(0.5f);
    checkVec(quarter, {s, 0.0f, -s});
    // A zero axis / zero vector leaves the rotation alone.
    c = cloud2();
    rot.blend = EffectorBlend::Add;
    rot.axis = glm::vec3(0.0f);
    CHECK(applyEffectors(c, std::vector<Effector>{rot}, set, 0.0) == 1);
    CHECK(c.rotations()[0] == glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

    c = cloud2();
    CHECK(applyEffectors(c, std::vector<Effector>{eff("red", EffectorOp::Color, 1.0f)}, set, 0.0) == 1);
    checkVec(glm::vec3(c.colors()[0]), {1.0f, 0.0f, 0.0f});
    CHECK(c.colors()[0].a == 0.25f); // alpha untouched
    c = cloud2();
    CHECK(applyEffectors(c, std::vector<Effector>{eff("red", EffectorOp::Color, 0.5f)}, set, 0.0) == 1);
    checkVec(glm::vec3(c.colors()[0]), {0.5f, 0.0f, 0.5f});
    c = cloud2();
    CHECK(applyEffectors(c, std::vector<Effector>{eff("red", EffectorOp::Color, 0.5f, EffectorBlend::Multiply)}, set, 0.0) == 1);
    checkVec(glm::vec3(c.colors()[0]), {0.0f, 0.0f, 0.0f});
    c = cloud2();
    CHECK(applyEffectors(c, std::vector<Effector>{eff("red", EffectorOp::Color, 0.5f, EffectorBlend::Max)}, set, 0.0) == 1);
    checkVec(glm::vec3(c.colors()[0]), {0.5f, 0.0f, 1.0f});
    // A scalar field as colour mixes colorA -> colorB by the value (defaults white -> black).
    c = cloud2();
    CHECK(applyEffectors(c, std::vector<Effector>{eff("one", EffectorOp::Color, 1.0f)}, set, 0.0) == 1);
    checkVec(glm::vec3(c.colors()[0]), {0.0f, 0.0f, 0.0f});
}

TEST_CASE("Attribute effector writes typed columns", "[spatial][effectors]") {
    const FieldSet set = fields();
    PointCloud c = cloud2();
    Effector a = eff("half", EffectorOp::Attribute, 2.0f);
    a.target = "heat";
    CHECK(applyEffectors(c, std::vector<Effector>{a}, set, 0.0) == 1);
    REQUIRE(c.attributes.typeOf("heat") == AttributeType::Float);
    CHECK((*c.attributes.view<float>("heat"))[0] == 1.0f);
    CHECK(applyEffectors(c, std::vector<Effector>{a}, set, 0.0) == 1);
    CHECK((*c.attributes.view<float>("heat"))[0] == 2.0f); // Add accumulates
    a.blend = EffectorBlend::Replace;
    CHECK(applyEffectors(c, std::vector<Effector>{a}, set, 0.0) == 1);
    CHECK((*c.attributes.view<float>("heat"))[0] == 1.0f);

    Effector v = eff("up", EffectorOp::Attribute, 3.0f);
    v.target = "wind";
    CHECK(applyEffectors(c, std::vector<Effector>{v}, set, 0.0) == 1);
    REQUIRE(c.attributes.typeOf("wind") == AttributeType::Vec3);
    checkVec((*c.attributes.view<glm::vec3>("wind"))[1], {0.0f, 3.0f, 0.0f});

    Effector col = eff("red", EffectorOp::Attribute, 1.0f);
    col.target = "tint";
    CHECK(applyEffectors(c, std::vector<Effector>{col}, set, 0.0) == 1);
    REQUIRE(c.attributes.typeOf("tint") == AttributeType::Color);
    CHECK((*c.attributes.view<glm::vec4>("tint"))[0] == glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));

    // Type clash and empty target are skipped.
    Effector clash = eff("up", EffectorOp::Attribute, 1.0f);
    clash.target = "heat";
    CHECK(applyEffectors(c, std::vector<Effector>{clash}, set, 0.0) == 0);
    Effector empty = eff("up", EffectorOp::Attribute, 1.0f);
    CHECK(applyEffectors(c, std::vector<Effector>{empty}, set, 0.0) == 0);
}

TEST_CASE("objectToWorld: sampling in world space, results back in object space", "[spatial][effectors]") {
    const FieldSet set = fields();
    // The bump sits at world (10, 0, 0); the object is translated there, so its origin is at the peak.
    PointCloud c(2);
    c.positions()[1] = {1.0f, 0.0f, 0.0f};
    Effector density = eff("bump", EffectorOp::Density, 1.0f);
    const glm::mat4 translate = glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, 0.0f));
    CHECK(applyEffectors(c, std::vector<Effector>{density}, set, 0.0, translate) == 1);
    CHECK_THAT(d(c.densities()[0]), WithinAbs(1.0, 1e-6));
    CHECK_THAT(d(c.densities()[1]), WithinAbs(0.5, 1e-6));
    PointCloud far(1);
    CHECK(applyEffectors(far, std::vector<Effector>{density}, set, 0.0) == 1); // object at the origin: outside
    CHECK(far.densities()[0] == 0.0f);

    // World vectors are rotated back through inverse(mat3(objectToWorld)).
    const glm::mat4 rotY = glm::rotate(glm::mat4(1.0f), glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
    FieldSet dirSet;
    FieldSpec px = make(FieldKind::Direction, "px");
    px.axis = {1.0f, 0.0f, 0.0f};
    dirSet.fields.push_back(px);
    PointCloud r(1);
    CHECK(applyEffectors(r, std::vector<Effector>{eff("px", EffectorOp::PositionOffset, 1.0f)}, dirSet, 0.0, rotY) == 1);
    checkVec(r.positions()[0], {0.0f, 0.0f, 1.0f});
    // A scaled object shrinks the offset in object units (3x3 inverse includes the scale).
    const glm::mat4 scaled = glm::scale(glm::mat4(1.0f), glm::vec3(2.0f));
    PointCloud sc(1);
    CHECK(applyEffectors(sc, std::vector<Effector>{eff("px", EffectorOp::PositionOffset, 1.0f)}, dirSet, 0.0, scaled) == 1);
    checkVec(sc.positions()[0], {0.5f, 0.0f, 0.0f});
    // Local-space fields ignore objectToWorld entirely.
    PointCloud l(1);
    CHECK(applyEffectors(l, std::vector<Effector>{eff("local", EffectorOp::PositionOffset, 1.0f)}, set, 0.0, rotY) == 1);
    checkVec(l.positions()[0], {1.0f, 0.0f, 0.0f});
}

TEST_CASE("applyEffectorsToRecords matches the cloud path and preserves the id/extra lanes", "[spatial][effectors]") {
    const FieldSet set = fields();
    PointCloud c = cloud2();
    c.ids()[0] = 7;
    c.ids()[1] = 9;
    REQUIRE(c.attributes.add("lane", AttributeType::Float).has_value());
    (*c.attributes.view<float>("lane"))[1] = 4.0f;
    std::vector<InstanceRecord> records;
    projectInstances(c, records, "lane");

    Effector rot = eff("one", EffectorOp::Rotation, 0.7f);
    rot.axis = {0.0f, 0.0f, 1.0f};
    Effector sc = eff("half", EffectorOp::Scale, 1.0f);
    Effector vel = eff("up", EffectorOp::Velocity, 1.0f);
    Effector attr = eff("half", EffectorOp::Attribute, 1.0f);
    attr.target = "x";
    const std::vector<Effector> list{eff("up", EffectorOp::PositionOffset, 2.0f), sc, rot, eff("red", EffectorOp::Color, 0.5f),
                                     eff("one", EffectorOp::Emission, 1.0f), eff("half", EffectorOp::Density, 1.0f), vel, attr};
    const glm::mat4 world = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 0.0f, 0.0f));
    CHECK(applyEffectors(c, list, set, 1.0, world) == 8);
    CHECK(applyEffectorsToRecords(records, list, set, 1.0, world) == 6); // Velocity and Attribute skipped
    for (std::size_t i = 0; i < 2; ++i) {
        const InstanceRecord& r = records[i];
        checkVec(glm::vec3(r.position), c.positions()[i], 1e-6);
        CHECK_THAT(d(r.position.w), WithinAbs(d(c.densities()[i]), 1e-6));
        for (int k = 0; k < 4; ++k) {
            CHECK_THAT(d(r.rotation[k]), WithinAbs(d(c.rotations()[i][k]), 1e-6));
        }
        checkVec(glm::vec3(r.scale), c.scales()[i], 1e-6);
        CHECK(r.scale.w == c.indices()[i]);
        checkVec(glm::vec3(r.color), glm::vec3(c.colors()[i]), 1e-6);
        CHECK(r.color.a == static_cast<float>(c.ids()[i]));
        checkVec(glm::vec3(r.emissive), c.emissives()[i], 1e-6);
    }
    CHECK(records[1].emissive.a == 4.0f);
    CHECK(records[0].emissive.a == 0.0f);
}

TEST_CASE("Effector JSON, hash and GPU packing", "[spatial][effectors]") {
    Effector e;
    e.field = "gust";
    e.op = EffectorOp::Scale;
    e.blend = EffectorBlend::Mix;
    e.enabled = false;
    e.strength = 2.5f;
    e.weight = 0.3f;
    e.axis = {1.0f, 0.0f, 0.0f};
    e.scaleAxis = {0.0f, 1.0f, 0.0f};
    e.target = "t";
    const nlohmann::json j = e.toJson();
    CHECK(j.at("op") == "scale");
    CHECK(j.at("blend") == "mix");
    auto back = Effector::fromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->field == "gust");
    CHECK(back->op == EffectorOp::Scale);
    CHECK(back->blend == EffectorBlend::Mix);
    CHECK_FALSE(back->enabled);
    CHECK(back->strength == 2.5f);
    CHECK(back->weight == 0.3f);
    CHECK(back->axis == e.axis);
    CHECK(back->scaleAxis == e.scaleAxis);
    CHECK(back->target == "t");
    CHECK(back->structuralHash() == e.structuralHash());
    Effector m = e;
    m.weight = 0.4f;
    CHECK(m.structuralHash() != e.structuralHash());
    m = e;
    m.field = "other";
    CHECK(m.structuralHash() != e.structuralHash());
    CHECK_FALSE(Effector::fromJson(nlohmann::json::object()).has_value());
    CHECK_FALSE(Effector::fromJson(nlohmann::json{{"field", "f"}, {"op", "bogus"}}).has_value());
    CHECK_FALSE(Effector::fromJson(nlohmann::json{{"field", "f"}, {"op", "attribute"}}).has_value());
    auto minimal = Effector::fromJson(nlohmann::json{{"field", "f"}});
    REQUIRE(minimal.has_value());
    CHECK(minimal->op == EffectorOp::PositionOffset);
    CHECK(minimal->strength == 1.0f);

    const FieldSet set = fields();
    Effector p = eff("half", EffectorOp::Rotation, 1.5f, EffectorBlend::Max);
    p.axis = {0.0f, 0.0f, 1.0f};
    p.weight = 0.7f;
    p.scaleAxis = {1.0f, 0.0f, 1.0f};
    const EffectorGpu g = packEffector(p, set);
    CHECK(g.op == static_cast<std::uint32_t>(EffectorOp::Rotation));
    CHECK(g.blend == static_cast<std::uint32_t>(EffectorBlend::Max));
    CHECK(g.fieldSlot == 2);
    CHECK(g.strength == 1.5f);
    CHECK(g.axisWeight == glm::vec4(0.0f, 0.0f, 1.0f, 0.7f));
    CHECK(g.scaleAxisPad == glm::vec4(1.0f, 0.0f, 1.0f, 0.0f));
    p.enabled = false;
    CHECK(packEffector(p, set).fieldSlot == -1);
    p.enabled = true;
    p.field = "nope";
    CHECK(packEffector(p, set).fieldSlot == -1);
    p.field = "off";
    CHECK(packEffector(p, set).fieldSlot == -1);
    CHECK(sizeof(EffectorGpu) == 48);
}
