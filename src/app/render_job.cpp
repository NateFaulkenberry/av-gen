#include "app/render_job.hpp"

#include <fstream>
#include <sstream>

#include "assets/exr.hpp"
#include "assets/image.hpp"
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "rendering/debug_visualizer.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace avgen::app {

namespace {

// ADR-320. Never reused within a process, which is the whole point: an address is.
std::uint64_t nextJobId() {
    static std::atomic<std::uint64_t> counter{0};
    return ++counter;
}

// The sRGB transfer function, on a value already clamped to 0-1. The same curve `linearToSrgb` in
// shaders/tonemap.wgsl applies as the last thing it does before writing the RGBA8 target -- so for
// a PNG or video render the preview copies bytes that already went through it, and this is used
// only for the EXR path, where the file is scene-linear and has no display encoding at all.
std::uint8_t encodeSrgb(float linear) {
    const float c = std::clamp(linear, 0.0f, 1.0f);
    const float s = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
    return static_cast<std::uint8_t>(std::lround(s * 255.0f));
}

// ADR-256: the mapping from what the identifier AOV actually carries to what a surface IS, written
// by the run that produced the frames.
//
// **The key is the identifier's low 16 bits, not its high 16.** ADR-256 assumed the high half was a
// material index; it is not. Every renderer that writes this target puts the object's own index
// within its pick space plus one there -- `thisEntity + 1`, `i + 1`, `objectId + 1` -- so two
// objects sharing one material get different numbers and an entity and a procedural with nothing
// in common get the same one. The low half is `packPickId`, which carries a two-bit `PickSpace`
// tag and is unique across all three. Both halves are written out so the file says what it is
// keyed on rather than leaving a reader to assume.
//
// Written only for `--aov id`, because without the identifier plane there is nothing to key.
nlohmann::ordered_json materialManifest(const scene::Scene& scene) {
    nlohmann::ordered_json j;
    j["format"] = "avgen-materials";
    j["version"] = 1;
    j["key"] = "objectId: the low 16 bits of the id AOV (scene::pickIndexOf plus a 2-bit PickSpace "
               "tag). NOT the high 16 bits, which carry the object's ordinal within its pick space "
               "and are named 'materialId' for historical reasons only (ADR-256)";
    nlohmann::ordered_json objects = nlohmann::ordered_json::array();
    const auto add = [&](scene::PickSpace space, std::size_t index, const std::string& name,
                         const scene::Material& material) {
        nlohmann::ordered_json o;
        o["objectId"] = scene::packPickId(space, index);
        o["materialId"] = static_cast<std::uint32_t>(index + 1); // what the high half will hold
        o["space"] = space == scene::PickSpace::Entity       ? "entity"
                     : space == scene::PickSpace::Procedural ? "procedural"
                                                             : "sdf";
        o["name"] = name;
        o["class"] = scene::surfaceClassName(material.surfaceClass);
        objects.push_back(std::move(o));
    };
    for (std::size_t i = 0; i < scene.entities.size(); ++i) {
        add(scene::PickSpace::Entity, i, scene.entities[i].name, scene.entities[i].material);
    }
    for (std::size_t i = 0; i < scene.procedurals.size(); ++i) {
        add(scene::PickSpace::Procedural, i, scene.procedurals[i].name, scene.procedurals[i].material);
    }
    for (std::size_t i = 0; i < scene.sdfs.size(); ++i) {
        add(scene::PickSpace::Sdf, i, scene.sdfs[i].name, scene.sdfs[i].material);
    }
    j["objects"] = std::move(objects);
    return j;
}

} // namespace




RenderJob::RenderJob(gpu::Context& context, gpu::ShaderLibrary& shaders, std::unique_ptr<Engine> engine,
                     RenderSettings settings, std::filesystem::path baseDir)
    : id_(nextJobId()), context_(context), shaders_(shaders), engine_(std::move(engine)), settings_(std::move(settings)),
      baseDir_(std::move(baseDir)) {}

RenderJob::~RenderJob() {
    cancelled_ = true;
    {
        std::lock_guard lock(mutex_);
        stopEncoders_ = true;
    }
    cv_.notify_all();
    spaceCv_.notify_all();
    for (auto& t : encoders_) {
        if (t.joinable()) {
            t.join();
        }
    }
}

void RenderJob::fail(std::string message) {
    {
        std::lock_guard lock(mutex_);
        if (error_.empty()) {
            error_ = std::move(message);
        }
    }
    spaceCv_.notify_all();
}

