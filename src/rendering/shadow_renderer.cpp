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
                                     float sceneRadius, const QualitySettings& quality) {
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

    // The shadowed depth range: cascades over the near part of the frustum only, because a
    // cascade fitted to a 200 m far plane wastes every texel on geometry nobody looks at.
    const float shadowFar = std::clamp(std::max(sceneRadius * 3.0f, cameraNear * 20.0f), cameraNear * 4.0f,
                                       std::max(cameraFar, cameraNear * 4.0f));
    const glm::mat4 invViewProj = glm::inverse(viewProj);
    const std::uint32_t cascades = std::clamp(quality.cascadeCount, 1u, kMaxCascades);
    const std::vector<float> splits = cascadeSplits(cameraNear, shadowFar, cascades);

    bool directionalDone = false;
    for (std::uint32_t i = 0; i < lights.size(); ++i) {
        const scene::PunctualLight& light = *lights[i];
        if (!light.enabled || !light.castsShadow || light.shadowStrength <= 0.0f) {
            continue;
        }
        if (light.type == scene::PunctualLight::Type::Directional) {
            if (directionalDone || views_.size() + cascades > kMaxShadowViews) {
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
                views_.push_back(view);
                nearDepth = splits[c];
            }
            directionalDone = true;
            stats_.cascades += cascades;
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
        // The constant bias is expressed in world units (a couple of the view's own texels) and
        // converted to normalised depth here, so a cascade covering 10 m and one covering 1 km get
        // the same visual bias instead of the same number. The shader scales it by the slope and
        // adds the normal offset on top.
        const float worldBias = views_[v].texelWorldSize * 2.0f + 0.005f;
        const float depthBias = worldBias / std::max(views_[v].depthRange, 1e-3f);
        im.block.views[v].params = glm::vec4(views_[v].texelWorldSize, depthBias,
                                             views_[v].cascade ? 1.0f : 0.0f, views_[v].farDistance);
    }
    im.block.info = glm::vec4(static_cast<float>(im.resolution), static_cast<float>(cascades),
                              static_cast<float>(quality.shadowPcfTaps), quality.softShadows ? 1.0f : 0.0f);
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
