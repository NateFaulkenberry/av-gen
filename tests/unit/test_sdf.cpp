#include "spatial/sdf.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <utility>
#include <vector>

using namespace avgen;
using namespace avgen::spatial;
using Catch::Matchers::WithinAbs;

namespace {

double d(float v) {
    return static_cast<double>(v);
}

constexpr float kFar = 1e9f;

void checkVec(const glm::vec3& v, const glm::vec3& expected, double tol = 1e-5) {
    CHECK_THAT(d(v.x), WithinAbs(d(expected.x), tol));
    CHECK_THAT(d(v.y), WithinAbs(d(expected.y), tol));
    CHECK_THAT(d(v.z), WithinAbs(d(expected.z), tol));
}

SdfNode node(SdfNodeKind kind) {
    SdfNode n;
    n.kind = kind;
    return n;
}

SdfNode sphere(float radius) {
    SdfNode n = node(SdfNodeKind::Sphere);
    n.radius = radius;
    return n;
}

SdfNode box(const glm::vec3& size) {
    SdfNode n = node(SdfNodeKind::Box);
    n.size = size;
    return n;
}

SdfNode unary(SdfNodeKind kind, SdfNode child) {
    SdfNode n = node(kind);
    n.children.push_back(std::move(child));
    return n;
}

SdfNode translate(const glm::vec3& t, SdfNode child) {
    SdfNode n = unary(SdfNodeKind::Translate, std::move(child));
    n.translation = t;
    return n;
}

SdfNode combo(SdfNodeKind kind, std::vector<SdfNode> children, float smooth = 0.5f) {
    SdfNode n = node(kind);
    n.children = std::move(children);
    n.smooth = smooth;
    return n;
}

SdfTree treeOf(SdfNode root) {
    SdfTree t;
    t.root = std::move(root);
    return t;
}

float evalAt(const SdfNode& n, const glm::vec3& p, double time = 0.0, const FieldSet* fields = nullptr) {
    return treeOf(n).evaluate(p, time, fields);
}

const std::vector<glm::vec3>& samplePoints() {
    static const std::vector<glm::vec3> points = [] {
        std::vector<glm::vec3> pts;
        for (int i = -3; i <= 3; ++i) {
            for (int j = -3; j <= 3; ++j) {
                for (int k = -3; k <= 3; ++k) {
                    pts.emplace_back(0.7f * static_cast<float>(i) + 0.13f, 0.55f * static_cast<float>(j) - 0.21f,
                                     0.8f * static_cast<float>(k) + 0.07f);
                }
            }
        }
        return pts;
    }();
    return points;
}

// A tree exercising nested domain ops, smooth combinations, displacement and disabled nodes
// (depth 8: translate > noise > union > displacement > combination > op > op > primitive).
SdfNode complexTree() {
    SdfNode ball = sphere(0.8f);
    SdfNode cube = box(glm::vec3(0.6f, 0.4f, 0.5f));
    SdfNode ring = node(SdfNodeKind::Torus);
    ring.radius = 1.2f;
    ring.rounding = 0.25f;
    SdfNode cyl = node(SdfNodeKind::Cylinder);
    cyl.radius = 0.3f;
    cyl.height = 3.0f;

    SdfNode scaled = unary(SdfNodeKind::Scale, std::move(cube));
    scaled.scale = 1.7f;
    scaled.translation = glm::vec3(9.0f); // ignored by Scale (must not leak into the result)
    SdfNode rotated = unary(SdfNodeKind::Rotate, std::move(scaled));
    rotated.rotationDegrees = glm::vec3(30.0f, -45.0f, 12.0f);
    SdfNode twisted = unary(SdfNodeKind::Twist, std::move(ring));
    twisted.amount = 0.6f;
    SdfNode bent = unary(SdfNodeKind::Bend, std::move(cyl));
    bent.amount = 0.4f;
    SdfNode repeated = unary(SdfNodeKind::Repeat, sphere(0.3f));
    repeated.size = glm::vec3(1.5f, 0.0f, 1.5f);
    repeated.count = 2;
    SdfNode polar = unary(SdfNodeKind::PolarRepeat, translate(glm::vec3(1.4f, 0.0f, 0.0f), sphere(0.35f)));
    polar.count = 5;
    SdfNode mirrored = unary(SdfNodeKind::Mirror, translate(glm::vec3(0.0f, 1.0f, 0.0f), sphere(0.4f)));
    mirrored.size = glm::vec3(0.0f, 1.0f, 0.0f);

    SdfNode disabledSphere = sphere(0.1f);
    disabledSphere.enabled = false;
    SdfNode disabledTranslate = translate(glm::vec3(5.0f), sphere(0.2f));
    disabledTranslate.enabled = false;

    SdfNode blend = combo(SdfNodeKind::SmoothUnion,
                          {std::move(ball), std::move(rotated), std::move(twisted), std::move(disabledSphere)}, 0.4f);
    SdfNode wavy = unary(SdfNodeKind::DisplaceWave, std::move(blend));
    wavy.amount = 0.05f;
    wavy.frequency = 4.0f;
    wavy.speed = 1.3f;
    wavy.axis = glm::vec3(0.3f, 1.0f, 0.2f);

    SdfNode cut = combo(SdfNodeKind::SmoothDifference, {sphere(1.1f), std::move(bent)}, 0.3f);
    SdfNode cells = unary(SdfNodeKind::DisplaceVoronoi, std::move(cut));
    cells.amount = 0.08f;
    cells.frequency = 1.7f;
    cells.seed = 3;

    SdfNode inter = combo(SdfNodeKind::SmoothIntersection,
                          {translate(glm::vec3(0.3f, 0.2f, 0.1f), box(glm::vec3(0.9f))), sphere(1.0f)}, 0.2f);
    SdfNode fielded = unary(SdfNodeKind::DisplaceField, std::move(inter));
    fielded.amount = 0.2f;
    fielded.reference = "pulse";

    SdfNode all = combo(SdfNodeKind::Union, {std::move(wavy), std::move(cells), std::move(fielded), std::move(repeated),
                                             std::move(polar), std::move(mirrored), std::move(disabledTranslate)});
    SdfNode noisy = unary(SdfNodeKind::DisplaceNoise, std::move(all));
    noisy.amount = 0.15f;
    noisy.frequency = 2.3f;
    noisy.speed = 0.7f;
    noisy.seed = 9;
    return translate(glm::vec3(0.2f, -0.1f, 0.3f), std::move(noisy));
}

FieldSet testFields() {
    FieldSet set;
    FieldSpec a;
    a.name = "other";
    a.strength = 0.1f;
    FieldSpec b;
    b.name = "pulse";
    b.strength = 0.65f;
    set.fields = {a, b};
    return set;
}

} // namespace

// ---- primitives -------------------------------------------------------------------------------

TEST_CASE("SDF sphere: exact distances", "[sdf]") {
    const SdfNode s = sphere(1.0f);
    CHECK_THAT(d(evalAt(s, {0, 0, 0})), WithinAbs(-1.0, 1e-6));
    CHECK_THAT(d(evalAt(s, {2, 0, 0})), WithinAbs(1.0, 1e-6));
    CHECK_THAT(d(evalAt(s, {1, 0, 0})), WithinAbs(0.0, 1e-6));
    CHECK_THAT(d(evalAt(s, {0, 3, 4})), WithinAbs(4.0, 1e-6));
}

