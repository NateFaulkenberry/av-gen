// The Shadow Lab's rendered half (§15, §37): what the frame actually shows, not what a diagnostic
// says about it.
//
// §37 is the reason this file exists at all. The shadow AOV recomputes the shadow term, and a CPU
// check of the cascade fit recomputes it again -- two recomputations agree with each other and can
// both be wrong about the frame. So the assertions below are on pixels the renderer wrote, with a
// control in the same pair of frames that says the measurement can move.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/debug_visualizer.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/procedural.hpp"
#include "scene/scene.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <vector>

using namespace avgen;

namespace {

constexpr std::uint32_t kWidth = 480;
constexpr std::uint32_t kHeight = 270;

std::unique_ptr<gpu::Context> makeContext() {
    static bool logInit = false;
    if (!logInit) {
        log::init(log::Level::Warn);
        logInit = true;
    }
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

std::uint64_t specHash(const scene::SourceSpec& s) {
    std::uint64_t h = 0x5AD0E51ull;
    const auto mix = [&](std::uint64_t v) { h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2); };
    mix(static_cast<std::uint64_t>(s.kind));
    mix(static_cast<std::uint64_t>(s.size.x * 1000.0f));
    mix(static_cast<std::uint64_t>(s.size.y * 1000.0f));
    return h == 0 ? 1 : h;
}

scene::InstanceRecord recordAt(glm::vec3 position, float scale) {
    scene::InstanceRecord r{};
    r.position = glm::vec4(position, 1.0f);
    r.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    r.scale = {scale, scale, scale, 0.0f};
    r.random = {0.25f, 0.5f, 0.75f, 0.125f};
    r.color = {1.0f, 1.0f, 1.0f, 0.0f};
    r.emissive = {1.0f, 1.0f, 1.0f, 0.0f};
    return r;
}

scene::MeshData floorMesh(float half) {
    scene::MeshData m;
    const glm::vec3 n(0.0f, 1.0f, 0.0f);
    m.vertices.push_back({{-half, 0.0f, -half}, n, {0.0f, 0.0f}});
    m.vertices.push_back({{half, 0.0f, -half}, n, {1.0f, 0.0f}});
    m.vertices.push_back({{half, 0.0f, half}, n, {1.0f, 1.0f}});
    m.vertices.push_back({{-half, 0.0f, half}, n, {0.0f, 1.0f}});
    m.indices = {0, 2, 1, 0, 3, 2};
    return m;
}

// One box floating three metres up, as a procedural object, with `rungs` rungs on its LOD ladder.
//
// The thresholds are deliberately far outside anything a detail multiplier can move them past: the
// projected radius here is about seventeen pixels, and rung 2 is taken at anything at or under 500
// while rung 3 needs one. So this object is on rung 2 -- the first camera-facing billboard --
// whatever `DetailLimits` does to the ladder, and on rung 0 when `rungs` is 1.
scene::ProceduralGeometry caster(const char* name, int rungs) {
    scene::ProceduralGeometry g;
    g.name = name;
    g.source.kind = scene::PrimitiveKind::Box;
    g.source.size = {2.0f, 2.0f, 2.0f};
    g.source.subdivisions = 1;
    g.meshHash = specHash(g.source);
    g.structureVersion = 1;
    g.material.baseColor = {0.75f, 0.72f, 0.68f};
    g.material.roughness = 0.9f;
    g.castsShadow = true;
    g.lod.cull = true;
    g.lod.maxDistance = 400.0f;
    g.lod.minScreenRadius = 0.5f;
    g.lod.lodCount = rungs;
    g.lod.lodByScreenSize = true;
    g.lod.lodDistances[0] = 1000.0f;
    g.lod.lodDistances[1] = 500.0f;
    g.lod.lodDistances[2] = 1.0f;
    g.lod.lodSpread = 0.0f;
    g.lod.lodHysteresis = 0.0f;
    g.instances.push_back(recordAt({0.0f, 3.0f, 0.0f}, 1.0f));
    return g;
}

// A floor, one floating caster, and a key light travelling +X at 34 degrees above the horizon --
// **exactly perpendicular to the camera's view axis**, which is the angle at which a camera-facing
// quad presents its edge to the sun. The shadow lands on open floor at x = 0 .. 4.45.
scene::Scene shadowLabScene(int rungs) {
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.showSkybox = false;
    s.environment.environmentIntensity = 0.0f;
    // The camera looks in a direction exactly perpendicular to the light: `dot(view, light) = 0`,
    // so a quad built from this camera's right and up presents the sun **nothing**. That is not an
    // exotic setup -- a key at right angles to the lens is an ordinary cross-light -- and it is the
    // configuration in which the defect below is total rather than partial. A camera 20 degrees off
    // it still darkens the floor and would have let both tests here pass over the bug.
    s.camera.position = {15.3f, 16.0f, 5.2f};
    s.camera.target = {4.5f, 0.0f, 0.0f};
    s.camera.fovYRadians = 0.9f;
    s.camera.nearPlane = 0.5f;
    s.camera.farPlane = 150.0f;

    const auto floor = s.addMesh(floorMesh(40.0f));
    auto& e = s.addEntity("floor", floor);
    e.material.baseColor = glm::vec3(0.85f);
    e.material.roughness = 0.95f;
    e.material.metallic = 0.0f;

    scene::PunctualLight key;
    key.name = "key";
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(0.829f, -0.5592f, 0.0f));
    key.color = glm::vec3(1.0f);
    key.intensity = 4.0f;
    key.castsShadow = true;
    key.contactShadow = false;
    key.softness = 0.2f;
    s.addLight(key);

