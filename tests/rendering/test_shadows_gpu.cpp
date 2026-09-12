// Shadows, contact shadows and ambient occlusion on the GPU (ADR-034), plus the auxiliary render
// targets and the clustered light path (ADR-033/035).
//
// The scenes here are deliberately trivial - a floor, a box above it, one directional light - so a
// named pixel can be asserted rather than a whole-image hash: a shadow either darkens the floor
// under the box or it does not.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/light_data.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;

namespace {

constexpr std::uint32_t kSize = 192;

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

gpu::ShaderLibrary makeShaders(gpu::Context& ctx) {
    return gpu::ShaderLibrary(ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
}

std::unique_ptr<rendering::SceneRenderer> makeRenderer(gpu::Context& ctx, gpu::ShaderLibrary& shaders) {
    auto renderer = std::make_unique<rendering::SceneRenderer>(ctx, shaders);
    REQUIRE(renderer->init().has_value());
    return renderer;
}

scene::MeshData boxMesh(glm::vec3 half) {
    scene::MeshData m;
    const glm::vec3 n[6] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    for (const glm::vec3 normal : n) {
        const glm::vec3 u = std::abs(normal.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::cross(glm::vec3(0, 1, 0), normal);
        const glm::vec3 v = glm::cross(normal, u);
        const glm::vec3 c = normal * half;
        const glm::vec3 du = u * half;
        const glm::vec3 dv = v * half;
        const auto base = static_cast<std::uint32_t>(m.vertices.size());
        m.vertices.push_back({c - du - dv, normal, {0, 0}});
        m.vertices.push_back({c + du - dv, normal, {1, 0}});
        m.vertices.push_back({c + du + dv, normal, {1, 1}});
        m.vertices.push_back({c - du + dv, normal, {0, 1}});
        m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return m;
}

// A wide floor at y = 0, a box floating above it at y = 3, and one directional light shining
// straight down. The box's shadow lands on the floor directly under it.
scene::Scene shadowScene(bool castsShadow) {
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.showSkybox = false;
    s.environment.environmentIntensity = 0.0f;
    s.camera.position = {0.0f, 12.0f, 14.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    s.camera.fovYRadians = 0.9f;
    s.camera.nearPlane = 0.5f;
    s.camera.farPlane = 120.0f;

    const auto floor = s.addMesh(boxMesh({20.0f, 0.25f, 20.0f}));
    const auto box = s.addMesh(boxMesh({2.0f, 2.0f, 2.0f}));
    {
        auto& e = s.addEntity("floor", floor);
        e.transform.position = {0.0f, -0.25f, 0.0f};
        e.material.baseColor = glm::vec3(0.8f);
        e.material.roughness = 0.9f;
        e.material.metallic = 0.0f;
    }
    {
        auto& e = s.addEntity("box", box);
        e.transform.position = {0.0f, 5.0f, 0.0f};
        e.material.baseColor = glm::vec3(0.8f);
        e.material.roughness = 0.9f;
    }
    scene::PunctualLight key;
    key.name = "key";
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(0.0f, -1.0f, 0.0f));
    key.color = glm::vec3(1.0f);
    key.intensity = 4.0f;
    key.castsShadow = castsShadow;
    key.contactShadow = false;
    key.softness = 0.4f;
    s.addLight(key);
    return s;
}

float luminanceAt(const gpu::Image8& image, std::uint32_t x, std::uint32_t y) {
    const std::size_t index = (static_cast<std::size_t>(y) * image.width + x) * 4;
    REQUIRE(index + 2 < image.rgba.size());
    return (0.2126f * static_cast<float>(image.rgba[index]) + 0.7152f * static_cast<float>(image.rgba[index + 1]) +
            0.0722f * static_cast<float>(image.rgba[index + 2])) /
           255.0f;
}

FrameTime frameAt(std::uint64_t index) {
    FrameTime t;
    t.frameIndex = index;
    t.renderTime = static_cast<double>(index) / 60.0;
    t.deltaTime = 1.0 / 60.0;
    return t;
}

} // namespace

TEST_CASE("a shadow-casting key light darkens the floor under an object", "[gpu][shadows]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    auto renderer = makeRenderer(*ctx, shaders);

    const scene::Scene lit = shadowScene(false);
    const scene::Scene shadowed = shadowScene(true);
    auto without = renderer->renderToImage(lit, frameAt(3), kSize, kSize);
    REQUIRE(without.has_value());
    auto with = renderer->renderToImage(shadowed, frameAt(3), kSize, kSize);
    REQUIRE(with.has_value());

    // The camera looks at the origin, so the floor point directly under the box is the centre of
    // the frame; the floor near the left edge on the same row is outside the shadow.
    const std::uint32_t shadowX = kSize / 2;
    const std::uint32_t shadowY = kSize / 2;
    const std::uint32_t openX = kSize / 12;
    const std::uint32_t openY = shadowY;

    const float shadowLit = luminanceAt(*without, shadowX, shadowY);
    const float shadowDark = luminanceAt(*with, shadowX, shadowY);
    const float openLit = luminanceAt(*without, openX, openY);
    const float openDark = luminanceAt(*with, openX, openY);

    INFO("under the box: " << shadowLit << " -> " << shadowDark << ", open floor: " << openLit << " -> " << openDark);
    CHECK(shadowLit > 0.05f);              // the control frame really is lit there
    CHECK(shadowDark < shadowLit * 0.75f); // the shadow visibly darkens it
    CHECK(openDark > openLit * 0.9f);      // and leaves the open floor alone
}

// The same floor and box, lit by a *point* light directly above the box instead of a directional
// one. Point lights had no shadow map at all until ADR-034 was extended with cube faces, so the box
// floated over a lit floor.
scene::Scene pointShadowScene(bool castsShadow) {
    scene::Scene s = shadowScene(false);
    s.lights.clear();
    scene::PunctualLight lamp;
    lamp.name = "lamp";
    lamp.type = scene::PunctualLight::Type::Point;
    lamp.position = {0.0f, 11.0f, 0.0f};
    lamp.color = glm::vec3(1.0f);
    lamp.intensity = 900.0f; // candela; the floor is six metres below the box
    lamp.range = 60.0f;
    lamp.castsShadow = castsShadow;
    lamp.contactShadow = false;
    lamp.softness = 0.3f;
    s.addLight(lamp);
    return s;
}

TEST_CASE("a point light casts a shadow", "[gpu][shadows][point]") {
    // Six faces in the ordinary shadow atlas, picked per fragment by the dominant axis of the
    // direction from the light -- no cube texture and no cube sampler, so the depth passes the
    // encoder already writes per view are unchanged.
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    auto renderer = makeRenderer(*ctx, shaders);

    const scene::Scene lit = pointShadowScene(false);
    const scene::Scene shadowed = pointShadowScene(true);
    auto without = renderer->renderToImage(lit, frameAt(3), kSize, kSize);
    REQUIRE(without.has_value());
    auto with = renderer->renderToImage(shadowed, frameAt(3), kSize, kSize);
    REQUIRE(with.has_value());
    CHECK(ctx->errorCount() == 0);

    // Six views were built for it, and they are the whole cube rather than part of one.
    CHECK(renderer->stats().shadows.points == 1);
    CHECK(renderer->stats().shadows.views == 6);

    const std::uint32_t shadowX = kSize / 2;
    const std::uint32_t shadowY = kSize / 2;
    const std::uint32_t openX = kSize / 12;

    const float shadowLit = luminanceAt(*without, shadowX, shadowY);
    const float shadowDark = luminanceAt(*with, shadowX, shadowY);
    const float openLit = luminanceAt(*without, openX, shadowY);
    const float openDark = luminanceAt(*with, openX, shadowY);

    INFO("under the box: " << shadowLit << " -> " << shadowDark << ", open floor: " << openLit
                           << " -> " << openDark);
    REQUIRE(shadowLit > 0.05f);            // the control frame really is lit under the box
    CHECK(shadowDark < shadowLit * 0.75f); // and the point light's shadow darkens it
    CHECK(openDark > openLit * 0.85f);     // while the open floor, lit from the same lamp, stays lit
}

TEST_CASE("a caster the camera cannot see still casts", "[gpu][shadows]") {
    // ADR-046: terrain's frustum cull set Entity::visible, the shadow pass honours visible, so a
    // hill behind the camera stopped casting into shot. `cameraCulled` is the camera's verdict and
    // nothing else's: the camera passes skip the entity, the shadow passes still get it and apply
    // the test that actually matters -- the cascade's own frustum.
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    auto renderer = makeRenderer(*ctx, shaders);

    const std::uint32_t shadowX = kSize / 2;
    const std::uint32_t shadowY = kSize / 2;

    scene::Scene drawn = shadowScene(true);
    auto withBox = renderer->renderToImage(drawn, frameAt(3), kSize, kSize);
    REQUIRE(withBox.has_value());

    scene::Scene offScreen = shadowScene(true);
    offScreen.entities[1].cameraCulled = true; // "the camera cannot see it"
    auto withoutBox = renderer->renderToImage(offScreen, frameAt(3), kSize, kSize);
    REQUIRE(withoutBox.has_value());

    scene::Scene absent = shadowScene(true);
    absent.entities[1].visible = false; // "it is not in the scene"
    auto withNothing = renderer->renderToImage(absent, frameAt(3), kSize, kSize);
    REQUIRE(withNothing.has_value());

    const float drawnShadow = luminanceAt(*withBox, shadowX, shadowY);
    const float culledShadow = luminanceAt(*withoutBox, shadowX, shadowY);
    const float absentShadow = luminanceAt(*withNothing, shadowX, shadowY);
    INFO("floor under the box: drawn " << drawnShadow << ", camera-culled " << culledShadow
                                       << ", hidden " << absentShadow);
    CHECK(absentShadow > 0.05f);                     // with no caster the floor is lit
    CHECK(drawnShadow < absentShadow * 0.75f);       // the caster darkens it
    CHECK(culledShadow < absentShadow * 0.75f);      // and still does when the camera cannot see it
    CHECK(std::abs(culledShadow - drawnShadow) < 0.02f); // by the same amount

    // The camera really did skip it, or the shadow above proves nothing about the split.
    CHECK(gpu::hashImage(*withBox) != gpu::hashImage(*withoutBox));
}

TEST_CASE("shadowed frames are deterministic across renderers", "[gpu][shadows]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    const scene::Scene s = shadowScene(true);
    auto a = makeRenderer(*ctx, shaders);
    auto first = a->renderToImage(s, frameAt(7), kSize, kSize);
    REQUIRE(first.has_value());
    auto b = makeRenderer(*ctx, shaders);
    auto second = b->renderToImage(s, frameAt(7), kSize, kSize);
    REQUIRE(second.has_value());
    CHECK(gpu::hashImage(*first) == gpu::hashImage(*second));
}

TEST_CASE("ambient occlusion is deterministic and darkens a crease", "[gpu][ao]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    // A box resting on the floor: the corner where they meet is the crease AO must find.
    scene::Scene s = shadowScene(false);
    s.entities[1].transform.position = {0.0f, 2.0f, 0.0f};
    s.lights[0].intensity = 0.2f; // ambient-dominant, so occlusion is what moves the pixels
    s.environment.environmentIntensity = 1.0f;

    auto withAo = makeRenderer(*ctx, shaders);
    withAo->setQuality(rendering::QualityTier::High);
    auto imageA = withAo->renderToImage(s, frameAt(11), kSize, kSize);
    REQUIRE(imageA.has_value());
    auto imageB = withAo->renderToImage(s, frameAt(11), kSize, kSize);
    REQUIRE(imageB.has_value());
    // A second render of the same frame index from the same renderer reuses the history, so the
    // determinism claim is about a fresh renderer seeing the same frame twice.
    auto again = makeRenderer(*ctx, shaders);
    again->setQuality(rendering::QualityTier::High);
    auto imageC = again->renderToImage(s, frameAt(11), kSize, kSize);
    REQUIRE(imageC.has_value());
    CHECK(gpu::hashImage(*imageA) == gpu::hashImage(*imageC));
    CHECK(withAo->stats().ao.width > 0);
}

TEST_CASE("a flat surface seen edge-on is not occluded by itself", "[gpu][ao]") {
    // GTAO takes a horizon from any sample whose direction has a large cosine against the view
    // vector, and on ground running away from the camera every coplanar sample has one. Without a
    // height test against the tangent plane the term collapses into a grey wash over anything seen
    // at a grazing angle -- which is invisible on an object seen from a normal angle and ruinous on
    // a landscape, where most of the frame is ground receding into the distance.
    //
    // A plane occludes nothing, so ambient light reaching it should be close to unoccluded from any
    // angle. It is not asserted to be exactly unoccluded: the bent normal the AO pass also produces
    // tilts the ambient lookup toward the horizon at a grazing angle, which is a real effect and
    // costs a little light. What is asserted is the size of the loss. Before the height test the
    // grazing row of this frame lost 51% of its ambient; it now loses 23%, and the rows further
    // away were never affected at all because the horizon march is clamped out there.
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.showSkybox = true;
    s.environment.environmentIntensity = 3.0f; // ambient-dominant: AO is what moves these pixels
    // Eye height on a wide floor, looking along it: the ground fills the lower frame at every angle
    // from steeply down to nearly edge-on, which is the range the horizon search has to survive.
    s.camera.position = {0.0f, 1.7f, 30.0f};
    s.camera.target = {0.0f, 1.4f, -40.0f};
    s.camera.fovYRadians = 0.8f;
    s.camera.nearPlane = 0.1f;
    s.camera.farPlane = 400.0f;
    const auto floor = s.addMesh(boxMesh({150.0f, 0.25f, 150.0f}));
    auto& e = s.addEntity("floor", floor);
    e.transform.position = {0.0f, -0.25f, 0.0f};
    e.material.baseColor = glm::vec3(0.8f);
    e.material.roughness = 0.9f;
    scene::PunctualLight key;
    key.name = "key";
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(0.3f, -0.9f, -0.3f));
    key.intensity = 0.15f; // the sky does the lighting, so occlusion is not hidden by a key
    key.castsShadow = false;
    key.contactShadow = false;
    s.addLight(key);

    rendering::QualitySettings quality = rendering::QualitySettings::forTier(rendering::QualityTier::High);
    quality.contactShadows = false;

    auto withAo = makeRenderer(*ctx, shaders);
    quality.ambientOcclusion = true;
    withAo->setQualitySettings(quality);
    auto lit = withAo->renderToImage(s, frameAt(9), kSize, kSize);
    REQUIRE(lit.has_value());

    auto withoutAo = makeRenderer(*ctx, shaders);
    quality.ambientOcclusion = false;
    withoutAo->setQualitySettings(quality);
    auto plain = withoutAo->renderToImage(s, frameAt(9), kSize, kSize);
    REQUIRE(plain.has_value());

    // Sample down the middle of the frame, from the near ground to the horizon: the grazing end is
    // where self-occlusion showed up worst, so the samples have to reach it.
    for (const std::uint32_t y : {kSize * 3u / 4u, kSize * 5u / 8u, kSize * 9u / 16u}) {
        const float a = luminanceAt(*lit, kSize / 2, y);
        const float b = luminanceAt(*plain, kSize / 2, y);
        INFO("row " << y << ": ao " << a << " vs no-ao " << b << " ratio " << (b > 0 ? a / b : 0.0f));
        REQUIRE(b > 0.02f); // the floor is actually lit here, or the comparison proves nothing
        CHECK(a >= b * 0.70f);
    }
}

TEST_CASE("the auxiliary targets are written by the scene pass", "[gpu][aux]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    auto renderer = makeRenderer(*ctx, shaders);
    const scene::Scene s = shadowScene(true);
    auto image = renderer->renderToImage(s, frameAt(2), kSize, kSize);
    REQUIRE(image.has_value());
    CHECK(renderer->normalRoughnessTexture() != nullptr);
    CHECK(renderer->velocityTexture() != nullptr);
    CHECK(renderer->emissionTexture() != nullptr);
    CHECK(renderer->identifierTexture() != nullptr);
    CHECK(renderer->linearDepthTexture() != nullptr);

    // The identifier target must name the two entities where they are on screen.
    auto ids = gpu::readTextureR32Uint(*ctx, renderer->identifierTexture(), kSize, kSize);
    REQUIRE(ids.has_value());
    const std::uint32_t centre = (*ids)[(static_cast<std::size_t>(kSize) / 2) * kSize + kSize / 2];
    CHECK((centre & 0xFFFFu) <= 1u);
}

TEST_CASE("the clustered path and the uniform fallback agree on one directional light",
          "[gpu][clusters]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    const scene::Scene s = shadowScene(false);
    auto clustered = makeRenderer(*ctx, shaders);
    auto a = clustered->renderToImage(s, frameAt(5), kSize, kSize);
    REQUIRE(a.has_value());

    // The fallback tier evaluates the eight-light uniform array instead of the froxel grid; with a
    // single directional light and no shadows the two paths must agree closely.
    auto fallback = makeRenderer(*ctx, shaders);
    rendering::QualitySettings quality = rendering::QualitySettings::forTier(rendering::QualityTier::Realtime);
    quality.ambientOcclusion = false;
    quality.contactShadows = false;
    clustered->setQualitySettings(quality);
    auto c = clustered->renderToImage(s, frameAt(5), kSize, kSize);
    REQUIRE(c.has_value());
    a = std::move(c);
    quality.clusteredLighting = false; // the only difference from here on
    fallback->setQualitySettings(quality);
    auto b = fallback->renderToImage(s, frameAt(5), kSize, kSize);
    REQUIRE(b.has_value());

    double diff = 0.0;
    for (std::size_t i = 0; i + 3 < a->rgba.size(); i += 4) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
            diff += std::abs(static_cast<double>(a->rgba[i + channel]) - static_cast<double>(b->rgba[i + channel]));
        }
    }
    const double mean = diff / (static_cast<double>(a->rgba.size()) * 0.75);
    INFO("mean channel difference " << mean);
    CHECK(mean < 3.0);
}