Result<void> RenderJob::start() {
    if (started_) {
        return avgen::fail("render job already started");
    }
    if (engine_ == nullptr || engine_->mode() != EngineMode::Offline) {
        return avgen::fail("render job needs an offline engine");
    }
    if (auto r = settings_.validate(); !r) {
        return r;
    }
    output_ = settings_.outputPath.is_absolute() ? settings_.outputPath : (baseDir_ / settings_.outputPath);
    output_ = output_.lexically_normal();
    if (settings_.outputPath.empty()) {
        return avgen::fail("render output path is empty");
    }
    end_ = settings_.resolvedEnd(engine_->durationSeconds(), engine_->timeline().durationSeconds());
    total_ = settings_.frameCount(end_);

    // ADR-186: the distance reductions this render is under. Told to the *engine*, not the
    // renderer, because two of the three live in the simulation rather than in a pass -- the rig
    // pose rate and the entity world's behaviour bands -- and the third reads the scene the engine
    // hands over. This is the same lesson ADR-147 taught about the tier: a policy set on the
    // interactive side only is a policy the deliverable never gets.
    {
        const scene::DetailLimits resolved = settings_.resolvedLimits();
        engine_->setDetailLimits(resolved);
        if (resolved.anyLifted()) {
            log::info("render: lifting live detail limits -- procedural distance cull {}, LOD rungs "
                      "{}, rig pose rate {}, entity behaviour bands {}",
                      resolved.proceduralDistanceCull ? "kept" : "off",
                      resolved.proceduralLodRungs ? "kept" : "off",
                      resolved.rigDistanceRate ? "kept" : "off",
                      resolved.entityDistanceCull ? "kept" : "off");
        }
    }

    // Warm-up: the first frames drawn with freshly compiled pipelines in a process differ by 1 LSB
    // in a few scattered pixels from every later render (Metal replaces the pipelines' GPU
    // binaries shortly after creation; Dawn caches pipeline objects device-wide, so a throwaway
    // renderer warms the real one's). Rendering two frames first makes the job's output identical
    // to any later job's. AVGEN_NO_WARMUP=1 reproduces the difference (development log,
    // 2026-09-09 investigation).
    if (std::getenv("AVGEN_NO_WARMUP") == nullptr) {
        rendering::SceneRenderer warm(context_, shaders_);
        if (auto r = warm.init(); !r) {
            return r;
        }
        if (auto r = warm.resize(settings_.width, settings_.height); !r) {
            return r;
        }
        // One engine update with deltaTime 0 (no integration drifts), rendered twice.
        FixedStepClock warmClock(settings_.fps);
        warmClock.restartAt(settings_.startSeconds);
        engine_->seekSeconds(settings_.startSeconds);
        const FrameTime t = engine_->tick(warmClock);
        engine_->setViewport(settings_.width, settings_.height);
        engine_->update(t);
        const rendering::ShaderFrameInputs inputs{&engine_->shaderLayers(),
                                                  engine_->hasFrame() ? &engine_->latestFrame() : nullptr};
        for (int i = 0; i < 2; ++i) {
            if (auto img = warm.renderToImage(engine_->scene(), t, settings_.width, settings_.height, &inputs); !img) {
                return std::unexpected(img.error());
            }
        }
    }
    renderer_ = std::make_unique<rendering::SceneRenderer>(context_, shaders_);
    if (auto r = renderer_->init(); !r) {
        return r;
    }
    // ADR-147 / §5.9: the tier the deliverable is rendered at. Without this the job ran at
    // whatever the renderer defaults to -- Realtime -- and a batch frame came out byte-identical
    // to an interactive one, so every promise the Offline tier makes (no temporal shortcut, no
    // representation or shading shortcut, full-resolution auxiliary passes) was stated in the tier
    // table and not kept by the path that produces the output anyone ships.
    {
        rendering::QualityTier tier = rendering::QualityTier::Offline;
        if (!rendering::qualityTierFromName(settings_.tier, tier)) {
            return std::unexpected(Error{fmt::format(
                "render: unknown tier '{}' (preview|realtime|high|offline)", settings_.tier)});
        }
        renderer_->setQuality(tier);
    }
    // ADR-212: supersampling. **Before the resize**, and after the tier, because `resize()` is what
    // consumes `renderScale` -- it sizes the HDR target to `output * renderScale` -- and
    // `setQuality(tier)` replaces the whole settings object. Set it after the resize and the log
    // line says 2.00x while the target is still the output size: the first version of this did
    // exactly that and produced a byte-identical sequence hash, which is the only reason it was
    // caught (ADR-182 -- a probe must prove it established the state it measures).
    if (settings_.supersample > 1.0f) {
        rendering::QualitySettings quality = renderer_->qualitySettings();
        quality.renderScale = std::min(settings_.supersample, 2.0f);
        renderer_->setQualitySettings(quality);
    }
    if (auto r = renderer_->resize(settings_.width, settings_.height); !r) {
        return r;
    }
    // Quality arms, applied before the pass toggles because an arm sets a policy field and a
    // toggle removes a pass from whatever policy chose.
    if (!settings_.qualityArms.empty()) {
        rendering::QualitySettings quality = renderer_->qualitySettings();
        std::istringstream stream(settings_.qualityArms);
        std::string name;
        std::string applied;
        while (std::getline(stream, name, ',')) {
            const auto begin = name.find_first_not_of(" \t");
            const auto end = name.find_last_not_of(" \t");
            if (begin == std::string::npos) {
                continue;
            }
            name = name.substr(begin, end - begin + 1);
            if (!rendering::SceneRenderer::setQualityArm(quality, name)) {
                return std::unexpected(Error{fmt::format(
                    "render: unknown quality arm '{}' (one of: {})", name,
                    rendering::SceneRenderer::qualityArmNames())});
            }
            applied += applied.empty() ? name : ", " + name;
        }
        renderer_->setQualitySettings(quality);
        log::warn("render: this is a DIAGNOSTIC render -- quality arm(s): {}", applied);
    }
    // The diagnostic arms, which used to stop at the interactive renderer (ADR-182). Applied after
    // the tier, because a tier is a policy and this is a removal from whatever policy chose.
    if (!settings_.disablePasses.empty()) {
        rendering::SceneRenderer::PassToggles toggles = renderer_->passToggles();
        std::istringstream stream(settings_.disablePasses);
        std::string name;
        std::vector<std::string> applied;
        while (std::getline(stream, name, ',')) {
            const auto begin = name.find_first_not_of(" \t");
            const auto end = name.find_last_not_of(" \t");
            if (begin == std::string::npos) {
                continue;
            }
            name = name.substr(begin, end - begin + 1);
            if (!rendering::SceneRenderer::setPassArm(toggles, name, false)) {
                return std::unexpected(Error{fmt::format(
                    "render: unknown phase '{}' to disable (one of: {})", name,
                    rendering::SceneRenderer::passArmNames())});
            }
            applied.push_back(name);
        }
        renderer_->setPassToggles(toggles);
        std::string list;
        for (const std::string& a : applied) {
            list += list.empty() ? a : ", " + a;
        }
        // Loud, because this frame is not the deliverable and a sequence that quietly came out
        // without its water is the kind of file somebody ships.
        log::warn("render: this is a DIAGNOSTIC render -- phase(s) disabled: {}", list);
    }
    compositor_ = std::make_unique<rendering::CompositionRenderer>(context_, shaders_);
    if (auto r = compositor_->init(); !r) {
        return r;
    }
    compositor_->setTimeline(&renderer_->timeline());
    compositor_->setInputProvider([this] {
        return rendering::CompositionRenderer::Input{&engine_->layers(),
                                                     engine_->timelineClock().seconds};
    });
    renderer_->setOverlay(compositor_.get());
    if (!engine_->layers().empty() && settings_.output == RenderOutput::ExrSequence) {
        // Worth saying out loud rather than shipping a sequence somebody discovers is missing its
        // captions in a grade. EXR carries the scene-linear image from *before* the tone map, and
        // the composition is display-referred and drawn after it (ADR-083).
        log::warn("render: EXR output is the scene-linear image before tone mapping; the {} "
                  "composition layer(s) are display-referred and will not appear in it",
                  engine_->layers().size());
    }
    {
        wgpu::TextureDescriptor desc{};
        desc.label = "render-job-ldr";
        desc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
        desc.dimension = wgpu::TextureDimension::e2D;
        desc.size = {settings_.width, settings_.height, 1};
        desc.format = wgpu::TextureFormat::RGBA8Unorm;
        ldr_ = context_.device().CreateTexture(&desc);
        if (ldr_ == nullptr) {
            return avgen::fail("cannot create the {}x{} render target", settings_.width, settings_.height);
        }
        ldrView_ = ldr_.CreateView();
    }
    ring_ = std::make_unique<gpu::ReadbackRing>(context_, 3);

    // ADR-277: the post chain's own intermediates. Resolved here, as the AOVs are, so an
    // unwritable directory fails at start() rather than after the first frame.
    if (!settings_.postStages.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(settings_.postStages, ec);
        if (ec) {
            return avgen::fail("cannot create '{}' for post stages: {}", settings_.postStages.string(),
                               ec.message());
        }
        log::warn("render: --post-stages is a DIAGNOSTIC -- every stage is read back synchronously, "
                  "so this run's frame timings mean nothing. Writing to '{}'",
                  settings_.postStages.string());
    }

    // ADR-242: AOV export. The auxiliary targets have been written every frame since ADR-035 and
    // nothing outside a debug view has ever read them; this is the consumer. Resolved here rather
    // than per frame so an unknown name fails at `start()` and not two hours into a sequence.
    if (auto list = settings_.aovList(); !list) {
        return std::unexpected(list.error());
    } else if (!list->empty()) {
        using Format = gpu::ReadbackRing::Format;
        for (const std::string& name : *list) {
            AovSource src;
            src.name = name;
            if (name == "normal") {
                // One target, so one readback: roughness travels with the normal rather than
                // costing a second copy to write the same bytes twice. It is oct-encoded on the
                // GPU and decoded on the way to the file -- see `decodeNormalRoughness`.
                src.texture = &renderer_->normalRoughnessTexture();
                src.format = Format::Rgba16Float;
                src.decodeNormal = true;
            } else if (name == "emission") {
                src.texture = &renderer_->emissionTexture();
                src.format = Format::Rgba16Float;
            } else if (name == "depth") {
                src.texture = &renderer_->linearDepthTexture();
                src.format = Format::R32Float;
                src.half = false; // metres against a far plane; a half would quantise it
            } else if (name == "velocity") {
                src.texture = &renderer_->velocityTexture();
                src.format = Format::Rg16Float;
            } else if (name == "id") {
                src.texture = &renderer_->identifierTexture();
                src.format = Format::R32Uint;
                src.half = false; // an identifier is an integer; half is exact only to 2048
            } else if (name == "shadow") {
                // ADR-255. The only AOV whose target is not written unless somebody asks: at the
                // `high` and `offline` tiers no shadow mask is built at all, because the lit pass
                // computes the term per pixel inline. So the request turns a dedicated
                // full-resolution pass on, and the pass is NOT consumed by the lit pass -- the
                // shaded frame is byte-for-byte what it would have been without `--aov shadow`.
                //
                // **The refusal that makes the file mean something.** A scene with no directional
                // light has no shadow-map term, so the mask would be a plane of 1.0: a valid EXR,
                // of the right size, in the right format, containing a constant. ADR-242 refused
                // exactly this shape once already and ADR-255 asks for it again here.
                if (const scene::Composition* comp = engine_->composition()) {
                    if (auto r = shadowAovPreconditions(comp->scene(), settings_.disablePasses); !r) {
                        return r;
                    }
                }
                renderer_->shadowMask().setExportRequested(true);
                src.texture = &renderer_->shadowTexture();
                src.format = Format::Rgba16Float;
            } else {
                return avgen::fail("render: unhandled aov '{}'", name); // aovList() validates
            }
            aovs_.push_back(std::move(src));
        }
        // One slot per AOV, so a frame's copies are all in flight at once and -- because the ring
        // hands slots out in order -- each slot sees the same format every frame instead of
        // resizing its staging buffer as the formats rotate past it.
        aovRing_ = std::make_unique<gpu::ReadbackRing>(
            context_, static_cast<std::uint32_t>(std::max<std::size_t>(3, aovs_.size())));
        aovDir_ = isSequence(settings_.output) ? output_ : output_.parent_path();
        std::error_code aovEc;
        std::filesystem::create_directories(aovDir_, aovEc);
        if (aovEc) {
            return avgen::fail("cannot create '{}' for AOVs: {}", aovDir_.string(), aovEc.message());
        }
        log::info("render: exporting {} AOV(s) beside the frames: {}", aovs_.size(), settings_.aovs);
        // ADR-256: the identifier plane is a plane of integers, and an integer is not a name. The
        // run that produced it writes what they mean, beside them, so the mapping cannot disagree
        // with the frames it describes -- which a hand-maintained list of ids silently would, the
        // first time the scene gained an object.
        bool wantsId = false;
        for (const AovSource& a : aovs_) {
            wantsId = wantsId || a.name == "id";
        }
        if (wantsId) {
            if (const scene::Composition* comp = engine_->composition()) {
                materialManifest_ = materialManifest(comp->scene()).dump(2);
                std::ofstream file(aovDir_ / "materials.json");
                file << materialManifest_ << "\n";
                if (!file) {
                    return avgen::fail("cannot write '{}'", (aovDir_ / "materials.json").string());
                }
                log::info("render: wrote materials.json ({} object(s)) beside the id AOV",
                          comp->scene().entities.size() + comp->scene().procedurals.size() +
                              comp->scene().sdfs.size());
            }
        }
    }
    std::error_code ec;
    if (isSequence(settings_.output)) {
        settings_.normalisePattern();
        std::filesystem::create_directories(output_, ec);
        if (ec) {
            return avgen::fail("cannot create '{}': {}", output_.string(), ec.message());
        }
    } else {
        std::filesystem::create_directories(output_.parent_path(), ec);
        assets::VideoSettings vs;
        vs.width = settings_.width;
        vs.height = settings_.height;
        vs.fps = settings_.fps;
        vs.codec = settings_.codec;
        vs.backend = settings_.backend;
        vs.quality = settings_.quality;
        if (settings_.muxAudio && engine_->hasAudio()) {
            vs.audio = engine_->audioPath();
            vs.audioOffsetSeconds = settings_.startSeconds;
        }
        auto writer = assets::openVideoWriter(output_, vs);
        if (!writer) {
            return std::unexpected(writer.error());
        }
        video_ = std::move(*writer);
    }

    // Position the offline engine at the start of the range.
    clock_ = std::make_unique<FixedStepClock>(settings_.fps);
    clock_->restartAt(settings_.startSeconds);
    engine_->seekSeconds(settings_.startSeconds);

    int threads = settings_.encoderThreads;
    if (threads <= 0) {
        threads = static_cast<int>(std::thread::hardware_concurrency()) - 1;
    }
    threads = std::clamp(threads, 1, 16);
    if (settings_.output == RenderOutput::Video) {
        threads = 1; // the writer is sequential
    }
    queueLimit_ = static_cast<std::size_t>(threads) * 2;
    for (int i = 0; i < threads; ++i) {
        encoders_.emplace_back([this] { encoderLoop(); });
    }
    startedAt_ = std::chrono::steady_clock::now();
    started_ = true;
    log::info("render: {} frames {}x{} @ {} fps, {:.3f}s..{:.3f}s -> {} ({}, {} encoder thread(s), {} readback slots)",
              total_, settings_.width, settings_.height, settings_.fps, settings_.startSeconds, end_, output_.string(),
              renderOutputName(settings_.output), threads, ring_->slots());
    return {};
}

