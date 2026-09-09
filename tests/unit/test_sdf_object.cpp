// Scene-level SDF objects (ADR-027): validation, structural hashing, rebuild and mesh caching,
// JSON round trip, and the rest/live parameter pattern (pre-order node paths, structural
// detection). The GPU side lives in tests/rendering/test_sdf_gpu.cpp.
#include "params/parameter_set.hpp"
#include "scene/sdf_object.hpp"
#include "spatial/sdf.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

using namespace avgen;
using namespace avgen::scene;
using spatial::SdfNode;
using spatial::SdfNodeKind;

namespace {

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

// smoothUnion(translate(sphere), twist(box), displaceNoise(sphere)); pre-order indices:
//   1 smoothUnion, 2 translate, 3 sphere, 4 twist, 5 box, 6 displaceNoise, 7 sphere
SdfObject makeRest() {
    SdfObject rest;
    rest.name = "blob";
    SdfNode translated = node(SdfNodeKind::Translate);
    translated.translation = {0.4f, 0.1f, -0.2f};
    translated.children.push_back(sphere(0.9f));
    SdfNode twisted = node(SdfNodeKind::Twist);
    twisted.amount = 0.35f;
    SdfNode boxNode = node(SdfNodeKind::Box);
    boxNode.size = {0.6f, 0.5f, 0.4f};
    twisted.children.push_back(std::move(boxNode));
    SdfNode noisy = node(SdfNodeKind::DisplaceNoise);
    noisy.amount = 0.12f;
    noisy.frequency = 2.5f;
    noisy.speed = 0.4f;
    noisy.seed = 9;
    noisy.children.push_back(sphere(0.5f));
    SdfNode root = node(SdfNodeKind::SmoothUnion);
    root.smooth = 0.3f;
    root.children.push_back(std::move(translated));
    root.children.push_back(std::move(twisted));
    root.children.push_back(std::move(noisy));
    rest.tree.root = std::move(root);
    rest.transform.position = {1.0f, 0.5f, -2.0f};
    rest.transform.scale = {1.5f, 1.5f, 1.5f};
    rest.material.baseColor = {0.2f, 0.6f, 0.9f};
    rest.material.roughness = 0.4f;
    rest.boundsMin = glm::vec3(-2.0f);
    rest.boundsMax = glm::vec3(2.0f);
    rest.resolution = 16;
    return rest;
}

} // namespace

TEST_CASE("SdfObject validates the tree, the bounds and the march settings", "[scene][sdf]") {
    SdfObject o = makeRest();
    CHECK(o.validate().has_value());

    SECTION("render mode names round trip") {
        CHECK(std::string(sdfRenderModeName(SdfRenderMode::Raymarch)) == "raymarch");
        CHECK(std::string(sdfRenderModeName(SdfRenderMode::Mesh)) == "mesh");
        CHECK(sdfRenderModeFromName("raymarch") == SdfRenderMode::Raymarch);
        CHECK(sdfRenderModeFromName("mesh") == SdfRenderMode::Mesh);
        CHECK_FALSE(sdfRenderModeFromName("marched").has_value());
    }
    SECTION("bounds need a positive extent on every axis") {
        o.boundsMax.y = o.boundsMin.y;
        CHECK_FALSE(o.validate().has_value());
    }
    SECTION("resolution is 2..256") {
        o.resolution = 1;
        CHECK_FALSE(o.validate().has_value());
        o.resolution = 257;
        CHECK_FALSE(o.validate().has_value());
        o.resolution = 256;
        CHECK(o.validate().has_value());
    }
    SECTION("maxSteps is 1..1024") {
        o.maxSteps = 0;
        CHECK_FALSE(o.validate().has_value());
        o.maxSteps = 1025;
        CHECK_FALSE(o.validate().has_value());
        o.maxSteps = 1024;
        CHECK(o.validate().has_value());
    }
    SECTION("epsilon > 0 and stepScale in 0.1..1") {
        o.epsilon = 0.0f;
        CHECK_FALSE(o.validate().has_value());
        o.epsilon = 0.001f;
        o.stepScale = 0.05f;
        CHECK_FALSE(o.validate().has_value());
        o.stepScale = 1.1f;
        CHECK_FALSE(o.validate().has_value());
        o.stepScale = 1.0f;
        CHECK(o.validate().has_value());
    }
    SECTION("an invalid tree is rejected") {
        o.tree.root.children.clear();
        o.tree.root.kind = SdfNodeKind::Translate; // a unary op needs a child
        CHECK_FALSE(o.validate().has_value());
    }
}

