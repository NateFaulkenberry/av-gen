// ADR-1144 (the anatomical SDF vocabulary: ellipsoid, tapered capsule, octahedron, facet, blend, fray and
// far field, compiled trees only) and ADR-1145 (a compiled latent): the CPU half. The kinds round-trip and are
// refused by name where they cannot run; their distances are the formulas the ADR states, each checked
// against an independent construction (a sphere, a capsule, a scaled ellipsoid, a child that would answer
// otherwise); the face the tool authors validates and is refused uncompiled. The GPU half (the compiled code
// against SdfTree::evaluate, and the compiled latent force against its CPU reference) is
// tests/rendering/test_sdf_anatomy_gpu.cpp.

#include "assets/asset_registry.hpp"
#include "core/noise.hpp"
#include "scene/composition.hpp"
#include "scene/sdf_object.hpp"
#include "spatial/sdf.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace avgen;
using json = nlohmann::json;
using spatial::SdfNode;
using spatial::SdfNodeKind;
using spatial::SdfTree;

namespace {

SdfNode make(SdfNodeKind kind) {
    SdfNode n;
    n.kind = kind;
    return n;
}

SdfTree treeOf(SdfNode root) {
    SdfTree t;
    t.root = std::move(root);
    return t;
}

SdfNode ellipsoid(glm::vec3 r) {
    SdfNode n = make(SdfNodeKind::Ellipsoid);
    n.size = r;
    return n;
}

SdfNode sphere(float r) {
    SdfNode n = make(SdfNodeKind::Sphere);
    n.radius = r;
    return n;
}

std::vector<glm::vec3> probes() {
    std::vector<glm::vec3> out;
    for (int i = -3; i <= 3; ++i) {
        for (int j = -3; j <= 3; ++j) {
            for (int k = -3; k <= 3; ++k) {
                out.emplace_back(0.61f * static_cast<float>(i) + 0.07f, 0.53f * static_cast<float>(j) - 0.11f,
                                 0.67f * static_cast<float>(k) + 0.03f);
            }
        }
    }
    return out;
}

Result<std::unique_ptr<scene::Composition>> load(const json& document) {
    static assets::AssetRegistry registry{testsupport::processTempDir()};
    return scene::Composition::fromJson(document, registry);
}

json sceneWith(const json& nodes) {
    return json{{"format", "avgen-scene"}, {"version", 1}, {"name", "anatomy"}, {"nodes", nodes}};
}

} // namespace

TEST_CASE("the anatomical kinds round-trip with their keys", "[sdf][anatomy]") {
    const json j = json::parse(R"({"root": {"kind": "smoothUnion", "smooth": 0.1, "children": [
        {"kind": "blend", "amount": 0.85, "axis": [1, 0, 0], "offset": 0.05, "smooth": 0.3, "children": [
            {"kind": "fray", "amount": 0.22, "frequency": 1.7, "speed": 0.1, "seed": 61, "radius": 1.4,
             "offset": 2.4, "size": [1, 0.75, 0], "translation": [0, -0.15, 0], "children": [
                {"kind": "ellipsoid", "size": [1.95, 3.35, 2.0]}]},
            {"kind": "facet", "size": [1.9, 3.2, 2.0], "count": 24, "offset": 0.93, "speed": 0.05, "amount": 0.3}]},
        {"kind": "farField", "translation": [0, -1.45, 0.62], "size": [1, 1.6, 1], "radius": 1.7, "offset": 1.0,
         "children": [{"kind": "taperedCapsule", "from": [0, 0.2, 0.05], "to": [0, -0.1, 0.12], "radius": 0.05,
                       "radius2": 0.004}]},
        {"kind": "octahedron", "radius": 0.5}]}})");
    auto tree = SdfTree::fromJson(j);
    REQUIRE(tree);
    REQUIRE(tree->validate(spatial::SdfEvaluator::Compiled));
    const json back = tree->toJson();
    auto again = SdfTree::fromJson(back);
    REQUIRE(again);
    CHECK(again->toJson() == back);
    CHECK(again->structuralHash() == tree->structuralHash());
    const SdfNode& capsule = again->root.children[1].children[0];
    CHECK(capsule.kind == SdfNodeKind::TaperedCapsule);
    CHECK(capsule.from == glm::vec3(0.0f, 0.2f, 0.05f));
    CHECK(capsule.to == glm::vec3(0.0f, -0.1f, 0.12f));
    CHECK(capsule.radius2 == Catch::Approx(0.004f));
    for (const glm::vec3& p : probes()) {
        CHECK(again->evaluate(p, 1.5) == tree->evaluate(p, 1.5));
    }
}

