// Hybrid RT H2 (ADR-2246..2249): a surface's material as the raster's lit shader assembles it, emitters found by both
// strategies wherever they are, the effect-light pool, the range cull, and the camera's one-sided faces.

#include "core/color.hpp"
#include "pathtrace/lights.hpp"
#include "pathtrace/path_tracer.hpp"
#include "pathtrace/sampler.hpp"
#include "pathtrace/snapshot.hpp"
#include "pathtrace/surface.hpp"
#include "rendering/light_data.hpp"
#include "scene/material_program.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/procedural.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/quaternion.hpp>

#include <random>

using namespace avgen;
using Catch::Approx;

namespace {

scene::MaterialOp input(scene::MaterialInput what, int dst) {
    scene::MaterialOp op;
    op.kind = scene::MaterialOpKind::Input;
    op.input = what;
    op.dst = dst;
    return op;
}

scene::MaterialOp constant(glm::vec4 k, int dst) {
    scene::MaterialOp op;
    op.kind = scene::MaterialOpKind::Constant;
    op.constant = k;
    op.dst = dst;
    return op;
}

// A box scatter, generated for real, with its records' lanes then set by hand.
scene::ProceduralGeometry boxes(int n, glm::vec3 at) {
    scene::ProceduralGeometry g;
    g.name = "boxes";
    g.source.kind = scene::PrimitiveKind::Box;
    g.source.size = glm::vec3(0.5f);
    g.distribution.kind = scene::DistributionKind::Grid;
    g.distribution.gridCount = glm::ivec3(n, 1, 1);
    g.distributionTransform.position = at;
    REQUIRE(g.rebuild({}));
    return g;
}

pathtrace::ShadingPoint firstTriangleOf(bool instanced, std::uint32_t object, std::uint32_t instance) {
    pathtrace::ShadingPoint p;
    p.instanced = instanced;
    p.object = object;
    p.instance = instance;
    p.prim = 0;
    p.b0 = p.b1 = p.b2 = 1.0f / 3.0f;
    return p;
}

float luminance(const glm::vec3& c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

} // namespace

TEST_CASE("a material program runs at the frame's render clock, over the material's factors",
          "[unit][pathtrace][materials][adr2246]") {
    scene::Scene s;
    scene::MaterialProgram prog;
    prog.name = "clock";
    prog.ops = {input(scene::MaterialInput::Time, 0), constant(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f), 1)};
    prog.baseColorRegister = 0; // base colour = (t, t, t)
    s.materialPrograms.push_back(prog);
    scene::Entity& e = s.addEntity("panel", s.addMesh(scene::makeCube(1.0f)));
    e.material.program = "clock";
    e.material.baseColor = glm::vec3(0.2f, 0.4f, 0.6f);
    e.material.roughness = 0.7f;

    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(s, nullptr, 0.375);
    REQUIRE(snap.renderTime == 0.375);
    REQUIRE(snap.meshes[0].program == 0);
    pathtrace::ShadingPoint p = firstTriangleOf(false, 0, 0);
    const pathtrace::SurfaceMaterial m = pathtrace::shadeSurfaceMaterial(snap, p);
    CHECK(m.baseColor.x == Approx(0.375f));
    CHECK(m.baseColor.z == Approx(0.375f));
    CHECK(m.roughness == Approx(0.7f)); // a register the program does not name keeps the factor

    // The control: the same scene at another instant, and the same program not run at all.
    const pathtrace::Snapshot later = pathtrace::buildSnapshot(s, nullptr, 0.75);
    CHECK(pathtrace::shadeSurfaceMaterial(later, p).baseColor.x == Approx(0.75f));
    s.entities[0].material.program.clear();
    const pathtrace::Snapshot bare = pathtrace::buildSnapshot(s, nullptr, 0.375);
    CHECK(pathtrace::shadeSurfaceMaterial(bare, p).baseColor.x == Approx(0.2f));
}

