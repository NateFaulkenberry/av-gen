// The Procedural Space POC's SDF additions (ADR-1001, ADR-1002): the morph combination, the plane
// fold, the recurse loop (tree vs packed interpreter, including the backward jump), named nodes in
// parameter paths, `count` and `axis` parameters, and the march/look block. GPU parity for the new
// kinds is in tests/rendering/test_sdf_gpu.cpp.
#include "params/parameter_set.hpp"
#include "scene/sdf_object.hpp"
#include "spatial/sdf.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace avgen;
using spatial::SdfNode;
using spatial::SdfNodeKind;
using spatial::SdfTree;
using Catch::Matchers::WithinAbs;

namespace {

SdfNode node(SdfNodeKind kind) {
    SdfNode n;
    n.kind = kind;
    return n;
}
SdfNode sphere(float r) {
    SdfNode n = node(SdfNodeKind::Sphere);
    n.radius = r;
    return n;
}
SdfNode box(glm::vec3 size) {
    SdfNode n = node(SdfNodeKind::Box);
    n.size = size;
    return n;
}
SdfNode unary(SdfNodeKind kind, SdfNode child) {
    SdfNode n = node(kind);
    n.children.push_back(std::move(child));
    return n;
}
SdfNode translate(glm::vec3 t, SdfNode child) {
    SdfNode n = unary(SdfNodeKind::Translate, std::move(child));
    n.translation = t;
    return n;
}
SdfNode combo(SdfNodeKind kind, std::vector<SdfNode> children) {
    SdfNode n = node(kind);
    n.children = std::move(children);
    return n;
}
SdfTree treeOf(SdfNode root) {
    SdfTree t;
    t.root = std::move(root);
    return t;
}

// A deterministic spread of sample points over [-3, 3]^3.
std::vector<glm::vec3> samplePoints() {
    std::vector<glm::vec3> out;
    std::uint32_t state = 12345u;
    auto next = [&] {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) / static_cast<float>(1u << 24) * 6.0f - 3.0f;
    };
    for (int i = 0; i < 400; ++i) {
        out.emplace_back(next(), next(), next());
    }
    return out;
}

// The tree and the packed interpreter agree (the GPU transliterates the packed one).
void checkPackedParity(const SdfTree& tree, float tol = 1e-5f) {
    REQUIRE(tree.validate());
    std::vector<spatial::SdfNodeGpu> packed;
    REQUIRE(spatial::packSdfTree(tree, packed) > 0);
    for (const glm::vec3& p : samplePoints()) {
        const float a = tree.evaluate(p, 0.7);
        const float b = spatial::evaluatePacked(packed, p, 0.7);
        INFO("p = " << p.x << ", " << p.y << ", " << p.z);
        CHECK_THAT(static_cast<double>(b), WithinAbs(static_cast<double>(a), static_cast<double>(tol)));
    }
}

// The largest |d(p) - d(q)| / |p - q| over nearby sample pairs: <= 1 (plus rounding) for a field a
// sphere tracer may step by at full stride.
float lipschitzEstimate(const SdfTree& tree) {
    float worst = 0.0f;
    for (const glm::vec3& p : samplePoints()) {
        for (const glm::vec3& dir : {glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1),
                                     glm::normalize(glm::vec3(1, 1, -1))}) {
            const glm::vec3 q = p + dir * 0.05f;
            worst = std::max(worst, std::fabs(tree.evaluate(p, 0.0) - tree.evaluate(q, 0.0)) / 0.05f);
        }
    }
    return worst;
}

} // namespace