// ADR-251: bring a supersampled HDR frame down to the output size.
//
// A box average over each output pixel's block of source pixels. Radiance is the ONE thing an
// average is correct for -- ADR-242 refuses AOVs under supersampling precisely because averaging two
// normals is not a normal, averaging two identifiers is a third object, and averaging two depths
// across a silhouette is a surface that is not there. None of that applies to light: the mean of the
// radiance arriving over a pixel's footprint IS the radiance of that pixel, which is what
// supersampling was doing for the LDR path on the GPU all along.
//
// On the CPU because there is no pass to put it in -- the EXR path is a copy, not a draw -- and
// because ADR-212 has already established that an offline render may spend pixels. It runs once per
// frame on the readback thread, not on the GPU's critical path.
//
// A non-integer ratio is not resolved and not silently approximated: the frame is left alone and the
// caller writes what the renderer produced, because a half-pixel box is a different filter and
// guessing which one is how a wrong image ships looking right.
void RenderJob::resolveToOutput(gpu::ImageF& image) {
    if (image.rgba.empty() || image.width == settings_.width) {
        return;
    }
    if (image.width < settings_.width || image.height < settings_.height ||
        image.width % settings_.width != 0 || image.height % settings_.height != 0) {
        log::warn("render: a {}x{} HDR frame does not resolve evenly to {}x{}; writing it unresolved",
                  image.width, image.height, settings_.width, settings_.height);
        return;
    }
    const std::uint32_t bx = image.width / settings_.width;
    const std::uint32_t by = image.height / settings_.height;
    const float inv = 1.0f / static_cast<float>(bx * by);
    gpu::ImageF out;
    out.width = settings_.width;
    out.height = settings_.height;
    out.rgba.resize(static_cast<std::size_t>(out.width) * out.height * 4);
    for (std::uint32_t y = 0; y < out.height; ++y) {
        for (std::uint32_t x = 0; x < out.width; ++x) {
            float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            for (std::uint32_t sy = 0; sy < by; ++sy) {
                const float* row = image.pixel(x * bx, y * by + sy);
                for (std::uint32_t sx = 0; sx < bx; ++sx) {
                    for (int c = 0; c < 4; ++c) {
                        acc[c] += row[sx * 4 + c];
                    }
                }
            }
            float* dst = out.rgba.data() + (static_cast<std::size_t>(y) * out.width + x) * 4;
            for (int c = 0; c < 4; ++c) {
                dst[c] = acc[c] * inv;
            }
        }
    }
    image = std::move(out);
}

