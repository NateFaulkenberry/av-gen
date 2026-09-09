// Splines in procedural geometry (ADR-026): the Spline distribution (counts, placements, frames,
// spacing, closed splines, missing spline), the Path deformer (straight and circular splines,
// amount, offset, fit scale, deformPointWith), JSON, parameters, validation and the contextual
// hash that rebuilds when the referenced spline changes.
#include "params/parameter_set.hpp"
#include "scene/procedural.hpp"
#include "spatial/spline.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <string>
#include <vector>

using namespace avgen;
using namespace avgen::scene;
using Catch::Matchers::WithinAbs;

namespace {

double d(float v) {
    return static_cast<double>(v);
}

void checkVec(const glm::vec3& v, const glm::vec3& expected, double tol = 1e-4) {
    CHECK_THAT(d(v.x), WithinAbs(d(expected.x), tol));
    CHECK_THAT(d(v.y), WithinAbs(d(expected.y), tol));
    CHECK_THAT(d(v.z), WithinAbs(d(expected.z), tol));
}

// A polyline from `from` to `to` (straight: the table interpolates it exactly).
spatial::Spline lineSpline(const std::string& name, glm::vec3 from, glm::vec3 to) {
    spatial::Spline s;
    s.name = name;
    s.kind = spatial::SplineKind::Polyline;
    s.generator = spatial::SplineGenerator::Line;
    s.start = from;
    s.end = to;
    s.count = 8;
    return s;
}

// A closed circle of `radius` around the origin in the XZ plane (axis +Y, first point on +X).
spatial::Spline circleSpline(const std::string& name, float radius) {
    spatial::Spline s;
    s.name = name;
    s.generator = spatial::SplineGenerator::Circle;
    s.closed = true;
    s.radius = radius;
    s.count = 64;
    s.samplesPerSegment = 16;
    return s;
}

Deformer pathDeformer(const std::string& spline, glm::vec3 axis, float amount = 1.0f) {
    Deformer p;
    p.kind = DeformerKind::Path;
    p.spline = spline;
    p.axis = axis;
    p.amount = amount;
    return p;
}

} // namespace

TEST_CASE("Spline distribution and Path deformer names round trip", "[scene][procedural][spline]") {
    CHECK(distributionKindName(DistributionKind::Spline) == std::string("spline"));
    CHECK(distributionKindFromName("spline") == DistributionKind::Spline);
    CHECK(deformerKindName(DeformerKind::Path) == std::string("path"));
    CHECK(deformerKindFromName("path") == DeformerKind::Path);
    CHECK_FALSE(deformerKindFromName("curve").has_value());
}

TEST_CASE("Spline distribution: counts by count and by spacing, open and closed", "[scene][procedural][spline]") {
    const spatial::Spline line = lineSpline("rail", {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 10.0f});
    Distribution dist;
    dist.kind = DistributionKind::Spline;
    dist.spline = "rail";
    dist.count = 7;
    CHECK(dist.instanceCount(&line) == 7);
    CHECK(dist.instanceCount(nullptr) == 7); // count when the spline is unknown
    dist.spacing = 2.0f;
    CHECK(dist.instanceCount(&line) == 6); // floor(10 / 2) + 1: both ends
    CHECK(dist.instanceCount(nullptr) == 7);
    dist.splineStart = 0.25f;
    dist.splineEnd = 0.75f;
    CHECK(dist.instanceCount(&line) == 3); // floor(5 / 2) + 1
    dist.spacing = 100.0f;
    CHECK(dist.instanceCount(&line) == 1); // never below one
    dist.count = 0;
    dist.spacing = 0.0f;
    CHECK(dist.instanceCount(&line) == 1);

    // A closed spline over a whole turn has no duplicate end: floor(length / spacing).
    const spatial::Spline ring = circleSpline("ring", 2.0f);
    const float length = ring.length();
    CHECK_THAT(d(length), WithinAbs(2.0 * glm::pi<double>() * 2.0, 0.02));
    Distribution closed;
    closed.kind = DistributionKind::Spline;
    closed.spline = "ring";
    closed.splineStart = 0.0f;
    closed.splineEnd = 1.0f;
    closed.spacing = 1.0f;
    CHECK(closed.instanceCount(&ring) == static_cast<int>(std::floor(length / 1.0f)));
    closed.splineEnd = 0.5f; // half a turn: an open arc again
    CHECK(closed.instanceCount(&ring) == static_cast<int>(std::floor(0.5f * length / 1.0f)) + 1);
}