TEST_CASE("contact shadows darken where an object meets a surface", "[gpu][shadows]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    // A small box resting on the floor, lit almost along the floor: the cascade covers the whole
    // frustum and cannot resolve the few centimetres where the two meet, which is exactly the gap
    // the screen-space march exists to fill (ADR-034).
    scene::Scene s = shadowScene(false); // no map, so the march is the only shadow in the frame
    s.entities[1].transform.position = {0.0f, 0.75f, 0.0f};
    s.meshes[1] = boxMesh({0.75f, 0.75f, 0.75f});
    ++s.meshVersion;
    s.lights[0].direction = glm::normalize(glm::vec3(0.25f, -1.0f, 0.35f));
    s.lights[0].contactShadow = true;

    rendering::QualitySettings quality = rendering::QualitySettings::forTier(rendering::QualityTier::Realtime);
    quality.ambientOcclusion = false; // isolate the contact march from the occlusion term
    quality.contactShadows = false;

    auto renderer = makeRenderer(*ctx, shaders);
    renderer->setQualitySettings(quality);
    auto without = renderer->renderToImage(s, frameAt(4), kSize, kSize);
    REQUIRE(without.has_value());

    quality.contactShadows = true;
    auto contact = makeRenderer(*ctx, shaders);
    contact->setQualitySettings(quality);
    auto with = contact->renderToImage(s, frameAt(4), kSize, kSize);
    REQUIRE(with.has_value());

    // Sum the darkening over the band of floor just around the box's base.
    double delta = 0.0;
    std::uint32_t counted = 0;
    for (std::uint32_t y = kSize / 2; y < kSize * 3 / 4; ++y) {
        for (std::uint32_t x = kSize / 3; x < kSize * 2 / 3; ++x) {
            delta += static_cast<double>(luminanceAt(*without, x, y)) -
                     static_cast<double>(luminanceAt(*with, x, y));
            ++counted;
        }
    }
    REQUIRE(counted > 0);
    INFO("mean darkening around the contact " << delta / counted);
    CHECK(delta > 0.0);              // the march only ever removes light
    CHECK(delta / counted > 0.0005); // and it removes a measurable amount of it
}