void RenderJob::decodeNormalRoughness(gpu::ImageF& image) {
    // The inverse of `octEncode`, matching shaders/common.wgsl line for line. Done here rather than
    // in a shader because there is no pass to put it in: the export is a copy, not a draw, and a
    // whole render pipeline to move four floats per pixel would cost more than it saves.
    for (std::size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
        const float ex = image.rgba[i];
        const float ey = image.rgba[i + 1];
        const float roughness = image.rgba[i + 2];
        float x = ex;
        float y = ey;
        const float z = 1.0f - std::abs(ex) - std::abs(ey);
        if (z < 0.0f) {
            const float sx = x >= 0.0f ? 1.0f : -1.0f;
            const float sy = y >= 0.0f ? 1.0f : -1.0f;
            const float nx = (1.0f - std::abs(y)) * sx;
            const float ny = (1.0f - std::abs(x)) * sy;
            x = nx;
            y = ny;
        }
        const float len = std::sqrt(x * x + y * y + z * z);
        // A texel no geometry wrote is (0,0,0,0), which decodes to a length of exactly one in z --
        // a normal pointing at the camera, everywhere there is sky. Left as zero instead, because
        // a matte of "where is there a surface" is half of what this pass is for.
        const bool empty = ex == 0.0f && ey == 0.0f && roughness == 0.0f;
        if (empty || len < 1e-6f) {
            image.rgba[i] = image.rgba[i + 1] = image.rgba[i + 2] = 0.0f;
            image.rgba[i + 3] = 0.0f;
            continue;
        }
        image.rgba[i] = x / len;
        image.rgba[i + 1] = y / len;
        image.rgba[i + 2] = z / len;
        image.rgba[i + 3] = roughness;
    }
}