    s.procedurals.push_back(caster("caster", rungs));
    return s;
}

// The pixel a world point lands on, through the scene's own camera. Going via the projection rather
// than picking a pixel by eye is what lets the assertion name a piece of *ground* -- and the same
// ground in two frames.
glm::ivec2 pixelOf(const scene::Scene& s, const glm::vec3& world) {
    const float aspect = static_cast<float>(kWidth) / static_cast<float>(kHeight);
    const glm::vec4 clip = s.camera.projection(aspect) * s.camera.view() * glm::vec4(world, 1.0f);
    REQUIRE(clip.w > 0.0f);
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    return {static_cast<int>((ndc.x * 0.5f + 0.5f) * static_cast<float>(kWidth)),
            static_cast<int>((0.5f - ndc.y * 0.5f) * static_cast<float>(kHeight))};
}

float luminanceAt(const gpu::Image8& image, glm::ivec2 p, int radius = 2) {
    double total = 0.0;
    int n = 0;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const int x = p.x + dx;
            const int y = p.y + dy;
            if (x < 0 || y < 0 || x >= static_cast<int>(image.width) || y >= static_cast<int>(image.height)) {
                continue;
            }
            const std::size_t i = (static_cast<std::size_t>(y) * image.width + static_cast<std::size_t>(x)) * 4;
            total += 0.2126 * image.rgba[i] + 0.7152 * image.rgba[i + 1] + 0.0722 * image.rgba[i + 2];
            ++n;
        }
    }
    return n > 0 ? static_cast<float>(total / n) / 255.0f : 0.0f;
}

FrameTime frameAt(std::uint64_t index) {
    FrameTime t;
    t.frameIndex = index;
    t.renderTime = static_cast<double>(index) / 60.0;
    t.deltaTime = 1.0 / 60.0;
    return t;
}

} // namespace