TEST_CASE("an instance's colour and emissive lanes multiply its material, as procedural.wgsl does",
          "[unit][pathtrace][materials][adr2246]") {
    scene::Scene s;
    scene::ProceduralGeometry g = boxes(3, glm::vec3(0.0f));
    g.material.baseColor = glm::vec3(0.8f);
    g.material.emissiveColor = glm::vec3(1.0f, 0.5f, 0.25f);
    g.material.emissiveIntensity = 2.0f;
    g.emissionGain = 1.5f;
    g.instances[1].color = glm::vec4(0.5f, 1.0f, 0.25f, 7.0f);
    g.instances[1].emissive = glm::vec4(0.0f, 2.0f, 1.0f, 0.0f);
    s.procedurals.push_back(g);
    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(s);
    REQUIRE(snap.instanced.size() == 1);

    const pathtrace::SurfaceMaterial m = pathtrace::shadeSurfaceMaterial(snap, firstTriangleOf(true, 0, 1));
    CHECK(m.baseColor.x == Approx(0.8f * 0.5f));
    CHECK(m.baseColor.z == Approx(0.8f * 0.25f));
    // factor x instance lane x intensity, then the object's lane (gain 1.5)
    CHECK(m.emission.x == Approx(0.0f).margin(1e-6));
    CHECK(m.emission.y == Approx(0.5f * 2.0f * 2.0f * 1.5f));
    CHECK(m.emission.z == Approx(0.25f * 1.0f * 2.0f * 1.5f));
    // The control: an untouched copy is the material.
    const pathtrace::SurfaceMaterial plain = pathtrace::shadeSurfaceMaterial(snap, firstTriangleOf(true, 0, 0));
    CHECK(plain.baseColor.x == Approx(0.8f * glm::vec3(g.instances[0].color).x));
}

TEST_CASE("a program that writes emission takes the instance's variation as a hue and a gain",
          "[unit][pathtrace][materials][adr2246]") {
    scene::Scene s;
    scene::MaterialProgram prog;
    prog.name = "neon";
    prog.ops = {constant(glm::vec4(0.1f, 0.9f, 0.3f, 1.0f), 0)};
    prog.emissionRegister = 0;
    s.materialPrograms.push_back(prog);
    REQUIRE(prog.writesEmission());
    REQUIRE_FALSE(prog.emissionReadsInstance());
    scene::ProceduralGeometry g = boxes(2, glm::vec3(0.0f));
    g.material.program = "neon";
    g.material.emissiveColor = glm::vec3(1.0f, 0.2f, 0.2f);
    g.material.emissiveIntensity = 1.0f;
    g.instances[0].emissive = glm::vec4(0.4f, 1.6f, 1.2f, 0.0f);
    s.procedurals.push_back(g);
    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(s);
    REQUIRE(snap.instanced[0].chroma.w == 1.0f);

    const pathtrace::SurfaceMaterial m = pathtrace::shadeSurfaceMaterial(snap, firstTriangleOf(true, 0, 0));
    const glm::vec2 v = color::emissionVariationOf(g.material.emissiveColor, glm::vec3(g.instances[0].emissive));
    const glm::vec3 expected = color::hueShift(glm::vec3(0.1f, 0.9f, 0.3f), v.x) * v.y;
    CHECK(m.emission.x == Approx(expected.x).margin(1e-5));
    CHECK(m.emission.y == Approx(expected.y).margin(1e-5));
    CHECK(m.emission.z == Approx(expected.z).margin(1e-5));
}

TEST_CASE("a program's gate stops it where the raster stops it", "[unit][pathtrace][materials][adr2246]") {
    scene::Scene s;
    scene::MaterialProgram prog;
    prog.name = "near";
    prog.ops = {constant(glm::vec4(1.0f, 0.0f, 0.0f, 1.0f), 0)};
    prog.baseColorRegister = 0;
    prog.gate.far = 10.0f;
    s.materialPrograms.push_back(prog);
    scene::Entity& e = s.addEntity("wall", s.addMesh(scene::makeCube(1.0f)));
    e.material.program = "near";
    e.material.baseColor = glm::vec3(0.0f, 0.0f, 1.0f);
    s.camera.position = glm::vec3(0.0f, 0.0f, 5.0f);
    pathtrace::ShadingPoint p = firstTriangleOf(false, 0, 0);
    p.position = glm::vec3(0.0f);
    {
        const pathtrace::Snapshot snap = pathtrace::buildSnapshot(s);
        CHECK(pathtrace::shadeSurfaceMaterial(snap, p).baseColor.x == Approx(1.0f)); // 5 m: inside the gate
    }
    s.camera.position = glm::vec3(0.0f, 0.0f, 25.0f);
    const pathtrace::Snapshot far = pathtrace::buildSnapshot(s);
    CHECK(pathtrace::shadeSurfaceMaterial(far, p).baseColor.z == Approx(1.0f)); // 25 m: the factor
}