TEST_CASE("SDF morph interpolates its children in order by amount", "[sdf][space]") {
    const glm::vec3 p(0.9f, 0.3f, -0.2f);
    const float a = treeOf(sphere(1.0f)).evaluate(p, 0.0);
    const float b = treeOf(box(glm::vec3(0.5f))).evaluate(p, 0.0);
    const float c = treeOf(translate({1, 0, 0}, sphere(0.3f))).evaluate(p, 0.0);
    auto morph = [&](float amount) {
        SdfNode m = combo(SdfNodeKind::Morph, {sphere(1.0f), box(glm::vec3(0.5f)), translate({1, 0, 0}, sphere(0.3f))});
        m.amount = amount;
        return treeOf(std::move(m));
    };
    CHECK_THAT(morph(0.0f).evaluate(p, 0.0), WithinAbs(a, 1e-6));
    CHECK_THAT(morph(1.0f).evaluate(p, 0.0), WithinAbs(b, 1e-6));
    CHECK_THAT(morph(2.0f).evaluate(p, 0.0), WithinAbs(c, 1e-6));
    CHECK_THAT(morph(0.5f).evaluate(p, 0.0), WithinAbs(0.5f * (a + b), 1e-6));
    CHECK_THAT(morph(1.25f).evaluate(p, 0.0), WithinAbs(b + 0.25f * (c - b), 1e-6));
    // Clamped to [0, k - 1].
    CHECK_THAT(morph(-3.0f).evaluate(p, 0.0), WithinAbs(a, 1e-6));
    CHECK_THAT(morph(9.0f).evaluate(p, 0.0), WithinAbs(c, 1e-6));
    // A disabled child drops out of the sequence: two enabled children, amount 1 = the last one.
    SdfTree t = morph(1.0f);
    t.root.children[1].enabled = false;
    CHECK_THAT(t.evaluate(p, 0.0), WithinAbs(c, 1e-6));
    for (const float amount : {0.0f, 0.3f, 1.0f, 1.7f, 2.0f}) {
        checkPackedParity(morph(amount));
    }
    // A convex mix of distance fields is still a valid bound.
    CHECK(lipschitzEstimate(morph(0.6f)) <= 1.001f);
}

TEST_CASE("SDF fold reflects the half-space behind its plane", "[sdf][space]") {
    SdfNode f = unary(SdfNodeKind::Fold, translate({2, 0, 0}, sphere(0.5f)));
    f.axis = {1, 0, 0};
    f.offset = 0.0f;
    const SdfTree t = treeOf(f);
    // The copy appears mirrored at x = -2, and points in front of the plane are unchanged.
    CHECK_THAT(t.evaluate({-2, 0, 0}, 0.0), WithinAbs(-0.5, 1e-6));
    CHECK_THAT(t.evaluate({2, 0, 0}, 0.0), WithinAbs(-0.5, 1e-6));
    CHECK_THAT(t.evaluate({3, 1, 0}, 0.0), WithinAbs(std::sqrt(2.0) - 0.5, 1e-5));
    // An offset plane (x = 1) puts the mirror image at 1 - (2 - 1) = 0.
    SdfTree shifted = t;
    shifted.root.offset = 1.0f;
    CHECK_THAT(shifted.evaluate({0, 0, 0}, 0.0), WithinAbs(-0.5, 1e-6));
    // An oblique plane, and parity with the packed program.
    SdfTree oblique = t;
    oblique.root.axis = {1.0f, 0.5f, -0.3f};
    oblique.root.offset = 0.4f;
    checkPackedParity(oblique);
    CHECK(lipschitzEstimate(oblique) <= 1.001f);
    // A disabled fold under another unary op passes through (a rule switched off by `enabled`).
    SdfNode wrapped = unary(SdfNodeKind::Twist, f);
    wrapped.children[0].enabled = false;
    const SdfTree off = treeOf(wrapped);
    REQUIRE(off.validate());
    CHECK(off.evaluate({-2, 0, 0}, 0.0) > 1.0f); // no mirror image any more
    CHECK_THAT(off.evaluate({2, 0, 0}, 0.0), WithinAbs(-0.5, 1e-6));
    checkPackedParity(off);
    // A zero axis is refused, as it is for a plane.
    SdfTree bad = t;
    bad.root.axis = glm::vec3(0.0f);
    CHECK_FALSE(bad.validate());
}