TEST_CASE("a LOD impostor casts the shadow its own mesh casts", "[gpu][shadows][lab][impostor]") {
    // The defect this pins: rungs 2 and 3 of the LOD ladder are camera-facing quads built in the
    // vertex shader from `frame.cameraRight` / `frame.cameraUp`, and the shadow passes used to hand
    // those shaders the CAMERA's two axes -- so the quad faced the viewer while being rasterised
    // from the light, and with the light at right angles to the camera it presented its edge and
    // cast nothing. A stationary caster under a stationary sun had a shadow whose size depended on
    // where the camera stood.
    //
    // The arm and its control are the same scene with one field different: `lod.lodCount`. Four
    // rungs puts the caster on rung 2; one rung can never demote. Everything else -- asset, size,
    // position, light, camera, tier -- is identical, which is what makes a difference between them
    // attributable to the ladder (ADR-182).
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto renderer = std::make_unique<rendering::SceneRenderer>(*ctx, shaders);
    REQUIRE(renderer->init().has_value());

    const scene::Scene impostor = shadowLabScene(4);
    const scene::Scene mesh = shadowLabScene(1);

    // Several frames: the procedural cull's instance counts reach the CPU through a readback that
    // is one to three frames behind, and a first frame is not an account of a settled scene.
    auto impostorImage = renderer->renderToImage(impostor, frameAt(8), kWidth, kHeight);
    REQUIRE(impostorImage.has_value());
    auto meshImage = renderer->renderToImage(mesh, frameAt(8), kWidth, kHeight);
    REQUIRE(meshImage.has_value());

    // The light travels +X at 34 degrees and runs 1.482 m along the ground per metre of height, so
    // a 2 m box whose centre is 3 m up lays its shadow across x = 1.97 .. 6.93 at z = 0. x = 4.5 is
    // the middle of it; x = 9.5 is past the end and is the lit reference. Both numbers are derived
    // from the light rather than found by looking, which is what lets the same two points be named
    // from a second camera below.
    const glm::ivec2 inShadow = pixelOf(impostor, {4.5f, 0.0f, 0.0f});
    const glm::ivec2 openFloor = pixelOf(impostor, {9.5f, 0.0f, 0.0f});
    REQUIRE(pixelOf(mesh, {4.5f, 0.0f, 0.0f}) == inShadow); // the two cameras really are the same

    const float impostorShadow = luminanceAt(*impostorImage, inShadow);
    const float meshShadow = luminanceAt(*meshImage, inShadow);
    const float impostorOpen = luminanceAt(*impostorImage, openFloor);
    const float meshOpen = luminanceAt(*meshImage, openFloor);

    INFO("in shadow: impostor " << impostorShadow << ", mesh " << meshShadow
                                << "; open floor: impostor " << impostorOpen << ", mesh " << meshOpen);

    // The control first: the mesh arm must actually cast something, or the comparison below is
    // between two frames with no shadow in either and passes for the wrong reason.
    CHECK(meshOpen > 0.05f);
    CHECK(meshShadow < meshOpen * 0.8f);
    // And the open floor is left alone in both, so "darker" means a shadow and not a dimmer frame.
    CHECK(impostorOpen > meshOpen * 0.9f);

    // The invariant. Before the fix this read 0.99 of the open floor -- the impostor cast nothing
    // measurable at all -- against the mesh arm's 0.5-ish.
    CHECK(impostorShadow < impostorOpen * 0.8f);
    // ...and the two arms are within a factor of each other rather than an order of magnitude.
    // Not equality: a flat card standing in for a box is a different occluder and is allowed to be,
    // and the thing this test forbids is a shadow whose size depends on the viewer.
    const float impostorDarkening = 1.0f - impostorShadow / impostorOpen;
    const float meshDarkening = 1.0f - meshShadow / meshOpen;
    INFO("darkening: impostor " << impostorDarkening << ", mesh " << meshDarkening);
    CHECK(impostorDarkening > meshDarkening * 0.4f);
}