TEST_CASE("Spline distribution: placements sit on the spline with aligned frames", "[scene][procedural][spline]") {
    const spatial::Spline line = lineSpline("rail", {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 10.0f});
    Distribution dist;
    dist.kind = DistributionKind::Spline;
    dist.spline = "rail";
    dist.count = 5;
    for (int i = 0; i < 5; ++i) {
        const Transform t = dist.placement(i, &line);
        checkVec(t.position, {0.0f, 0.0f, 2.5f * static_cast<float>(i)});
        // +Z forward along the tangent, +Y up along the normal, unit scale.
        checkVec(t.rotation * glm::vec3(0.0f, 0.0f, 1.0f), {0.0f, 0.0f, 1.0f});
        checkVec(t.rotation * glm::vec3(0.0f, 1.0f, 0.0f), {0.0f, 1.0f, 0.0f});
        checkVec(t.scale, glm::vec3(1.0f));
    }
    // Roll turns the up vector about the tangent (right-handed: pi/2 on +Z sends +Y to -X).
    dist.roll = glm::half_pi<float>();
    checkVec(dist.placement(2, &line).rotation * glm::vec3(0.0f, 1.0f, 0.0f), {-1.0f, 0.0f, 0.0f});
    dist.roll = 0.0f;
    // The offset is in the frame: x along the binormal (+X here), y along the normal, z along the tangent.
    dist.splineOffset = {1.0f, 2.0f, 3.0f};
    checkVec(dist.placement(0, &line).position, {1.0f, 2.0f, 3.0f});
    dist.splineOffset = glm::vec3(0.0f);
    // A sub-range of the length.
    dist.splineStart = 0.25f;
    dist.splineEnd = 0.75f;
    checkVec(dist.placement(0, &line).position, {0.0f, 0.0f, 2.5f});
    checkVec(dist.placement(4, &line).position, {0.0f, 0.0f, 7.5f});
    // Reversed ranges run backwards.
    dist.splineStart = 1.0f;
    dist.splineEnd = 0.0f;
    checkVec(dist.placement(0, &line).position, {0.0f, 0.0f, 10.0f});
    checkVec(dist.placement(4, &line).position, {0.0f, 0.0f, 0.0f});
    dist.splineStart = 0.0f;
    dist.splineEnd = 1.0f;
    // alignToSpline off: identity rotation, positions unchanged.
    dist.alignToSpline = false;
    const Transform plain = dist.placement(3, &line);
    checkVec(plain.position, {0.0f, 0.0f, 7.5f});
    CHECK_THAT(d(plain.rotation.w), WithinAbs(1.0, 1e-6));
    // Spacing mode: instances `spacing` apart from the start.
    dist.alignToSpline = true;
    dist.spacing = 4.0f;
    REQUIRE(dist.instanceCount(&line) == 3);
    checkVec(dist.placement(1, &line).position, {0.0f, 0.0f, 4.0f});
    checkVec(dist.placement(2, &line).position, {0.0f, 0.0f, 8.0f});
    dist.spacing = 0.0f;
    // Without the spline every placement is the identity.
    const Transform none = dist.placement(2, nullptr);
    checkVec(none.position, glm::vec3(0.0f));
    CHECK_THAT(d(none.rotation.w), WithinAbs(1.0, 1e-6));
    checkVec(none.scale, glm::vec3(1.0f));
    // Out-of-range indices clamp.
    checkVec(dist.placement(99, &line).position, {0.0f, 0.0f, 10.0f});
    checkVec(dist.placement(-3, &line).position, {0.0f, 0.0f, 0.0f});
}

TEST_CASE("Spline distribution: a closed spline distributes without the duplicate end", "[scene][procedural][spline]") {
    const spatial::Spline ring = circleSpline("ring", 3.0f);
    Distribution dist;
    dist.kind = DistributionKind::Spline;
    dist.spline = "ring";
    dist.count = 4;
    // Four instances a quarter turn apart: +X, -Z, -X, +Z (angles increase towards -Z about +Y).
    const glm::vec3 expected[4] = {{3.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -3.0f}, {-3.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 3.0f}};
    for (int i = 0; i < 4; ++i) {
        const Transform t = dist.placement(i, &ring);
        checkVec(t.position, expected[i], 0.02);
        CHECK_THAT(d(glm::length(t.position)), WithinAbs(3.0, 0.01));
        // Forward is the circle tangent, up stays +Y (the circle lies in XZ with up +Y).
        const glm::vec3 forward = t.rotation * glm::vec3(0.0f, 0.0f, 1.0f);
        CHECK_THAT(d(glm::dot(forward, glm::normalize(t.position))), WithinAbs(0.0, 0.02));
        checkVec(t.rotation * glm::vec3(0.0f, 1.0f, 0.0f), {0.0f, 1.0f, 0.0f}, 0.01);
    }
    // Two whole turns still wrap; a half turn is an open arc whose last instance is at the far end.
    dist.splineEnd = 2.0f;
    checkVec(dist.placement(0, &ring).position, expected[0], 0.02);
    checkVec(dist.placement(1, &ring).position, expected[2], 0.02);
    dist.splineEnd = 0.5f;
    checkVec(dist.placement(3, &ring).position, expected[2], 0.02);
}

