// The shadow mask and the lit pass have to agree about which normal a shadow term is biased along
// (ADR-111).
//
// The mask (shaders/shadow_mask.wgsl) runs before the scene pass, over the linear depth the prepass
// resolved, so the only normal it can have is one reconstructed from that depth: a *geometric*
// normal. The lit pass used to hand `shadowFactor` its *shading* normal -- the interpolated vertex
// normal after the material program's perturbation and the material's normal map. Those are not the
// same vector, the shadow term's normal-offset and slope-scaled bias are both computed from it, and
// so a normal-mapped surface got a different shadow depending on whether the mask was on.
//
// Every test here is a differential: the same scene rendered with `PassToggles::shadowMask` off (the
// full-resolution path, which is the reference by definition) and on, and the fraction of pixels
// that disagree. The fraction is never zero -- the mask is a half-resolution estimate and a
// bilateral upsample of one -- so each case asserts a ceiling that the pre-fix build exceeds, and
// each is paired with a control showing the measurement is not vacuous.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/render_quality.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

using namespace avgen;

namespace {

constexpr std::uint32_t kSize = 256;

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

FrameTime frameAt(std::uint64_t index) {
    FrameTime t;
    t.frameIndex = index;
    t.renderTime = static_cast<double>(index) / 60.0;
    t.deltaTime = 1.0 / 60.0;
    return t;
}

scene::MeshData boxMesh(glm::vec3 half, float uvScale = 1.0f) {
    scene::MeshData m;
    const glm::vec3 n[6] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    for (const glm::vec3 normal : n) {
        const glm::vec3 u =
            std::abs(normal.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::cross(glm::vec3(0, 1, 0), normal);
        const glm::vec3 v = glm::cross(normal, u);
        const glm::vec3 c = normal * half;
        const glm::vec3 du = u * half;
        const glm::vec3 dv = v * half;
        const auto base = static_cast<std::uint32_t>(m.vertices.size());
        m.vertices.push_back({c - du - dv, normal, {0, 0}});
        m.vertices.push_back({c + du - dv, normal, {uvScale, 0}});
        m.vertices.push_back({c + du + dv, normal, {uvScale, uvScale}});
        m.vertices.push_back({c - du + dv, normal, {0, uvScale}});
        m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return m;
}

// A tangent-space normal map of crossed sine ripples. Steep on purpose: the whole point is a
// shading normal that is a long way from the geometric one, so that biasing the shadow lookup along
// the wrong one of the two is measurable rather than theoretical.
scene::TextureData rippleNormalMap(std::uint32_t size, float periods, float amplitude) {
    scene::TextureData tex;
    tex.name = "ripple-normal";
    tex.width = size;
    tex.height = size;
    tex.format = scene::TextureFormat::Rgba8Unorm; // linear, as a normal map must be
    tex.data.resize(static_cast<std::size_t>(size) * size * 4);
    const float twoPi = 6.28318531f;
    for (std::uint32_t y = 0; y < size; ++y) {
        for (std::uint32_t x = 0; x < size; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(size);
            const float v = static_cast<float>(y) / static_cast<float>(size);
            // height = amplitude * sin(2*pi*p*u) * sin(2*pi*p*v); the normal is (-dh/du, -dh/dv, 1).
            const float dhdu = amplitude * twoPi * periods * std::cos(twoPi * periods * u) *
                               std::sin(twoPi * periods * v);
            const float dhdv = amplitude * twoPi * periods * std::sin(twoPi * periods * u) *
                               std::cos(twoPi * periods * v);
            glm::vec3 n = glm::normalize(glm::vec3(-dhdu, -dhdv, 1.0f));
            const std::size_t i = (static_cast<std::size_t>(y) * size + x) * 4;
            tex.data[i + 0] = static_cast<std::uint8_t>(std::lround((n.x * 0.5f + 0.5f) * 255.0f));
            tex.data[i + 1] = static_cast<std::uint8_t>(std::lround((n.y * 0.5f + 0.5f) * 255.0f));
            tex.data[i + 2] = static_cast<std::uint8_t>(std::lround((n.z * 0.5f + 0.5f) * 255.0f));
            tex.data[i + 3] = 255;
        }
    }
    return tex;
}

// The flat control for the same map: every texel is +Z, so the shading normal equals the geometric
// one and the only residual left is the mask's own half-resolution error. Without this arm a
// tightened ceiling could be met by a mask that had simply become blurrier.
scene::TextureData flatNormalMap(std::uint32_t size) {
    return rippleNormalMap(size, 1.0f, 0.0f);
}

// Deliberately assertion-free: this is called once per pixel by `differingFraction`, and a Catch2
// assertion in that loop costs more than the render it is measuring (and prints a line per pixel).
// The callers REQUIRE the images are the size they asked for; this only has to be safe.
float luminanceAt(const gpu::Image8& image, std::uint32_t x, std::uint32_t y) {
    const std::size_t index = (static_cast<std::size_t>(y) * image.width + x) * 4;
    if (index + 3 > image.rgba.size()) {
        return 0.0f;
    }
    return (0.2126f * static_cast<float>(image.rgba[index]) +
            0.7152f * static_cast<float>(image.rgba[index + 1]) +
            0.0722f * static_cast<float>(image.rgba[index + 2])) /
           255.0f;
}

// The fraction of pixels whose luminance differs by more than `tolerance` (in 0..1 luminance). Two
// counts over 8-bit output, so the quantisation floor is 1/255: the tolerance is deliberately a
// little above it.
float differingFraction(const gpu::Image8& a, const gpu::Image8& b, float tolerance = 0.008f) {
    REQUIRE(a.width == b.width);
    REQUIRE(a.height == b.height);
    std::uint64_t differing = 0;
    for (std::uint32_t y = 0; y < a.height; ++y) {
        for (std::uint32_t x = 0; x < a.width; ++x) {
            if (std::abs(luminanceAt(a, x, y) - luminanceAt(b, x, y)) > tolerance) {
                ++differing;
            }
        }
    }
    return static_cast<float>(differing) /
           static_cast<float>(static_cast<std::uint64_t>(a.width) * a.height);
}

struct MaskResidual {
    float fraction = 0.0f;
    gpu::Image8 reference; // mask off: the full-resolution path
    gpu::Image8 masked;
};

// Renders the scene both ways and reports how far apart they are. The reference arm is rendered
// first and the toggles restored afterwards, so a caller may reuse the renderer.
//
// **Every caller binds its scenes to named locals that outlive the whole comparison, and that is
// load-bearing.** SceneRenderer caches its uploaded meshes and textures against the *address* of
// the scene it uploaded them from. Pass a temporary and the next temporary can land on the same
// stack address, at which point the renderer decides its cache is still valid and draws the second
// scene with the first scene's textures. That is not hypothetical: written with temporaries, the
// bumpy and the flat arms of these tests rendered byte-identical frames and every ceiling below
// passed for a build in which nothing had been fixed.
MaskResidual maskResidual(rendering::SceneRenderer& renderer, const scene::Scene& s, std::uint64_t frame,
                          float tolerance = 0.008f) {
    rendering::SceneRenderer::PassToggles off;
    off.shadowMask = false;
    renderer.setPassToggles(off);
    auto reference = renderer.renderToImage(s, frameAt(frame), kSize, kSize);
    REQUIRE(reference.has_value());

    renderer.setPassToggles(rendering::SceneRenderer::PassToggles{});
    auto masked = renderer.renderToImage(s, frameAt(frame), kSize, kSize);
    REQUIRE(masked.has_value());
    REQUIRE(renderer.shadowMask().active());

    MaskResidual out;
    out.fraction = differingFraction(*reference, *masked, tolerance);
    out.reference = std::move(*reference);
    out.masked = std::move(*masked);
    return out;
}

struct BumpySceneDesc {
    bool bumpy = true;               // false: the flat-normal-map control
    bool contactShadows = false;     // the contact march, which is never masked
    glm::vec3 lightDirection{0.35f, -0.75f, -0.55f};
    glm::vec3 cameraPosition{0.0f, 9.0f, 16.0f};
    float cameraFar = 900.0f;
    float normalScale = 1.0f;
    bool boxRestsOnFloor = false;    // the contact case: a small box sitting on the floor
    bool castsShadow = true;         // false: no map term, so the contact march is the only shadow
    // How wide the world is, which is what sets the cascade texel size -- and the cascade texel is
    // the scale of the normal offset, so it is the knob that decides whether a disagreement about
    // the normal is visible at all. A 30 m floor makes one too small to measure; 400 m is a
    // landscape, and is the size at which the pre-ADR-111 build's error shows.
    float floorHalfExtent = 400.0f;
};

// A wide normal-mapped floor, a box floating above it, one shadowing directional light. The floor
// runs away from the camera so that a good part of the frame is seen at a grazing angle, which is
// where the normal-offset bias is largest and a disagreement about the normal shows up first.
scene::Scene bumpyScene(const BumpySceneDesc& desc) {
    scene::Scene s;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.environment.showSkybox = false;
    s.environment.environmentIntensity = 0.0f;
    s.camera.position = desc.cameraPosition;
    s.camera.target = {0.0f, 1.0f, -6.0f};
    s.camera.fovYRadians = 0.9f;
    s.camera.nearPlane = 0.5f;
    s.camera.farPlane = desc.cameraFar;

    const auto normalMap = s.addTexture(desc.bumpy ? rippleNormalMap(128, 6.0f, 0.06f) : flatNormalMap(128));
    const auto floor = s.addMesh(boxMesh({desc.floorHalfExtent, 0.25f, desc.floorHalfExtent},
                                         desc.floorHalfExtent * 0.27f));
    const auto box = s.addMesh(boxMesh({2.0f, 2.0f, 2.0f}));
    {
        auto& e = s.addEntity("floor", floor);
        e.transform.position = {0.0f, -0.25f, 0.0f};
        e.material.baseColor = glm::vec3(0.8f);
        e.material.roughness = 0.9f;
        e.material.metallic = 0.0f;
        e.material.normalScale = desc.normalScale;
        e.material.normalTexture.texture = normalMap;
    }
    {
        auto& e = s.addEntity("box", box);
        e.transform.position = desc.boxRestsOnFloor ? glm::vec3(0.0f, 2.0f, -2.0f)
                                                    : glm::vec3(0.0f, 4.0f, -2.0f);
        e.material.baseColor = glm::vec3(0.8f);
        e.material.roughness = 0.9f;
    }
    scene::PunctualLight key;
    key.name = "key";
    key.type = scene::PunctualLight::Type::Directional;
    key.direction = glm::normalize(desc.lightDirection);
    key.color = glm::vec3(1.0f);
    key.intensity = 4.0f;
    key.castsShadow = desc.castsShadow;
    key.contactShadow = desc.contactShadows;
    key.softness = 0.4f;
    s.addLight(key);
    return s;
}

} // namespace

// ---- the measurement itself ---------------------------------------------------------------------

TEST_CASE("the mask agrees with the full-resolution path on a normal-mapped surface",
          "[gpu][shadows][mask][normals]") {
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    auto renderer = makeRenderer(*ctx, shaders);

    BumpySceneDesc bumpyDesc;
    bumpyDesc.lightDirection = glm::vec3(0.5f, -0.22f, -0.84f); // ~13 degrees above the floor
    BumpySceneDesc flatDesc = bumpyDesc;
    flatDesc.bumpy = false;
    const scene::Scene bumpyScene_ = bumpyScene(bumpyDesc);
    const scene::Scene flatScene_ = bumpyScene(flatDesc);
    const MaskResidual bumpy = maskResidual(*renderer, bumpyScene_, 11);
    const MaskResidual flat = maskResidual(*renderer, flatScene_, 11);

    INFO("normal-mapped residual " << bumpy.fraction * 100.0f << "%, flat control "
                                   << flat.fraction * 100.0f << "%");

    // The control: with a flat normal map the shading normal *is* the geometric normal, so whatever
    // residual remains is the mask's own half-resolution error and nothing to do with normals. This
    // is the arm that stops the ceiling below being met by a mask that merely got blurrier -- it
    // measured 1.04% before the fix and 1.04% after, to three figures.
    CHECK(flat.fraction < 0.02f);

    // The claim: a normal-mapped surface disagrees with the reference *less* than a flat one, not
    // more. Measured on this scene: 0.94% before ADR-111, 0.71% after, against a flat control of
    // 1.04% that does not move. The 0.85 leaves room either side of the post-fix number while
    // staying under the pre-fix one.
    CHECK(bumpy.fraction < flat.fraction * 0.85f);
}

TEST_CASE("the normal-map residual measurement is not vacuous", "[gpu][shadows][mask][normals]") {
    // The negative control for the test above: if the ripple normal map made no difference to the
    // frame at all, that test would pass for a build in which nothing was fixed. So render the same
    // scene bumpy and flat through the *same* path and require that they are far apart.
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    auto renderer = makeRenderer(*ctx, shaders);

    rendering::SceneRenderer::PassToggles off;
    off.shadowMask = false;
    renderer->setPassToggles(off);

    BumpySceneDesc bumpyDesc;
    BumpySceneDesc flatDesc;
    flatDesc.bumpy = false;
    const scene::Scene bumpyScene_ = bumpyScene(bumpyDesc);
    const scene::Scene flatScene_ = bumpyScene(flatDesc);
    auto bumpy = renderer->renderToImage(bumpyScene_, frameAt(11), kSize, kSize);
    REQUIRE(bumpy.has_value());
    auto flat = renderer->renderToImage(flatScene_, frameAt(11), kSize, kSize);
    REQUIRE(flat.has_value());

    const float difference = differingFraction(*bumpy, *flat);
    INFO("bumpy vs flat, both at full resolution: " << difference * 100.0f << "% of pixels");
    CHECK(difference > 0.15f); // the normal map really does change the image
}

TEST_CASE("the mask agrees at a grazing light angle", "[gpu][shadows][mask][normals]") {
    // The normal offset is scaled by (1 + 2 * (1 - nDotL)) and the constant bias by tan(acos(nDotL)),
    // so both grow as the light rakes the surface: this is the angle at which a disagreement about
    // the normal is amplified most.
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    auto renderer = makeRenderer(*ctx, shaders);

    BumpySceneDesc grazing;
    grazing.lightDirection = glm::vec3(0.55f, -0.12f, -0.82f); // ~8 degrees above the floor
    BumpySceneDesc grazingFlat = grazing;
    grazingFlat.bumpy = false;
    const scene::Scene bumpyScene_ = bumpyScene(grazing);
    const scene::Scene flatScene_ = bumpyScene(grazingFlat);
    const MaskResidual bumpy = maskResidual(*renderer, bumpyScene_, 12);
    const MaskResidual flat = maskResidual(*renderer, flatScene_, 12);

    INFO("grazing: normal-mapped " << bumpy.fraction * 100.0f << "%, flat " << flat.fraction * 100.0f
                                   << "%");
    // Measured: 2.15% normal-mapped before ADR-111, 0.91% after; the flat control sits at 1.33%
    // either way. This is the sharpest of the five cases, which is what the geometry predicts --
    // the normal offset is scaled up hardest exactly here.
    CHECK(flat.fraction < 0.02f);
    CHECK(bumpy.fraction < flat.fraction);
    CHECK(bumpy.fraction < 0.012f);
}

TEST_CASE("the contact march survives the normal the shadow term is biased along",
          "[gpu][shadows][mask][normals]") {
    // The contact march is never masked -- it stays at full resolution in the lit pass -- but it is
    // offset along the same normal, and it marches through the *depth buffer*, which holds the
    // geometric surface. A march started along a normal-mapped normal starts somewhere that surface
    // is not, so the depth buffer immediately reads as an occluder in front of it: the march
    // manufactures shadow out of the normal map.
    //
    // The map term is switched off here (`castsShadow = false`) so the march is the only shadow in
    // the frame, which is the same isolation the engine's own contact test uses. The march is gated
    // by the quality settings rather than by the light, so that is the arm.
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);

    BumpySceneDesc desc;
    desc.contactShadows = true;
    desc.castsShadow = false;
    desc.boxRestsOnFloor = true;
    desc.lightDirection = glm::vec3(0.25f, -0.62f, 0.4f); // rakes across the box's base
    BumpySceneDesc flatDesc = desc;
    flatDesc.bumpy = false;
    const scene::Scene bumpyScene_ = bumpyScene(desc);
    const scene::Scene flatScene_ = bumpyScene(flatDesc);

    rendering::QualitySettings quality =
        rendering::QualitySettings::forTier(rendering::QualityTier::Realtime);
    quality.ambientOcclusion = false; // keep the half-resolution occlusion term out of this

    auto measure = [&](const scene::Scene& s, std::uint64_t frame) {
        quality.contactShadows = false;
        auto off = makeRenderer(*ctx, shaders);
        off->setQualitySettings(quality);
        auto without = off->renderToImage(s, frameAt(frame), kSize, kSize);
        REQUIRE(without.has_value());
        quality.contactShadows = true;
        auto on = makeRenderer(*ctx, shaders);
        on->setQualitySettings(quality);
        auto with = on->renderToImage(s, frameAt(frame), kSize, kSize);
        REQUIRE(with.has_value());
        return differingFraction(*with, *without);
    };

    const float flatEffect = measure(flatScene_, 13);
    const float bumpyEffect = measure(bumpyScene_, 13);
    INFO("contact march moves " << flatEffect * 100.0f << "% of pixels on a flat floor, "
                                << bumpyEffect * 100.0f << "% on the same floor normal-mapped");

    // The march still works: it is not the geometric normal that turned it off. The flat-floor
    // number is 0.148% before ADR-111 and 0.148% after -- identical to six figures, which is the
    // strongest form this control can take: the change provably does nothing where there is no
    // normal map to get wrong.
    CHECK(flatEffect > 0.001f);
    // And the march no longer depends on the normal map. Before the fix the normal-mapped floor got
    // 3.34% -- more than twenty times the identical flat floor -- none of it cast by anything: the
    // march was starting off the geometric surface and reading that surface as its own occluder.
    // After, 0.085%.
    CHECK(bumpyEffect < flatEffect * 1.5f);
}

TEST_CASE("the mask agrees where the contact march also runs", "[gpu][shadows][mask][normals]") {
    // The same scene with the map term back on, so both shadow terms are live at once: the masked
    // map term at half resolution and the unmasked march at full. The residual is the mask's.
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    auto renderer = makeRenderer(*ctx, shaders);

    BumpySceneDesc desc;
    desc.contactShadows = true;
    desc.boxRestsOnFloor = true;
    desc.lightDirection = glm::vec3(0.25f, -0.62f, 0.4f);
    const scene::Scene s = bumpyScene(desc);

    rendering::QualitySettings quality =
        rendering::QualitySettings::forTier(rendering::QualityTier::Realtime);
    quality.ambientOcclusion = false;
    quality.contactShadows = true;
    renderer->setQualitySettings(quality);

    const MaskResidual residual = maskResidual(*renderer, s, 13);
    INFO("with the contact march on, mask residual " << residual.fraction * 100.0f << "%");
    // An envelope, not a discriminator: 0.48% before ADR-111 and 0.47% after. The march is not
    // masked, so the mask's residual barely moves here; what this pins is that running both terms
    // at once does not multiply their disagreement.
    CHECK(residual.fraction < 0.01f);
}

TEST_CASE("the mask agrees across a cascade transition", "[gpu][shadows][mask][normals]") {
    // Two cascades meet somewhere down the floor, and they differ in texel size, in normal offset
    // (derived from that texel size) and in constant bias. `shadowFactor` crossfades the band, and
    // the crossfade is evaluated with whatever normal it was handed -- so the band is where a
    // normal disagreement turns into a visible seam rather than a scatter of pixels. A long far
    // plane puts the splits on screen.
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    auto renderer = makeRenderer(*ctx, shaders);

    BumpySceneDesc desc;
    desc.cameraPosition = glm::vec3(0.0f, 25.0f, 60.0f); // a long run of floor, near to far
    // Raking, like the grazing case. The normal offset is the largest of the three things that
    // differ between two cascades, and it is largest when the light is low, so this is the light
    // that puts a normal disagreement *inside* the crossfade band rather than beside it.
    desc.lightDirection = glm::vec3(0.55f, -0.12f, -0.82f);
    BumpySceneDesc flatDesc = desc;
    flatDesc.bumpy = false;
    const scene::Scene bumpyScene_ = bumpyScene(desc);
    const scene::Scene flatScene_ = bumpyScene(flatDesc);
    const MaskResidual bumpy = maskResidual(*renderer, bumpyScene_, 14);
    const MaskResidual flat = maskResidual(*renderer, flatScene_, 14);

    INFO("cascade transition: normal-mapped " << bumpy.fraction * 100.0f << "%, flat "
                                              << flat.fraction * 100.0f << "%");
    CHECK(renderer->shadows().stats().cascades >= 2); // there really are cascades to cross
    // Measured: normal-mapped 1.29% before ADR-111 and 0.94% after, against a flat control of 1.18%
    // that does not move. Before the fix the normal-mapped floor was the *worse* of the two; after,
    // it is the better, which is what the ordering below asserts.
    CHECK(flat.fraction < 0.02f);
    CHECK(bumpy.fraction < flat.fraction);
}

TEST_CASE("the mask agrees while the camera moves", "[gpu][shadows][mask][normals]") {
    // A still frame can hide a bias error that a moving one cannot: the cascade snaps to a different
    // texel each frame, so the normal offset lands on a different side of a shadow texel edge, and a
    // term biased along the wrong normal flickers. Three positions along a dolly, each measured
    // against its own full-resolution reference.
    auto ctx = makeContext();
    auto shaders = makeShaders(*ctx);
    auto renderer = makeRenderer(*ctx, shaders);

    float worst = 0.0f;
    for (int step = 0; step < 3; ++step) {
        BumpySceneDesc desc;
        // Deliberately not a whole number of texels: the point is to land between snaps.
        desc.lightDirection = glm::vec3(0.55f, -0.12f, -0.82f);
        desc.cameraPosition = glm::vec3(0.0f, 9.0f, 16.0f - 1.37f * static_cast<float>(step));
        const scene::Scene dollied = bumpyScene(desc);
        const MaskResidual r = maskResidual(*renderer, dollied, 20 + static_cast<std::uint64_t>(step));
        INFO("dolly step " << step << ": " << r.fraction * 100.0f << "%");
        worst = std::max(worst, r.fraction);
    }
    INFO("worst residual over the dolly: " << worst * 100.0f << "%");
    // Measured worst-of-three: 2.28% before ADR-111, 0.97% after.
    CHECK(worst < 0.015f);
}