TEST_CASE("an impostor's shadow does not depend on where the camera stands",
          "[gpu][shadows][lab][impostor]") {
    // §26's form of the same invariant, and the one that cannot be satisfied by accident: the same
    // caster, the same sun, the same piece of floor, photographed from two places. A shadow is a
    // fact about a caster and a light; a shadow that changes when only the camera moves is a bug
    // whatever it looks like.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto renderer = std::make_unique<rendering::SceneRenderer>(*ctx, shaders);
    REQUIRE(renderer->init().has_value());

    // Two cameras chosen by one number: how much of a camera-facing quad's area the light sees.
    // `across` looks in a direction exactly perpendicular to the light, so a quad built for it
    // presents the light nothing; `along` looks 27 degrees off the light, so a quad built for it
    // presents almost everything. Before the fix that ratio WAS the shadow.
    //
    // Both are also steep enough to see the floor past the caster. The first version of this test
    // put the second camera at the sun's own altitude, from which the shadow is exactly behind the
    // object that casts it -- the probe read the box, not the floor, and reported no shadow where
    // there was a perfectly good one. A viewpoint from which the thing being measured is invisible
    // is a probe fault, not a finding (ADR-182).
    scene::Scene across = shadowLabScene(4);   // the fixture's own camera: perpendicular
    scene::Scene along = shadowLabScene(4);
    along.camera.position = {-5.5f, 17.0f, 0.0f};
    along.camera.target = {4.5f, 0.0f, 0.0f};

    auto acrossImage = renderer->renderToImage(across, frameAt(8), kWidth, kHeight);
    REQUIRE(acrossImage.has_value());
    auto alongImage = renderer->renderToImage(along, frameAt(8), kWidth, kHeight);
    REQUIRE(alongImage.has_value());

    const auto darkening = [&](const scene::Scene& s, const gpu::Image8& image) {
        const float shadow = luminanceAt(image, pixelOf(s, {4.5f, 0.0f, 0.0f}));
        const float open = luminanceAt(image, pixelOf(s, {9.5f, 0.0f, 0.0f}));
        REQUIRE(open > 0.05f);
        return 1.0f - shadow / open;
    };
    const float acrossDarkening = darkening(across, *acrossImage);
    const float alongDarkening = darkening(along, *alongImage);
    INFO("darkening across the light " << acrossDarkening << ", along it " << alongDarkening);

    // Both cameras see a shadow at all...
    CHECK(acrossDarkening > 0.15f);
    CHECK(alongDarkening > 0.15f);
    // ...and they see the same one, to within the difference two cascade fits make. Before the fix
    // the perpendicular camera measured essentially zero while the other measured a full blob,
    // because the quad was oriented for the viewer in both.
    CHECK(std::abs(acrossDarkening - alongDarkening) < 0.25f);
}

// ================================================================================================
// The cascade overlay
// ================================================================================================

TEST_CASE("the cascade overlay draws the volume the renderer uploaded", "[gpu][shadows][lab][debug]") {
    // The overlay's one load-bearing property: it draws from the views it is HANDED, and fits
    // nothing of its own. An overlay that fitted its own cascades would agree with the renderer
    // exactly until the day it mattered (§37), so the check here is that the geometry it emits is
    // the inverse of the matrix the depth pass rasterised with -- and that an empty span draws
    // nothing, which is the honest picture of a frame with no shadow views.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::DebugDraw draw(*ctx, shaders); // the builder never touches the device

    scene::Scene s;
    s.camera.position = {0.0f, 6.0f, 14.0f};
    s.camera.target = {0.0f, 3.0f, -80.0f};
    s.camera.fovYRadians = 0.96f;
    s.camera.nearPlane = 0.5f;
    s.camera.farPlane = 200.0f;

    const glm::vec3 light = glm::normalize(glm::vec3(0.829f, -0.5592f, 0.0f));
    const float aspect = 16.0f / 9.0f;
    const glm::mat4 inverseCamera =
        glm::inverse(s.camera.projection(aspect) * s.camera.view());
    std::vector<rendering::ShadowView> views;
    views.push_back(rendering::fitDirectionalCascade(inverseCamera, 0.5f, 200.0f, 0.5f, 6.19f, light,
                                                     2048, 120.0f));
    views.push_back(rendering::fitDirectionalCascade(inverseCamera, 0.5f, 200.0f, 6.19f, 19.99f, light,
                                                     2048, 120.0f));

    rendering::DebugViewOptions options;
    options.frustumAspect = aspect;

    // The control first: the switch is on and there are no views, so there is nothing to draw and
    // nothing is drawn. Without this the assertion below passes for a builder that draws a box
    // whatever it is given.
    options.shadowCascades = true;
    options.shadowCascadeSlices = true;
    rendering::buildDebugGeometry(draw, s, options, 0.0, nullptr, nullptr, {});
    CHECK(draw.empty());

    // ...and the other control: views, and the switches off.
    draw.clear();
    options.shadowCascades = false;
    options.shadowCascadeSlices = false;
    rendering::buildDebugGeometry(draw, s, options, 0.0, nullptr, nullptr, views);
    CHECK(draw.empty());

    draw.clear();
    options.shadowCascades = true;
    rendering::buildDebugGeometry(draw, s, options, 0.0, nullptr, nullptr, views);
    CHECK(draw.lineVertexCount() == 2 * 24);  // twelve edges, two vertices each, per view
    CHECK(draw.pointVertexCount() == 2);      // the texel-snapped centre of each

    // Every vertex is a corner of one of the two boxes the renderer would rasterise into. Checked
    // by containment rather than by count: the overlay is allowed to order its edges how it likes,
    // and it is not allowed to draw a volume the renderer did not use.
    std::vector<glm::vec3> corners;
    for (const rendering::ShadowView& v : views) {
        for (const glm::vec3& c : rendering::frustumCorners(glm::inverse(v.viewProj))) {
            corners.push_back(c);
        }
    }
    for (const rendering::DebugVertex& vertex : draw.lineVertices()) {
        float nearest = 1e9f;
        for (const glm::vec3& c : corners) {
            nearest = std::min(nearest, glm::length(vertex.position - c));
        }
        INFO("vertex " << vertex.position.x << ", " << vertex.position.y << ", " << vertex.position.z);
        CHECK(nearest < 1e-2f);
    }
    // The snapped centres, which are the stabilisation instrument: they are what moves in whole
    // texels or not at all.
    for (std::size_t i = 0; i < views.size(); ++i) {
        CHECK(glm::length(draw.pointVertices()[i].position - views[i].center) < 1e-3f);
    }

    // The camera slice is a different volume from the light-space box, and drawing one when the
    // other was asked for is the mistake this pair of switches exists to make impossible.
    draw.clear();
    options.shadowCascades = false;
    options.shadowCascadeSlices = true;
    rendering::buildDebugGeometry(draw, s, options, 0.0, nullptr, nullptr, views);
    CHECK(draw.lineVertexCount() == 2 * 24);
    CHECK(draw.pointVertexCount() == 0);
    bool anyOutsideTheLightBoxes = false;
    for (const rendering::DebugVertex& vertex : draw.lineVertices()) {
        float nearest = 1e9f;
        for (const glm::vec3& c : corners) {
            nearest = std::min(nearest, glm::length(vertex.position - c));
        }
        anyOutsideTheLightBoxes = anyOutsideTheLightBoxes || nearest > 1.0f;
    }
    CHECK(anyOutsideTheLightBoxes);

    // And one view at a time.
    draw.clear();
    options.shadowCascades = true;
    options.shadowCascadeSlices = false;
    options.shadowCascade = 1;
    rendering::buildDebugGeometry(draw, s, options, 0.0, nullptr, nullptr, views);
    CHECK(draw.lineVertexCount() == 24);
    CHECK(draw.pointVertexCount() == 1);
    CHECK(glm::length(draw.pointVertices()[0].position - views[1].center) < 1e-3f);
}