TEST_CASE("generateCloud places along the scene spline and contextualHash follows the spline",
          "[scene][procedural][spline]") {
    spatial::SplineSet splines;
    splines.splines.push_back(lineSpline("rail", {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 8.0f}));
    GenerationContext ctx;
    ctx.splines = &splines;

    ProceduralGeometry g;
    g.distribution.kind = DistributionKind::Spline;
    g.distribution.spline = "rail";
    g.distribution.count = 5;
    const spatial::PointCloud cloud = g.generateCloud(ctx);
    REQUIRE(cloud.count() == 5);
    const auto positions = cloud.positions();
    for (std::size_t i = 0; i < 5; ++i) {
        checkVec(positions[i], {1.0f, 0.0f, 2.0f * static_cast<float>(i)});
    }
    // Without the context the placements are the identity.
    const spatial::PointCloud plain = g.generateCloud();
    REQUIRE(plain.count() == 5);
    checkVec(plain.positions()[4], glm::vec3(0.0f));
    // An unknown name behaves like no spline.
    g.distribution.spline = "missing";
    checkVec(g.generateCloud(ctx).positions()[4], glm::vec3(0.0f));
    g.distribution.spline = "rail";

    // rebuild(ctx) is keyed by contextualHash: editing the spline rebuilds, nothing else changes.
    CHECK(g.rebuild(ctx));
    CHECK(g.structureVersion == 1);
    CHECK_FALSE(g.rebuild(ctx));
    const std::uint64_t before = g.contextualHash(ctx);
    splines.splines[0].end = {1.0f, 0.0f, 16.0f};
    CHECK(g.contextualHash(ctx) != before);
    CHECK(g.rebuild(ctx));
    CHECK(g.structureVersion == 2);
    checkVec(g.cloud.positions()[4], {1.0f, 0.0f, 16.0f});
    // A spline that is not referenced does not matter; a non-spline distribution ignores splines.
    splines.splines.push_back(circleSpline("other", 1.0f));
    CHECK_FALSE(g.rebuild(ctx));
    g.distribution.kind = DistributionKind::Radial;
    CHECK(g.rebuild(ctx));
    const std::uint64_t radial = g.contextualHash(ctx);
    splines.splines[0].end = {1.0f, 0.0f, 20.0f};
    CHECK(g.contextualHash(ctx) == radial);
    CHECK(g.contextualHash(GenerationContext{}) == radial);
}

TEST_CASE("Path deformer: a straight +Z spline is the identity up to translation", "[scene][procedural][spline]") {
    const spatial::Spline line = lineSpline("rail", {0.0f, 0.0f, -5.0f}, {0.0f, 0.0f, 5.0f});
    Deformer path = pathDeformer("rail", {0.0f, 0.0f, 1.0f});
    path.pathScale = 1.0f;
    path.pathOffset = 5.0f; // coord 0 maps to the middle of the line: exact identity
    const glm::vec3 points[] = {{0.5f, 0.0f, 0.0f}, {0.0f, 0.5f, 1.0f}, {-0.3f, 0.4f, -1.0f}, {0.1f, -0.2f, 0.7f}};
    for (const glm::vec3& p : points) {
        checkVec(applyPathDeformer(path, p, line, 2.0f), p);
    }
    // Offsetting the path slides the shape along the tangent; amount 0.5 goes halfway.
    path.pathOffset = 6.0f;
    checkVec(applyPathDeformer(path, points[0], line, 2.0f), points[0] + glm::vec3(0.0f, 0.0f, 1.0f));
    path.amount = 0.5f;
    checkVec(applyPathDeformer(path, points[0], line, 2.0f), points[0] + glm::vec3(0.0f, 0.0f, 0.5f));
    path.amount = 0.0f;
    checkVec(applyPathDeformer(path, points[0], line, 2.0f), points[0]);
    path.amount = 1.0f;
    path.pathOffset = 5.0f;
    // Fit mode (pathScale 0): the source extent maps to the length (10 / 2 = 5 units per unit).
    path.pathScale = 0.0f;
    checkVec(applyPathDeformer(path, {0.0f, 0.0f, 0.5f}, line, 2.0f), {0.0f, 0.0f, 2.5f});
    // pathRoll rotates the cross-section about the tangent.
    path.pathScale = 1.0f;
    path.pathRoll = glm::half_pi<float>();
    checkVec(applyPathDeformer(path, {0.5f, 0.0f, 0.0f}, line, 2.0f), {0.0f, 0.5f, 0.0f});
    path.pathRoll = 0.0f;
    // Other kinds are untouched by applyPathDeformer; applyDeformer ignores Path.
    Deformer twist;
    twist.amount = 1.0f;
    checkVec(applyPathDeformer(twist, points[0], line, 2.0f), points[0]);
    checkVec(applyDeformer(path, points[3], 0.0), points[3]);
}