TEST_CASE("SdfObject::structuralHash covers the tree, bounds, resolution and render mode", "[scene][sdf]") {
    const SdfObject rest = makeRest();
    const std::uint64_t base = rest.structuralHash();
    CHECK(base != 0);
    CHECK(SdfObject(rest).structuralHash() == base);

    auto changed = [&](auto&& mutate) {
        SdfObject o = rest;
        mutate(o);
        return o.structuralHash() != base;
    };
    CHECK(changed([](SdfObject& o) { o.tree.root.children[0].translation.x += 0.1f; }));
    CHECK(changed([](SdfObject& o) { o.tree.root.smooth = 0.7f; }));
    CHECK(changed([](SdfObject& o) { o.tree.root.children[1].enabled = false; }));
    CHECK(changed([](SdfObject& o) { o.boundsMin = glm::vec3(-3.0f); }));
    CHECK(changed([](SdfObject& o) { o.boundsMax = glm::vec3(3.0f); }));
    CHECK(changed([](SdfObject& o) { o.resolution = 32; }));
    CHECK(changed([](SdfObject& o) { o.renderMode = SdfRenderMode::Mesh; }));
    // Per-frame uniforms are not structural.
    CHECK_FALSE(changed([](SdfObject& o) { o.transform.position = {9.0f, 9.0f, 9.0f}; }));
    CHECK_FALSE(changed([](SdfObject& o) { o.transform.scale = glm::vec3(3.0f); }));
    CHECK_FALSE(changed([](SdfObject& o) { o.material.baseColor = {1.0f, 0.0f, 0.0f}; }));
    CHECK_FALSE(changed([](SdfObject& o) { o.maxSteps = 64; }));
    CHECK_FALSE(changed([](SdfObject& o) { o.epsilon = 0.01f; }));
    CHECK_FALSE(changed([](SdfObject& o) { o.stepScale = 0.5f; }));
    CHECK_FALSE(changed([](SdfObject& o) { o.normalEpsilon = 0.01f; }));
    CHECK_FALSE(changed([](SdfObject& o) { o.visible = false; }));
}

TEST_CASE("SdfObject::rebuild meshes in Mesh mode and caches by hash", "[scene][sdf]") {
    SdfObject o = makeRest();

    SECTION("Raymarch mode keeps no mesh") {
        CHECK(o.rebuild());
        CHECK(o.structureVersion == 1);
        CHECK(o.mesh.vertices.empty());
        CHECK(o.meshHash == 0);
        CHECK_FALSE(o.rebuild()); // nothing changed
        CHECK(o.structureVersion == 1);
        o.tree.root.children[0].translation.x += 0.5f;
        CHECK(o.rebuild());
        CHECK(o.structureVersion == 2);
        // The transform is not structural: no rebuild.
        o.transform.position = {5.0f, 0.0f, 0.0f};
        CHECK_FALSE(o.rebuild());
        CHECK(o.structureVersion == 2);
    }
    SECTION("Mesh mode meshes through spatial::meshSdf and re-meshes on a structural change") {
        o.renderMode = SdfRenderMode::Mesh;
        REQUIRE(o.rebuild());
        CHECK(o.structureVersion == 1);
        REQUIRE(o.mesh.valid());
        CHECK_FALSE(o.mesh.vertices.empty());
        CHECK(o.mesh.indices.size() % 3 == 0);
        CHECK(o.mesh.name == "blob");
        CHECK(o.meshHash == o.structuralHash());
        const std::size_t vertices = o.mesh.vertices.size();

        CHECK_FALSE(o.rebuild()); // cached
        CHECK(o.mesh.vertices.size() == vertices);
        CHECK(o.structureVersion == 1);

        o.resolution = 24;
        REQUIRE(o.rebuild());
        CHECK(o.structureVersion == 2);
        CHECK(o.mesh.vertices.size() != vertices);
        CHECK(o.meshHash == o.structuralHash());

        // Switching back to Raymarch drops the mesh.
        o.renderMode = SdfRenderMode::Raymarch;
        CHECK(o.rebuild());
        CHECK(o.mesh.vertices.empty());
        CHECK(o.meshHash == 0);
    }
    SECTION("the meshed surface agrees with the field's sign") {
        SdfObject s;
        s.name = "ball";
        s.tree.root = sphere(1.0f);
        s.renderMode = SdfRenderMode::Mesh;
        s.boundsMin = glm::vec3(-1.6f);
        s.boundsMax = glm::vec3(1.6f);
        s.resolution = 24;
        REQUIRE(s.rebuild());
        REQUIRE(s.mesh.valid());
        for (const Vertex& v : s.mesh.vertices) {
            CHECK(std::abs(s.tree.evaluate(v.position, 0.0)) < 0.1f);
        }
    }
}