TEST_CASE("the caster overlay colours by what the frame did, not by what the camera kept",
          "[gpu][shadows][lab][debug]") {
    // `submittedOnly` skips camera-culled entities, and a camera-culled entity that still casts is
    // the interesting case -- so the caster overlay runs in its own loop and does not honour it.
    // An overlay that hid the only object worth looking at while another switch was on would be
    // worse than none.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::DebugDraw draw(*ctx, shaders);

    scene::Scene s;
    s.camera.position = {0.0f, 6.0f, 14.0f};
    s.camera.target = {0.0f, 3.0f, -80.0f};
    s.camera.fovYRadians = 0.96f;
    s.camera.nearPlane = 0.5f;
    s.camera.farPlane = 200.0f;
    const auto mesh = s.addMesh(scene::makeCube(1.0f));
    s.addEntity("on-screen", mesh);
    s.addEntity("off-screen-but-casting", mesh);
    s.entities[0].transform.position = {0.0f, 1.0f, -20.0f};
    s.entities[1].transform.position = {0.0f, 1.0f, 8.0f};
    s.entities[1].cameraCulled = true;

    const glm::vec3 light = glm::normalize(glm::vec3(0.829f, -0.5592f, 0.0f));
    const glm::mat4 inverseCamera =
        glm::inverse(s.camera.projection(16.0f / 9.0f) * s.camera.view());
    std::vector<rendering::ShadowView> views;
    views.push_back(rendering::fitDirectionalCascade(inverseCamera, 0.5f, 200.0f, 0.5f, 19.99f, light,
                                                     2048, 120.0f));

    rendering::DebugViewOptions options;
    options.shadowCasters = true;
    options.submittedOnly = true; // deliberately: the overlay must ignore it
    rendering::buildDebugGeometry(draw, s, options, 0.0, nullptr, nullptr, views);
    CHECK(draw.lineVertexCount() == 2 * 24); // a box for each entity, the culled one included

    // The colours are the verdict, so they have to differ. Amber for the caster the camera rejected,
    // green for the one it kept.
    glm::vec4 first = draw.lineVertices()[0].color;
    glm::vec4 second = draw.lineVertices()[24].color;
    CHECK(glm::length(first - second) > 0.2f);
    CHECK(rendering::casterState(s.entities[0], s.meshBounds(mesh), {}) == rendering::CasterState::Caster);
}

