#include "params/parameter_set.hpp"
#include "scene/procedural.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <string>

using namespace avgen;
using namespace avgen::scene;
using Catch::Matchers::WithinAbs;

namespace {

ProceduralGeometry makeRest() {
    ProceduralGeometry rest;
    rest.name = "columns";
    rest.source.kind = PrimitiveKind::Cylinder;
    rest.source.radius = 0.4f;
    rest.distribution.kind = DistributionKind::Radial;
    rest.distribution.count = 6;
    rest.distribution.radius = 5.0f;
    Deformer twist;
    twist.kind = DeformerKind::Twist;
    twist.amount = 0.5f;
    Deformer noise;
    noise.kind = DeformerKind::Noise;
    noise.space = DeformSpace::World;
    noise.amount = 0.1f;
    rest.deformers = {twist, noise};
    return rest;
}

} // namespace

TEST_CASE("Procedural parameters register under the prefix with relative labels", "[scene][procedural][params]") {
    params::ParameterSet params;
    const ProceduralGeometry rest = makeRest();
    const std::string prefix = "procedural/columns/";
    const ProceduralParameters p = registerProceduralParameters(params, rest, prefix);
    CHECK(p.prefix == prefix);
    CHECK_FALSE(p.all.empty());
    CHECK(params.size() == p.all.size());

    for (const char* rel : {"source/kind", "source/radius", "source/height", "source/size", "source/subdivisions",
                            "source/radialSegments", "source/heightSegments", "source/caps", "source/segments",
                            "source/rings", "source/majorRadius", "source/minorRadius", "source/majorSegments",
                            "source/minorSegments", "source/position", "source/rotation", "source/scale",
                            "distribution/kind", "distribution/count", "distribution/start", "distribution/end",
                            "distribution/orientAlong", "distribution/spacing", "distribution/gridCountX",
                            "distribution/gridCountY", "distribution/gridCountZ", "distribution/gridSpacing",
                            "distribution/radius", "distribution/startAngle", "distribution/endAngle",
                            "distribution/plane", "distribution/center", "distribution/orientation",
                            "distribution/radiusGrowth", "distribution/turns", "distribution/spiralHeight",
                            "distribution/spiralAngle", "transform/position", "transform/rotation", "transform/scale",
                            "variation/seed", "variation/position", "variation/rotation", "variation/scale",
                            "variation/uniformScale", "deform/1/amount", "deform/1/speed", "deform/1/phase",
                            "deform/1/frequency", "deform/1/scale", "deform/1/falloff", "deform/1/center",
                            "deform/1/axis", "deform/1/enabled", "deform/2/amount", "deform/2/enabled",
                            "material/baseColor", "material/emissiveColor", "material/emissive", "material/roughness",
                            "material/metallic", "materialVariation/hueShift", "materialVariation/hueGradient",
                            "materialVariation/valueRandom", "materialVariation/emissiveRandom",
                            "materialVariation/emissiveGradient", "visible"}) {
        INFO(rel);
        const params::IParameter* param = params.find(prefix + rel);
        REQUIRE(param != nullptr);
        CHECK(param->group() == "procedural/columns");
    }
    CHECK(params.find(prefix + "deform/3/amount") == nullptr); // only existing slots

    CHECK(params.find(prefix + "source/radius")->label() == "source/radius");
    CHECK(params.find(prefix + "deform/1/amount")->label() == "twist/amount");
    CHECK(params.find(prefix + "deform/2/amount")->label() == "noise/amount");
    CHECK(params.find(prefix + "material/baseColor")->kind() == params::ParamKind::Color);
    CHECK(params.find(prefix + "material/emissiveColor")->kind() == params::ParamKind::Color);
    CHECK(params.find(prefix + "visible")->kind() == params::ParamKind::Bool);
    CHECK(params.find(prefix + "distribution/count")->kind() == params::ParamKind::Int);
    CHECK(params.find(prefix + "source/kind")->kind() == params::ParamKind::Int);
    CHECK(params.find(prefix + "distribution/count")->hardMin(0) == 1.0f);
    CHECK(params.find(prefix + "distribution/count")->hardMax(0) == 1048576.0f);
    // The range has to cover the whole enum, and it has to be derived from the enum rather than
    // from whichever kind happens to be last today. Naming a member here is what let Mesh be added
    // while the parameter stayed clamped to Tube -- the same failure the comment was warning about,
    // repeated one kind later. Walk upward while each value still round-trips through its own name.
    int lastKind = 0;
    for (int k = 1; k < 64; ++k) {
        const auto back = primitiveKindFromName(primitiveKindName(static_cast<PrimitiveKind>(k)));
        if (!back || static_cast<int>(*back) != k) {
            break;
        }
        lastKind = k;
    }
    CHECK(lastKind >= static_cast<int>(PrimitiveKind::Mesh));
    CHECK(params.find(prefix + "source/kind")->hardMax(0) == static_cast<float>(lastKind));
    CHECK(params.find(prefix + "material/emissive")->softMax(0) == 8.0f);

    REQUIRE(p.sourceRadius != nullptr);
    CHECK(p.sourceRadius->value() == 0.4f);
    REQUIRE(p.distributionCount != nullptr);
    CHECK(p.distributionCount->value() == 6);
    REQUIRE(p.distributionRadius != nullptr);
    CHECK(p.distributionRadius->value() == 5.0f);
    REQUIRE(p.deformerAmount[0] != nullptr);
    CHECK(p.deformerAmount[0]->value() == 0.5f);
    REQUIRE(p.deformerAmount[1] != nullptr);
    CHECK(p.deformerAmount[2] == nullptr);
    REQUIRE(p.deformerSpeed[1] != nullptr);
    REQUIRE(p.baseColor != nullptr);
    CHECK(p.baseColor->value() == rest.material.baseColor);
    REQUIRE(p.visible != nullptr);
    CHECK(p.visible->value());
    REQUIRE(p.transformRotation != nullptr);
    REQUIRE(p.seed != nullptr);
    CHECK(p.seed->value() == 12345);

    // Registering again returns the same parameters.
    const ProceduralParameters again = registerProceduralParameters(params, rest, prefix);
    CHECK(again.sourceRadius == p.sourceRadius);
    CHECK(params.size() == p.all.size());
}