void RenderJob::encoderLoop() {
    for (;;) {
        Pending item;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [&] { return stopEncoders_ || !queue_.empty(); });
            if (queue_.empty()) {
                return; // stopEncoders_ and drained
            }
            item = std::move(queue_.front());
            queue_.pop_front();
        }
        spaceCv_.notify_one();
        Result<void> r;
        if (!item.aov.empty()) {
            // An AOV, whatever the beauty output is -- including a video, where the frame itself
            // has no file. `written_` counts it like any other unit of work, so a progress bar
            // that reaches the end still means the work is done.
            r = assets::writeExr(settings_.aovFile(aovDir_, item.index, item.aov), item.imageF.width,
                                 item.imageF.height, item.imageF.rgba, item.aovHalf);
            if (!r) {
                fail(fmt::format("frame {} aov {}: {}", item.index, item.aov, r.error().message));
                cancelled_ = true;
            }
            std::lock_guard lock(mutex_);
            ++written_;
            continue;
        }
        if (settings_.output == RenderOutput::PngSequence) {
            r = assets::writePng(settings_.frameFile(output_, item.index), item.image.width, item.image.height,
                                 item.image.rgba);
        } else if (settings_.output == RenderOutput::ExrSequence) {
            r = assets::writeExr(settings_.frameFile(output_, item.index), item.imageF.width, item.imageF.height,
                                 item.imageF.rgba, true);
        } else {
            r = video_->writeFrame(item.image.rgba);
        }
        if (!r) {
            fail(fmt::format("frame {}: {}", item.index, r.error().message));
            cancelled_ = true;
        }
        std::lock_guard lock(mutex_);
        ++written_;
    }
}

// ADR-277: every intermediate the post chain rendered for the frame just submitted, as its own
// scene-linear EXR, plus a manifest naming them with the structural numbers a person would
// otherwise have to open a file to get (ADR-170 prefers those over milliseconds).
//
// The capture handles alias transient-pool entries. `SceneRenderer::render` has already called
// `pool_->endFrame()`, which RETURNS them to the pool rather than destroying them, so their
// contents are the frame's until the next render reuses a slot. That is why this runs here and
// not at the end of the job, and it is the same window `test_post_artifact_forensics_gpu.cpp`
// reads in.
Result<void> RenderJob::writePostStages() {
    const rendering::PostCapture capture = renderer_->post().takeCapture();
    nlohmann::json manifest;
    manifest["format"] = "avgen-post-stages";
    manifest["version"] = 1;
    manifest["frame"] = rendered_;
    manifest["output"] = {{"width", settings_.width}, {"height", settings_.height}};
    manifest["exposure"] = {{"scale", renderer_->stats().post.exposureScale},
                            {"ev100", renderer_->stats().post.exposureEv100},
                            {"meteredLuminance", renderer_->stats().post.meteredLuminance}};
    nlohmann::json stages = nlohmann::json::array();
    for (const rendering::PostCaptureStage& stage : capture.stages) {
        auto image = gpu::readTextureF16(context_, stage.texture.texture, stage.texture.width, stage.texture.height);
        if (!image) {
            return std::unexpected(image.error());
        }
        // '/' is a directory separator and a stage name is "bloom/down3"; the file is flat so the
        // manifest and the listing sort together.
        std::string file = stage.name;
        std::replace(file.begin(), file.end(), '/', '-');
        file = fmt::format("frame_{:06d}.{}.exr", rendered_, file);
        if (auto r = assets::writeExr(settings_.postStages / file, image->width, image->height, image->rgba);
            !r) {
            return r;
        }
        double total = 0.0;
        float peak = 0.0f;
        const std::size_t count = static_cast<std::size_t>(image->width) * image->height;
        for (std::size_t i = 0; i < count; ++i) {
            const float* px = image->rgba.data() + i * 4;
            const float lum = 0.2126f * px[0] + 0.7152f * px[1] + 0.0722f * px[2];
            peak = std::max(peak, lum);
            total += static_cast<double>(lum);
        }
        stages.push_back({{"name", stage.name},
                          {"file", file},
                          {"width", image->width},
                          {"height", image->height},
                          {"peakLuminance", peak},
                          {"meanLuminance", count == 0 ? 0.0 : total / static_cast<double>(count)}});
    }
    manifest["stages"] = std::move(stages);
    const std::filesystem::path path = settings_.postStages / fmt::format("frame_{:06d}.stages.json", rendered_);
    std::ofstream file(path);
    file << manifest.dump(1);
    if (!file) {
        return avgen::fail("cannot write '{}'", path.string());
    }
    log::info("render: wrote {} post stage(s) for frame {} to '{}'", capture.stages.size(), rendered_,
              settings_.postStages.string());
    return {};
}