TEST_CASE("the anatomical kinds are refused by name where they cannot run", "[sdf][anatomy]") {
    // The interpreter has none of them: an uncompiled tree is refused, naming the kind and the remedy.
    const SdfTree withEllipsoid = treeOf(ellipsoid(glm::vec3(1.0f)));
    auto interp = withEllipsoid.validate(spatial::SdfEvaluator::Interpreter);
    REQUIRE_FALSE(interp);
    CHECK(interp.error().message.find("'ellipsoid'") != std::string::npos);
    CHECK(interp.error().message.find("compile") != std::string::npos);
    CHECK(withEllipsoid.validate(spatial::SdfEvaluator::Compiled));
    // ... wherever it sits (here under a union, a translate and a disabled op)
    SdfNode t = make(SdfNodeKind::Translate);
    t.children.push_back(make(SdfNodeKind::Octahedron));
    SdfNode u = make(SdfNodeKind::Union);
    u.children = {sphere(1.0f), t};
    auto deep = treeOf(u).validate(spatial::SdfEvaluator::Interpreter);
    REQUIRE_FALSE(deep);
    CHECK(deep.error().message.find("'octahedron'") != std::string::npos);
    // Bad parameters, by name.
    const auto refused = [](SdfNode n, const char* needle) {
        auto ok = treeOf(std::move(n)).validate(spatial::SdfEvaluator::Compiled);
        INFO(needle << ": " << (ok ? std::string("accepted") : ok.error().message));
        REQUIRE_FALSE(ok);
        CHECK(ok.error().message.find(needle) != std::string::npos);
    };
    refused(ellipsoid(glm::vec3(1.0f, 0.0f, 1.0f)), "'ellipsoid'");
    SdfNode cap = make(SdfNodeKind::TaperedCapsule);
    cap.from = cap.to = glm::vec3(0.3f);
    refused(cap, "'from' and 'to' must differ");
    SdfNode facet = make(SdfNodeKind::Facet);
    facet.count = 3;
    refused(facet, "'facet': count");
    facet.count = 65;
    refused(facet, "'facet': count");
    SdfNode blend = make(SdfNodeKind::Blend);
    blend.children = {sphere(1.0f)};
    refused(blend, "'blend' needs exactly 2 children");
    blend.children = {sphere(1.0f), sphere(0.5f), sphere(0.2f)};
    refused(blend, "'blend' needs");
    SdfNode fray = make(SdfNodeKind::Fray);
    fray.children = {sphere(1.0f)};
    fray.amount = 2.0f;
    refused(fray, "'fray': |amount|");
    fray.amount = 0.2f;
    fray.radius = 2.0f;
    fray.offset = 1.0f;
    refused(fray, "'fray': offset");
    SdfNode far = make(SdfNodeKind::FarField);
    far.children = {sphere(1.0f)};
    far.radius = 0.0f;
    refused(far, "'farField': radius");
    // An unknown key value of the wrong type is refused by the JSON reader, by key.
    auto badFrom = SdfTree::fromJson(json::parse(R"({"root": {"kind": "taperedCapsule", "from": [1, 2]}})"));
    REQUIRE_FALSE(badFrom);
    CHECK(badFrom.error().message.find("'from'") != std::string::npos);
}

TEST_CASE("an sdf object using an anatomical kind must compile, and a latent may then bind to it",
          "[sdf][anatomy][latent][composition]") {
    json sdf = json{{"tree", {{"root", {{"kind", "ellipsoid"}, {"size", {1.0, 0.6, 0.8}}}}}},
                    {"boundsMin", {-2, -2, -2}},
                    {"boundsMax", {2, 2, 2}},
                    {"visible", false}};
    auto uncompiled = scene::SdfObject::fromJson(sdf);
    REQUIRE_FALSE(uncompiled);
    CHECK(uncompiled.error().message.find("compile") != std::string::npos);
    sdf["compile"] = true;
    REQUIRE(scene::SdfObject::fromJson(sdf));
    json particles = json::parse(R"({"capacity": 1024, "shape": "box", "extent": [2, 2, 2], "spawnRate": 0,
                                     "latent": {"sdf": "mask"}})");
    auto comp = load(sceneWith(json::array({json{{"name", "mask"}, {"kind", "sdf"}, {"sdf", sdf}},
                                            json{{"name", "matter"}, {"kind", "particles"}, {"particles", particles}}})));
    INFO((comp ? std::string() : comp.error().message));
    CHECK(comp);
}