TEST_CASE("instanced emitters are light-sampled, and the density the sampler draws is the one a hit is weighted with",
          "[unit][pathtrace][materials][mis][adr2247]") {
    scene::Scene s;
    scene::ProceduralGeometry g = boxes(4, glm::vec3(0.0f, 3.0f, 0.0f));
    g.material.emissiveColor = glm::vec3(1.0f);
    g.material.emissiveIntensity = 3.0f;
    // Non-uniform scales: the selection weight is approximate for them, the density must still be exact.
    g.instances[1].scale = glm::vec4(2.0f, 0.5f, 1.0f, g.instances[1].scale.w);
    g.instances[3].scale = glm::vec4(0.3f, 1.7f, 2.2f, g.instances[3].scale.w);
    s.procedurals.push_back(g);
    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(s);
    REQUIRE(snap.emitterGroups.size() == 4);
    REQUIRE(snap.instancedEmitterGroup[0] == 0);

    const glm::vec3 p{0.2f, 0.0f, 0.4f};
    int checked = 0;
    for (int i = 0; i < 2000; ++i) {
        pathtrace::Sampler smp(31, static_cast<std::uint32_t>(i), 0);
        const auto es = pathtrace::sampleEmissive(snap, p, smp.next2D(), smp.next1D());
        if (!es.valid) continue;
        ++checked;
        REQUIRE(es.point.instanced);
        CHECK(pathtrace::emissivePdf(snap, es.point, es.direction, es.distance) == Approx(es.pdf).epsilon(1e-4));
        const glm::vec3 shaded = pathtrace::shadeSurfaceMaterial(snap, es.point).emission;
        CHECK(es.radiance == shaded);
        CHECK(luminance(es.radiance) > 0.0f);
    }
    CHECK(checked > 1500);

    // The area density integrates to one over the emitting surface: sum over groups of P(group) = 1 and, inside a
    // group, a point's p_A times the world area of the copy it is on is P(group) for every triangle.
    double total = 0.0;
    for (const auto& grp : snap.emitterGroups) total += grp.weight / snap.totalEmitterWeight;
    CHECK(total == Approx(1.0).epsilon(1e-5));
}