TEST_CASE("SDF box: exact distances", "[sdf]") {
    const SdfNode b = box({1.0f, 2.0f, 3.0f});
    CHECK_THAT(d(evalAt(b, {0, 0, 0})), WithinAbs(-1.0, 1e-6));
    CHECK_THAT(d(evalAt(b, {0.5f, 0, 0})), WithinAbs(-0.5, 1e-6));
    CHECK_THAT(d(evalAt(b, {3, 0, 0})), WithinAbs(2.0, 1e-6));
    CHECK_THAT(d(evalAt(b, {1, 2, 3})), WithinAbs(0.0, 1e-6));
    CHECK_THAT(d(evalAt(b, {2, 3, 0})), WithinAbs(std::sqrt(2.0), 1e-6));
    CHECK_THAT(d(evalAt(b, {0, 0, -5})), WithinAbs(2.0, 1e-6));
}

TEST_CASE("SDF rounded box: rounding shrinks the corners", "[sdf]") {
    SdfNode rb = node(SdfNodeKind::RoundedBox);
    rb.size = glm::vec3(1.0f);
    rb.rounding = 0.25f;
    CHECK_THAT(d(evalAt(rb, {0, 0, 0})), WithinAbs(-1.0, 1e-6));
    CHECK_THAT(d(evalAt(rb, {1, 0, 0})), WithinAbs(0.0, 1e-6));
    CHECK_THAT(d(evalAt(rb, {2, 0, 0})), WithinAbs(1.0, 1e-6));
    // Sharp corner lies outside the rounded solid: 0.25 * sqrt(3) - 0.25.
    CHECK_THAT(d(evalAt(rb, {1, 1, 1})), WithinAbs(0.25 * std::sqrt(3.0) - 0.25, 1e-6));
    CHECK(evalAt(rb, {1, 1, 1}) > 0.0f);
    CHECK(evalAt(box(glm::vec3(1.0f)), {1, 1, 1}) == 0.0f);
}

TEST_CASE("SDF cylinder: radius and height along Y", "[sdf]") {
    SdfNode c = node(SdfNodeKind::Cylinder);
    c.radius = 1.0f;
    c.height = 2.0f;
    CHECK_THAT(d(evalAt(c, {0, 0, 0})), WithinAbs(-1.0, 1e-6));
    CHECK_THAT(d(evalAt(c, {2, 0, 0})), WithinAbs(1.0, 1e-6));
    CHECK_THAT(d(evalAt(c, {0, 2, 0})), WithinAbs(1.0, 1e-6));
    CHECK_THAT(d(evalAt(c, {0, 1, 0})), WithinAbs(0.0, 1e-6));
    CHECK_THAT(d(evalAt(c, {0, 0, 1})), WithinAbs(0.0, 1e-6));
    CHECK_THAT(d(evalAt(c, {2, 2, 0})), WithinAbs(std::sqrt(2.0), 1e-6));
    CHECK_THAT(d(evalAt(c, {0.5f, 0.9f, 0})), WithinAbs(-0.1, 1e-6));
}

TEST_CASE("SDF capsule: segment of length height plus round caps", "[sdf]") {
    SdfNode c = node(SdfNodeKind::Capsule);
    c.radius = 0.5f;
    c.height = 2.0f;
    CHECK_THAT(d(evalAt(c, {0, 0, 0})), WithinAbs(-0.5, 1e-6));
    CHECK_THAT(d(evalAt(c, {0, -1, 0})), WithinAbs(-0.5, 1e-6));
    CHECK_THAT(d(evalAt(c, {0, 1.5f, 0})), WithinAbs(0.0, 1e-6));
    CHECK_THAT(d(evalAt(c, {0, 2, 0})), WithinAbs(0.5, 1e-6));
    CHECK_THAT(d(evalAt(c, {1, 0, 0})), WithinAbs(0.5, 1e-6));
    CHECK_THAT(d(evalAt(c, {0, -1, 1})), WithinAbs(0.5, 1e-6));
}

TEST_CASE("SDF torus: major radius and minor rounding", "[sdf]") {
    SdfNode t = node(SdfNodeKind::Torus);
    t.radius = 2.0f;
    t.rounding = 0.5f;
    CHECK_THAT(d(evalAt(t, {2, 0, 0})), WithinAbs(-0.5, 1e-6));
    CHECK_THAT(d(evalAt(t, {0, 0, 0})), WithinAbs(1.5, 1e-6));
    CHECK_THAT(d(evalAt(t, {2.5f, 0, 0})), WithinAbs(0.0, 1e-6));
    CHECK_THAT(d(evalAt(t, {0, 0, 4})), WithinAbs(1.5, 1e-6));
    CHECK_THAT(d(evalAt(t, {2, 0.5f, 0})), WithinAbs(0.0, 1e-6));
    CHECK_THAT(d(evalAt(t, {0, 0, -2})), WithinAbs(-0.5, 1e-6));
}

TEST_CASE("SDF plane: normalised axis and offset", "[sdf]") {
    SdfNode p = node(SdfNodeKind::Plane);
    p.axis = glm::vec3(0.0f, 2.0f, 0.0f);
    p.offset = 1.0f;
    CHECK_THAT(d(evalAt(p, {0, 3, 0})), WithinAbs(2.0, 1e-6));
    CHECK_THAT(d(evalAt(p, {5, 1, -5})), WithinAbs(0.0, 1e-6));
    CHECK_THAT(d(evalAt(p, {0, 0, 0})), WithinAbs(-1.0, 1e-6));
    p.axis = glm::vec3(1.0f, 1.0f, 0.0f);
    p.offset = 0.0f;
    CHECK_THAT(d(evalAt(p, {1, 1, 0})), WithinAbs(std::sqrt(2.0), 1e-6));
}

TEST_CASE("SDF cone: apex at +height/2, base at -height/2", "[sdf]") {
    SdfNode c = node(SdfNodeKind::Cone);
    c.radius = 1.0f;
    c.height = 2.0f;
    CHECK_THAT(d(evalAt(c, {0, 1, 0})), WithinAbs(0.0, 1e-6));   // apex
    CHECK_THAT(d(evalAt(c, {0, -1, 0})), WithinAbs(0.0, 1e-6));  // base centre
    CHECK_THAT(d(evalAt(c, {1, -1, 0})), WithinAbs(0.0, 1e-6));  // base rim
    CHECK_THAT(d(evalAt(c, {0, 0, 0})), WithinAbs(-1.0 / std::sqrt(5.0), 1e-6));
    CHECK_THAT(d(evalAt(c, {0, 2, 0})), WithinAbs(1.0, 1e-6));   // above the apex
    CHECK_THAT(d(evalAt(c, {0, -2, 0})), WithinAbs(1.0, 1e-6));  // below the base
    CHECK_THAT(d(evalAt(c, {2, -1, 0})), WithinAbs(1.0, 1e-6));  // beside the rim
    CHECK(evalAt(c, {1, 1, 0}) > 0.0f);
    CHECK_THAT(d(evalAt(c, {0.5f, 0, 0})), WithinAbs(0.0, 1e-6)); // on the slant (radius 0.5 at y = 0)
    CHECK(evalAt(c, {0.6f, 0, 0}) > 0.0f);
    CHECK(evalAt(c, {0.4f, 0, 0}) < 0.0f);
}