TEST_CASE("ellipsoid, tapered capsule and octahedron are the stated distances", "[sdf][anatomy]") {
    // Equal radii: the bound is the sphere's exact distance.
    const SdfTree ball = treeOf(ellipsoid(glm::vec3(0.8f)));
    const SdfTree sph = treeOf(sphere(0.8f));
    for (const glm::vec3& p : probes()) {
        CHECK(ball.evaluate(p, 0.0) == Catch::Approx(sph.evaluate(p, 0.0)).margin(1e-5));
    }
    // Unequal radii: zero on the surface, negative inside, a bound (never above the true distance) outside.
    const SdfTree e = treeOf(ellipsoid(glm::vec3(1.95f, 3.35f, 2.0f)));
    CHECK(e.evaluate(glm::vec3(1.95f, 0.0f, 0.0f), 0.0) == Catch::Approx(0.0f).margin(1e-5));
    CHECK(e.evaluate(glm::vec3(0.0f, 3.35f, 0.0f), 0.0) == Catch::Approx(0.0f).margin(1e-5));
    CHECK(e.evaluate(glm::vec3(0.5f, 0.6f, 0.4f), 0.0) < 0.0f); // (the bound is 0 at the very centre: k0 = 0)
    CHECK(e.evaluate(glm::vec3(3.0f, 0.0f, 0.0f), 0.0) <= 3.0f - 1.95f + 1e-5f);
    CHECK(e.evaluate(glm::vec3(3.0f, 0.0f, 0.0f), 0.0) > 0.0f);
    // Equal radii: the tapered capsule is the capsule (a Capsule node's segment runs along y, +-height/2).
    SdfNode tc = make(SdfNodeKind::TaperedCapsule);
    tc.from = glm::vec3(0.0f, -0.6f, 0.0f);
    tc.to = glm::vec3(0.0f, 0.6f, 0.0f);
    tc.radius = tc.radius2 = 0.3f;
    SdfNode cap = make(SdfNodeKind::Capsule);
    cap.radius = 0.3f;
    cap.height = 1.2f;
    const SdfTree a = treeOf(tc);
    const SdfTree b = treeOf(cap);
    for (const glm::vec3& p : probes()) {
        CHECK(a.evaluate(p, 0.0) == Catch::Approx(b.evaluate(p, 0.0)).margin(1e-5));
    }
    // Tapered: the radius at each end, interpolated along the segment.
    tc.radius = 0.17f;
    tc.radius2 = 0.07f;
    const SdfTree t = treeOf(tc);
    CHECK(t.evaluate(glm::vec3(0.0f, -0.6f, 0.0f) + glm::vec3(0.17f, 0.0f, 0.0f), 0.0) == Catch::Approx(0.0f).margin(1e-5));
    CHECK(t.evaluate(glm::vec3(0.07f, 0.6f, 0.0f), 0.0) == Catch::Approx(0.0f).margin(1e-5));
    CHECK(t.evaluate(glm::vec3(0.12f, 0.0f, 0.0f), 0.0) == Catch::Approx(0.0f).margin(1e-5));
    // Octahedron: |x| + |y| + |z| = s on the surface, exact along an axis and at a face centre.
    SdfNode o = make(SdfNodeKind::Octahedron);
    o.radius = 0.5f;
    const SdfTree oc = treeOf(o);
    CHECK(oc.evaluate(glm::vec3(0.9f, 0.0f, 0.0f), 0.0) == Catch::Approx(0.4f).margin(1e-5));
    const glm::vec3 face = glm::vec3(0.5f / 3.0f);
    CHECK(oc.evaluate(face, 0.0) == Catch::Approx(0.0f).margin(1e-5));
    CHECK(oc.evaluate(face * 2.0f, 0.0) == Catch::Approx(glm::length(face)).margin(1e-5));
    CHECK(oc.evaluate(glm::vec3(0.0f), 0.0) == Catch::Approx(-0.5f * 0.57735027f).margin(1e-5));
}