TEST_CASE("the froxel grid the compute pass builds matches the CPU reference", "[gpu][clusters]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    auto renderer = makeRenderer(*ctx, shaders);

    scene::Scene s = shadowScene(false);
    s.lights[0].castsShadow = false;
    // A spread of local lights with explicit ranges, so the CPU reference and the compute pass
    // agree on every influence radius without depending on the intensity heuristic.
    const glm::vec3 positions[] = {{0.0f, 3.0f, 0.0f},  {8.0f, 2.0f, -6.0f}, {-9.0f, 4.0f, 5.0f},
                                   {0.0f, 1.0f, 10.0f}, {14.0f, 6.0f, 12.0f}};
    const float ranges[] = {6.0f, 12.0f, 20.0f, 3.0f, 25.0f};
    for (int i = 0; i < 5; ++i) {
        scene::PunctualLight l;
        l.name = "local" + std::to_string(i);
        l.type = i % 2 == 0 ? scene::PunctualLight::Type::Point : scene::PunctualLight::Type::Spot;
        l.position = positions[i];
        l.direction = glm::normalize(glm::vec3(0.1f, -1.0f, 0.1f));
        l.intensity = 3.0f;
        l.range = ranges[i];
        s.addLight(l);
    }
    auto image = renderer->renderToImage(s, frameAt(1), kSize, kSize);
    REQUIRE(image.has_value());
    CHECK(renderer->stats().clusteredLights == 5);

    // The same grid the renderer built this frame.
    rendering::ClusterGrid grid;
    grid.zNear = std::max(s.camera.nearPlane, 0.01f);
    grid.zFar = std::max(s.camera.farPlane, grid.zNear * 2.0f);
    grid.tanHalfFovY = std::tan(s.camera.effectiveFovY() * 0.5f);
    grid.aspect = 1.0f; // the readback renders square
    const glm::mat4 view = s.camera.view();
    std::vector<glm::vec3> viewPositions;
    std::vector<float> radii;
    for (int i = 0; i < 5; ++i) {
        viewPositions.push_back(glm::vec3(view * glm::vec4(positions[i], 1.0f)));
        radii.push_back(ranges[i]);
    }
    const auto expected = rendering::assignClusters(grid, viewPositions, radii);

    const std::uint64_t words =
        static_cast<std::uint64_t>(rendering::kClusterCount) * (1 + rendering::kMaxLightsPerCluster);
    auto raw = gpu::readBuffer(*ctx, renderer->clusterBuffer(), 0, words * sizeof(std::uint32_t));
    REQUIRE(raw.has_value());
    std::vector<std::uint32_t> data(static_cast<std::size_t>(words));
    std::memcpy(data.data(), raw->data(), raw->size());

    std::size_t mismatches = 0;
    std::size_t populated = 0;
    for (std::uint32_t c = 0; c < rendering::kClusterCount; ++c) {
        const std::uint32_t count = data[c];
        std::vector<std::uint32_t> got(
            data.begin() + static_cast<std::ptrdiff_t>(rendering::kClusterCount + c * rendering::kMaxLightsPerCluster),
            data.begin() + static_cast<std::ptrdiff_t>(rendering::kClusterCount + c * rendering::kMaxLightsPerCluster + count));
        if (got != expected[c]) {
            ++mismatches;
        }
        populated += expected[c].empty() ? 0 : 1;
    }
    INFO("populated froxels " << populated << ", mismatches " << mismatches);
    CHECK(populated > 0);
    CHECK(mismatches == 0);
}