TEST_CASE("SdfObject JSON round trips every member", "[scene][sdf]") {
    SdfObject rest = makeRest();
    rest.visible = false;
    rest.renderMode = SdfRenderMode::Mesh;
    rest.maxSteps = 96;
    rest.epsilon = 0.004f;
    rest.stepScale = 0.75f;
    rest.normalEpsilon = 0.003f;
    rest.transform.rotation = glm::quat(glm::radians(glm::vec3(15.0f, -40.0f, 25.0f)));
    rest.material.emissiveColor = {0.1f, 0.2f, 0.3f};
    rest.material.emissiveIntensity = 2.5f;
    rest.material.metallic = 0.8f;

    const nlohmann::json j = rest.toJson();
    CHECK(j.at("renderMode") == "mesh");
    CHECK(j.contains("tree"));
    CHECK(j.contains("position"));
    CHECK(j.contains("rotation"));
    CHECK(j.contains("scale"));
    CHECK(j.contains("boundsMin"));
    CHECK(j.contains("boundsMax"));
    CHECK(j.at("resolution") == 16);

    auto back = SdfObject::fromJson(j);
    REQUIRE(back.has_value());
    CHECK(back->name == rest.name);
    CHECK(back->visible == rest.visible);
    CHECK(back->renderMode == rest.renderMode);
    CHECK(back->resolution == rest.resolution);
    CHECK(back->maxSteps == rest.maxSteps);
    CHECK(back->epsilon == rest.epsilon);
    CHECK(back->stepScale == rest.stepScale);
    CHECK(back->normalEpsilon == rest.normalEpsilon);
    CHECK(back->boundsMin == rest.boundsMin);
    CHECK(back->boundsMax == rest.boundsMax);
    CHECK(back->transform.position == rest.transform.position);
    CHECK(back->transform.scale == rest.transform.scale);
    CHECK(glm::length(back->transform.rotation * glm::vec3(1.0f, 0.0f, 0.0f) -
                      rest.transform.rotation * glm::vec3(1.0f, 0.0f, 0.0f)) < 1e-4f);
    CHECK(back->material.baseColor == rest.material.baseColor);
    CHECK(back->material.emissiveIntensity == rest.material.emissiveIntensity);
    CHECK(back->material.metallic == rest.material.metallic);
    CHECK(back->tree.structuralHash() == rest.tree.structuralHash());
    CHECK(back->structuralHash() == rest.structuralHash());
    // Every member but the rotation is byte-identical; degrees -> quaternion -> degrees rounds at
    // float precision, so that one is compared with a tolerance.
    nlohmann::json a = j;
    nlohmann::json b = back->toJson();
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(std::abs(a.at("rotation").at(i).get<float>() - b.at("rotation").at(i).get<float>()) < 1e-3f);
    }
    a.erase("rotation");
    b.erase("rotation");
    CHECK(a == b);
}

TEST_CASE("SdfObject JSON fills defaults for missing members and rejects bad ones", "[scene][sdf]") {
    const auto minimal = SdfObject::fromJson(nlohmann::json::object());
    REQUIRE(minimal.has_value());
    const SdfObject def;
    CHECK(minimal->name == def.name);
    CHECK(minimal->visible == def.visible);
    CHECK(minimal->renderMode == def.renderMode);
    CHECK(minimal->boundsMin == def.boundsMin);
    CHECK(minimal->boundsMax == def.boundsMax);
    CHECK(minimal->resolution == def.resolution);
    CHECK(minimal->maxSteps == def.maxSteps);
    CHECK(minimal->epsilon == def.epsilon);
    CHECK(minimal->stepScale == def.stepScale);
    CHECK(minimal->normalEpsilon == def.normalEpsilon);
    CHECK(minimal->tree.structuralHash() == def.tree.structuralHash());

    CHECK_FALSE(SdfObject::fromJson(nlohmann::json::array()).has_value());
    CHECK_FALSE(SdfObject::fromJson(nlohmann::json{{"renderMode", "marched"}}).has_value());
    CHECK_FALSE(SdfObject::fromJson(nlohmann::json{{"resolution", 1}}).has_value());
    CHECK_FALSE(SdfObject::fromJson(nlohmann::json{{"resolution", 2.5}}).has_value());
    CHECK_FALSE(SdfObject::fromJson(nlohmann::json{{"position", {1.0, 2.0}}}).has_value());
    CHECK_FALSE(SdfObject::fromJson(nlohmann::json{{"stepScale", 2.0}}).has_value());
}