TEST_CASE("Path deformer: a circle spline bends a Y-axis column into a ring", "[scene][procedural][spline]") {
    const spatial::Spline ring = circleSpline("ring", 2.0f);
    const float length = ring.length();
    Deformer path = pathDeformer("ring", {0.0f, 1.0f, 0.0f});
    path.pathScale = 0.0f; // fit: the column height 2 maps to the whole circumference
    // Points at coord 0 map to distance pathOffset: the circle's first point (+X), then a quarter
    // turn (-Z).
    checkVec(applyPathDeformer(path, glm::vec3(0.0f), ring, 2.0f), {2.0f, 0.0f, 0.0f}, 0.01);
    path.pathOffset = length * 0.25f;
    checkVec(applyPathDeformer(path, glm::vec3(0.0f), ring, 2.0f), {0.0f, 0.0f, -2.0f}, 0.01);
    path.pathOffset = 0.0f;
    // Every point of the column axis lands on the ring; the height wraps once around.
    for (const float y : {-1.0f, -0.5f, 0.0f, 0.25f, 0.5f, 0.99f}) {
        const glm::vec3 bent = applyPathDeformer(path, {0.0f, y, 0.0f}, ring, 2.0f);
        CHECK_THAT(d(glm::length(bent)), WithinAbs(2.0, 0.01));
        CHECK_THAT(d(bent.y), WithinAbs(0.0, 1e-4));
        const float angle = std::atan2(-bent.z, bent.x); // angles increase towards -Z
        const float expected = y * glm::pi<float>(); // y in [-1, 1] -> a full turn from +X
        CHECK_THAT(d(std::remainder(angle - expected, 2.0f * glm::pi<float>())), WithinAbs(0.0, 0.02));
    }
    // The cross-section: for a +Y axis the basis is u = +Z, v = +X. At the start (position +X,
    // tangent -Z) the frame is normal +Y, binormal cross(normal, tangent) = -X (inwards), so the
    // column's +X offset rises along the normal and its +Z offset moves inwards along the binormal.
    checkVec(applyPathDeformer(path, {0.5f, 0.0f, 0.0f}, ring, 2.0f), {2.0f, 0.5f, 0.0f}, 0.01);
    checkVec(applyPathDeformer(path, {0.0f, 0.0f, 0.5f}, ring, 2.0f), {1.5f, 0.0f, 0.0f}, 0.02);
    // Half amount: halfway between the column and the ring.
    path.amount = 0.5f;
    checkVec(applyPathDeformer(path, {0.0f, 0.5f, 0.0f}, ring, 2.0f),
             glm::mix(glm::vec3(0.0f, 0.5f, 0.0f), glm::vec3(0.0f, 0.0f, -2.0f), 0.5f), 0.01);
}