// ---- combinations --------------------------------------------------------------------------------

TEST_CASE("SDF union/intersection/difference fold children in order", "[sdf]") {
    const SdfNode a = sphere(1.0f);
    const SdfNode b = translate({1.0f, 0.0f, 0.0f}, box(glm::vec3(0.5f)));
    const SdfNode c = translate({0.0f, 0.8f, 0.0f}, sphere(0.3f));
    const SdfNode u = combo(SdfNodeKind::Union, {a, b, c});
    const SdfNode i = combo(SdfNodeKind::Intersection, {a, b, c});
    const SdfNode df = combo(SdfNodeKind::Difference, {a, b, c});
    for (const glm::vec3& p : samplePoints()) {
        const float da = evalAt(a, p);
        const float db = evalAt(b, p);
        const float dc = evalAt(c, p);
        CHECK(evalAt(u, p) == std::min(da, std::min(db, dc)));
        CHECK(evalAt(i, p) == std::max(da, std::max(db, dc)));
        CHECK(evalAt(df, p) == std::max(std::max(da, -db), -dc));
    }
    CHECK(evalAt(df, {0, 0, 0}) < 0.0f);
    CHECK(evalAt(df, {0.9f, 0, 0}) > 0.0f); // carved by the box
}

TEST_CASE("SDF smooth union: never above min, equal to min away from the blend", "[sdf]") {
    const SdfNode a = sphere(1.0f);
    const SdfNode b = translate({2.5f, 0.0f, 0.0f}, sphere(1.0f));
    const SdfNode su = combo(SdfNodeKind::SmoothUnion, {a, b}, 0.5f);
    for (const glm::vec3& p : samplePoints()) {
        const float da = evalAt(a, p);
        const float db = evalAt(b, p);
        const float ds = evalAt(su, p);
        CHECK(ds <= std::min(da, db) + 1e-6f);
        if (std::fabs(da - db) >= 0.5f) {
            CHECK_THAT(d(ds), WithinAbs(d(std::min(da, db)), 1e-6));
        }
    }
    // Midway the two spheres are 0.25 apart each; the blend fills the neck.
    const glm::vec3 mid(1.25f, 0.0f, 0.0f);
    CHECK_THAT(d(evalAt(a, mid)), WithinAbs(0.25, 1e-6));
    CHECK(evalAt(su, mid) < 0.25f - 0.1f);
    // k = 0.5, a = b = 0.25: h = 0.5, smin = 0.25 - 0.5 * 0.25 = 0.125.
    CHECK_THAT(d(evalAt(su, mid)), WithinAbs(0.125, 1e-6));
}

TEST_CASE("SDF smooth intersection/difference mirror smooth union", "[sdf]") {
    const SdfNode a = sphere(1.0f);
    const SdfNode b = translate({0.6f, 0.0f, 0.0f}, box(glm::vec3(0.7f)));
    const SdfNode si = combo(SdfNodeKind::SmoothIntersection, {a, b}, 0.3f);
    const SdfNode sd = combo(SdfNodeKind::SmoothDifference, {a, b}, 0.3f);
    for (const glm::vec3& p : samplePoints()) {
        const float da = evalAt(a, p);
        const float db = evalAt(b, p);
        CHECK(evalAt(si, p) >= std::max(da, db) - 1e-6f);
        CHECK(evalAt(sd, p) >= std::max(da, -db) - 1e-6f);
        if (std::fabs(da - db) >= 0.3f) {
            CHECK_THAT(d(evalAt(si, p)), WithinAbs(d(std::max(da, db)), 1e-6));
        }
        if (std::fabs(da + db) >= 0.3f) {
            CHECK_THAT(d(evalAt(sd, p)), WithinAbs(d(std::max(da, -db)), 1e-6));
        }
    }
    // Explicit value at a blend point: a = b = 0 -> -smin(0, 0, k) = k / 4.
    const SdfNode s1 = sphere(1.0f);
    const SdfNode s2 = translate({2.0f, 0.0f, 0.0f}, sphere(1.0f));
    const SdfNode si2 = combo(SdfNodeKind::SmoothIntersection, {s1, s2}, 0.4f);
    CHECK_THAT(d(evalAt(si2, {1, 0, 0})), WithinAbs(0.1, 1e-6));
}

TEST_CASE("SDF combination with no enabled children is far away", "[sdf]") {
    SdfNode off = sphere(1.0f);
    off.enabled = false;
    const SdfNode u = combo(SdfNodeKind::Union, {off, off});
    CHECK(evalAt(u, {0, 0, 0}) == kFar);
    SdfNode offRoot = sphere(1.0f);
    offRoot.enabled = false;
    CHECK(evalAt(offRoot, {0, 0, 0}) == kFar);
    // A disabled unary op passes through to its child; a disabled child of a unary op is far.
    SdfNode t = translate({5.0f, 0.0f, 0.0f}, sphere(1.0f));
    t.enabled = false;
    CHECK_THAT(d(evalAt(t, {0, 0, 0})), WithinAbs(-1.0, 1e-6));
    SdfNode sc = unary(SdfNodeKind::Scale, off);
    sc.scale = 2.0f;
    CHECK(evalAt(sc, {0, 0, 0}) == kFar * 2.0f);
}

// ---- domain operations ----------------------------------------------------------------------------

TEST_CASE("SDF translate moves the child", "[sdf]") {
    const SdfNode t = translate({2.0f, 0.0f, 0.0f}, sphere(1.0f));
    CHECK_THAT(d(evalAt(t, {2, 0, 0})), WithinAbs(-1.0, 1e-6));
    CHECK_THAT(d(evalAt(t, {0, 0, 0})), WithinAbs(1.0, 1e-6));
    CHECK_THAT(d(evalAt(t, {2, 0, 3})), WithinAbs(2.0, 1e-6));
}

TEST_CASE("SDF rotate applies Euler degrees like composition nodes", "[sdf]") {
    SdfNode r = unary(SdfNodeKind::Rotate, box({2.0f, 0.5f, 0.5f}));
    r.rotationDegrees = glm::vec3(0.0f, 90.0f, 0.0f);
    // The long axis (+X) now points along -Z (Ry(90) * (1,0,0) = (0,0,-1)); both ends are inside.
    CHECK_THAT(d(evalAt(r, {0, 0, 1.5f})), WithinAbs(-0.5, 1e-5));
    CHECK_THAT(d(evalAt(r, {0, 0, -1.5f})), WithinAbs(-0.5, 1e-5));
    CHECK_THAT(d(evalAt(r, {1.5f, 0, 0})), WithinAbs(1.0, 1e-5));
    // General rotation: q * local surface point is on the surface.
    r.rotationDegrees = glm::vec3(30.0f, 40.0f, 50.0f);
    const glm::quat q = glm::quat(glm::radians(r.rotationDegrees));
    const glm::vec3 world = q * glm::vec3(2.0f, 0.25f, -0.5f);
    CHECK_THAT(d(evalAt(r, world)), WithinAbs(0.0, 1e-5));
    CHECK_THAT(d(evalAt(r, q * glm::vec3(3.0f, 0.0f, 0.0f))), WithinAbs(1.0, 1e-5));
}