TEST_CASE("SDF parameters register under the prefix with pre-order node paths", "[scene][sdf][params]") {
    params::ParameterSet params;
    const SdfObject rest = makeRest();
    const std::string prefix = "sdf/blob/";
    const SdfParameters p = registerSdfParameters(params, rest, prefix);
    CHECK(p.prefix == prefix);
    CHECK_FALSE(p.all.empty());
    CHECK(params.size() == p.all.size());

    for (const char* rel : {"visible", "transform/position", "transform/rotation", "transform/scale",
                            "material/baseColor", "material/emissiveColor", "material/emissive", "material/roughness",
                            "material/metallic", "bounds/min", "bounds/max", "resolution",
                            // 1 smoothUnion, 2 translate, 3 sphere, 4 twist, 5 box, 6 displaceNoise, 7 sphere
                            "node/1/smooth", "node/1/enabled", "node/2/translation", "node/2/enabled",
                            "node/3/radius", "node/3/enabled", "node/4/amount", "node/4/enabled", "node/5/size",
                            "node/5/enabled", "node/6/amount", "node/6/frequency", "node/6/speed", "node/6/enabled",
                            "node/7/radius", "node/7/enabled"}) {
        INFO(rel);
        const params::IParameter* param = params.find(prefix + rel);
        REQUIRE(param != nullptr);
        CHECK(param->group() == "sdf/blob");
    }
    // Only the members a kind uses, and no node past the tree.
    CHECK(params.find(prefix + "node/3/size") == nullptr);
    CHECK(params.find(prefix + "node/1/radius") == nullptr);
    CHECK(params.find(prefix + "node/5/radius") == nullptr);
    CHECK(params.find(prefix + "node/8/radius") == nullptr);

    CHECK(params.find(prefix + "node/1/smooth")->label() == "smoothUnion/smooth");
    CHECK(params.find(prefix + "node/2/translation")->label() == "translate/translation");
    CHECK(params.find(prefix + "node/6/amount")->label() == "displaceNoise/amount");
    CHECK(params.find(prefix + "material/baseColor")->kind() == params::ParamKind::Color);
    CHECK(params.find(prefix + "visible")->kind() == params::ParamKind::Bool);
    CHECK(params.find(prefix + "resolution")->kind() == params::ParamKind::Int);
    CHECK(params.find(prefix + "resolution")->hardMin(0) == 2.0f);
    CHECK(params.find(prefix + "resolution")->hardMax(0) == 256.0f);

    // The handle vectors have one slot per node, filled where the kind has that member.
    REQUIRE(p.nodeRadius.size() == static_cast<std::size_t>(rest.tree.nodeCount()));
    REQUIRE(p.nodeAmount.size() == p.nodeRadius.size());
    REQUIRE(p.nodeSmooth.size() == p.nodeRadius.size());
    REQUIRE(p.nodeSmooth[0] != nullptr);
    CHECK(p.nodeSmooth[0]->value() == 0.3f);
    CHECK(p.nodeRadius[0] == nullptr);
    REQUIRE(p.nodeRadius[2] != nullptr);
    CHECK(p.nodeRadius[2]->value() == 0.9f);
    REQUIRE(p.nodeAmount[3] != nullptr);
    CHECK(p.nodeAmount[3]->value() == 0.35f);
    REQUIRE(p.nodeAmount[5] != nullptr);
    CHECK(p.nodeAmount[5]->value() == 0.12f);
    REQUIRE(p.nodeRadius[6] != nullptr);
    CHECK(p.nodeRadius[6]->value() == 0.5f);
    REQUIRE(p.visible != nullptr);
    CHECK(p.visible->value());
    REQUIRE(p.position != nullptr);
    CHECK(p.position->value() == rest.transform.position);
    REQUIRE(p.scale != nullptr);
    REQUIRE(p.rotation != nullptr);
    REQUIRE(p.baseColor != nullptr);
    CHECK(p.baseColor->value() == rest.material.baseColor);
    REQUIRE(p.emissive != nullptr);

    // Registering again returns the same parameters, and unregistering empties the set.
    const SdfParameters again = registerSdfParameters(params, rest, prefix);
    CHECK(again.nodeRadius[2] == p.nodeRadius[2]);
    CHECK(params.size() == p.all.size());
    unregisterSdfParameters(params, p);
    CHECK(params.size() == 0);
}