Result<void> RenderJob::renderOne() {
    const FrameTime time = engine_->tick(*clock_);
    engine_->setViewport(settings_.width, settings_.height);
    engine_->update(time);
    const rendering::ShaderFrameInputs shaderInputs{&engine_->shaderLayers(),
                                                    engine_->hasFrame() ? &engine_->latestFrame() : nullptr};
    // The debug overlays, if `--debug-draw` asked for any. Built from the scene the frame is about
    // to be drawn from, so the lines are the same frame's as the pixels. An empty option set builds
    // nothing and costs a handful of branches, which is what a deliverable render pays.
    renderer_->setDebugDepthTest(debug_.depthTest);
    // Both extra spans, which this call did not pass and so drew neither the rung overlay nor the
    // cascades -- on the one path `--debug-draw` can actually be reached from. The shadow views are
    // the previous frame's, as they are for the live window: this runs before `render()` fits the
    // new ones, and a box one frame stale is the honest option (the alternative, fitting a second
    // set here, is a diagnostic that agrees with the renderer by construction).
    const rendering::ProceduralLodLevels lodLevels =
        rendering::readProceduralLodLevels(renderer_->procedurals(), engine_->scene(), debug_);
    rendering::buildDebugGeometry(renderer_->debugDraw(), engine_->scene(), debug_, time.renderTime,
                                  nullptr, &lodLevels, renderer_->shadows().views());
    // The same clock the live path uses, so frame f lands in the same place either way.
    // The frame's passes and its readback copy go into one command buffer; the ring submits it
    // and starts the map, and only blocks when all its slots are still on the GPU.
    wgpu::CommandEncoder encoder = context_.device().CreateCommandEncoder();
    const gpu::TargetView target{ldrView_, wgpu::TextureFormat::RGBA8Unorm, settings_.width, settings_.height};
    // ADR-277. Arming widens the pyramid and wide targets with CopySrc and changes nothing else:
    // every pass, every uniform and every resolution is the production one.
    if (!settings_.postStages.empty()) {
        renderer_->post().armCapture();
    }
    if (auto r = renderer_->render(encoder, engine_->scene(), time, target, &shaderInputs); !r) {
        return r;
    }
    const bool exr = settings_.output == RenderOutput::ExrSequence;
    const auto& source = exr ? renderer_->hdrOutputTexture() : ldr_;
    const auto format = exr ? gpu::ReadbackRing::Format::Rgba16Float : gpu::ReadbackRing::Format::Rgba8;
    // ADR-251: read the SOURCE's own extent, not the output's.
    //
    // `ldr_` is created at the output size, so for a video or PNG render these are the same number
    // and this changes nothing. The HDR texture is not: `resize()` sizes it to `output *
    // renderScale`, so under ADR-212's supersampling it is twice the output in each axis -- and
    // copying `settings_.width x settings_.height` out of it took the TOP-LEFT QUARTER. The file was
    // the right size, the right format, scene-linear and full of the wrong part of the picture.
    //
    // Measured: with `--supersample 2 --format exr`, the frame was bit-exact the top-left quarter of
    // the same render's unsupersampled frame -- 0 of 57,600 pixels differing on all three channels --
    // against controls at top-right (4.70% differing), bottom-left (74.93%) and centre (51.93%).
    // The kept quarter peaked at 0.0069 where the whole frame peaks at 4.0391: it was sky.
    const std::uint32_t sourceWidth = exr ? source.GetWidth() : settings_.width;
    const std::uint32_t sourceHeight = exr ? source.GetHeight() : settings_.height;
    if (auto r = ring_->enqueue(encoder, source, sourceWidth, sourceHeight, rendered_, format); !r) {
        return r;
    }
    // The auxiliary copies go after the beauty enqueue, which finished and submitted the frame's
    // command buffer -- so these use the overload that makes its own, which is exactly what it is
    // for: a texture whose contents are already on the GPU. The index encodes (frame, slot) so a
    // completed copy knows which AOV of which frame it is without a side table.
    for (std::size_t i = 0; i < aovs_.size(); ++i) {
        const AovSource& a = aovs_[i];
        if (a.texture == nullptr || *a.texture == nullptr) {
            continue; // a target the renderer has not created at this size
        }
        if (auto r = aovRing_->enqueue(*a.texture, settings_.width, settings_.height,
                                       rendered_ * aovs_.size() + i, a.format);
            !r) {
            return r;
        }
    }
    if (!settings_.postStages.empty()) {
        if (auto r = writePostStages(); !r) {
            return r;
        }
    }
    if (const scene::Composition* comp = engine_->composition()) {
        const auto counts = comp->entityWorld().counts();
        entityFullMax_ = std::max(entityFullMax_, counts.full);
        entityCoarseMax_ = std::max(entityCoarseMax_, counts.coarse);
        entitySkippedMax_ = std::max(entitySkippedMax_, counts.skipped);
        const scene::RigStats& rig = comp->rigStats();
        rigPosedMax_ = std::max(rigPosedMax_, rig.posed);
        rigRateLimitedMax_ = std::max(rigRateLimitedMax_, rig.rateLimited);
        rigCulledMax_ = std::max(rigCulledMax_, rig.culled);
    }
    log::trace("render frame {} t={:.4f} submitted ({} in flight)", rendered_, time.renderTime, ring_->inFlight());
    ++rendered_;
    return drain(false);
}

void RenderJob::handleFrame(gpu::ReadbackRing::Frame frame) {
    // The resolve comes BEFORE the hash, because the hash is the deliverable's proof and it has to
    // describe the frame that is written rather than an intermediate nobody receives.
    resolveToOutput(frame.imageF);
    lastHash_ = frame.format == gpu::ReadbackRing::Format::Rgba16Float ? gpu::hashImage(frame.imageF)
                                                                       : gpu::hashImage(frame.image);
    sequenceHash_ = (sequenceHash_ ^ lastHash_) * 1099511628211ull;
    frameHashes_.push_back(lastHash_);
    ++readBack_;
    log::debug("render frame {} hash={:016x}", frame.index, lastHash_);
    // ADR-320, and it has to be HERE: after `resolveToOutput` (so a supersampled EXR frame is the
    // resolved one that gets written, not the 2x intermediate) and after the hash (so the copy can
    // carry the hash of the frame it is a copy of), but before the `std::move` below hands the
    // image to the encoder queue and leaves `frame` empty. There is no other point in this
    // function where both the final pixels and their hash exist.
    if (previewEnabled_.load(std::memory_order_relaxed)) {
        capturePreview(frame);
    }
    {
        std::unique_lock lock(mutex_);
        // Frames already on the GPU when a cancel arrives are still written (partial output is
        // kept); only a failed encoder drops them, since it stops consuming.
        if (queue_.size() >= queueLimit_) {
            const auto begin = std::chrono::steady_clock::now();
            spaceCv_.wait(lock, [&] { return queue_.size() < queueLimit_ || stopEncoders_ || !error_.empty(); });
            encoderWaitSeconds_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        }
        if (error_.empty() && !stopEncoders_) {
            queue_.push_back(Pending{frame.index, std::move(frame.image), std::move(frame.imageF)});
        }
    }
    cv_.notify_one();
}