TEST_CASE("SDF scale keeps true distances", "[sdf]") {
    SdfNode s = unary(SdfNodeKind::Scale, sphere(1.0f));
    s.scale = 2.0f;
    CHECK_THAT(d(evalAt(s, {3, 0, 0})), WithinAbs(1.0, 1e-6));
    CHECK_THAT(d(evalAt(s, {0, 0, 0})), WithinAbs(-2.0, 1e-6));
    CHECK_THAT(d(evalAt(s, {0, 2, 0})), WithinAbs(0.0, 1e-6));
    SdfNode nested = unary(SdfNodeKind::Scale, s);
    nested.scale = 0.25f;
    CHECK_THAT(d(evalAt(nested, {1, 0, 0})), WithinAbs(0.5, 1e-6));
}

TEST_CASE("SDF mirror reflects on the masked axes", "[sdf]") {
    SdfNode m = unary(SdfNodeKind::Mirror, translate({2.0f, 0.0f, 0.0f}, sphere(1.0f)));
    m.size = glm::vec3(1.0f, 0.0f, 0.0f);
    CHECK_THAT(d(evalAt(m, {2, 0, 0})), WithinAbs(-1.0, 1e-6));
    CHECK_THAT(d(evalAt(m, {-2, 0, 0})), WithinAbs(-1.0, 1e-6));
    CHECK_THAT(d(evalAt(m, {0, 0, 0})), WithinAbs(1.0, 1e-6));
    // Y not mirrored: a copy translated on Y stays single.
    SdfNode my = unary(SdfNodeKind::Mirror, translate({0.0f, 2.0f, 0.0f}, sphere(1.0f)));
    my.size = glm::vec3(1.0f, 0.0f, 0.0f);
    CHECK_THAT(d(evalAt(my, {0, -2, 0})), WithinAbs(3.0, 1e-6));
}

TEST_CASE("SDF repeat: infinite and limited copies", "[sdf]") {
    SdfNode r = unary(SdfNodeKind::Repeat, sphere(0.5f));
    r.size = glm::vec3(2.0f, 0.0f, 0.0f);
    CHECK_THAT(d(evalAt(r, {4, 0, 0})), WithinAbs(-0.5, 1e-6));
    CHECK_THAT(d(evalAt(r, {-6, 0, 0})), WithinAbs(-0.5, 1e-6));
    CHECK_THAT(d(evalAt(r, {1, 0, 0})), WithinAbs(0.5, 1e-6));
    CHECK_THAT(d(evalAt(r, {0, 2, 0})), WithinAbs(1.5, 1e-6)); // no repeat on Y
    r.count = 1;
    CHECK_THAT(d(evalAt(r, {2, 0, 0})), WithinAbs(-0.5, 1e-6));
    CHECK_THAT(d(evalAt(r, {4, 0, 0})), WithinAbs(1.5, 1e-6)); // clamped to the copy at x = 2
    CHECK_THAT(d(evalAt(r, {-4, 0, 0})), WithinAbs(1.5, 1e-6));
    r.size = glm::vec3(2.0f, 2.0f, 2.0f);
    r.count = 0;
    CHECK_THAT(d(evalAt(r, {4, -2, 6})), WithinAbs(-0.5, 1e-6));
}

TEST_CASE("SDF polar repeat spreads copies about Y", "[sdf]") {
    SdfNode pr = unary(SdfNodeKind::PolarRepeat, translate({2.0f, 0.0f, 0.0f}, sphere(0.5f)));
    pr.count = 4;
    CHECK_THAT(d(evalAt(pr, {2, 0, 0})), WithinAbs(-0.5, 1e-5));
    CHECK_THAT(d(evalAt(pr, {0, 0, 2})), WithinAbs(-0.5, 1e-5));
    CHECK_THAT(d(evalAt(pr, {-2, 0, 0})), WithinAbs(-0.5, 1e-5));
    CHECK_THAT(d(evalAt(pr, {0, 0, -2})), WithinAbs(-0.5, 1e-5));
    // A diagonal point is midway between two copies: 45 degrees off, radius 2.
    const float diag = 2.0f / std::sqrt(2.0f);
    const float expected = glm::length(glm::vec2(2.0f * std::cos(glm::quarter_pi<float>()) - 2.0f,
                                                 2.0f * std::sin(glm::quarter_pi<float>()))) -
                           0.5f;
    CHECK_THAT(d(evalAt(pr, {diag, 0, diag})), WithinAbs(d(expected), 1e-5));
    pr.count = 0; // no-op
    CHECK_THAT(d(evalAt(pr, {0, 0, 2})), WithinAbs(d(std::sqrt(8.0f) - 0.5f), 1e-5));
}

TEST_CASE("SDF twist and bend: identity at zero amount and on their symmetry sets", "[sdf]") {
    SdfNode cyl = node(SdfNodeKind::Cylinder);
    cyl.radius = 0.5f;
    cyl.height = 4.0f;
    SdfNode tw = unary(SdfNodeKind::Twist, cyl);
    tw.amount = 1.3f;
    // A Y cylinder is invariant under rotation about Y, so the twist leaves it unchanged.
    for (const glm::vec3& p : samplePoints()) {
        CHECK_THAT(d(evalAt(tw, p)), WithinAbs(d(evalAt(cyl, p)), 1e-5));
    }
    SdfNode twBox = unary(SdfNodeKind::Twist, box({1.0f, 2.0f, 0.2f}));
    twBox.amount = glm::half_pi<float>(); // 90 degrees per unit of Y
    CHECK_THAT(d(evalAt(twBox, {0.9f, 0, 0})), WithinAbs(d(evalAt(box({1.0f, 2.0f, 0.2f}), {0.9f, 0, 0})), 1e-5));
    // At y = 1 the slab has rotated a quarter turn: the +X direction now hits the thin side.
    CHECK(evalAt(twBox, {0.9f, 1.0f, 0}) > 0.0f);
    CHECK(evalAt(twBox, {0, 1.0f, 0.9f}) < 0.0f);
    twBox.amount = 0.0f;
    CHECK(evalAt(twBox, {0.9f, 1.0f, 0}) == evalAt(box({1.0f, 2.0f, 0.2f}), {0.9f, 1.0f, 0}));

    SdfNode bd = unary(SdfNodeKind::Bend, sphere(1.0f));
    bd.amount = 0.8f;
    // Bend is the identity on the plane x = 0.
    CHECK_THAT(d(evalAt(bd, {0, 0.5f, 0.2f})), WithinAbs(d(evalAt(sphere(1.0f), {0, 0.5f, 0.2f})), 1e-6));
    // Elsewhere it is a rotation of the point about Z by amount * x (a sphere is invariant).
    for (const glm::vec3& p : samplePoints()) {
        CHECK_THAT(d(evalAt(bd, p)), WithinAbs(d(evalAt(sphere(1.0f), p)), 1e-5));
    }
    SdfNode bdBox = unary(SdfNodeKind::Bend, box({3.0f, 0.2f, 0.5f}));
    bdBox.amount = 0.5f;
    CHECK(evalAt(bdBox, {2.5f, 0, 0}) > 0.0f); // the bar curved away from the X axis
    bdBox.amount = 0.0f;
    CHECK(evalAt(bdBox, {2.5f, 0, 0}) < 0.0f);
}

// ---- displacement ------------------------------------------------------------------------------