TEST_CASE("applySdfParameters copies finals and reports structural change", "[scene][sdf][params]") {
    params::ParameterSet params;
    const SdfObject rest = makeRest();
    const SdfParameters p = registerSdfParameters(params, rest, "sdf/blob/");
    SdfObject live = rest;

    params.resetFinals();
    CHECK_FALSE(applySdfParameters(p, rest, live)); // defaults: nothing moved
    CHECK(live.structuralHash() == rest.structuralHash());

    SECTION("non-structural finals are copied without a structural change") {
        p.position->setBase({3.0f, 0.0f, 1.0f});
        p.scale->setBase(glm::vec3(2.0f));
        p.rotation->setBase({0.0f, 90.0f, 0.0f});
        p.baseColor->setBase({1.0f, 0.0f, 0.0f});
        p.emissive->setBase(4.0f);
        p.visible->setBase(false);
        params.resetFinals();
        CHECK_FALSE(applySdfParameters(p, rest, live));
        CHECK(live.transform.position == glm::vec3(3.0f, 0.0f, 1.0f));
        CHECK(live.transform.scale == glm::vec3(2.0f));
        CHECK(glm::length(live.transform.rotation * glm::vec3(1.0f, 0.0f, 0.0f) - glm::vec3(0.0f, 0.0f, -1.0f)) < 1e-4f);
        CHECK(live.material.baseColor == glm::vec3(1.0f, 0.0f, 0.0f));
        CHECK(live.material.emissiveIntensity == 4.0f);
        CHECK_FALSE(live.visible);
        // Structural members untouched by parameters keep the rest values.
        CHECK(live.maxSteps == rest.maxSteps);
        CHECK(live.material.roughness == rest.material.roughness);
    }
    SECTION("node parameters are structural (Raymarch: live uniforms, Mesh: a re-mesh)") {
        p.nodeRadius[2]->setBase(1.4f);
        params.resetFinals();
        CHECK(applySdfParameters(p, rest, live));
        CHECK(live.tree.root.children[0].children[0].radius == 1.4f);
        CHECK(live.tree.root.children[0].children[0].kind == SdfNodeKind::Sphere); // structure from rest
        CHECK(live.structuralHash() != rest.structuralHash());

        // Applying the same finals again reports no further change.
        CHECK_FALSE(applySdfParameters(p, rest, live));

        p.nodeSmooth[0]->setBase(0.9f);
        p.nodeAmount[5]->setBase(0.3f);
        params.resetFinals();
        CHECK(applySdfParameters(p, rest, live));
        CHECK(live.tree.root.smooth == 0.9f);
        CHECK(live.tree.root.children[2].amount == 0.3f);
    }
    SECTION("bounds and resolution are structural") {
        params.find("sdf/blob/resolution")->setBaseComponent(0, 24.0f);
        params.resetFinals();
        CHECK(applySdfParameters(p, rest, live));
        CHECK(live.resolution == 24);
        params.find("sdf/blob/bounds/min")->setBaseComponent(1, -4.0f);
        params.resetFinals();
        CHECK(applySdfParameters(p, rest, live));
        CHECK(live.boundsMin.y == -4.0f);
    }
    SECTION("disabling a node is structural and the mesh cache follows") {
        live.renderMode = SdfRenderMode::Mesh;
        SdfObject meshRest = rest;
        meshRest.renderMode = SdfRenderMode::Mesh;
        REQUIRE(live.rebuild());
        REQUIRE(live.mesh.valid());
        const std::uint64_t builtHash = live.meshHash;

        params.find("sdf/blob/node/4/enabled")->setBaseComponent(0, 0.0f);
        params.resetFinals();
        CHECK(applySdfParameters(p, meshRest, live));
        CHECK_FALSE(live.tree.root.children[1].enabled);
        CHECK(live.meshHash == builtHash); // stale until rebuild()
        CHECK(live.rebuild());
        CHECK(live.meshHash == live.structuralHash());
        CHECK(live.meshHash != builtHash);
    }
}