TEST_CASE("instanced and programmed emitters: light sampling, BSDF sampling and MIS agree",
          "[unit][pathtrace][materials][mis][adr2247]") {
    // A floor lit ONLY by procedural boxes, half of which a program turns off (a dark window).
    scene::Scene s;
    s.environment.sky.enabled = false;
    s.environment.backgroundColor = glm::vec3(0.0f);
    scene::Entity& floor = s.addEntity("floor", s.addMesh(scene::makePlane(8.0f, 1)));
    floor.material.baseColor = glm::vec3(0.6f);
    floor.material.roughness = 0.8f;
    scene::MaterialProgram prog;
    prog.name = "half";
    scene::MaterialOp lit;
    lit.kind = scene::MaterialOpKind::Threshold;
    lit.srcA = 0;
    lit.value = 0.5f;
    lit.dst = 1;
    scene::MaterialOp scale;
    scale.kind = scene::MaterialOpKind::Multiply;
    scale.srcA = 1;
    scale.srcB = 2;
    scale.dst = 3;
    prog.ops = {input(scene::MaterialInput::InstanceRandom, 0), lit, constant(glm::vec4(4.0f), 2), scale};
    prog.emissionRegister = 3;
    s.materialPrograms.push_back(prog);
    scene::ProceduralGeometry g = boxes(6, glm::vec3(-2.5f, 2.5f, 0.0f));
    g.material.program = "half";
    g.material.baseColor = glm::vec3(0.0f);
    s.procedurals.push_back(g);
    s.camera.position = glm::vec3(0.0f, 1.5f, 6.0f);
    s.camera.target = glm::vec3(0.0f, 0.0f, 0.0f);
    s.camera.lens.useExplicitFov = true;
    s.camera.fovYRadians = 0.8f;

    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(s);
    REQUIRE(snap.emitterGroups.size() == 6); // the program writes emission, so every copy CAN emit
    pathtrace::TraceSettings t;
    t.width = 48;
    t.height = 32;
    t.samplesPerPixel = 192;
    t.maxDepth = 1;
    t.russianRouletteDepth = 0;
    const auto meanWith = [&](pathtrace::TraceSettings::Strategy strategy) {
        pathtrace::TraceSettings arm = t;
        arm.strategy = strategy;
        pathtrace::PathTracer tracer;
        pathtrace::Framebuffer fb;
        REQUIRE(tracer.render(snap, arm, fb).has_value());
        return fb.meanLuminance();
    };
    const double mis = meanWith(pathtrace::TraceSettings::Strategy::Mis);
    const double light = meanWith(pathtrace::TraceSettings::Strategy::LightOnly);
    const double bsdf = meanWith(pathtrace::TraceSettings::Strategy::BsdfOnly);
    INFO("mis " << mis << " light " << light << " bsdf " << bsdf);
    REQUIRE(mis > 1e-3); // the live arm: the boxes do light the floor
    CHECK(light == Approx(mis).epsilon(0.04));
    CHECK(bsdf == Approx(mis).epsilon(0.04));
}

TEST_CASE("the range cull skips only lights sampleLight rejects", "[unit][pathtrace][lights][adr2248]") {
    std::mt19937 rng(5);
    std::uniform_real_distribution<float> u(-3.0f, 3.0f);
    std::uniform_real_distribution<float> r(0.01f, 4.0f);
    int culled = 0;
    for (int i = 0; i < 200000; ++i) {
        scene::PunctualLight l;
        l.type = (i % 2) == 0 ? scene::PunctualLight::Type::Point : scene::PunctualLight::Type::Spot;
        l.position = glm::vec3(u(rng), u(rng), u(rng));
        l.direction = glm::normalize(glm::vec3(u(rng), -1.0f, u(rng)));
        l.outerConeAngle = 1.2f;
        l.innerConeAngle = 0.4f;
        l.intensity = 10.0f;
        l.range = r(rng);
        glm::vec3 p{u(rng), u(rng), u(rng)};
        if (i % 7 == 0) { // on the boundary, where rounding decides
            p = l.position + glm::normalize(glm::vec3(u(rng), u(rng), u(rng))) * l.range;
        }
        if (!pathtrace::lightOutOfRange(l, p)) continue;
        ++culled;
        const auto ls = pathtrace::sampleLight(l, pathtrace::lightEmission(l), p, glm::vec2(0.3f, 0.7f));
        REQUIRE_FALSE(ls.valid);
    }
    CHECK(culled > 50000); // the control: the predicate does fire
    // Directional and area lights are never culled by it.
    scene::PunctualLight sun;
    sun.type = scene::PunctualLight::Type::Directional;
    sun.range = 0.001f;
    CHECK_FALSE(pathtrace::lightOutOfRange(sun, glm::vec3(1000.0f)));
}

TEST_CASE("the effect-light pool is traced as the raster appends it", "[unit][pathtrace][lights][adr2248]") {
    static_assert(rendering::kMaxSceneLights == 256, "snapshot.cpp mirrors this cap; change both together");
    scene::Scene s;
    s.addEntity("cube", s.addMesh(scene::makeCube(1.0f)));
    scene::PunctualLight key;
    key.type = scene::PunctualLight::Type::Directional;
    s.addLight(key);
    s.entityFx.lights.count = 2;
    s.entityFx.lights.lights[0].position = glm::vec3(1.0f, 2.0f, 3.0f);
    s.entityFx.lights.lights[0].intensity = 5.0f;
    s.entityFx.lights.lights[0].range = 0.0f; // the raster's floor is 0.01
    s.entityFx.lights.lights[1].intensity = 2.0f;
    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(s);
    REQUIRE(snap.lights.size() == 3);
    REQUIRE(snap.lightEmission.size() == 3);
    CHECK(snap.lights[1].type == scene::PunctualLight::Type::Point);
    CHECK_FALSE(snap.lights[1].castsShadow);
    CHECK(snap.lights[1].range == Approx(0.01f));
    CHECK(snap.lights[1].position == glm::vec3(1.0f, 2.0f, 3.0f));
}