TEST_CASE("deformPointWith resolves Path deformers through the context", "[scene][procedural][spline]") {
    spatial::SplineSet splines;
    splines.splines.push_back(lineSpline("rail", {0.0f, 0.0f, -5.0f}, {0.0f, 0.0f, 5.0f}));
    Deformer path = pathDeformer("rail", {0.0f, 0.0f, 1.0f});
    path.pathScale = 1.0f;
    path.pathOffset = 6.0f; // +1 along Z
    Deformer twist;
    twist.kind = DeformerKind::Twist;
    twist.axis = {0.0f, 0.0f, 1.0f};
    twist.amount = glm::half_pi<float>(); // a quarter turn per unit of z
    const glm::mat4 world = glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, 0.0f));
    DeformContext ctx;
    ctx.splines = &splines;
    ctx.sourceExtent = 2.0f;
    const glm::vec3 p(0.5f, 0.0f, 0.0f);
    // Path then twist: the point slides to z = 1 and turns a quarter turn about Z.
    checkVec(deformPointWith({path, twist}, p, world, 0.0, ctx), {10.0f, 0.5f, 1.0f});
    // Twist then path: no twist at z = 0, then the slide.
    checkVec(deformPointWith({twist, path}, p, world, 0.0, ctx), {10.5f, 0.0f, 1.0f});
    // A missing spline, no spline set or a disabled deformer leaves the point alone.
    Deformer missing = path;
    missing.spline = "nope";
    checkVec(deformPointWith({missing}, p, world, 0.0, ctx), {10.5f, 0.0f, 0.0f});
    checkVec(deformPointWith({path}, p, world, 0.0, DeformContext{}), {10.5f, 0.0f, 0.0f});
    Deformer off = path;
    off.enabled = false;
    checkVec(deformPointWith({off}, p, world, 0.0, ctx), {10.5f, 0.0f, 0.0f});
    // World-space Path deformers are skipped (object space only).
    Deformer worldPath = path;
    worldPath.space = DeformSpace::World;
    checkVec(deformPointWith({worldPath}, p, world, 0.0, ctx), {10.5f, 0.0f, 0.0f});
    // Field deformers still go through the context's field set (none here: skipped).
    Deformer field;
    field.kind = DeformerKind::Field;
    field.field = "f";
    field.amount = 3.0f;
    checkVec(deformPointWith({field, path}, p, world, 0.0, ctx), {10.5f, 0.0f, 1.0f});
    // Matches deformPoint for stacks without Path deformers.
    checkVec(deformPointWith({twist}, {0.5f, 0.0f, 1.0f}, world, 0.0, ctx),
             deformPoint({twist}, {0.5f, 0.0f, 1.0f}, world, 0.0));
}

TEST_CASE("Spline distribution and Path deformer JSON round trip and validation", "[scene][procedural][spline]") {
    ProceduralGeometry g;
    g.name = "rail";
    g.distribution.kind = DistributionKind::Spline;
    g.distribution.spline = "cameraPath";
    g.distribution.count = 12;
    g.distribution.spacing = 0.5f;
    g.distribution.splineStart = 0.1f;
    g.distribution.splineEnd = 0.9f;
    g.distribution.alignToSpline = false;
    g.distribution.roll = 0.3f;
    g.distribution.splineOffset = {0.1f, 0.2f, 0.3f};
    Deformer path = pathDeformer("bend", {0.0f, 0.0f, 1.0f}, 0.75f);
    path.pathOffset = 1.5f;
    path.pathScale = 2.5f;
    path.pathRoll = 0.4f;
    g.deformers.push_back(path);
    const nlohmann::json j = g.toJson();
    CHECK(j["distribution"]["kind"] == "spline");
    CHECK(j["distribution"]["spline"] == "cameraPath");
    CHECK(j["deformers"][0]["kind"] == "path");
    CHECK(j["deformers"][0]["spline"] == "bend");
    auto back = ProceduralGeometry::fromJson(j);
    REQUIRE(back.has_value());
    const Distribution& dd = back->distribution;
    CHECK(dd.kind == DistributionKind::Spline);
    CHECK(dd.spline == "cameraPath");
    CHECK(dd.count == 12);
    CHECK(dd.spacing == 0.5f);
    CHECK(dd.splineStart == 0.1f);
    CHECK(dd.splineEnd == 0.9f);
    CHECK_FALSE(dd.alignToSpline);
    CHECK(dd.roll == 0.3f);
    CHECK(dd.splineOffset == glm::vec3(0.1f, 0.2f, 0.3f));
    REQUIRE(back->deformers.size() == 1);
    const Deformer& bd = back->deformers[0];
    CHECK(bd.kind == DeformerKind::Path);
    CHECK(bd.spline == "bend");
    CHECK(bd.amount == 0.75f);
    CHECK(bd.pathOffset == 1.5f);
    CHECK(bd.pathScale == 2.5f);
    CHECK(bd.pathRoll == 0.4f);
    CHECK(back->structuralHash() == g.structuralHash());
    // The hash covers every spline field of the distribution.
    ProceduralGeometry h = g;
    h.distribution.spline = "other";
    CHECK(h.structuralHash() != g.structuralHash());
    h = g;
    h.distribution.roll = 0.0f;
    CHECK(h.structuralHash() != g.structuralHash());
    h = g;
    h.distribution.splineOffset.z = 1.0f;
    CHECK(h.structuralHash() != g.structuralHash());
    // Deformer fields are per-frame uniforms: not structural.
    h = g;
    h.deformers[0].pathOffset = 9.0f;
    CHECK(h.structuralHash() == g.structuralHash());

    // Validation: a Spline distribution and a Path deformer both need a spline name.
    CHECK(g.validate().has_value());
    ProceduralGeometry bad = g;
    bad.distribution.spline.clear();
    CHECK_FALSE(bad.validate().has_value());
    bad = g;
    bad.deformers[0].spline.clear();
    CHECK_FALSE(bad.validate().has_value());
    bad = g;
    bad.distribution.spacing = -1.0f;
    CHECK_FALSE(bad.validate().has_value());
    // Missing keys default.
    nlohmann::json minimal = {{"name", "x"}, {"distribution", {{"kind", "spline"}, {"spline", "s"}}}};
    auto parsed = ProceduralGeometry::fromJson(minimal);
    REQUIRE(parsed.has_value());
    CHECK(parsed->distribution.splineEnd == 1.0f);
    CHECK(parsed->distribution.alignToSpline);
}