TEST_CASE("SDF displacement changes the distance by at most the amount", "[sdf]") {
    const SdfNode base = sphere(1.0f);
    SdfNode noisy = unary(SdfNodeKind::DisplaceNoise, base);
    noisy.amount = 0.2f;
    noisy.frequency = 3.0f;
    noisy.speed = 1.0f;
    SdfNode cells = unary(SdfNodeKind::DisplaceVoronoi, base);
    cells.amount = 0.3f;
    cells.frequency = 2.0f;
    SdfNode wavy = unary(SdfNodeKind::DisplaceWave, base);
    wavy.amount = 0.25f;
    wavy.frequency = 5.0f;
    wavy.speed = 2.0f;
    wavy.axis = glm::vec3(1.0f, 0.5f, 0.0f);
    bool noisyMoved = false;
    bool cellsMoved = false;
    bool wavyMoved = false;
    for (const glm::vec3& p : samplePoints()) {
        const float b = evalAt(base, p);
        const float dn = evalAt(noisy, p, 0.4) - b;
        const float dv = evalAt(cells, p, 0.4) - b;
        const float dw = evalAt(wavy, p, 0.4) - b;
        CHECK(std::fabs(dn) <= 0.2f + 1e-6f);
        CHECK(std::fabs(dv) <= 0.3f + 1e-6f);
        CHECK(std::fabs(dw) <= 0.25f + 1e-6f);
        noisyMoved = noisyMoved || std::fabs(dn) > 1e-3f;
        cellsMoved = cellsMoved || std::fabs(dv) > 1e-3f;
        wavyMoved = wavyMoved || std::fabs(dw) > 1e-3f;
    }
    CHECK(noisyMoved);
    CHECK(cellsMoved);
    CHECK(wavyMoved);
    // Time animates noise and waves.
    CHECK(evalAt(noisy, {0.3f, 0.2f, 0.1f}, 0.0) != evalAt(noisy, {0.3f, 0.2f, 0.1f}, 1.0));
    CHECK(evalAt(wavy, {0.3f, 0.2f, 0.1f}, 0.0) != evalAt(wavy, {0.3f, 0.2f, 0.1f}, 0.5));
    // Wave at the origin and t = 0 is sin(0) = 0.
    CHECK_THAT(d(evalAt(wavy, {0, 0, 0}, 0.0)), WithinAbs(-1.0, 1e-6));
}

TEST_CASE("SDF field displacement needs the field set and a known name", "[sdf]") {
    SdfNode f = unary(SdfNodeKind::DisplaceField, sphere(1.0f));
    f.amount = 0.5f;
    f.reference = "pulse";
    const FieldSet fields = testFields();
    CHECK_THAT(d(evalAt(f, {0, 0, 0}, 0.0, &fields)), WithinAbs(-1.0 + 0.5 * 0.65, 1e-6));
    CHECK_THAT(d(evalAt(f, {0, 0, 0}, 0.0, nullptr)), WithinAbs(-1.0, 1e-6));
    f.reference = "missing";
    CHECK_THAT(d(evalAt(f, {0, 0, 0}, 0.0, &fields)), WithinAbs(-1.0, 1e-6));
}

// ---- normals --------------------------------------------------------------------------------------

TEST_CASE("SDF normal matches the analytic sphere normal", "[sdf]") {
    const SdfTree t = treeOf(translate({1.0f, -2.0f, 0.5f}, sphere(1.5f)));
    const glm::vec3 centre(1.0f, -2.0f, 0.5f);
    for (const glm::vec3& dir : {glm::vec3(1, 0, 0), glm::vec3(0, 0, -1), glm::normalize(glm::vec3(1, 2, 3)),
                                 glm::normalize(glm::vec3(-2, 0.5f, 1))}) {
        const glm::vec3 p = centre + dir * 1.5f;
        const glm::vec3 n = t.normal(p, 0.0);
        checkVec(n, dir, 1e-3);
        CHECK_THAT(d(glm::length(n)), WithinAbs(1.0, 1e-6));
        // Normals are well defined off the surface too (gradient of the distance).
        checkVec(t.normal(centre + dir * 2.5f, 0.0), dir, 1e-3);
    }
}

TEST_CASE("SDF normal of an axis-aligned box face", "[sdf]") {
    const SdfTree t = treeOf(box({1.0f, 2.0f, 3.0f}));
    checkVec(t.normal({1.0f, 0.3f, -0.4f}, 0.0), {1, 0, 0}, 1e-3);
    checkVec(t.normal({0.2f, -2.0f, 0.1f}, 0.0), {0, -1, 0}, 1e-3);
}

// ---- packing and the packed interpreter ----------------------------------------------------------

TEST_CASE("SDF packed layout: begin/end pairs around unary ops, post-order combinations", "[sdf]") {
    SdfNode sc = unary(SdfNodeKind::Scale, box(glm::vec3(0.5f)));
    sc.scale = 3.0f;
    SdfNode rot = unary(SdfNodeKind::Rotate, sphere(1.0f));
    rot.rotationDegrees = glm::vec3(10.0f, 20.0f, 30.0f);
    SdfNode disabled = sphere(0.2f);
    disabled.enabled = false;
    SdfNode u = combo(SdfNodeKind::SmoothUnion, {sphere(1.0f), sc, disabled, rot}, 0.25f);
    SdfNode root = translate({1.0f, 2.0f, 3.0f}, u);
    SdfNode wave = unary(SdfNodeKind::DisplaceWave, root);
    wave.amount = 0.1f;
    wave.frequency = 2.0f;
    wave.speed = 0.5f;
    const SdfTree tree = treeOf(wave);
    REQUIRE(tree.validate());

    std::vector<SdfNodeGpu> packed;
    const int count = packSdfTree(tree, packed);
    // wave B, translate B, sphere, scale B, box, scale E, union(2), rotate B, sphere, rotate E,
    // union(2), translate E, wave E: combinations fold binary after every child but the first.
    REQUIRE(count == 13);
    REQUIRE(packed.size() == 13);
    const auto k = [](SdfNodeKind kind) { return static_cast<std::uint32_t>(kind); };
    CHECK(packed[0].kind == k(SdfNodeKind::DisplaceWave));
    CHECK(packed[0].childCount == 0xFFFFu);
    CHECK(packed[1].kind == k(SdfNodeKind::Translate));
    CHECK(packed[1].childCount == 0xFFFFu);
    checkVec(glm::vec3(packed[1].p3), {1, 2, 3});
    CHECK(packed[2].kind == k(SdfNodeKind::Sphere));
    CHECK(packed[2].childCount == 0u);
    CHECK(packed[3].kind == k(SdfNodeKind::Scale));
    CHECK(packed[3].childCount == 0xFFFFu);
    CHECK(packed[3].p1.w == 3.0f);
    CHECK(packed[4].kind == k(SdfNodeKind::Box));
    checkVec(glm::vec3(packed[4].p1), glm::vec3(0.5f));
    CHECK(packed[5].kind == k(SdfNodeKind::Scale));
    CHECK(packed[5].childCount == 1u);
    CHECK(packed[6].kind == k(SdfNodeKind::SmoothUnion));
    CHECK(packed[6].childCount == 2u);
    CHECK(packed[6].p3.w == 0.25f);
    CHECK(packed[7].kind == k(SdfNodeKind::Rotate));
    CHECK(packed[7].childCount == 0xFFFFu);
    const glm::quat q = glm::quat(glm::radians(glm::vec3(10.0f, 20.0f, 30.0f)));
    CHECK(packed[7].p4 == glm::vec4(q.x, q.y, q.z, q.w));
    CHECK(packed[8].kind == k(SdfNodeKind::Sphere));
    CHECK(packed[9].kind == k(SdfNodeKind::Rotate));
    CHECK(packed[9].childCount == 1u);
    CHECK(packed[10].kind == k(SdfNodeKind::SmoothUnion));
    CHECK(packed[10].childCount == 2u); // the disabled sphere is gone: two folds for three children
    CHECK(packed[11].kind == k(SdfNodeKind::Translate));
    CHECK(packed[11].childCount == 1u);
    CHECK(packed[12].kind == k(SdfNodeKind::DisplaceWave));
    CHECK(packed[12].childCount == 1u);
    CHECK(packed[12].p4.x == 2.0f);
    CHECK(packed[12].p5 == glm::vec4(2.0f, 0.5f, 0.0f, 0.0f));
    CHECK(packed[12].fieldSlot == -1);
    CHECK(packed[12].p2.w == 0.1f);
    // A single-child combination emits no fold node; the far-away empty combination is one node.
    REQUIRE(packSdfTree(treeOf(combo(SdfNodeKind::Intersection, {sphere(1.0f)})), packed) == 1);
    CHECK(packed[0].kind == k(SdfNodeKind::Sphere));

    // A unary op over a disabled child wraps the empty combination.
    SdfNode gone = sphere(1.0f);
    gone.enabled = false;
    const SdfTree emptyChild = treeOf(translate({1.0f, 0.0f, 0.0f}, gone));
    REQUIRE(packSdfTree(emptyChild, packed) == 3);
    CHECK(packed[1].kind == k(SdfNodeKind::Union));
    CHECK(packed[1].childCount == 0u);
    CHECK(evaluatePacked(packed, {0, 0, 0}, 0.0) == kFar);
    // Field slots resolve by name.
    SdfNode f = unary(SdfNodeKind::DisplaceField, sphere(1.0f));
    f.reference = "pulse";
    const FieldSet fields = testFields();
    REQUIRE(packSdfTree(treeOf(f), packed, &fields) == 3);
    CHECK(packed[0].fieldSlot == 1);
    CHECK(packed[2].fieldSlot == 1);
    REQUIRE(packSdfTree(treeOf(f), packed, nullptr) == 3);
    CHECK(packed[0].fieldSlot == -1);
}