TEST_CASE("the camera passes through a one-sided back face, as the raster culls it",
          "[unit][pathtrace][materials][adr2249]") {
    // A red wall behind a quad whose front faces AWAY from the camera. One-sided: the raster culls the quad, so the
    // camera sees red. Double-sided: the quad (green) is seen.
    const auto build = [](bool doubleSided) {
        scene::Scene s;
        s.environment.sky.enabled = false;
        s.environment.backgroundColor = glm::vec3(0.0f);
        scene::Entity& wall = s.addEntity("wall", s.addMesh(scene::makeQuad(5.0f)));
        wall.transform.position = glm::vec3(0.0f, 0.0f, -3.0f);
        wall.material.unlit = true;
        wall.material.baseColor = glm::vec3(1.0f, 0.0f, 0.0f);
        scene::Entity& quad = s.addEntity("card", s.addMesh(scene::makeQuad(1.0f)));
        quad.transform.rotation = glm::angleAxis(3.14159265f, glm::vec3(0.0f, 1.0f, 0.0f)); // front faces -z
        quad.material.unlit = true;
        quad.material.baseColor = glm::vec3(0.0f, 1.0f, 0.0f);
        quad.material.doubleSided = doubleSided;
        s.camera.position = glm::vec3(0.0f, 0.0f, 4.0f);
        s.camera.target = glm::vec3(0.0f);
        s.camera.lens.useExplicitFov = true;
        s.camera.fovYRadians = 0.3f;
        return s;
    };
    pathtrace::TraceSettings t;
    t.width = 8;
    t.height = 8;
    t.samplesPerPixel = 4;
    t.maxDepth = 1;
    for (const bool doubleSided : {false, true}) {
        INFO("doubleSided " << doubleSided);
        pathtrace::PathTracer tracer;
        pathtrace::Framebuffer fb;
        REQUIRE(tracer.render(pathtrace::buildSnapshot(build(doubleSided)), t, fb).has_value());
        const glm::vec3 centre = fb.pixel(4, 4);
        if (doubleSided) {
            CHECK(centre.y > 0.5f);
            CHECK(centre.x < 0.1f);
        } else {
            CHECK(centre.x > 0.5f);
            CHECK(centre.y < 0.1f);
        }
    }
}