TEST_CASE("SDF recurse unions the child over its levels", "[sdf][space]") {
    SdfNode child = translate({1.5f, 0, 0}, box(glm::vec3(0.4f)));
    SdfNode r = unary(SdfNodeKind::Recurse, child);
    r.count = 0;
    r.scale = 2.0f;
    r.translation = {1.0f, 0.0f, 0.5f};
    r.size = {1, 0, 1};
    const SdfTree childTree = treeOf(child);
    // Level 0 alone is the child.
    for (const glm::vec3& p : samplePoints()) {
        CHECK(treeOf(r).evaluate(p, 0.0) == childTree.evaluate(p, 0.0));
    }
    // Two levels equal the hand-written union of the child at p and at the next level's point / 2.
    r.count = 1;
    const SdfTree two = treeOf(r);
    for (const glm::vec3& p : samplePoints()) {
        const glm::vec3 folded(std::fabs(p.x), p.y, std::fabs(p.z));
        const glm::vec3 next = folded * 2.0f - glm::vec3(1.0f, 0.0f, 0.5f);
        const float expected = std::min(childTree.evaluate(p, 0.0), childTree.evaluate(next, 0.0) / 2.0f);
        CHECK_THAT(two.evaluate(p, 0.0), WithinAbs(expected, 1e-5));
    }
    // The packed loop (a backward jump per level) equals the tree, with rotation, nesting and a
    // morph inside the loop body.
    r.count = 4;
    r.rotationDegrees = {10.0f, 30.0f, 0.0f};
    checkPackedParity(treeOf(r));
    SdfNode inner = unary(SdfNodeKind::Recurse, combo(SdfNodeKind::Morph, {box(glm::vec3(0.3f)), sphere(0.4f)}));
    inner.children[0].amount = 0.3f;
    inner.count = 2;
    inner.scale = 1.6f;
    inner.translation = {0.5f, 0.0f, 0.0f};
    inner.size = glm::vec3(1.0f);
    SdfNode outer = unary(SdfNodeKind::Recurse, combo(SdfNodeKind::Union, {inner, sphere(0.2f)}));
    outer.count = 3;
    outer.scale = 2.2f;
    outer.translation = {0.0f, 1.0f, 0.3f};
    outer.size = {0, 1, 1};
    checkPackedParity(treeOf(outer));
    // Fold, rotation and uniform scale are distance-preserving, so the union stays a bound (§25).
    CHECK(lipschitzEstimate(treeOf(r)) <= 1.001f);
}

TEST_CASE("SDF recurse limits: levels, scale and loop nesting", "[sdf][space]") {
    SdfNode r = unary(SdfNodeKind::Recurse, sphere(0.5f));
    r.scale = 2.0f;
    r.count = spatial::kMaxSdfRecurseLevels;
    CHECK(treeOf(r).validate());
    r.count = spatial::kMaxSdfRecurseLevels + 1;
    CHECK_FALSE(treeOf(r).validate());
    r.count = 2;
    r.scale = 0.0f;
    CHECK_FALSE(treeOf(r).validate());
    r.scale = 2.0f;
    SdfNode two = unary(SdfNodeKind::Recurse, r);
    two.scale = 2.0f;
    CHECK(treeOf(two).validate());
    SdfNode three = unary(SdfNodeKind::Recurse, two);
    three.scale = 2.0f;
    CHECK_FALSE(treeOf(three).validate()); // kMaxSdfLoops = 2
    // A truncated program (an END without its BEGIN) is far rather than out of bounds.
    std::vector<spatial::SdfNodeGpu> packed;
    REQUIRE(spatial::packSdfTree(treeOf(r), packed) > 2);
    packed.erase(packed.begin());
    CHECK(spatial::evaluatePacked(packed, glm::vec3(0.1f), 0.0) >= 1e8f);
}

TEST_CASE("SDF node names: validation, JSON and structural hash", "[sdf][space]") {
    SdfNode root = combo(SdfNodeKind::Union, {sphere(1.0f), box(glm::vec3(0.5f))});
    root.name = "hall";
    root.children[1].name = "columns_2";
    SdfTree t = treeOf(root);
    CHECK(t.validate());
    const auto back = SdfTree::fromJson(t.toJson());
    REQUIRE(back);
    CHECK(back->root.name == "hall");
    CHECK(back->root.children[1].name == "columns_2");
    CHECK(back->structuralHash() == t.structuralHash());
    SdfTree renamed = t;
    renamed.root.name = "hall2";
    CHECK(renamed.structuralHash() != t.structuralHash());
    SdfTree dup = t;
    dup.root.children[0].name = "hall";
    CHECK_FALSE(dup.validate());
    SdfTree digits = t;
    digits.root.name = "12";
    CHECK_FALSE(digits.validate());
    SdfTree slash = t;
    slash.root.name = "a/b";
    CHECK_FALSE(slash.validate());
}

