#include "rendering/shadow_renderer.hpp"

#include "rendering/light_data.hpp"

#include "core/log.hpp"
#include "gpu/context.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstring>
#include <array>
#include <cmath>

namespace avgen::rendering {

// ---- GPU ---------------------------------------------------------------------------------------

struct ShadowRenderer::Impl {
    explicit Impl(gpu::Context& c) : context(c) {}

    Result<void> ensureAtlas(std::uint32_t resolution, std::uint32_t layers);

    gpu::Context& context;
    wgpu::Texture atlas;
    wgpu::TextureView atlasView;
    std::vector<wgpu::TextureView> layerViews;
    wgpu::Texture dummyAtlas;
    wgpu::TextureView dummyAtlasView;
    wgpu::Sampler comparison;
    wgpu::Buffer uniforms;
    std::array<wgpu::Buffer, kMaxShadowViews> viewUniforms;
    std::uint64_t frameUniformSize = 0;
    std::uint32_t resolution = 0;
    std::uint32_t layers = 0;
    ShadowUniforms block{};
    bool initialised = false;
};

ShadowRenderer::ShadowRenderer(gpu::Context& context) : impl_(std::make_unique<Impl>(context)) {}
ShadowRenderer::~ShadowRenderer() = default;

Result<void> ShadowRenderer::Impl::ensureAtlas(std::uint32_t wantResolution, std::uint32_t wantLayers) {
    const std::uint32_t want = std::max(wantLayers, 1u);
    if (atlas != nullptr && resolution == wantResolution && layers >= want) {
        return {};
    }
    const auto& device = context.device();
    wgpu::TextureDescriptor desc{};
    desc.label = "shadow-atlas";
    desc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
    desc.dimension = wgpu::TextureDimension::e2D;
    desc.size = {wantResolution, wantResolution, std::max(want, kMaxShadowViews)};
    desc.format = ShadowRenderer::kFormat;
    device.PushErrorScope(wgpu::ErrorFilter::Validation);
    wgpu::Texture created = device.CreateTexture(&desc);
    std::string error;
    auto future = device.PopErrorScope(
        wgpu::CallbackMode::WaitAnyOnly, [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
            if (type != wgpu::ErrorType::NoError) {
                error = gpu::Context::toString(msg);
            }
        });
    context.waitFor(future);
    if (!error.empty() || !created) {
        return fail("shadow atlas {}x{}x{}: {}", wantResolution, wantResolution, want, error);
    }
    atlas = std::move(created);
    resolution = wantResolution;
    layers = std::max(want, kMaxShadowViews);
    {
        wgpu::TextureViewDescriptor view{};
        view.label = "shadow-atlas-view";
        view.dimension = wgpu::TextureViewDimension::e2DArray;
        view.arrayLayerCount = layers;
        atlasView = atlas.CreateView(&view);
    }
    layerViews.clear();
    for (std::uint32_t i = 0; i < layers; ++i) {
        wgpu::TextureViewDescriptor view{};
        view.label = "shadow-atlas-layer";
        view.dimension = wgpu::TextureViewDimension::e2D;
        view.baseArrayLayer = i;
        view.arrayLayerCount = 1;
        layerViews.push_back(atlas.CreateView(&view));
    }
    return {};
}

Result<void> ShadowRenderer::init(std::uint64_t frameUniformSize) {
    Impl& im = *impl_;
    const auto& device = im.context.device();
    im.frameUniformSize = frameUniformSize;
    {
        wgpu::BufferDescriptor desc{};
        desc.label = "shadow-uniforms";
        desc.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
        desc.size = sizeof(ShadowUniforms);
        im.uniforms = device.CreateBuffer(&desc);
        desc.label = "shadow-view-frame-uniforms";
        desc.size = frameUniformSize;
        for (auto& buffer : im.viewUniforms) {
            buffer = device.CreateBuffer(&desc);
        }
    }
    {
        wgpu::SamplerDescriptor desc{};
        desc.label = "shadow-comparison-sampler";
        desc.addressModeU = wgpu::AddressMode::ClampToEdge;
        desc.addressModeV = wgpu::AddressMode::ClampToEdge;
        desc.addressModeW = wgpu::AddressMode::ClampToEdge;
        desc.magFilter = wgpu::FilterMode::Linear;
        desc.minFilter = wgpu::FilterMode::Linear;
        desc.compare = wgpu::CompareFunction::LessEqual;
        im.comparison = device.CreateSampler(&desc);
    }
    {
        // Bound in place of the atlas by the depth-only passes, which render into it.
        wgpu::TextureDescriptor desc{};
        desc.label = "shadow-atlas-placeholder";
        desc.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::RenderAttachment;
        desc.dimension = wgpu::TextureDimension::e2D;
        desc.size = {1, 1, kMaxShadowViews};
        desc.format = kFormat;
        im.dummyAtlas = device.CreateTexture(&desc);
        wgpu::TextureViewDescriptor view{};
        view.dimension = wgpu::TextureViewDimension::e2DArray;
        view.arrayLayerCount = kMaxShadowViews;
        im.dummyAtlasView = im.dummyAtlas.CreateView(&view);
    }
    if (auto r = im.ensureAtlas(1024, kMaxShadowViews); !r) {
        return r;
    }
    im.initialised = true;
    return {};
}

std::uint32_t ShadowRenderer::update(const std::vector<const scene::PunctualLight*>& lights,
                                     const glm::mat4& viewProj, float cameraNear, float cameraFar,
                                     float sceneRadius, const QualitySettings& quality,
                                     float rangeOverride) {
    Impl& im = *impl_;
    views_.clear();
    stats_ = ShadowStats{};
    if (!im.initialised) {
        return 0;
    }
    if (auto r = im.ensureAtlas(quality.shadowResolution, kMaxShadowViews); !r) {
        log::warn("shadows disabled this frame: {}", r.error().message);
        return 0;
    }
    stats_.resolution = im.resolution;

    // The shadowed depth range (ADR-112): the world's size, clamped to the camera's, and then
    // shortened until the coarsest cascade's texel is small enough to resolve something. The
    // arithmetic and the reasoning live in shadow_math so they can be checked without a device.
    // `kShadowRangeReference`, deliberately, and not `im.resolution`: how far the shadows reach is
    // the same in a preview and in the final render, and only how sharp they are differs.
    float shadowFar = directionalShadowRange(cameraNear, cameraFar, sceneRadius,
                                             kShadowRangeReference, quality.shadowTexelTarget);
    // The scene's own range wins when it has one. It is clamped to the camera rather than trusted:
    // a range shorter than a few near planes fits cascades to a sliver and one past the far plane
    // fits them to a frustum the camera does not have, and both read as "the shadows vanished".
    if (rangeOverride > 0.0f) {
        const float near = std::max(cameraNear, 1e-3f);
        shadowFar = std::clamp(rangeOverride, near * 4.0f, std::max(cameraFar, near * 4.0f));
    }
    stats_.range = shadowFar;
    const glm::mat4 invViewProj = glm::inverse(viewProj);
    const std::uint32_t cascades = std::clamp(quality.cascadeCount, 1u, kMaxCascades);
    const std::vector<float> splits = cascadeSplits(cameraNear, shadowFar, cascades);
    // §15's "camera position", and it is taken from the matrix the fit was made from rather than
    // from the scene's camera. Those are the same thing until a frozen view or a debug camera makes
    // them different, and the number worth reporting is the one the cascades were fitted to.
    //
    // Recorded because the first version was wrong in a way that read as right: unprojecting
    // NDC (0, 0, 0) gives the centre of the NEAR PLANE, not the eye, and on a 0.5 m near plane
    // those differ by half a metre -- small enough to look like the camera and large enough to be
    // the wrong answer. The eye is that point walked back along the view axis by exactly the near
    // distance, which is a number this function is already given.
    {
        const glm::vec4 nearH = invViewProj * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        const glm::vec4 farH = invViewProj * glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
        if (std::abs(nearH.w) > 1e-8f && std::abs(farH.w) > 1e-8f) {
            const glm::vec3 nearCentre = glm::vec3(nearH) / nearH.w;
            const glm::vec3 farCentre = glm::vec3(farH) / farH.w;
            const glm::vec3 axis = farCentre - nearCentre;
            const float length = glm::length(axis);
            stats_.cameraPosition =
                length > 1e-6f ? nearCentre - axis * (cameraNear / length) : nearCentre;
        }
    }
    for (std::uint32_t c = 0; c < kMaxCascades; ++c) {
        stats_.splits[c] = c < splits.size() ? splits[c] : shadowFar;
    }
    // shaders/shadows.wgsl: SHADOW_RANGE_FADE of the last split, eased. Published because "where do
    // the shadows stop" has two answers -- where the map ends and where the fade begins -- and the
    // second is the one a person sees.
    stats_.fadeStart = (splits.empty() ? shadowFar : splits.back()) * (1.0f - 0.18f);

    bool directionalDone = false;
    for (std::uint32_t i = 0; i < lights.size(); ++i) {
        const scene::PunctualLight& light = *lights[i];
        if (!light.enabled || !light.castsShadow || light.shadowStrength <= 0.0f) {
            continue;
        }
        if (light.type == scene::PunctualLight::Type::Directional) {
            if (directionalDone || views_.size() + cascades > kMaxShadowViews) {
                // Said out loud, once per light per frame-of-first-occurrence, because the
                // alternative is a light whose `castsShadow` is ticked and which casts nothing,
                // with nothing anywhere saying why. That is exactly ADR-358's defect -- a
                // shadow-casting key that cast nothing -- and it was diagnosable only by reading an
                // AOV and recognising a constant.
                //
                // Reachable now in a way it was not: a scene file could author a second
                // shadow-casting sun, but only a person editing JSON would. A Lights panel with a
                // "Cast Shadows" checkbox makes a sun-and-moon rig the obvious thing to build.
                //
                // A warning rather than a larger budget, deliberately. `kMaxShadowViews` is 8 and
                // raising it is a per-frame cost paid by every scene in the repository to serve a
                // case nobody has hit yet; what the user needs is to be told, not to be charged.
                // Revisit when somebody measures what the ninth view costs.
                if (shadowWarned_.insert(light.name).second) {
                    log::warn("shadows: light '{}' casts no shadow -- this frame already has a "
                              "cascaded directional light and the renderer fits only one. Turn off "
                              "'Cast Shadows' on the other directional light, or accept that this "
                              "one lights without shadowing.",
                              light.name);
                }
                continue; // one cascaded directional light per frame
            }
            float nearDepth = cameraNear;
            for (std::uint32_t c = 0; c < cascades; ++c) {
                // The sub-frustum corners come from the camera's own near/far (the matrix the
                // inverse was built from); `splits` only says how deep this cascade reaches.
                ShadowView view = fitDirectionalCascade(invViewProj, cameraNear, cameraFar, nearDepth,
                                                        splits[c], light.direction, im.resolution,
                                                        std::max(sceneRadius, 1.0f));
                view.lightIndex = static_cast<int>(i);
                // Nothing else stands between a cascade fit and the GPU. The renderer guards the
                // camera and the entity matrices; a shadow view is built from the camera's *inverse*
                // view-projection and a scene radius, and either can arrive non-finite from a scene
                // whose bounds went wrong. What reaches the shader then is a projection that maps
                // everything to NaN -- a shadow map of nothing, or of everything, with no error
                // anywhere.
                const bool finiteView = [&] {
                    for (int col = 0; col < 4; ++col) {
                        for (int row = 0; row < 4; ++row) {
                            if (!std::isfinite(view.viewProj[col][row])) {
                                return false;
                            }
                        }
                    }
                    return true;
                }();
                if (!finiteView) {
                    log::warn("shadows: cascade {} of light '{}' fitted a non-finite view "
                              "(scene radius {}, split {}); it is not rendered",
                              c, light.name, sceneRadius, splits[c]);
                    nearDepth = splits[c];
                    continue;
                }
                stats_.coarsestTexel = std::max(stats_.coarsestTexel, view.texelWorldSize);
                views_.push_back(view);
                nearDepth = splits[c];
            }
            directionalDone = true;
            stats_.cascades += cascades;
            stats_.lightDirection = light.direction;
        } else if (light.type == scene::PunctualLight::Type::Spot) {
            if (views_.size() >= kMaxShadowViews) {
                continue;
            }
            const float range = light.range > 0.0f ? light.range : std::max(sceneRadius * 2.0f, 1.0f);
            ShadowView view = fitSpotShadow(light, im.resolution, range);
            view.lightIndex = static_cast<int>(i);
            views_.push_back(view);
            ++stats_.spots;
        } else if (views_.size() + kPointShadowFaces <= kMaxShadowViews) {
            // Point, and every area shape: six faces around the light. A Rect or a Disk only emits
            // into a hemisphere, so three of its six faces are wasted -- but a single perspective
            // map cannot cover 180 degrees, and a cube that is right for every shape is worth more
            // than a special case per shape that is nearly right.
            //
            // Expensive by construction: six depth passes, which is why it is opt-in through the
            // light's own `castsShadow` and why a light that does not fit the remaining budget goes
            // without rather than getting a partial cube.
            const float range = light.range > 0.0f ? light.range
                                                   : std::max(lightInfluenceRadius(light), 1.0f);
            for (ShadowView view : fitPointShadow(light, im.resolution, range)) {
                view.lightIndex = static_cast<int>(i);
                views_.push_back(view);
            }
            ++stats_.points;
        }
    }
    stats_.views = static_cast<std::uint32_t>(views_.size());

    im.block = ShadowUniforms{};
    for (std::size_t v = 0; v < views_.size(); ++v) {
        im.block.views[v].viewProj = views_[v].viewProj;
        // The bias, and the report, from one place. `reportView` owns the arithmetic and the
        // paragraph explaining it; this loop writes the uniform from what it returns, so the bias a
        // diagnostic prints cannot drift from the bias the shader subtracts (§37).
        const ShadowViewReport report = reportView(views_[v], static_cast<std::uint32_t>(v));
        stats_.view[v] = report;
        im.block.views[v].params = glm::vec4(views_[v].texelWorldSize, report.depthBias,
                                             views_[v].cascade ? 1.0f : 0.0f, views_[v].farDistance);
    }
    im.block.info = glm::vec4(static_cast<float>(im.resolution), static_cast<float>(cascades),
                              static_cast<float>(quality.shadowPcfTaps), quality.softShadows ? 1.0f : 0.0f);
    // ADR-227: the blocker search's own budget. Clamped here rather than in the shader so the
    // number the tier asked for is the number the frame used, and 0 is not a legal request -- the
    // shader reads 0 as "the lane was never written" and falls back to the filter's count.
    im.block.info2 = glm::vec4(static_cast<float>(std::clamp(quality.pcssBlockerTaps, 1u, 32u)), 0.0f,
                               0.0f, 0.0f);
    im.block.splits = glm::vec4(splits.empty() ? shadowFar : splits[0],
                                splits.size() > 1 ? splits[1] : shadowFar,
                                splits.size() > 2 ? splits[2] : shadowFar,
                                splits.size() > 3 ? splits[3] : shadowFar);
    return stats_.views;
}

void ShadowRenderer::upload(const void* frameUniforms, std::uint64_t frameUniformSize) {
    Impl& im = *impl_;
    if (!im.initialised) {
        return;
    }
    const auto& queue = im.context.queue();
    queue.WriteBuffer(im.uniforms, 0, &im.block, sizeof(im.block));
    if (frameUniforms == nullptr || frameUniformSize != im.frameUniformSize) {
        return;
    }
    // Each view gets the frame block with its own view-projection: the depth-only passes then run
    // the ordinary vertex shaders (entities, procedural instances, meshed SDFs) with no changes.
    std::vector<std::uint8_t> copy(static_cast<std::size_t>(frameUniformSize));
    for (std::size_t v = 0; v < views_.size() && v < kMaxShadowViews; ++v) {
        std::memcpy(copy.data(), frameUniforms, copy.size());
        std::memcpy(copy.data(), &views_[v].viewProj, sizeof(glm::mat4));
        const glm::mat4 inverse = glm::inverse(views_[v].viewProj);
        std::memcpy(copy.data() + sizeof(glm::mat4), &inverse, sizeof(glm::mat4));
        // ...and the billboard basis, which the block used to carry straight through from the
        // camera. The LOD ladder's rungs 2 and 3 are camera-facing quads built in the vertex shader
        // from `frame.cameraRight` / `frame.cameraUp`; leaving the camera's there meant an impostor
        // was oriented for the viewer and then rasterised from the light, so it presented its edge
        // to the sun whenever the sun was at right angles to the camera -- and a stationary tree
        // under a stationary sun cast a shadow whose size depended on where the camera stood.
        // Measured on the lab fixture's plan view before this line existed: the impostor row
        // darkened 0.4% of the ground its shadow falls on where the identical mesh row darkened
        // 28.3%.
        //
        // Only these two lanes are replaced, and that is deliberate. `cameraPos` stays the camera's
        // because `deformWorld` and the wind take a to-camera vector from it, and a caster deformed
        // differently in the shadow pass than in the camera pass is a shadow that does not line up
        // with its object. Nothing else in a depth-only pass reads either axis: the only other
        // readers of `frame.cameraRight` are the debug point sprites, GTAO and the shadow-mask
        // pass, and none of the three runs here.
        const glm::vec4 right(views_[v].right, 0.0f);
        const glm::vec4 up(views_[v].up, 0.0f);
        std::memcpy(copy.data() + kFrameCameraRightOffset, &right, sizeof(glm::vec4));
        std::memcpy(copy.data() + kFrameCameraUpOffset, &up, sizeof(glm::vec4));
        queue.WriteBuffer(im.viewUniforms[v], 0, copy.data(), copy.size());
    }
}

int ShadowRenderer::viewForLight(std::uint32_t lightIndex, bool& cascaded, bool& cube) const {
    cascaded = false;
    cube = false;
    for (std::size_t v = 0; v < views_.size(); ++v) {
        if (views_[v].lightIndex == static_cast<int>(lightIndex)) {
            cascaded = views_[v].cascade;
            cube = views_[v].cube;
            return static_cast<int>(v); // the first of its run: a cascade's, or a cube's +X face
        }
    }
    return -1;
}

const wgpu::Buffer& ShadowRenderer::viewUniforms(std::uint32_t view) const {
    return impl_->viewUniforms[std::min<std::uint32_t>(view, kMaxShadowViews - 1)];
}

const wgpu::TextureView& ShadowRenderer::layerView(std::uint32_t view) const {
    return impl_->layerViews[std::min<std::size_t>(view, impl_->layerViews.size() - 1)];
}

const wgpu::TextureView& ShadowRenderer::atlasView() const {
    return impl_->atlasView;
}

const wgpu::TextureView& ShadowRenderer::dummyAtlasView() const {
    return impl_->dummyAtlasView;
}

const wgpu::Buffer& ShadowRenderer::uniforms() const {
    return impl_->uniforms;
}

const wgpu::Sampler& ShadowRenderer::comparisonSampler() const {
    return impl_->comparison;
}

std::uint32_t ShadowRenderer::resolution() const {
    return impl_->resolution;
}

bool ShadowRenderer::ready() const {
    return impl_->initialised;
}

} // namespace avgen::rendering