// ================================================================================================
// The ecology's own caster list (ADR-265, ADR-287)
// ================================================================================================

namespace {

// A floor, one floating box, and the fixture's key travelling +X at 34 degrees -- but photographed
// from straight down -Z, so "off the left of frame" and "in shot" are two different places on one
// ground plane and a caster can be put in the first while its shadow lands in the second.
//
// 34 degrees runs 1.482 m of shadow per metre of height, so a box 18 m up throws its shadow 26.7 m
// to +X of itself. That gap is the whole fixture: it is what lets the caster sit far outside the
// frustum while the thing it casts sits in the middle of the frame. A caster on the ground would
// have its shadow four metres away and the frame edge would have to fall inside those four metres.
scene::Scene offScreenCasterScene(float casterX, bool casts) {
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.showSkybox = false;
    s.environment.environmentIntensity = 0.0f;
    s.camera.position = {0.0f, 14.0f, 34.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    s.camera.fovYRadians = 0.9f;
    s.camera.nearPlane = 0.5f;
    s.camera.farPlane = 200.0f;

    const auto floor = s.addMesh(floorMesh(90.0f));
    auto& e = s.addEntity("floor", floor);
    e.material.baseColor = glm::vec3(0.85f);
    e.material.roughness = 0.95f;
    e.material.metallic = 0.0f;

    scene::PunctualLight key;
    key.name = "key";
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(0.829f, -0.5592f, 0.0f));
    key.color = glm::vec3(1.0f);
    key.intensity = 4.0f;
    key.castsShadow = true;
    key.contactShadow = false;
    key.softness = 0.2f;
    s.addLight(key);

    scene::ProceduralGeometry g = caster("ecology", 1);
    g.source.size = {6.0f, 6.0f, 6.0f};
    g.meshHash = specHash(g.source) ^ 0x9E37ull;
    g.castsShadow = casts;
    g.instances.clear();
    g.instances.push_back(recordAt({casterX, 18.0f, 0.0f}, 1.0f));
    s.procedurals.push_back(g);
    return s;
}

// Whether a world point projects inside the frame at all -- the fixture check that says "the camera
// cannot see this", rather than a pixel coordinate that happens to land off the edge of an array.
bool insideFrame(const scene::Scene& s, const glm::vec3& world) {
    const float aspect = static_cast<float>(kWidth) / static_cast<float>(kHeight);
    const glm::vec4 clip = s.camera.projection(aspect) * s.camera.view() * glm::vec4(world, 1.0f);
    if (clip.w <= 0.0f) {
        return false;
    }
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    return std::abs(ndc.x) <= 1.0f && std::abs(ndc.y) <= 1.0f && ndc.z >= 0.0f && ndc.z <= 1.0f;
}

} // namespace