TEST_CASE("SDF parameters: named nodes, count, axis, march and look", "[scene][sdf][params][space]") {
    scene::SdfObject rest;
    rest.name = "space";
    SdfNode ring = unary(SdfNodeKind::PolarRepeat, translate({3, 0, 0}, box(glm::vec3(0.3f, 2.0f, 0.3f))));
    ring.name = "ring";
    ring.count = 8;
    SdfNode fold = unary(SdfNodeKind::Fold, sphere(0.5f));
    fold.axis = {1, 0, 0};
    SdfNode morph = combo(SdfNodeKind::Morph, {ring, fold});
    morph.name = "state";
    rest.tree.root = morph;
    rest.boundsMin = glm::vec3(-10.0f);
    rest.boundsMax = glm::vec3(10.0f);
    REQUIRE(rest.validate());

    params::ParameterSet params;
    const scene::SdfParameters p = scene::registerSdfParameters(params, rest, "sdf/space/");
    // Named nodes by name; the unnamed fold by its pre-order index (state 1, ring 2, translate 3,
    // box 4, fold 5, sphere 6).
    for (const char* rel : {"node/state/amount", "node/ring/count", "node/ring/enabled", "node/5/axis", "node/5/offset",
                            "node/3/translation", "march/maxSteps", "march/epsilon", "march/stepScale",
                            "march/maxDistance", "look/ao/strength", "look/ao/distance", "look/edge/intensity",
                            "look/edge/width", "look/edge/color", "look/shadow/strength", "look/shadow/softness",
                            "look/shadow/direction", "look/shadow/steps"}) {
        INFO(rel);
        CHECK(params.find(std::string("sdf/space/") + rel) != nullptr);
    }
    CHECK(params.find("sdf/space/node/1/amount") == nullptr);
    CHECK(params.find("sdf/space/node/2/count") == nullptr);
    CHECK(params.find("sdf/space/node/ring/count")->kind() == params::ParamKind::Int);
    CHECK(params.find("sdf/space/look/edge/color")->kind() == params::ParamKind::Color);

    params.find("sdf/space/node/state/amount")->setBaseComponent(0, 0.75f);
    params.find("sdf/space/node/ring/count")->setBaseComponent(0, 12.0f);
    params.find("sdf/space/node/5/axis")->setBaseComponent(1, 1.0f);
    params.find("sdf/space/march/maxSteps")->setBaseComponent(0, 200.0f);
    params.find("sdf/space/march/maxDistance")->setBaseComponent(0, 60.0f);
    params.find("sdf/space/look/edge/intensity")->setBaseComponent(0, 3.0f);
    params.find("sdf/space/look/shadow/steps")->setBaseComponent(0, 48.0f);
    params.resetFinals();
    scene::SdfObject live = rest;
    CHECK(scene::applySdfParameters(p, rest, live));
    CHECK(live.tree.root.amount == 0.75f);
    CHECK(live.tree.root.children[0].count == 12);
    CHECK(live.tree.root.children[1].axis == glm::vec3(1.0f, 1.0f, 0.0f));
    CHECK(live.maxSteps == 200);
    CHECK(live.maxDistance == 60.0f);
    CHECK(live.look.edgeIntensity == 3.0f);
    CHECK(live.look.shadowSteps == 48);
    CHECK(live.validate());
    scene::unregisterSdfParameters(params, p);
    CHECK(params.size() == 0);
}