TEST_CASE("applyProceduralParameters copies finals and rebuilds only on structural change",
          "[scene][procedural][params]") {
    params::ParameterSet params;
    const ProceduralGeometry rest = makeRest();
    const ProceduralParameters p = registerProceduralParameters(params, rest, "procedural/columns/");
    ProceduralGeometry live = rest;

    params.resetFinals();
    CHECK(applyProceduralParameters(p, rest, live)); // first build
    CHECK(live.structureVersion == 1);
    CHECK(live.instances.size() == 6);
    CHECK_FALSE(applyProceduralParameters(p, rest, live)); // nothing changed
    CHECK(live.structureVersion == 1);

    p.distributionCount->setBase(8);
    params.resetFinals();
    CHECK(applyProceduralParameters(p, rest, live));
    CHECK(live.distribution.count == 8);
    CHECK(live.instances.size() == 8);
    CHECK(live.structureVersion == 2);

    // Deformer amount: copied, no rebuild.
    p.deformerAmount[0]->setBase(1.25f);
    p.deformerSpeed[1]->setBase(0.4f);
    params.resetFinals();
    CHECK_FALSE(applyProceduralParameters(p, rest, live));
    CHECK(live.deformers.size() == 2);
    CHECK(live.deformers[0].amount == 1.25f);
    CHECK(live.deformers[0].kind == DeformerKind::Twist);
    CHECK(live.deformers[1].speed == 0.4f);
    CHECK(live.deformers[1].space == DeformSpace::World);
    CHECK(live.structureVersion == 2);

    // Material colour: copied, no rebuild.
    p.baseColor->setBase({0.2f, 0.4f, 0.6f});
    p.emissive->setBase(3.0f);
    p.roughness->setBase(0.8f);
    p.visible->setBase(false);
    params.resetFinals();
    CHECK_FALSE(applyProceduralParameters(p, rest, live));
    CHECK(live.material.baseColor == glm::vec3(0.2f, 0.4f, 0.6f));
    CHECK(live.material.emissiveIntensity == 3.0f);
    CHECK(live.material.roughness == 0.8f);
    CHECK_FALSE(live.visible);

    // Kinds arrive as ints; transforms as Euler degrees; source radius is structural.
    params.findAs<int>("procedural/columns/distribution/kind")->setBase(static_cast<int>(DistributionKind::Grid));
    params.findAs<int>("procedural/columns/distribution/gridCountX")->setBase(3);
    params.findAs<int>("procedural/columns/distribution/gridCountY")->setBase(2);
    params.findAs<int>("procedural/columns/distribution/gridCountZ")->setBase(1);
    p.transformRotation->setBase({0.0f, 90.0f, 0.0f});
    p.sourceRadius->setBase(0.9f);
    params.findAs<int>("procedural/columns/source/kind")->setBase(static_cast<int>(PrimitiveKind::Sphere));
    params.resetFinals();
    const std::uint64_t meshBefore = live.meshHash;
    CHECK(applyProceduralParameters(p, rest, live));
    CHECK(live.distribution.kind == DistributionKind::Grid);
    CHECK(live.distribution.gridCount == glm::ivec3(3, 2, 1));
    CHECK(live.instances.size() == 6);
    CHECK(live.source.kind == PrimitiveKind::Sphere);
    CHECK(live.source.radius == 0.9f);
    CHECK(live.meshHash != meshBefore);
    const glm::vec3 rotatedX = live.distributionTransform.rotation * glm::vec3(1.0f, 0.0f, 0.0f);
    CHECK_THAT(static_cast<double>(rotatedX.z), WithinAbs(-1.0, 1e-5));

    // Modulated finals (not bases) are what gets applied. The radius is copied but is not part
    // of a grid's structure, so no rebuild; grid spacing is.
    p.distributionRadius->setFinalComponent(0, 9.0f);
    CHECK_FALSE(applyProceduralParameters(p, rest, live));
    CHECK(live.distribution.radius == 9.0f);
    params.find("procedural/columns/distribution/gridSpacing")->setFinalComponent(0, 3.0f);
    CHECK(applyProceduralParameters(p, rest, live));
    CHECK(live.distribution.gridSpacing.x == 3.0f);
    CHECK_THAT(static_cast<double>(glm::length(glm::vec3(live.instances[1].position - live.instances[0].position))),
               WithinAbs(3.0, 1e-5));

    // Empty handle set is a no-op.
    ProceduralParameters none;
    CHECK_FALSE(applyProceduralParameters(none, rest, live));
}

TEST_CASE("unregisterProceduralParameters removes everything it registered", "[scene][procedural][params]") {
    params::ParameterSet params;
    params::ParamDesc<float> other;
    other.path = "other/value";
    other.hardMin = 0.0f;
    other.hardMax = 1.0f;
    params.add(other);
    const ProceduralGeometry rest = makeRest();
    const ProceduralParameters p = registerProceduralParameters(params, rest, "procedural/columns/");
    const std::size_t registered = p.all.size();
    CHECK(params.size() == registered + 1);
    unregisterProceduralParameters(params, p);
    CHECK(params.size() == 1);
    CHECK(params.find("procedural/columns/source/radius") == nullptr);
    CHECK(params.find("procedural/columns/deform/1/amount") == nullptr);
    CHECK(params.find("procedural/columns/visible") == nullptr);
    CHECK(params.find("other/value") != nullptr);
    // Registering again after unregistering works.
    const ProceduralParameters again = registerProceduralParameters(params, rest, "procedural/columns/");
    CHECK(again.all.size() == registered);
    CHECK(params.size() == registered + 1);
}