// ---- the half-resolution shadow mask (ADR-087) ---------------------------------------------------

namespace {

// A floor the camera sees almost edge-on, one shadow-casting light raking across it, and a box
// throwing a shadow onto it. The grazing angle is the point: it is where a normal reconstructed
// from the depth buffer is most nearly perpendicular to the view, and where orienting that normal
// by its own view-space z -- which is what this used to do, and what gtao.wgsl did -- becomes a
// coin flip. When it lands wrong the normal points into the ground, the shadow lookup's normal
// offset pushes its sample point under the surface, and open lit floor fills with acne.
// A wide ground plane with a gentle undulation, the shape Glowmere's terrain has. Flat ground
// cannot reproduce the defect this scene exists for: a perfectly flat surface reconstructs one
// exact normal whose view-space z has a definite sign, so orienting by that sign happens to work.
// It is a surface whose facets face slightly different ways -- so that some of them are within
// rounding of perpendicular to the view -- where the sign test becomes a coin flip.
scene::MeshData undulatingGround(float extent, std::uint32_t cells, float amplitude) {
    scene::MeshData m;
    const float step = extent / static_cast<float>(cells);
    const auto height = [amplitude](float x, float z) {
        return amplitude * (std::sin(x * 0.011f) * std::cos(z * 0.017f) + 0.6f * std::sin(z * 0.031f));
    };
    for (std::uint32_t j = 0; j <= cells; ++j) {
        for (std::uint32_t i = 0; i <= cells; ++i) {
            const float x = -extent * 0.5f + static_cast<float>(i) * step;
            const float z = -extent * 0.5f + static_cast<float>(j) * step;
            const float y = height(x, z);
            // Analytic normal, so the *shading* normal is smooth while the triangles are facets --
            // exactly the arrangement the mask has to cope with.
            const float dx = (height(x + 0.5f, z) - height(x - 0.5f, z));
            const float dz = (height(x, z + 0.5f) - height(x, z - 0.5f));
            const glm::vec3 n = glm::normalize(glm::vec3(-dx, 1.0f, -dz));
            m.vertices.push_back({{x, y, z}, n, {static_cast<float>(i), static_cast<float>(j)}});
        }
    }
    const std::uint32_t stride = cells + 1;
    for (std::uint32_t j = 0; j < cells; ++j) {
        for (std::uint32_t i = 0; i < cells; ++i) {
            const std::uint32_t a = j * stride + i;
            m.indices.insert(m.indices.end(), {a, a + stride, a + stride + 1, a, a + stride + 1, a + 1});
        }
    }
    return m;
}

scene::Scene grazingScene() {
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.showSkybox = false;
    s.environment.environmentIntensity = 0.0f;
    // Big, because the defect scales with the cascade's world texel size and that is the scene
    // radius over the atlas resolution. In a twenty-metre room a normal offset of a texel is a
    // centimetre and points nowhere in particular without consequence; over half a kilometre it is
    // most of a metre, and pointing it into the ground is what filled Glowmere with acne.
    s.camera.position = {0.0f, 6.0f, 300.0f};
    s.camera.target = {0.0f, 4.0f, -300.0f};
    s.camera.fovYRadians = 0.9f;
    s.camera.nearPlane = 0.5f;
    s.camera.farPlane = 2000.0f;

    const auto floor = s.addMesh(undulatingGround(1200.0f, 72, 2.5f));
    const auto box = s.addMesh(boxMesh({8.0f, 8.0f, 8.0f}));
    {
        auto& e = s.addEntity("floor", floor);
        e.material.baseColor = glm::vec3(0.8f);
        e.material.roughness = 0.9f;
    }
    {
        auto& e = s.addEntity("box", box);
        e.transform.position = {0.0f, 30.0f, -120.0f};
        e.material.baseColor = glm::vec3(0.8f);
        e.material.roughness = 0.9f;
    }
    scene::PunctualLight key;
    key.name = "key";
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(glm::vec3(0.35f, -0.4f, -0.6f)); // raking, like a low moon
    key.color = glm::vec3(1.0f);
    key.intensity = 4.0f;
    key.castsShadow = true;
    key.contactShadow = false; // the march is not masked; this test is about the map term
    key.softness = 0.4f;
    s.addLight(key);
    return s;
}

float meanLuminance(const gpu::Image8& image, std::uint32_t x0, std::uint32_t y0, std::uint32_t x1,
                    std::uint32_t y1) {
    double total = 0.0;
    std::uint32_t n = 0;
    for (std::uint32_t y = y0; y < y1; ++y) {
        for (std::uint32_t x = x0; x < x1; ++x) {
            total += static_cast<double>(luminanceAt(image, x, y));
            ++n;
        }
    }
    return n > 0 ? static_cast<float>(total / n) : 0.0f;
}

} // namespace

