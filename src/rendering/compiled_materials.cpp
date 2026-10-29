#include "rendering/compiled_materials.hpp"

#include "core/log.hpp"
#include "gpu/resource_stats.hpp"
#include "rendering/lit_features.hpp"
#include "scene/material_compiler.hpp"

#include <thread>

namespace avgen::rendering {

namespace {
double msSince(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}
} // namespace

CompiledMaterialVariants::CompiledMaterialVariants(gpu::Context& context, gpu::ShaderLibrary& shaders,
                                                   std::string shaderFile, std::string label, Builder builder)
    : context_(context), shaders_(shaders), shaderFile_(std::move(shaderFile)), label_(std::move(label)),
      builder_(std::move(builder)) {}

void CompiledMaterialVariants::note(std::span<const scene::MaterialProgramGpu> programs, std::uint32_t litFeatures) {
    if (mode_ == CompiledMaterialsMode::Off) {
        return;
    }
    litFeatures &= 0x1FFu;
    key_ = (programs.empty() ? 0x5ec1a1u : scene::materialStructureKey(programs)) ^
           (static_cast<std::uint64_t>(litFeatures) * 0x9e3779b97f4a7c15ull); // ADR-1815: the features are structure
    if ((programs.empty() && litFeatures == 0x1FFu) || (current_ && current_->key == key_)) {
        return;
    }
    if (const auto it = variants_.find(key_); it != variants_.end()) {
        current_ = it->second; // built on an earlier visit (or still building)
        return;
    }
    build(programs, key_, litFeatures);
}

const CompiledMaterialVariants::Variant* CompiledMaterialVariants::activeVariant() const {
    const Variant* v = current_.get();
    return mode_ != CompiledMaterialsMode::Off && v != nullptr && v->key == key_ && v->pending == 0 && !v->failed
               ? v
               : nullptr;
}

wgpu::RenderPipeline CompiledMaterialVariants::pipeline(std::size_t i) const {
    const Variant* v = suppressed_ ? nullptr : activeVariant();
    return v != nullptr && i < v->pipelines.size() ? v->pipelines[i] : wgpu::RenderPipeline{};
}

void CompiledMaterialVariants::waitUntilReady(double timeoutMs) {
    if (mode_ != CompiledMaterialsMode::Blocking || !current_ || current_->pending == 0) {
        return;
    }
    const auto start = std::chrono::steady_clock::now();
    while (current_->pending > 0 && msSince(start) < timeoutMs) {
        context_.processEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (current_->pending > 0) {
        log::warn("{}: compiled material programs not ready after {:.0f} ms; this frame draws them interpreted",
                  label_, timeoutMs);
    }
}

void CompiledMaterialVariants::reset() {
    current_.reset();
    variants_.clear();
    order_.clear();
    baseSource_.clear();
}

void CompiledMaterialVariants::build(std::span<const scene::MaterialProgramGpu> programs, std::uint64_t key,
                                     std::uint32_t litFeatures) {
    auto slot = std::make_shared<Variant>();
    slot->key = key;
    slot->started = std::chrono::steady_clock::now();
    current_ = slot; // a callback still running holds its own slot
    variants_[key] = slot;
    order_.push_back(key);
    if (order_.size() > kMaxVariants) {
        variants_.erase(order_.front());
        order_.erase(order_.begin());
    }
    if (baseSource_.empty()) {
        baseSource_ = shaders_.loadSource(shaderFile_).value_or(std::string());
    }
    const std::string source = scene::spliceCompiledMaterials(baseSource_, scene::compiledMaterialLibraryWgsl(programs));
    if (source.empty()) {
        slot->failed = true;
        log::warn("{}: {} has no compiled-materials markers; programs stay interpreted", label_, shaderFile_);
        return;
    }
    const auto moduleStart = std::chrono::steady_clock::now();
    auto module = shaders_.compile(source, label_ + "-compiled-materials");
    // ADR-1812: module creation (WGSL parse and validation) is synchronous, on this thread; only the pipelines are
    // built on Dawn's workers. Logged so a stall on the calling thread is visible.
    log::info("{}: compiled-materials module created in {:.1f} ms on the calling thread", label_, msSince(moduleStart));
    if (!module) {
        slot->failed = true;
        log::warn("{}: compiling the material programs failed, they stay interpreted: {}", label_,
                  module.error().message);
        return;
    }
    // AllowProcessEvents: a callback runs only inside processEvents, never while the builder is still starting
    // pipelines, so the slot can grow as each one starts.
    const std::string label = label_ + " (lit features: " + litFeatureNames(litFeatures) + ")";
    const bool ok = builder_(*module, programs, litFeatures, [&](const wgpu::RenderPipelineDescriptor& d) {
        const std::size_t i = slot->pipelines.size();
        slot->pipelines.emplace_back();
        ++slot->pending;
        static_cast<void>(gpu::pipelineCountersNoteAsync());
        context_.device().CreateRenderPipelineAsync(
            &d, wgpu::CallbackMode::AllowProcessEvents,
            [slot, i, label](wgpu::CreatePipelineAsyncStatus status, wgpu::RenderPipeline pipeline,
                             wgpu::StringView msg) {
                if (status == wgpu::CreatePipelineAsyncStatus::Success && pipeline) {
                    slot->pipelines[i] = std::move(pipeline);
                } else {
                    slot->failed = true;
                    log::warn("{}: a compiled-materials pipeline failed, programs stay interpreted: {}", label,
                              gpu::Context::toString(msg));
                }
                if (--slot->pending == 0 && !slot->failed) {
                    log::info("{}: compiled variant ready after {:.1f} ms (on Dawn's workers)", label,
                              msSince(slot->started));
                }
            });
    });
    if (!ok || slot->pipelines.empty()) {
        slot->failed = true;
    }
}

} // namespace avgen::rendering