TEST_CASE("a procedural instance the camera cannot see casts into shot",
          "[gpu][shadows][lab][casters]") {
    // The rendered half of ADR-287. `ProceduralRenderer` ran one cull against the camera frustum
    // and `drawShadow` drew the args it wrote, so this box -- 46 m off the left of frame, its
    // shadow landing in the middle of the picture -- cast nothing at all. Entities have had a
    // second pass over exactly this case since ADR-046; the ecology had none.
    //
    // §37: the assertion is on the pixels, not on a counter. The counters are checked too, at the
    // end, and where the two disagree the image wins.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto renderer = std::make_unique<rendering::SceneRenderer>(*ctx, shaders);
    REQUIRE(renderer->init().has_value());

    constexpr float kCasterX = -46.0f;
    // 18 m up, 34 degrees: the shadow lands 26.68 m to +X of the caster.
    const glm::vec3 shadowGround(kCasterX + 18.0f * 1.4826f, 0.0f, 0.0f);
    const glm::vec3 openGround(kCasterX + 18.0f * 1.4826f + 16.0f, 0.0f, 0.0f);

    const scene::Scene arm = offScreenCasterScene(kCasterX, true);
    // The control is the same scene with ONE field different: the object does not cast. Same
    // camera, same light, same geometry, same tier -- so a difference between the two frames is
    // that field and nothing else (ADR-182).
    const scene::Scene control = offScreenCasterScene(kCasterX, false);

    // The fixture's own controls, before either frame is read. If the camera can see the caster
    // this test proves nothing, and if the shadow falls outside the frame the probe is pointed at
    // the wrong place -- the mistake ADR-265 records paying for once already.
    REQUIRE_FALSE(insideFrame(arm, {kCasterX, 18.0f, 0.0f}));
    REQUIRE(insideFrame(arm, shadowGround));
    REQUIRE(insideFrame(arm, openGround));

    auto armImage = renderer->renderToImage(arm, frameAt(8), kWidth, kHeight);
    REQUIRE(armImage.has_value());
    const auto armCounts = renderer->procedurals().readCullCounts("ecology");
    auto controlImage = renderer->renderToImage(control, frameAt(8), kWidth, kHeight);
    REQUIRE(controlImage.has_value());

    const glm::ivec2 shadowPixel = pixelOf(arm, shadowGround);
    const glm::ivec2 openPixel = pixelOf(arm, openGround);
    const float armShadow = luminanceAt(*armImage, shadowPixel, 3);
    const float armOpen = luminanceAt(*armImage, openPixel, 3);
    const float controlShadow = luminanceAt(*controlImage, shadowPixel, 3);
    const float controlOpen = luminanceAt(*controlImage, openPixel, 3);
    INFO("arm: shadow " << armShadow << " open " << armOpen << "; control: shadow " << controlShadow
                        << " open " << controlOpen);

    // The control first: the ground is lit in both frames, so "darker" below means a shadow and not
    // a dimmer picture.
    CHECK(controlOpen > 0.05f);
    CHECK(armOpen > controlOpen * 0.9f);
    // And the control really does leave that piece of ground alone -- if it darkened it too, the
    // arm would be measuring something other than this caster.
    CHECK(controlShadow > controlOpen * 0.9f);

    // The invariant: before this fix the arm was the control, to the pixel.
    CHECK(armShadow < armOpen * 0.75f);

    // The counters, checked after the image and never instead of it. This is the number ADR-265
    // reported as "one number where there should be two": the camera list is empty and the caster
    // list is not.
    REQUIRE(armCounts.has_value());
    INFO("camera list " << armCounts->visible << ", caster list " << armCounts->shadowVisible
                        << ", records " << armCounts->records);
    CHECK(armCounts->records == 1);
    CHECK(armCounts->visible == 0);
    CHECK(armCounts->shadowVisible == 1);
}

TEST_CASE("an instance past the cascades is drawn and does not cast", "[gpu][shadows][lab][casters]") {
    // The other direction of the asymmetry, and the reason the caster list is not simply a superset
    // of the camera's: the cascades stop at the shadow range (ADR-112, 77 m here), so an instance
    // the camera sees well beyond it has no map to be drawn into. A second list that kept it would
    // be spending draws on geometry that cannot write a texel.
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto renderer = std::make_unique<rendering::SceneRenderer>(*ctx, shaders);
    REQUIRE(renderer->init().has_value());

    scene::Scene s = offScreenCasterScene(0.0f, true);
    // Straight down the lens, 150 m out and 18 m up: comfortably inside the frame, comfortably past
    // the range the cascades reach.
    s.procedurals[0].instances.clear();
    s.procedurals[0].instances.push_back(recordAt({0.0f, 18.0f, -150.0f}, 1.0f));
    ++s.procedurals[0].structureVersion;
    REQUIRE(insideFrame(s, {0.0f, 18.0f, -150.0f}));

    auto image = renderer->renderToImage(s, frameAt(8), kWidth, kHeight);
    REQUIRE(image.has_value());
    const auto counts = renderer->procedurals().readCullCounts("ecology");
    REQUIRE(counts.has_value());
    INFO("camera list " << counts->visible << ", caster list " << counts->shadowVisible);
    // The control: the camera really does keep it, so "does not cast" is a statement about the
    // caster list and not about an instance that was culled outright.
    CHECK(counts->visible == 1);
    CHECK(counts->shadowVisible == 0);
}

