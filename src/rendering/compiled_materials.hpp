#pragma once
// ADR-1812: compiled material programs for every lit path, not only the procedural one.
//
// ADR-1493 compiled a scene's material programs into straight-line WGSL, but only for the procedural renderer's lit
// pipelines; entity meshes, skinned meshes and SDF surfaces kept the interpreter (`evaluateMaterialProgramFast` falls
// back to it), and the render audit measured that interpreter at 18-31% of the GPU frame on Aurora Borealis, Talking
// Heads and Rift, all of whose programs are on entity meshes.
//
// This is the part of ADR-1493's procedural variant that every lit path needs, once: a shader's source with the
// programs spliced in, keyed by the programs' structure (`scene::materialStructureKey`), whose pipelines are built on
// Dawn's workers. Each owner (the scene renderer's entity pass, the skinning renderer, ...) says which shader and
// which pipelines; this keeps the variants, their keys and their state.
//
// Two modes:
//   - Async (the live editor, ADR-1493): a variant builds in the background and the interpreter draws until it lands.
//   - Blocking (offline renders, ADR-1812): `waitUntilReady` holds the frame until the variant is built, so every frame
//     of a render is drawn compiled -- a render is still a pure function of the timeline, the reason ADR-1493 kept
//     offline on the interpreter.

#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "scene/material_program.hpp"

#include <webgpu/webgpu_cpp.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace avgen::rendering {

enum class CompiledMaterialsMode {
    Off,      // the interpreter everywhere (tests, and the default)
    Async,    // live: build in the background, interpreter meanwhile (ADR-1493)
    Blocking, // offline: wait for the variant before drawing the frame (ADR-1812)
};

class CompiledMaterialVariants {
public:
    struct Variant {
        std::uint64_t key = 0;
        std::vector<wgpu::RenderPipeline> pipelines; // in the order the builder started them
        int pending = 0;
        bool failed = false;
        std::chrono::steady_clock::time_point started{};
    };
    // Starts the variant's pipelines from `module` (the spliced shader): call `start(desc)` once per pipeline, in a
    // fixed order; `pipeline(i)` later returns the i-th. A builder that cannot make its descriptors returns false.
    using Start = std::function<void(const wgpu::RenderPipelineDescriptor&)>;
    using Builder = std::function<bool(const wgpu::ShaderModule& module, std::span<const scene::MaterialProgramGpu> programs,
                                       const Start& start)>;

    CompiledMaterialVariants(gpu::Context& context, gpu::ShaderLibrary& shaders, std::string shaderFile,
                             std::string label, Builder builder);

    void setMode(CompiledMaterialsMode mode) { mode_ = mode; }
    // The `interpreted` diagnostic arm: keep building, but hand out no pipeline (the interpreter draws).
    void setSuppressed(bool suppressed) { suppressed_ = suppressed; }
    [[nodiscard]] CompiledMaterialsMode mode() const { return mode_; }

    // Called every frame with the frame's programs; starts a build when their structure is new.
    void note(std::span<const scene::MaterialProgramGpu> programs);
    // The i-th pipeline of the variant for this frame's programs, or null while there is none ready (the caller then
    // draws its interpreter pipeline).
    [[nodiscard]] wgpu::RenderPipeline pipeline(std::size_t i) const;
    [[nodiscard]] bool active() const { return activeVariant() != nullptr; }
    [[nodiscard]] std::size_t pendingCompiles() const { return current_ && current_->pending > 0 ? 1 : 0; }
    // Blocking mode: processes Dawn's events until this frame's variant has landed or failed (or `timeoutMs`).
    void waitUntilReady(double timeoutMs = 600000.0);
    // Drops every variant and the cached base source (a shader reload).
    void reset();

private:
    [[nodiscard]] const Variant* activeVariant() const;
    void build(std::span<const scene::MaterialProgramGpu> programs, std::uint64_t key);

    gpu::Context& context_;
    gpu::ShaderLibrary& shaders_;
    std::string shaderFile_;
    std::string label_;
    Builder builder_;
    CompiledMaterialsMode mode_ = CompiledMaterialsMode::Off;
    bool suppressed_ = false;
    std::string baseSource_; // the shader with its includes resolved, loaded on first need
    std::shared_ptr<Variant> current_;
    std::uint64_t key_ = 0;
    std::map<std::uint64_t, std::shared_ptr<Variant>> variants_;
    std::vector<std::uint64_t> order_;
    static constexpr std::size_t kMaxVariants = 16;
};

} // namespace avgen::rendering