TEST_CASE("SDF packed evaluation equals the tree evaluation", "[sdf]") {
    const FieldSet fields = testFields();
    std::vector<SdfTree> trees;
    trees.push_back(treeOf(complexTree()));
    {
        SdfNode rb = node(SdfNodeKind::RoundedBox);
        rb.size = glm::vec3(0.8f, 0.5f, 0.6f);
        rb.rounding = 0.2f;
        SdfNode cone = node(SdfNodeKind::Cone);
        cone.radius = 0.7f;
        cone.height = 1.6f;
        SdfNode cap = node(SdfNodeKind::Capsule);
        cap.radius = 0.25f;
        cap.height = 1.5f;
        SdfNode plane = node(SdfNodeKind::Plane);
        plane.axis = glm::vec3(0.2f, 1.0f, 0.1f);
        plane.offset = -0.8f;
        SdfNode rot = unary(SdfNodeKind::Rotate, cone);
        rot.rotationDegrees = glm::vec3(-80.0f, 15.0f, 200.0f);
        SdfNode sc = unary(SdfNodeKind::Scale, rot);
        sc.scale = 0.6f;
        SdfNode diff = combo(SdfNodeKind::Difference, {rb, sc, translate({0.3f, 0.2f, -0.4f}, cap)});
        SdfNode inter = combo(SdfNodeKind::Intersection, {diff, plane});
        SdfNode su = combo(SdfNodeKind::SmoothUnion, {inter, translate({0.0f, -1.2f, 0.0f}, sphere(0.5f))}, 0.35f);
        SdfNode tw = unary(SdfNodeKind::Twist, su);
        tw.amount = -0.9f;
        SdfNode dn = unary(SdfNodeKind::DisplaceNoise, tw);
        dn.amount = 0.1f;
        dn.frequency = 3.1f;
        dn.speed = 0.4f;
        dn.seed = 21;
        trees.push_back(treeOf(dn));
    }
    {
        // Wide combination with a nested wide combination (distance stack pressure).
        std::vector<SdfNode> inner;
        for (int i = 0; i < 8; ++i) {
            inner.push_back(translate({0.4f * static_cast<float>(i), 0.0f, 0.0f}, sphere(0.2f)));
        }
        std::vector<SdfNode> outer;
        for (int i = 0; i < 7; ++i) {
            outer.push_back(translate({0.0f, 0.4f * static_cast<float>(i), 0.0f}, box(glm::vec3(0.15f))));
        }
        outer.push_back(combo(SdfNodeKind::SmoothUnion, inner, 0.2f));
        trees.push_back(treeOf(combo(SdfNodeKind::SmoothUnion, outer, 0.3f)));
    }
    trees.push_back(treeOf(sphere(1.0f)));
    for (const SdfTree& tree : trees) {
        REQUIRE(tree.validate());
        std::vector<SdfNodeGpu> packed;
        const int count = packSdfTree(tree, packed, &fields);
        REQUIRE(count > 0);
        REQUIRE(count <= 2 * tree.nodeCount());
        for (const double time : {0.0, 0.75, 3.2}) {
            for (const glm::vec3& p : samplePoints()) {
                const float a = tree.evaluate(p, time, &fields);
                const float b = evaluatePacked(packed, p, time, &fields);
                CHECK_THAT(d(b), WithinAbs(d(a), 1e-5));
            }
        }
    }
}

TEST_CASE("SDF packed evaluation of an empty program or a truncated program is far", "[sdf]") {
    std::vector<SdfNodeGpu> none;
    CHECK(evaluatePacked(none, {0, 0, 0}, 0.0) == kFar);
    std::vector<SdfNodeGpu> packed;
    REQUIRE(packSdfTree(treeOf(combo(SdfNodeKind::Union, {sphere(1.0f), sphere(2.0f)})), packed) == 3);
    packed.erase(packed.begin()); // the union now pops more than was pushed
    CHECK(evaluatePacked(packed, {0, 0, 0}, 0.0) == kFar);
}

// ---- meshing --------------------------------------------------------------------------------------

namespace {

std::map<std::pair<std::uint32_t, std::uint32_t>, int> edgeCounts(const scene::MeshData& m) {
    std::map<std::pair<std::uint32_t, std::uint32_t>, int> edges;
    for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        for (int e = 0; e < 3; ++e) {
            const std::uint32_t a = m.indices[i + static_cast<std::size_t>(e)];
            const std::uint32_t b = m.indices[i + static_cast<std::size_t>((e + 1) % 3)];
            ++edges[{std::min(a, b), std::max(a, b)}];
        }
    }
    return edges;
}

bool sameBytes(const scene::MeshData& a, const scene::MeshData& b) {
    if (a.vertices.size() != b.vertices.size() || a.indices != b.indices) {
        return false;
    }
    return a.vertices.empty() ||
           std::memcmp(a.vertices.data(), b.vertices.data(), a.vertices.size() * sizeof(scene::Vertex)) == 0;
}

} // namespace