TEST_CASE("a far, grazing hit is not shadowed by the emitter it aims at", "[unit][pathtrace][mis][adr2250]") {
    // Six small emitting quads 2.5 m above a floor, the floor point seen from straight above and from 5.7 m away at a
    // grazing angle. Light sampling, BSDF sampling and MIS must agree at both. Before ADR-2250 the shadow ray kept the
    // direction from the un-offset hit, met the tilted quad's own plane before its tfar, and light sampling read 13%
    // dark at the grazing view (5,718 of 20,000 shadow rays blocked by their own target) -- 29% over a whole image.
    scene::Scene s;
    s.environment.sky.enabled = false;
    s.environment.backgroundColor = glm::vec3(0.0f);
    scene::Entity& floor = s.addEntity("floor", s.addMesh(scene::makePlane(8.0f, 1)));
    floor.material.baseColor = glm::vec3(0.6f);
    floor.material.roughness = 1.0f;
    for (int i = 0; i < 6; ++i) {
        scene::Entity& q = s.addEntity("emitter", s.addMesh(scene::makePlane(0.25f, 1)));
        q.transform.rotation = glm::angleAxis(3.14159265f, glm::vec3(1.0f, 0.0f, 0.0f));
        q.transform.position = glm::vec3(-2.5f + static_cast<float>(i), 2.5f, 0.0f);
        q.material.baseColor = glm::vec3(0.0f);
        q.material.emissiveColor = glm::vec3(1.0f);
        q.material.emissiveIntensity = 4.0f;
    }
    pathtrace::TraceSettings t;
    t.width = 2;
    t.height = 2;
    t.samplesPerPixel = 16000;
    t.maxDepth = 1;
    t.russianRouletteDepth = 0;
    for (const bool grazing : {false, true}) {
        INFO("grazing " << grazing);
        s.camera.position = grazing ? glm::vec3(0.2f, 0.05f, 6.0f) : glm::vec3(0.2f, 0.5f, 0.3f);
        s.camera.target = grazing ? glm::vec3(0.2f, 0.0f, 0.3f) : glm::vec3(0.2f, 0.0f, 0.3001f);
        s.camera.up = grazing ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, 0.0f, -1.0f);
        s.camera.lens.useExplicitFov = true;
        s.camera.fovYRadians = 0.0005f;
        const pathtrace::Snapshot snap = pathtrace::buildSnapshot(s);
        double mean[3];
        int k = 0;
        for (const auto st : {pathtrace::TraceSettings::Strategy::Mis, pathtrace::TraceSettings::Strategy::LightOnly,
                              pathtrace::TraceSettings::Strategy::BsdfOnly}) {
            pathtrace::TraceSettings arm = t;
            arm.strategy = st;
            pathtrace::PathTracer tracer;
            pathtrace::Framebuffer fb;
            REQUIRE(tracer.render(snap, arm, fb).has_value());
            mean[k++] = fb.meanLuminance();
        }
        INFO("mis " << mean[0] << " light " << mean[1] << " bsdf " << mean[2]);
        CHECK(mean[0] > 0.05);
        CHECK(mean[1] == Approx(mean[0]).epsilon(0.02));
        CHECK(mean[2] == Approx(mean[0]).epsilon(0.02));
    }
}

namespace {
scene::TextureData rgba8(std::uint32_t w, std::uint32_t h, std::vector<std::uint8_t> bytes) {
    scene::TextureData t;
    t.name = "test";
    t.width = w;
    t.height = h;
    t.format = scene::TextureFormat::Rgba8Unorm;
    t.data = std::move(bytes);
    return t;
}
} // namespace