void RenderJob::capturePreview(const gpu::ReadbackRing::Frame& frame) {
    const auto began = std::chrono::steady_clock::now();
    const bool linear = frame.format == gpu::ReadbackRing::Format::Rgba16Float;
    const std::uint32_t sw = linear ? frame.imageF.width : frame.image.width;
    const std::uint32_t sh = linear ? frame.imageF.height : frame.image.height;
    if (sw == 0 || sh == 0 || (linear ? frame.imageF.rgba.empty() : frame.image.rgba.empty())) {
        return;
    }
    // Point-sampled on an integer stride, not box-filtered, and that is the whole of why this is
    // cheap enough to leave on. A box filter reads every source pixel -- 2.07 M of them at 1080p,
    // per frame, on the thread the render is stepped from. This reads one per *output* pixel:
    // 129,600 at 1080p, a factor of 16 fewer, and the factor grows with the resolution because the
    // output is capped. The cost is aliasing on fine detail, which the panel says out loud rather
    // than letting somebody read a shimmer in a 480-pixel thumbnail as a defect in the render.
    const std::uint32_t longest = std::max(sw, sh);
    const std::uint32_t step = std::max<std::uint32_t>(1, (longest + kPreviewMaxDimension - 1) / kPreviewMaxDimension);
    FramePreview& out = previewScratch_;
    out.width = (sw + step - 1) / step;
    out.height = (sh + step - 1) / step;
    out.sourceWidth = sw;
    out.sourceHeight = sh;
    out.step = step;
    out.index = frame.index;
    out.hash = lastHash_;
    out.linearSource = linear;
    out.rgba.resize(static_cast<std::size_t>(out.width) * out.height * 4);
    std::uint8_t* dst = out.rgba.data();
    if (linear) {
        // Scene-linear floats clamped to 0-1 and sRGB-encoded: the "clamp" operator at exposure 1,
        // with no chroma retention, no vignette and no grain. That is a real, nameable view of the
        // file's own numbers and it is NOT the project's tone map -- ACES, AgX, Reinhard and
        // Khronos Neutral live in shaders/tonemap.wgsl, on the GPU, and a second CPU copy of them
        // here would be a preview free to drift from the picture it claims to be of. The panel
        // states which of the two it is showing; see ADR-320.
        for (std::uint32_t y = 0; y < out.height; ++y) {
            const float* row = frame.imageF.pixel(0, y * step);
            for (std::uint32_t x = 0; x < out.width; ++x) {
                const float* p = row + static_cast<std::size_t>(x) * step * 4;
                *dst++ = encodeSrgb(p[0]);
                *dst++ = encodeSrgb(p[1]);
                *dst++ = encodeSrgb(p[2]);
                *dst++ = 0xFF;
            }
        }
    } else {
        // A straight byte copy. The RGBA8 target is what tonemap.wgsl wrote, sRGB-encoded already,
        // and it is what `writePng` and the video writer are handed unaltered -- so every pixel
        // here is bit-identical to a pixel of the file.
        for (std::uint32_t y = 0; y < out.height; ++y) {
            const std::uint8_t* row = frame.image.pixel(0, y * step);
            for (std::uint32_t x = 0; x < out.width; ++x) {
                const std::uint8_t* p = row + static_cast<std::size_t>(x) * step * 4;
                *dst++ = p[0];
                *dst++ = p[1];
                *dst++ = p[2];
                *dst++ = 0xFF;
            }
        }
    }
    // Everything above happened outside the lock, on this frame's own scratch buffer. The lock
    // covers a swap of two vectors and four scalars, so a UI thread polling at 60 Hz and a render
    // producing at whatever rate it manages never wait on each other for longer than that.
    {
        std::lock_guard lock(previewMutex_);
        if (previewFresh_) {
            ++previewDropped_; // nobody came for the last one; the newest wins
        }
        std::swap(preview_, previewScratch_);
        previewFresh_ = true;
        ++previewTapped_;
        previewSeconds_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
    }
}

bool RenderJob::takePreview(FramePreview& out) {
    std::lock_guard lock(previewMutex_);
    if (!previewFresh_) {
        return false;
    }
    std::swap(out, preview_);
    previewFresh_ = false;
    return true;
}

std::uint64_t RenderJob::previewTapped() const {
    std::lock_guard lock(previewMutex_);
    return previewTapped_;
}

std::uint64_t RenderJob::previewDropped() const {
    std::lock_guard lock(previewMutex_);
    return previewDropped_;
}

double RenderJob::previewSeconds() const {
    std::lock_guard lock(previewMutex_);
    return previewSeconds_;
}

Result<void> RenderJob::drain(bool all) {
    if (all) {
        if (auto r = ring_->flush(); !r) {
            return r;
        }
    }
    while (auto frame = ring_->poll()) {
        handleFrame(std::move(*frame));
    }
    if (!ring_->error().empty()) {
        return avgen::fail("{}", ring_->error());
    }
    // The AOV ring drains beside it and never through `handleFrame`: these images are not the
    // deliverable and must not touch the frame hashes, the sequence hash or the frame count.
    if (aovRing_ != nullptr) {
        if (all) {
            if (auto r = aovRing_->flush(); !r) {
                return r;
            }
        }
        while (auto frame = aovRing_->poll()) {
            const std::size_t slot = static_cast<std::size_t>(frame->index % aovs_.size());
            Pending item;
            item.index = frame->index / aovs_.size();
            item.imageF = std::move(frame->imageF);
            item.aov = aovs_[slot].name;
            item.aovHalf = aovs_[slot].half;
            if (aovs_[slot].decodeNormal) {
                decodeNormalRoughness(item.imageF);
            }
            {
                std::unique_lock lock(mutex_);
                if (queue_.size() >= queueLimit_) {
                    spaceCv_.wait(lock, [&] { return queue_.size() < queueLimit_ || stopEncoders_ || !error_.empty(); });
                }
                if (error_.empty() && !stopEncoders_) {
                    queue_.push_back(std::move(item));
                }
            }
            cv_.notify_one();
        }
        if (!aovRing_->error().empty()) {
            return avgen::fail("aov readback: {}", aovRing_->error());
        }
    }
    return {};
}