TEST_CASE("SdfObject march cap and look round trip through JSON and validate", "[scene][sdf][space]") {
    scene::SdfObject o;
    o.name = "space";
    o.maxDistance = 120.0f;
    o.look.aoStrength = 0.6f;
    o.look.aoDistance = 2.0f;
    o.look.edgeIntensity = 4.0f;
    o.look.edgeWidth = 0.03f;
    o.look.edgeColor = {0.1f, 0.9f, 0.7f};
    o.look.shadowStrength = 0.5f;
    o.look.shadowSoftness = 12.0f;
    o.look.shadowDirection = {0.2f, 1.0f, -0.4f};
    o.look.shadowSteps = 40;
    const auto back = scene::SdfObject::fromJson(o.toJson());
    REQUIRE(back);
    CHECK(back->maxDistance == 120.0f);
    CHECK(back->look.aoStrength == 0.6f);
    CHECK(back->look.aoDistance == 2.0f);
    CHECK(back->look.edgeIntensity == 4.0f);
    CHECK(back->look.edgeWidth == 0.03f);
    CHECK(back->look.edgeColor == glm::vec3(0.1f, 0.9f, 0.7f));
    CHECK(back->look.shadowStrength == 0.5f);
    CHECK(back->look.shadowSoftness == 12.0f);
    CHECK(back->look.shadowDirection == glm::vec3(0.2f, 1.0f, -0.4f));
    CHECK(back->look.shadowSteps == 40);
    // The depth-only opt-outs (default on: the ADR-027 behaviour).
    CHECK(back->depthPrepass);
    CHECK(back->castShadows);
    scene::SdfObject optOut = o;
    optOut.depthPrepass = false;
    optOut.castShadows = false;
    const auto optBack = scene::SdfObject::fromJson(optOut.toJson());
    REQUIRE(optBack);
    CHECK_FALSE(optBack->depthPrepass);
    CHECK_FALSE(optBack->castShadows);
    CHECK(optOut.structuralHash() == o.structuralHash());
    // Old files (no look block) keep the defaults: everything off.
    const auto plain = scene::SdfObject::fromJson(nlohmann::json{{"name", "x"}});
    REQUIRE(plain);
    CHECK(plain->look.aoStrength == 0.0f);
    CHECK(plain->look.edgeIntensity == 0.0f);
    CHECK(plain->look.shadowStrength == 0.0f);
    CHECK(plain->maxDistance == 0.0f);
    // Bad values are errors.
    auto bad = o.toJson();
    bad["look"]["shadowSteps"] = 0;
    CHECK_FALSE(scene::SdfObject::fromJson(bad));
    bad = o.toJson();
    bad["maxDistance"] = -1.0f;
    CHECK_FALSE(scene::SdfObject::fromJson(bad));
    bad = o.toJson();
    bad["look"]["edgeWidth"] = 0.0f;
    CHECK_FALSE(scene::SdfObject::fromJson(bad));
    // The look and the cap are per-frame uniforms, not structure.
    scene::SdfObject other = o;
    other.look.edgeIntensity = 9.0f;
    other.maxDistance = 5.0f;
    CHECK(other.structuralHash() == o.structuralHash());
}

TEST_CASE("SDF compile: WGSL source, parameter table and structural key", "[sdf][space]") {
    SdfNode ring = unary(SdfNodeKind::PolarRepeat, translate({3, 0, 0}, box(glm::vec3(0.3f, 2.0f, 0.3f))));
    ring.count = 8;
    SdfNode nest = unary(SdfNodeKind::Recurse, sphere(0.5f));
    nest.count = 2;
    nest.scale = 2.0f;
    SdfNode morph = combo(SdfNodeKind::Morph, {ring, nest, box(glm::vec3(1.0f))});
    morph.amount = 0.5f;
    SdfNode root = unary(SdfNodeKind::Twist, morph);
    root.amount = 0.1f;
    SdfTree tree = treeOf(root);
    REQUIRE(tree.validate());

    std::vector<spatial::SdfNodeGpu> table;
    const std::string src = spatial::sdfCompileWgsl(tree, table);
    CHECK(src.find("fn sdfField(offset: u32, count: u32, p: vec3<f32>, t: f32, world: mat4x4<f32>) -> f32") !=
          std::string::npos);
    // Kind-specialised: only the helpers this tree uses, never the generic dispatchers.
    CHECK(src.find("sdfPrimitive(") == std::string::npos);
    CHECK(src.find("sdfWarp(") == std::string::npos);
    CHECK(src.find("sdfFinishUnary(") == std::string::npos);
    CHECK(src.find("sdfWarpTwist(") != std::string::npos);
    CHECK(src.find("sdfWarpPolar(") != std::string::npos);
    CHECK(src.find("sdfRecurseStep(") != std::string::npos);
    CHECK(src.find("sdfFinishUnaryNoise(") == std::string::npos);
    // One table record per effective node, in emission order, with live parameter values.
    CHECK(table.size() == static_cast<std::size_t>(tree.nodeCount()));
    std::vector<spatial::SdfNodeGpu> again;
    spatial::sdfCompileTable(tree, again);
    REQUIRE(again.size() == table.size());
    CHECK(std::memcmp(again.data(), table.data(), table.size() * sizeof(spatial::SdfNodeGpu)) == 0);

    // The key ignores values (a morph's amount, counts, sizes) and sees structure.
    const std::uint64_t key = spatial::sdfCompileKey(tree);
    SdfTree moved = tree;
    moved.root.amount = 0.9f;
    moved.root.children[0].amount = 2.0f;
    moved.root.children[0].children[0].count = 12;
    CHECK(spatial::sdfCompileKey(moved) == key);
    std::string movedSrc = spatial::sdfCompileWgsl(moved, table);
    CHECK(movedSrc == src);
    SdfTree restructured = tree;
    restructured.root.children[0].children[2].enabled = false;
    CHECK(spatial::sdfCompileKey(restructured) != key);
    SdfTree rekinded = tree;
    rekinded.root.children[0].children[2].kind = SdfNodeKind::Sphere;
    CHECK(spatial::sdfCompileKey(rekinded) != key);
}