TEST_CASE("the shadow mask leaves open lit ground alone", "[gpu][shadows][mask]") {
    // The class of failure this guards: a mask that shadows ground nothing is standing on. It is
    // what the flipped reconstruction normal of ADR-087 did, over a whole valley, smoothly enough
    // to read as art rather than as a bug.
    //
    // **It does not reproduce that particular defect, and that was checked rather than assumed.**
    // Reintroducing the `normal.z < 0` orientation leaves this scene's numbers identical to six
    // figures. The degeneracy needs a surface whose facets sit within rounding of perpendicular to
    // the view *and* a cascade whose world texels are large enough for the resulting offset to
    // reach through the ground, and a scene small enough to assert a named pixel on does not have
    // both. What caught it was rendering Glowmere with the mask written straight to the screen and
    // diffing it against the same term at full resolution; that is the procedure to repeat, and
    // `--disable shadowmask` is the arm for it. Forcing the mask to zero *does* move this test's
    // number (0.73 -> 0.41), so the scene genuinely reads the mask and the assertion is not vacuous.
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    auto renderer = makeRenderer(*ctx, shaders);
    const scene::Scene s = grazingScene();

    rendering::SceneRenderer::PassToggles off;
    off.shadowMask = false;
    renderer->setPassToggles(off);
    auto unmasked = renderer->renderToImage(s, frameAt(4), kSize, kSize);
    REQUIRE(unmasked.has_value());

    renderer->setPassToggles(rendering::SceneRenderer::PassToggles{});
    auto masked = renderer->renderToImage(s, frameAt(4), kSize, kSize);
    REQUIRE(masked.has_value());
    REQUIRE(renderer->shadowMask().active());
    CHECK(renderer->shadowMask().stats().width * 2 <= kSize + 1); // it really is at half resolution

    // The bottom strip of the frame is floor running away from the camera, lit and unoccluded.
    const std::uint32_t y0 = kSize * 3 / 4;
    const float open = meanLuminance(*unmasked, 0, y0, kSize, kSize);
    const float openMasked = meanLuminance(*masked, 0, y0, kSize, kSize);
    INFO("open grazing floor: " << open << " unmasked -> " << openMasked << " masked");
    REQUIRE(open > 0.02f); // the control really is lit there
    CHECK(openMasked > open * 0.9f);
    CHECK(openMasked < open * 1.1f);
}