TEST_CASE("Spline distribution and Path deformer parameters register and apply", "[scene][procedural][spline][params]") {
    params::ParameterSet params;
    ProceduralGeometry rest;
    rest.distribution.kind = DistributionKind::Spline;
    rest.distribution.spline = "rail";
    rest.distribution.splineStart = 0.2f;
    rest.distribution.splineEnd = 0.8f;
    Deformer path = pathDeformer("rail", {0.0f, 0.0f, 1.0f});
    path.pathOffset = 1.0f;
    path.pathScale = 2.0f;
    path.pathRoll = 0.5f;
    rest.deformers.push_back(path);
    Deformer twist;
    rest.deformers.push_back(twist);
    const std::string prefix = "procedural/rail/";
    const ProceduralParameters p = registerProceduralParameters(params, rest, prefix);
    for (const char* rel : {"distribution/splineStart", "distribution/splineEnd", "distribution/alignToSpline",
                            "distribution/roll", "distribution/splineOffset", "deform/1/pathOffset",
                            "deform/1/pathScale", "deform/1/pathRoll"}) {
        INFO(rel);
        CHECK(params.find(prefix + rel) != nullptr);
    }
    CHECK(params.find(prefix + "deform/2/pathOffset") == nullptr); // only Path slots
    CHECK(params.find(prefix + "deform/1/pathOffset")->label() == "path/pathOffset");
    REQUIRE(p.splineStart != nullptr);
    REQUIRE(p.splineEnd != nullptr);
    CHECK(p.splineStart->value() == 0.2f);
    CHECK(p.splineEnd->value() == 0.8f);
    CHECK(params.find(prefix + "distribution/kind")->hardMax(0) >= 5.0f); // Spline is selectable

    ProceduralGeometry live = rest;
    p.splineStart->setBase(0.3f);
    p.splineEnd->setBase(0.6f);
    dynamic_cast<params::Parameter<float>*>(params.find(prefix + "deform/1/pathOffset"))->setBase(4.0f);
    dynamic_cast<params::Parameter<float>*>(params.find(prefix + "deform/1/pathRoll"))->setBase(1.0f);
    dynamic_cast<params::Parameter<float>*>(params.find(prefix + "distribution/roll"))->setBase(0.7f);
    params.resetFinals();
    applyProceduralParameters(p, rest, live);
    CHECK(live.distribution.splineStart == 0.3f);
    CHECK(live.distribution.splineEnd == 0.6f);
    CHECK(live.distribution.roll == 0.7f);
    CHECK(live.distribution.spline == "rail"); // the name is structural: from rest
    CHECK(live.deformers[0].pathOffset == 4.0f);
    CHECK(live.deformers[0].pathRoll == 1.0f);
    CHECK(live.deformers[0].pathScale == 2.0f);
    CHECK(live.deformers[0].spline == "rail");
    unregisterProceduralParameters(params, p);
    CHECK(params.size() == 0);
}