TEST_CASE("SDF compiled trees are not bound by the interpreter's stacks (ADR-1005)", "[sdf][space]") {
    // Ten nested unary operators: two past the interpreter's 8-entry point stack.
    constexpr int kNesting = spatial::kMaxSdfStack + 2;
    SdfNode deep = sphere(1.0f);
    for (int i = 0; i < kNesting; ++i) {
        deep = translate({0.1f, 0.0f, 0.0f}, deep);
    }
    const SdfTree tree = treeOf(deep);
    // The interpreter refuses it; the compiled evaluator accepts it.
    CHECK_FALSE(tree.validate());
    CHECK_FALSE(tree.validate(spatial::SdfEvaluator::Interpreter));
    CHECK(tree.validate(spatial::SdfEvaluator::Compiled));
    // The CPU reference evaluates it exactly (the translations add up to 1 along x).
    CHECK_THAT(tree.evaluate(glm::vec3(1.0f, 0.0f, 0.0f), 0.0), WithinAbs(-1.0f, 1e-5f));
    CHECK_THAT(tree.evaluate(glm::vec3(3.0f, 0.0f, 0.0f), 0.0), WithinAbs(1.0f, 1e-5f));
    // It compiles to straight-line WGSL, one table record per node, with no stack in sight.
    std::vector<spatial::SdfNodeGpu> table;
    const std::string src = spatial::sdfCompileWgsl(tree, table);
    CHECK(table.size() == static_cast<std::size_t>(kNesting + 1));
    CHECK(src.find("fn sdfField(") != std::string::npos);
    CHECK(src.find("pts[") == std::string::npos);

    // An SdfObject is validated for the evaluator it asks for.
    scene::SdfObject o;
    o.name = "deep";
    o.tree = tree;
    o.compile = false;
    CHECK_FALSE(o.validate());
    o.compile = true;
    CHECK(o.validate());
    // ... including when it is loaded from JSON: `compile` is read before the check.
    const auto loaded = scene::SdfObject::fromJson(o.toJson());
    REQUIRE(loaded);
    CHECK(loaded->compile);
    nlohmann::json interpreted = o.toJson();
    interpreted["compile"] = false;
    CHECK_FALSE(scene::SdfObject::fromJson(interpreted));

    // The other limits still hold for compiled trees: depth (kMaxSdfDepth) ...
    SdfNode tooDeep = sphere(1.0f);
    for (int i = 0; i < spatial::kMaxSdfDepth; ++i) {
        tooDeep = translate({0.0f, 0.0f, 0.0f}, tooDeep);
    }
    CHECK_FALSE(treeOf(tooDeep).validate(spatial::SdfEvaluator::Compiled));
    CHECK(treeOf(tooDeep.children[0]).validate(spatial::SdfEvaluator::Compiled));
    // ... and arity.
    SdfNode twoChildren = translate({0.0f, 0.0f, 0.0f}, deep);
    twoChildren.children.push_back(sphere(1.0f));
    CHECK_FALSE(treeOf(twoChildren).validate(spatial::SdfEvaluator::Compiled));
}