TEST_CASE("the shadow mask reproduces the shadow it replaces", "[gpu][shadows][mask]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    auto renderer = makeRenderer(*ctx, shaders);
    const scene::Scene lit = shadowScene(false);
    const scene::Scene shadowed = shadowScene(true);

    rendering::SceneRenderer::PassToggles off;
    off.shadowMask = false;
    renderer->setPassToggles(off);
    auto unmasked = renderer->renderToImage(shadowed, frameAt(5), kSize, kSize);
    REQUIRE(unmasked.has_value());
    auto control = renderer->renderToImage(lit, frameAt(5), kSize, kSize);
    REQUIRE(control.has_value());

    renderer->setPassToggles(rendering::SceneRenderer::PassToggles{});
    auto masked = renderer->renderToImage(shadowed, frameAt(5), kSize, kSize);
    REQUIRE(masked.has_value());

    const std::uint32_t cx = kSize / 2;
    const std::uint32_t cy = kSize / 2;
    const float open = luminanceAt(*control, cx, cy);
    const float dark = luminanceAt(*unmasked, cx, cy);
    const float darkMasked = luminanceAt(*masked, cx, cy);
    INFO("under the box: " << open << " unshadowed, " << dark << " unmasked, " << darkMasked << " masked");
    REQUIRE(dark < open * 0.75f);                       // there is a shadow to reproduce
    CHECK(std::abs(darkMasked - dark) < open * 0.15f);  // and the mask reproduces it
}