TEST_CASE("facet: flat faces that bound the ellipsoid, turning with time", "[sdf][anatomy]") {
    SdfNode f = make(SdfNodeKind::Facet);
    f.size = glm::vec3(1.0f);
    f.count = 24;
    f.offset = 0.93f;
    f.speed = 0.05f;
    f.amount = 0.3f;
    const SdfTree facet = treeOf(f);
    const SdfTree ref = treeOf(sphere(0.93f));
    // Every face's support is 0.93: inside the unit ball the facet field sits above the inscribed sphere's
    // (a polytope circumscribing the 0.93 sphere), and along any one of the plane normals it is exactly it.
    int above = 0;
    for (const glm::vec3& p : probes()) {
        if (glm::length(p) < 1.6f) {
            CHECK(facet.evaluate(p, 0.0) <= ref.evaluate(p, 0.0) + 1e-5f);
            above += facet.evaluate(p, 0.0) < ref.evaluate(p, 0.0) - 1e-3f ? 1 : 0;
        }
    }
    CHECK(above > 0); // and it is not the sphere: the planes leave edges
    // The first plane's normal at t = 0: k = 0, z = 1 - 1/24, phase 0.3 sin(0).
    const float z = 1.0f - 1.0f / 24.0f;
    const float rr = std::sqrt(1.0f - z * z);
    const glm::vec3 n0(rr, z, 0.0f);
    CHECK(facet.evaluate(n0 * 0.93f, 0.0) == Catch::Approx(0.0f).margin(1e-4));
    // Time turns the planes: the same point reads differently a minute later.
    CHECK(facet.evaluate(glm::vec3(0.4f, 0.7f, 0.5f), 0.0) != facet.evaluate(glm::vec3(0.4f, 0.7f, 0.5f), 60.0));
}

TEST_CASE("blend, fray and far field are the stated operations", "[sdf][anatomy]") {
    const SdfNode a = sphere(1.0f);
    SdfNode boxNode = make(SdfNodeKind::Box);
    boxNode.size = glm::vec3(0.7f, 0.5f, 0.9f);
    // Blend with no axis: amount alone; 0 is the first child, 1 the second, between linear.
    SdfNode blend = make(SdfNodeKind::Blend);
    blend.children = {a, boxNode};
    const SdfTree sa = treeOf(a);
    const SdfTree sb = treeOf(boxNode);
    for (const float w : {0.0f, 0.3f, 1.0f}) {
        blend.amount = w;
        blend.axis = glm::vec3(0.0f);
        const SdfTree t = treeOf(blend);
        for (const glm::vec3& p : probes()) {
            const float d0 = sa.evaluate(p, 0.0);
            const float d1 = sb.evaluate(p, 0.0);
            CHECK(t.evaluate(p, 0.0) == Catch::Approx(d0 + (d1 - d0) * w).margin(1e-5));
        }
    }
    // With an axis: the first child on the far side of the ramp, the blend on the near side.
    blend.amount = 1.0f;
    blend.axis = glm::vec3(2.0f, 0.0f, 0.0f); // not unit: normalised
    blend.offset = 0.05f;
    blend.smooth = 0.3f;
    const SdfTree half = treeOf(blend);
    CHECK(half.evaluate(glm::vec3(-0.6f, 0.2f, 0.1f), 0.0) == Catch::Approx(sa.evaluate(glm::vec3(-0.6f, 0.2f, 0.1f), 0.0)));
    CHECK(half.evaluate(glm::vec3(0.6f, 0.2f, 0.1f), 0.0) == Catch::Approx(sb.evaluate(glm::vec3(0.6f, 0.2f, 0.1f), 0.0)));

    // Fray with amount 0 is its child; otherwise an ellipsoid frayed by f is the ellipsoid of radii f r at that
    // point, exactly (f varies in space, so compare point by point).
    const glm::vec3 r(1.95f, 3.35f, 2.0f);
    SdfNode fray = make(SdfNodeKind::Fray);
    fray.children = {ellipsoid(r)};
    fray.amount = 0.0f;
    fray.radius = 0.5f;
    fray.offset = 1.5f;
    fray.size = glm::vec3(1.0f, 0.75f, 0.0f);
    fray.frequency = 1.7f;
    fray.seed = 61;
    const SdfTree plain = treeOf(ellipsoid(r));
    for (const glm::vec3& p : probes()) {
        CHECK(treeOf(fray).evaluate(p, 0.0) == Catch::Approx(plain.evaluate(p, 0.0)).margin(1e-6));
    }
    fray.amount = 0.22f;
    fray.speed = 0.1f;
    const SdfTree frayed = treeOf(fray);
    int moved = 0;
    for (const glm::vec3& p : probes()) {
        const float m = [&] {
            const float x = glm::length((p - fray.translation) * fray.size);
            const float tt = std::clamp((x - fray.radius) / (fray.offset - fray.radius), 0.0f, 1.0f);
            return tt * tt * (3.0f - 2.0f * tt);
        }();
        const float n = noise::valueNoise(p * fray.frequency + glm::vec3(0.0f, 0.0f, fray.speed * 2.0f), fray.seed);
        const float f = 1.0f + fray.amount * (n - 0.5f) * m;
        const float expected = treeOf(ellipsoid(r * f)).evaluate(p, 0.0);
        CHECK(frayed.evaluate(p, 2.0) == Catch::Approx(expected).margin(1e-5));
        moved += std::abs(expected - plain.evaluate(p, 0.0)) > 1e-3f ? 1 : 0;
    }
    CHECK(moved > 50);

    // Far field: beyond the radius the guide, |(p - c) s| - offset, whatever the child says; inside, the child.
    SdfNode far = make(SdfNodeKind::FarField);
    far.translation = glm::vec3(0.0f, -1.45f, 0.62f);
    far.size = glm::vec3(1.0f, 1.6f, 1.0f);
    far.radius = 1.7f;
    far.offset = 1.0f;
    SdfNode slit = make(SdfNodeKind::Translate);
    slit.translation = far.translation;
    slit.children = {ellipsoid(glm::vec3(0.95f, 0.13f, 0.85f))};
    far.children = {slit};
    const SdfTree guided = treeOf(far);
    const SdfTree bare = treeOf(slit);
    int guidedCount = 0;
    for (const glm::vec3& p : probes()) {
        const float g = glm::length((p - far.translation) * far.size);
        if (g > far.radius) {
            CHECK(guided.evaluate(p, 0.0) == Catch::Approx(g - far.offset).margin(1e-6));
            ++guidedCount;
        } else {
            CHECK(guided.evaluate(p, 0.0) == bare.evaluate(p, 0.0));
        }
    }
    CHECK(guidedCount > 100);
    // The point of it: far above the slit the bare bound's gradient is almost all y (matter falls onto the plane
    // y = mouth), the guide's points at the mouth's centre.
    const glm::vec3 p(3.0f, 2.5f, 0.6f);
    const glm::vec3 nb = bare.normal(p, 0.0);
    const glm::vec3 ng = guided.normal(p, 0.0);
    const glm::vec3 radial = glm::normalize((p - far.translation) * far.size * far.size);
    CHECK(std::abs(nb.y) > 0.95f);
    CHECK(glm::dot(ng, radial) > 0.99f);
}