TEST_CASE("SDF meshing: sphere by surface nets is closed, outward and on the surface", "[sdf]") {
    const SdfTree tree = treeOf(sphere(1.0f));
    const int res = 24;
    const glm::vec3 lo(-1.5f);
    const glm::vec3 hi(1.5f);
    auto meshResult = meshSdf(tree, lo, hi, res);
    REQUIRE(meshResult);
    const scene::MeshData& mesh = *meshResult;
    REQUIRE(mesh.valid());
    CHECK(mesh.indices.size() % 3 == 0);
    CHECK(mesh.indices.size() / 3 > 0);
    const float cellSize = glm::length((hi - lo) / static_cast<float>(res));
    for (const scene::Vertex& v : mesh.vertices) {
        CHECK(std::fabs(tree.evaluate(v.position, 0.0)) < cellSize);
        CHECK_THAT(d(glm::length(v.normal)), WithinAbs(1.0, 1e-5));
        CHECK(glm::dot(v.normal, v.position) > 0.0f);
        CHECK(v.uv == glm::vec2(0.0f));
        // Tetrahedron normals of a sphere match the radial direction.
        checkVec(v.normal, glm::normalize(v.position), 2e-2);
    }
    // Closed: every edge is shared by exactly two triangles.
    for (const auto& [edge, count] : edgeCounts(mesh)) {
        CHECK(count == 2);
    }
    // Outward winding: the geometric normal of every triangle agrees with the SDF normal.
    double volume = 0.0;
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const glm::vec3& a = mesh.vertices[mesh.indices[i]].position;
        const glm::vec3& b = mesh.vertices[mesh.indices[i + 1]].position;
        const glm::vec3& c = mesh.vertices[mesh.indices[i + 2]].position;
        const glm::vec3 n = glm::cross(b - a, c - a);
        const glm::vec3 centroid = (a + b + c) / 3.0f;
        CHECK(glm::dot(n, tree.normal(centroid, 0.0)) > 0.0f);
        volume += d(glm::dot(a, glm::cross(b, c)));
    }
    volume /= 6.0;
    CHECK_THAT(volume, WithinAbs(4.0 / 3.0 * 3.14159265, 0.15));
    // Determinism.
    auto again = meshSdf(tree, lo, hi, res);
    REQUIRE(again);
    CHECK(sameBytes(mesh, *again));
}

TEST_CASE("SDF meshing of a difference stays closed and outward", "[sdf]") {
    SdfNode cut = combo(SdfNodeKind::Difference, {sphere(1.0f), translate({0.8f, 0.0f, 0.0f}, sphere(0.6f))});
    const SdfTree tree = treeOf(cut);
    auto mesh = meshSdf(tree, glm::vec3(-1.5f), glm::vec3(1.5f), 20);
    REQUIRE(mesh);
    REQUIRE(mesh->valid());
    // Triangles straddling the concave crease may disagree with the gradient at their centroid
    // (sliver triangles); the rest must agree, and the signed volume must be that of the solid.
    std::size_t disagree = 0;
    double volume = 0.0;
    const std::size_t triangles = mesh->indices.size() / 3;
    for (std::size_t i = 0; i + 2 < mesh->indices.size(); i += 3) {
        const glm::vec3& a = mesh->vertices[mesh->indices[i]].position;
        const glm::vec3& b = mesh->vertices[mesh->indices[i + 1]].position;
        const glm::vec3& c = mesh->vertices[mesh->indices[i + 2]].position;
        const glm::vec3 centroid = (a + b + c) / 3.0f;
        if (glm::dot(glm::cross(b - a, c - a), tree.normal(centroid, 0.0)) <= 0.0f) {
            ++disagree;
        }
        volume += d(glm::dot(a, glm::cross(b, c)));
    }
    volume /= 6.0;
    CHECK(disagree * 100 < triangles);
    // Sphere (4.189) minus the lens shared with the second sphere (0.570).
    CHECK_THAT(volume, WithinAbs(4.18879 - 0.56973, 0.3));
    for (const auto& [edge, count] : edgeCounts(*mesh)) {
        CHECK(count == 2);
    }
}

TEST_CASE("SDF meshing rejects bad resolutions, bounds and sample counts", "[sdf]") {
    const SdfTree tree = treeOf(sphere(1.0f));
    CHECK_FALSE(meshSdf(tree, glm::vec3(-2.0f), glm::vec3(2.0f), 1));
    CHECK_FALSE(meshSdf(tree, glm::vec3(-2.0f), glm::vec3(2.0f), 257));
    CHECK_FALSE(meshSdf(tree, glm::vec3(-2.0f), glm::vec3(2.0f), 256)); // 257^3 samples > 8M
    CHECK_FALSE(meshSdf(tree, glm::vec3(2.0f), glm::vec3(-2.0f), 8));
    CHECK(meshSdf(tree, glm::vec3(-2.0f), glm::vec3(2.0f), 2));
    // A tree entirely outside the bounds yields an empty mesh (not an error).
    auto empty = meshSdf(treeOf(translate({10.0f, 0.0f, 0.0f}, sphere(1.0f))), glm::vec3(-2.0f), glm::vec3(2.0f), 8);
    REQUIRE(empty);
    CHECK(empty->vertices.empty());
    CHECK(empty->indices.empty());
}

// ---- JSON, hashing, validation -------------------------------------------------------------------

TEST_CASE("SDF kind names round trip", "[sdf]") {
    for (int i = 0; i <= static_cast<int>(SdfNodeKind::DisplaceField); ++i) {
        const auto kind = static_cast<SdfNodeKind>(i);
        const auto back = sdfNodeKindFromName(sdfNodeKindName(kind));
        REQUIRE(back);
        CHECK(*back == kind);
    }
    CHECK(sdfNodeKindFromName("roundedBox") == SdfNodeKind::RoundedBox);
    CHECK(sdfNodeKindFromName("smoothDifference") == SdfNodeKind::SmoothDifference);
    CHECK(sdfNodeKindFromName("polarRepeat") == SdfNodeKind::PolarRepeat);
    CHECK(sdfNodeKindFromName("displaceField") == SdfNodeKind::DisplaceField);
    CHECK_FALSE(sdfNodeKindFromName("Sphere"));
    CHECK(sdfNodeIsPrimitive(SdfNodeKind::Cone));
    CHECK_FALSE(sdfNodeIsPrimitive(SdfNodeKind::Union));
    CHECK(sdfNodeMaxChildren(SdfNodeKind::Sphere) == 0);
    CHECK(sdfNodeMaxChildren(SdfNodeKind::Twist) == 1);
    CHECK(sdfNodeMaxChildren(SdfNodeKind::DisplaceNoise) == 1);
    CHECK(sdfNodeMaxChildren(SdfNodeKind::SmoothDifference) == 8);
}