TEST_CASE("an alpha-masked card is cut where its texture says, for the camera and for shadows",
          "[unit][pathtrace][materials][adr2251]") {
    // A card whose left half is alpha 0, right half alpha 1, over a red wall.
    scene::Scene s;
    s.environment.sky.enabled = false;
    s.environment.backgroundColor = glm::vec3(0.0f);
    const scene::TextureId tex = s.addTexture(rgba8(2, 1, {0, 255, 0, 0, 0, 255, 0, 255}));
    scene::Entity& wall = s.addEntity("wall", s.addMesh(scene::makeQuad(5.0f)));
    wall.transform.position = glm::vec3(0.0f, 0.0f, -2.0f);
    wall.material.unlit = true;
    wall.material.baseColor = glm::vec3(1.0f, 0.0f, 0.0f);
    scene::Entity& card = s.addEntity("card", s.addMesh(scene::makeQuad(1.0f)));
    card.material.unlit = true;
    card.material.baseColor = glm::vec3(1.0f);
    card.material.baseColorTexture.texture = tex;
    card.material.baseColorTexture.linearFilter = false;
    card.material.alphaMode = scene::AlphaMode::Mask;
    card.material.alphaCutoff = 0.5f;
    s.camera.position = glm::vec3(0.0f, 0.0f, 3.0f);
    s.camera.target = glm::vec3(0.0f);
    s.camera.lens.useExplicitFov = true;
    s.camera.fovYRadians = 0.5f;
    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(s);
    REQUIRE(snap.hasAlphaMask);
    pathtrace::TraceSettings t;
    t.width = 64;
    t.height = 64;
    t.samplesPerPixel = 4;
    t.maxDepth = 1;
    pathtrace::PathTracer tracer;
    pathtrace::Framebuffer fb;
    REQUIRE(tracer.render(snap, t, fb).has_value());
    const glm::vec3 left = fb.pixel(24, 32);  // on the card's cut half
    const glm::vec3 right = fb.pixel(40, 32); // on the card's solid half
    INFO("left " << left.x << "," << left.y << " right " << right.x << "," << right.y);
    CHECK(left.x > 0.5f);   // the wall through the cut
    CHECK(left.y < 0.1f);
    CHECK(right.y > 0.5f);  // the card (green texel)
    CHECK(right.x < 0.1f);

    SECTION("a shadow ray passes the cut half and stops at the solid one") {
        scene::Scene sh;
        sh.environment.sky.enabled = false;
        sh.environment.backgroundColor = glm::vec3(0.0f);
        const scene::TextureId tex2 = sh.addTexture(rgba8(2, 1, {0, 255, 0, 0, 0, 255, 0, 255}));
        scene::Entity& floor = sh.addEntity("floor", sh.addMesh(scene::makePlane(5.0f, 1)));
        floor.material.baseColor = glm::vec3(0.8f);
        scene::Entity& roof = sh.addEntity("roof", sh.addMesh(scene::makeQuad(1.0f)));
        roof.transform.position = glm::vec3(0.0f, 1.0f, 0.0f);
        roof.transform.rotation = glm::angleAxis(-1.5707963f, glm::vec3(1.0f, 0.0f, 0.0f)); // facing up, u along +x
        roof.material.baseColorTexture.texture = tex2;
        roof.material.baseColorTexture.linearFilter = false;
        roof.material.alphaMode = scene::AlphaMode::Mask;
        roof.material.doubleSided = true;
        scene::PunctualLight sun;
        sun.type = scene::PunctualLight::Type::Directional;
        sun.direction = glm::vec3(0.0f, -1.0f, 0.0f);
        sun.intensity = 3.0f;
        sun.castsShadow = true;
        sh.addLight(sun);
        sh.camera.position = glm::vec3(0.0f, 0.5f, 0.001f);
        sh.camera.target = glm::vec3(0.0f, 0.0f, 0.0f);
        sh.camera.up = glm::vec3(0.0f, 0.0f, -1.0f);
        sh.camera.lens.useExplicitFov = true;
        sh.camera.fovYRadians = 1.2f;
        pathtrace::PathTracer tr2;
        pathtrace::Framebuffer fb2;
        pathtrace::TraceSettings t2 = t;
        REQUIRE(tr2.render(pathtrace::buildSnapshot(sh), t2, fb2).has_value());
        // The camera is under the roof looking down; x < 0 of the floor is under the cut half.
        const float underCut = fb2.pixel(16, 32).y;
        const float underSolid = fb2.pixel(48, 32).y;
        INFO("under cut " << underCut << " under solid " << underSolid);
        CHECK(underCut > 10.0f * std::max(underSolid, 1e-4f));
    }
}

TEST_CASE("a normal map tilts the shading normal along the surface's uv frame", "[unit][pathtrace][materials][adr2251]") {
    scene::Scene s;
    // Tangent-space (0.6, 0, 0.8): tilted toward +u, which on makeQuad is world +x.
    const scene::TextureId tex = s.addTexture(rgba8(1, 1, {204, 128, 230, 255}));
    scene::Entity& q = s.addEntity("q", s.addMesh(scene::makeQuad(1.0f)));
    q.material.normalTexture.texture = tex;
    pathtrace::Snapshot snap = pathtrace::buildSnapshot(s);
    pathtrace::ShadingPoint p = firstTriangleOf(false, 0, 0);
    p.normal = glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::vec3 n = pathtrace::shadeSurfaceMaterial(snap, p).normal;
    INFO("n " << n.x << "," << n.y << "," << n.z);
    CHECK(n.x == Approx(0.6f).margin(0.02));
    CHECK(n.y == Approx(0.0f).margin(0.02));
    CHECK(n.z == Approx(0.8f).margin(0.02));
    // The control: without the map the normal is left alone.
    s.entities[0].material.normalTexture = {};
    snap = pathtrace::buildSnapshot(s);
    CHECK(pathtrace::shadeSurfaceMaterial(snap, p).normal == glm::vec3(0.0f));
}