TEST_CASE("an offline render builds no shadow mask", "[gpu][shadows][mask]") {
    // Determinism: the offline tier computes the term per pixel, so an offline frame is the frame
    // this optimisation did not touch, whatever the preview did. Asserted here rather than trusted,
    // because the difference is a quality setting one edit away from being lost.
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    auto renderer = makeRenderer(*ctx, shaders);
    const scene::Scene s = shadowScene(true);

    renderer->setQuality(rendering::QualityTier::Offline);
    auto offline = renderer->renderToImage(s, frameAt(6), kSize, kSize);
    REQUIRE(offline.has_value());
    CHECK_FALSE(renderer->shadowMask().active());

    rendering::SceneRenderer::PassToggles off;
    off.shadowMask = false;
    renderer->setPassToggles(off);
    auto again = renderer->renderToImage(s, frameAt(6), kSize, kSize);
    REQUIRE(again.has_value());
    CHECK(gpu::hashImage(*offline) == gpu::hashImage(*again));

    renderer->setQuality(rendering::QualityTier::Realtime);
    renderer->setPassToggles(rendering::SceneRenderer::PassToggles{});
    auto realtime = renderer->renderToImage(s, frameAt(6), kSize, kSize);
    REQUIRE(realtime.has_value());
    CHECK(renderer->shadowMask().active());
}