TEST_CASE("an object that loses a rung keeps its caster list", "[gpu][shadows][lab][casters]") {
    // The caster list lives in the second half of the same `visible` buffer as the camera list, and
    // where the second half BEGINS is the object's rung count. The cull buffers are grow-only, so a
    // rung count that goes DOWN leaves the shader laying its two lists out at one base
    // (`counts.y`, this frame's rung count) while the draw groups read another (`cullLodCount`, the
    // high-water mark). The shadow draw then reads a slice nobody wrote this frame.
    //
    // **The first version of this test could not fail.** It used one instance, and the compacted
    // list of a one-record object is `[0]` at every slice -- including an untouched one, because the
    // buffer is zeroed when it is allocated. The probe read the right answer out of the wrong place
    // and reported no defect. So this one carries two records and makes the right answer `[1]`: a
    // stale slice then draws the wrong instance, and the wrong instance is a thousand metres away.
    //
    // The counters cannot see any of this either -- the cull pass writes its stats correctly
    // whichever slice the draw goes on to read. Only the pixels can (§37).
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto renderer = std::make_unique<rendering::SceneRenderer>(*ctx, shaders);
    REQUIRE(renderer->init().has_value());

    constexpr float kCasterX = -46.0f;
    const glm::vec3 shadowGround(kCasterX + 18.0f * 1.4826f, 0.0f, 0.0f);
    const glm::vec3 openGround(kCasterX + 18.0f * 1.4826f + 16.0f, 0.0f, 0.0f);

    // Record 0 is a thousand metres away and past `maxDistance`, so neither list ever keeps it;
    // record 1 is the caster. The compacted list is therefore `[1]`, which is a number a zeroed
    // slice cannot produce.
    const auto sceneWith = [&](int rungs) {
        scene::Scene s = offScreenCasterScene(kCasterX, true);
        s.procedurals[0].lod.lodCount = rungs;
        s.procedurals[0].instances.clear();
        s.procedurals[0].instances.push_back(recordAt({0.0f, 18.0f, -1000.0f}, 1.0f));
        s.procedurals[0].instances.push_back(recordAt({kCasterX, 18.0f, 0.0f}, 1.0f));
        return s;
    };
    const auto darkening = [&](const gpu::Image8& image, const scene::Scene& s) {
        const float shadow = luminanceAt(image, pixelOf(s, shadowGround), 3);
        const float open = luminanceAt(image, pixelOf(s, openGround), 3);
        REQUIRE(open > 0.05f);
        return 1.0f - shadow / open;
    };

    // Four rungs first, so the cull buffers are allocated for four and the high-water mark is set.
    const scene::Scene wide = sceneWith(4);
    auto first = renderer->renderToImage(wide, frameAt(8), kWidth, kHeight);
    REQUIRE(first.has_value());
    const float settled = darkening(*first, wide);

    // The control: a second frame at the SAME rung count. Everything else about this pair is
    // identical to the arm below, so a difference between them is the rung count and nothing else.
    auto controlImage = renderer->renderToImage(wide, frameAt(9), kWidth, kHeight);
    REQUIRE(controlImage.has_value());
    const float control = darkening(*controlImage, wide);

    // The arm: the same object, by the same name, on two rungs.
    const scene::Scene narrow = sceneWith(2);
    auto armImage = renderer->renderToImage(narrow, frameAt(10), kWidth, kHeight);
    REQUIRE(armImage.has_value());
    const float arm = darkening(*armImage, narrow);

    INFO("darkening: settled " << settled << ", control (still four rungs) " << control
                               << ", arm (dropped to two) " << arm);
    // The controls first, or the arm is a comparison against nothing: four rungs casts, and it
    // still casts on a second frame.
    CHECK(settled > 0.15f);
    CHECK(control > 0.15f);
    // The invariant: dropping a rung does not drop the shadow.
    CHECK(arm > 0.15f);
}