TEST_CASE("SDF JSON round trip preserves the structural hash", "[sdf]") {
    const SdfTree tree = treeOf(complexTree());
    const nlohmann::json j = tree.toJson();
    const std::string text = j.dump();
    auto back = SdfTree::fromJson(nlohmann::json::parse(text));
    REQUIRE(back);
    CHECK(back->structuralHash() == tree.structuralHash());
    CHECK(back->nodeCount() == tree.nodeCount());
    CHECK(back->toJson() == j);
    const FieldSet fields = testFields();
    for (const glm::vec3& p : samplePoints()) {
        CHECK(back->evaluate(p, 0.5, &fields) == tree.evaluate(p, 0.5, &fields));
    }
    // Only non-default members are written.
    const nlohmann::json s = sphere(1.0f).toJson();
    CHECK(s.size() == 1);
    CHECK(s.at("kind") == "sphere");
    const nlohmann::json t = translate({1.0f, 0.0f, 0.0f}, sphere(2.0f)).toJson();
    CHECK(t.contains("translation"));
    CHECK(t.at("children").size() == 1);
    CHECK(t.at("children").at(0).at("radius") == 2.0f);
    CHECK_FALSE(t.contains("radius"));
}

TEST_CASE("SDF JSON tolerates missing members and rejects bad ones", "[sdf]") {
    auto minimal = SdfTree::fromJson(nlohmann::json::parse(R"({"root": {"kind": "box"}})"));
    REQUIRE(minimal);
    CHECK(minimal->root.kind == SdfNodeKind::Box);
    CHECK(minimal->root.size == glm::vec3(1.0f));
    // A bare node is accepted as the root.
    auto bare = SdfTree::fromJson(nlohmann::json::parse(R"({"kind": "torus", "radius": 3})"));
    REQUIRE(bare);
    CHECK(bare->root.kind == SdfNodeKind::Torus);
    CHECK(bare->root.radius == 3.0f);
    CHECK_FALSE(SdfTree::fromJson(nlohmann::json::parse(R"({"root": {"kind": "blob"}})")));
    CHECK_FALSE(SdfTree::fromJson(nlohmann::json::parse(R"({"root": {"kind": "sphere", "radius": "big"}})")));
    CHECK_FALSE(SdfTree::fromJson(nlohmann::json::parse(R"({"root": {"kind": "sphere", "size": [1, 2]}})")));
    CHECK_FALSE(SdfTree::fromJson(nlohmann::json::parse(R"({"root": {"kind": "union", "children": 3}})")));
    CHECK_FALSE(SdfTree::fromJson(nlohmann::json::parse("[]")));
    // Nesting deeper than the depth limit is refused while parsing.
    std::string deep;
    for (int i = 0; i < 10; ++i) {
        deep += R"({"kind": "translate", "children": [)";
    }
    deep += R"({"kind": "sphere"})";
    for (int i = 0; i < 10; ++i) {
        deep += "]}";
    }
    CHECK_FALSE(SdfTree::fromJson(nlohmann::json::parse(deep)));
}

TEST_CASE("SDF structural hash covers every member", "[sdf]") {
    const SdfTree base = treeOf(complexTree());
    const std::uint64_t h = base.structuralHash();
    CHECK(h == treeOf(complexTree()).structuralHash());
    SdfTree t = base;
    t.root.translation.z += 0.01f;
    CHECK(t.structuralHash() != h);
    t = base;
    t.root.children[0].reference = "other";
    CHECK(t.structuralHash() != h);
    t = base;
    t.root.children[0].enabled = false;
    CHECK(t.structuralHash() != h);
    t = base;
    t.root.children[0].children[0].seed += 1;
    CHECK(t.structuralHash() != h);
    t = base;
    t.root.children[0].children[0].children.pop_back();
    CHECK(t.structuralHash() != h);
}

TEST_CASE("SDF validate: limits on depth, arity, counts and parameters", "[sdf]") {
    CHECK(treeOf(complexTree()).validate());
    CHECK(treeOf(sphere(1.0f)).validate());

    // Depth: 8 levels pass, 9 fail.
    SdfNode chain = sphere(1.0f);
    for (int i = 0; i < 7; ++i) {
        chain = translate({0.1f, 0.0f, 0.0f}, chain);
    }
    CHECK(treeOf(chain).validate());
    CHECK_FALSE(treeOf(translate({0.1f, 0.0f, 0.0f}, chain)).validate());

    // Arity.
    CHECK_FALSE(treeOf(combo(SdfNodeKind::Union, {})).validate());
    SdfNode sphereWithChild = sphere(1.0f);
    sphereWithChild.children.push_back(sphere(1.0f));
    CHECK_FALSE(treeOf(sphereWithChild).validate());
    SdfNode twoChildren = translate({0.0f, 0.0f, 0.0f}, sphere(1.0f));
    twoChildren.children.push_back(sphere(1.0f));
    CHECK_FALSE(treeOf(twoChildren).validate());
    CHECK_FALSE(treeOf(node(SdfNodeKind::Twist)).validate());
    SdfNode disabledChild = sphere(1.0f);
    disabledChild.enabled = false;
    CHECK_FALSE(treeOf(translate({0.0f, 0.0f, 0.0f}, disabledChild)).validate());
    std::vector<SdfNode> nine(9, sphere(1.0f));
    CHECK_FALSE(treeOf(combo(SdfNodeKind::Union, nine)).validate());
    std::vector<SdfNode> eight(8, sphere(1.0f));
    CHECK(treeOf(combo(SdfNodeKind::Union, eight)).validate());

    // Node count: 1 + 8 + 64 = 73 > 64.
    std::vector<SdfNode> groups(8, combo(SdfNodeKind::Union, eight));
    const SdfTree big = treeOf(combo(SdfNodeKind::Union, groups));
    CHECK(big.nodeCount() == 73);
    CHECK_FALSE(big.validate());
    // 1 + 7 + 8 = 16 nodes is fine.
    std::vector<SdfNode> seven(7, sphere(1.0f));
    seven.push_back(combo(SdfNodeKind::Union, eight));
    CHECK(treeOf(combo(SdfNodeKind::Union, seven)).validate());
    // Wide unions nested as last children: binary folds keep the distance stack at nesting + 1.
    std::vector<SdfNode> level2(7, sphere(1.0f));
    level2.push_back(combo(SdfNodeKind::Union, eight));
    std::vector<SdfNode> level1(7, sphere(1.0f));
    level1.push_back(combo(SdfNodeKind::Union, level2));
    const SdfTree deepWide = treeOf(combo(SdfNodeKind::Union, level1));
    CHECK(deepWide.nodeCount() == 25);
    CHECK(deepWide.validate());
    std::vector<SdfNodeGpu> packed;
    CHECK(packSdfTree(deepWide, packed) == 22 + 7 + 7 + 7); // 22 spheres + (8-1) folds x 3

    // Parameters.
    SdfNode neg = sphere(-1.0f);
    CHECK_FALSE(treeOf(neg).validate());
    SdfNode negSize = box({1.0f, -1.0f, 1.0f});
    CHECK_FALSE(treeOf(negSize).validate());
    SdfNode negCount = unary(SdfNodeKind::Repeat, sphere(1.0f));
    negCount.count = -1;
    CHECK_FALSE(treeOf(negCount).validate());
    SdfNode nan = sphere(1.0f);
    nan.radius = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(treeOf(nan).validate());
    SdfNode zeroScale = unary(SdfNodeKind::Scale, sphere(1.0f));
    zeroScale.scale = 0.0f;
    CHECK_FALSE(treeOf(zeroScale).validate());
    SdfNode zeroAxis = node(SdfNodeKind::Plane);
    zeroAxis.axis = glm::vec3(0.0f);
    CHECK_FALSE(treeOf(zeroAxis).validate());
}