Result<void> RenderJob::finish() {
    if (ring_) {
        if (auto r = drain(true); !r) {
            fail(r.error().message);
        }
    }
    {
        std::lock_guard lock(mutex_);
        stopEncoders_ = true;
    }
    cv_.notify_all();
    for (auto& t : encoders_) {
        if (t.joinable()) {
            t.join();
        }
    }
    encoders_.clear();
    // ADR-256, the check that makes the manifest worth trusting: re-derive it now and compare. A
    // scene whose object set moved during the render -- ADR-091 live-tier population, a rebuild on
    // seek -- would leave a mapping that is well-formed, plausible, and about a different set of
    // objects than the frames beside it. That is the exact failure the manifest exists to prevent,
    // so it is not left to be assumed away.
    if (!materialManifest_.empty()) {
        if (const scene::Composition* comp = engine_->composition()) {
            const std::string now = materialManifest(comp->scene()).dump(2);
            if (now != materialManifest_) {
                log::warn("render: the scene's objects changed during the render, so materials.json "
                          "describes the frames it was written beside and not the ones that "
                          "followed. Rewriting it as UNSTABLE; a per-class measurement over this "
                          "render is not valid (ADR-256)");
                nlohmann::ordered_json unstable = nlohmann::ordered_json::parse(now, nullptr, false);
                if (!unstable.is_discarded()) {
                    unstable["stable"] = false;
                    unstable["reason"] = "the scene's object set changed during the render";
                    std::ofstream file(aovDir_ / "materials.json");
                    file << unstable.dump(2) << "\n";
                }
            }
        }
    }
    if (video_) {
        if (auto r = video_->finish(); !r) {
            fail("video: " + r.error().message);
        }
    }
    done_ = true;
    std::string error;
    {
        std::lock_guard lock(mutex_);
        error = error_;
    }
    const auto p = progress();
    if (error.empty()) {
        log::info("render complete: {} frames in {:.1f}s ({:.1f} fps), {} written, sequence hash {:016x}, GPU errors: {}",
                  p.framesReadBack, p.elapsedSeconds, p.renderFps, p.framesWritten, p.sequenceHash,
                  context_.errorCount());
        // The structural check on ADR-186's lift, reported whether or not it looks right. An
        // entity counted coarse or skipped in an offline render is a distant character that froze
        // or stuttered in the deliverable -- the artifact the lift exists to remove.
        log::info("render: entity updates per frame, worst case -- full {}, coarse {}, skipped {}{}",
                  entityFullMax_, entityCoarseMax_, entitySkippedMax_,
                  (entityCoarseMax_ > 0 || entitySkippedMax_ > 0)
                      ? "  <-- the distance limits reached the simulation; distant bodies stuttered or froze"
                      : "  (every body simulated at full rate at every distance)");
    log::info("render: rig poses per frame, worst case -- posed {}, rate-limited {}, culled {}{}",
              rigPosedMax_, rigRateLimitedMax_, rigCulledMax_,
              (rigRateLimitedMax_ > 0 || rigCulledMax_ > 0)
                  ? "  <-- the rig ladder reached the deliverable; distant characters slid or glided"
                  : "  (every rig posed at its authored rate at every distance)");
        log::info("render: the render thread waited {:.2f}s for the GPU (readback ring full) and {:.2f}s for the "
                  "encoders (queue full)",
                  ring_ ? ring_->blockedSeconds() : 0.0, encoderWaitSeconds_);
        if (context_.errorCount() != 0) {
            return avgen::fail("{} GPU validation error(s) during the render", context_.errorCount());
        }
        return {};
    }
    return avgen::fail("{}", error);
}

bool RenderJob::step(int maxFrames, double budgetSeconds) {
    if (!started_ || done_) {
        return true;
    }
    const auto begin = std::chrono::steady_clock::now();
    for (int i = 0; i < maxFrames; ++i) {
        if (cancelled_ || rendered_ >= total_) {
            break;
        }
        if (auto r = renderOne(); !r) {
            fail(r.error().message);
            cancelled_ = true;
            break;
        }
        if (budgetSeconds > 0.0 &&
            std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count() >= budgetSeconds) {
            break;
        }
    }
    if (cancelled_ || rendered_ >= total_) {
        (void)finish();
        return true;
    }
    return false;
}

Result<void> RenderJob::run() {
    if (!started_) {
        if (auto r = start(); !r) {
            return r;
        }
    }
    auto lastLog = std::chrono::steady_clock::now();
    while (!done_) {
        (void)step(1);
        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(now - lastLog).count() >= 1.0 && !done_) {
            const auto p = progress();
            log::info("render: {}/{} frames ({:.0f}%), {:.1f} fps, hash {:016x}", p.framesRendered, p.framesTotal,
                      p.fraction() * 100.0, p.renderFps, p.lastFrameHash);
            lastLog = now;
        }
    }
    std::string error;
    {
        std::lock_guard lock(mutex_);
        error = error_;
    }
    if (!error.empty()) {
        return avgen::fail("{}", error);
    }
    if (context_.errorCount() != 0) {
        return avgen::fail("{} GPU validation error(s) during the render", context_.errorCount());
    }
    return {};
}

void RenderJob::cancel() {
    cancelled_ = true;
    spaceCv_.notify_all();
}

RenderProgress RenderJob::progress() const {
    RenderProgress p;
    p.framesRendered = rendered_;
    p.framesReadBack = readBack_;
    p.framesTotal = total_;
    p.lastFrameHash = lastHash_;
    p.sequenceHash = sequenceHash_;
    p.finished = done_;
    p.cancelled = cancelled_;
    if (started_) {
        p.elapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt_).count();
        p.renderFps = p.elapsedSeconds > 0.0 ? static_cast<double>(rendered_) / p.elapsedSeconds : 0.0;
    }
    {
        std::lock_guard lock(const_cast<std::mutex&>(mutex_));
        p.framesWritten = written_;
        p.error = error_;
    }
    return p;
}

} // namespace avgen::app