TEST_CASE("the authored Astral Forge face validates compiled, and only compiled", "[sdf][anatomy]") {
    // The tool's output, checked in as the T01 comparison scene's latent.
    std::ifstream in(std::string(AVGEN_SOURCE_DIR) + "/examples/astral-forge/compare-t01-face.scene.json");
    REQUIRE(in.good());
    const json scene = json::parse(in);
    bool found = false;
    for (const json& n : scene.at("nodes")) {
        if (n.at("name") != "latent") {
            continue;
        }
        found = true;
        auto tree = SdfTree::fromJson(n.at("sdf").at("tree"));
        REQUIRE(tree);
        INFO(tree->nodeCount() << " nodes");
        CHECK(tree->validate(spatial::SdfEvaluator::Compiled));
        CHECK_FALSE(tree->validate(spatial::SdfEvaluator::Interpreter));
        CHECK(n.at("sdf").at("compile") == true);
        // Points of the anatomy: the eye ball's front is on the surface, the cheek inside the plate's shell,
        // the socket is a hole, and a point a long way off reads as distance (the far-field guides).
        CHECK(std::abs(tree->evaluate(glm::vec3(-0.76f, 0.72f, 0.8f), 0.0)) < 0.06f); // the left eye's front
        CHECK(tree->evaluate(glm::vec3(-1.0f, -0.4f, 0.55f), 0.0) < 0.05f);          // the cheek, on the plate
        CHECK(tree->evaluate(glm::vec3(-0.76f, 0.72f, 1.3f), 0.0) > 0.2f);          // in front of the eye: open
        CHECK(tree->evaluate(glm::vec3(0.0f, 0.0f, 30.0f), 0.0) > 20.0f);
    }
    CHECK(found);
}
