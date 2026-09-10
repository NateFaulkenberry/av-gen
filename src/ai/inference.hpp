#pragma once

// The AI runtime abstraction (ADR-065, milestone 7 of the cinematic upgrade).
//
// The point of this file is to make AI *optional and replaceable*, and to be small enough that
// having it costs nothing when it is switched off.
//
// Three rules it enforces by shape rather than by documentation:
//
//   - No AI runs on the audio thread or the render thread. Nothing here is callable from either:
//     inference goes through the job system, and the renderer consumes whatever result last
//     finished. There is no synchronous "run this now" entry point at all.
//   - AI is never required. A scene with no model available renders exactly as it would have; the
//     registry returns "not available" and callers carry on. An optional failure must never become
//     a total failure.
//   - Nothing about a specific backend appears above this line. `InferenceBackend` is an interface;
//     ONNX Runtime, Core ML or anything else is one implementation of it, chosen at load time.
//
// **No third-party inference backend is bundled today.** What ships is the interface, the registry,
// the cache and one built-in analytic backend that is real, deterministic and useful for testing
// the whole pipeline end to end without a download, a licence or a network. That is deliberate:
// the brief asks to investigate ONNX Runtime, and adding a large binary dependency before anything
// consumes it would be the wrong order.

#include "core/error.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::ai {

// What a model does. Deliberately a short list: these are the things worth running near a frame
// loop. Generative image synthesis is not here, because it belongs in an offline job that produces
// an asset, not in a per-frame service.
enum class ModelTask : std::uint8_t { Depth, Segmentation, OpticalFlow, Classification, Embedding };
[[nodiscard]] const char* modelTaskName(ModelTask t);
[[nodiscard]] std::optional<ModelTask> modelTaskFromName(std::string_view name);

// A dense tensor, NHWC, float32. Small and concrete on purpose: every task above takes an image and
// returns an image or a short vector, and a general tensor library would be a dependency bought for
// no current use.
struct Tensor {
    std::vector<float> data;
    int width = 0;
    int height = 0;
    int channels = 0;

    [[nodiscard]] std::size_t elements() const {
        return static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
               static_cast<std::size_t>(channels);
    }
    [[nodiscard]] bool empty() const { return data.empty(); }
    [[nodiscard]] float at(int x, int y, int c) const;
    void resize(int w, int h, int ch);
};

struct ModelInfo {
    std::string id;
    ModelTask task = ModelTask::Depth;
    std::string backend;      // which implementation loaded it
    int inputWidth = 0;       // 0 = any
    int inputHeight = 0;
    int outputChannels = 1;
    std::string version;      // part of the cache key: a new model must not read an old cache
};

// One implementation. A backend that cannot load a file says so and the registry moves on to the
// next; that is how a build without ONNX still works.
class InferenceBackend {
public:
    virtual ~InferenceBackend() = default;
    [[nodiscard]] virtual std::string_view name() const = 0;
    [[nodiscard]] virtual bool canLoad(const std::filesystem::path& path) const = 0;
    [[nodiscard]] virtual Result<ModelInfo> load(const std::filesystem::path& path, ModelTask task) = 0;
    [[nodiscard]] virtual Result<Tensor> run(const ModelInfo& model, const Tensor& input) = 0;
    virtual void unload(const ModelInfo& model) = 0;
};

// The built-in backend. Not a stub: it computes real, deterministic results from the input, so the
// cache, the job pipeline and every consumer can be tested and demonstrated without downloading
// anything. Its "depth" is a luminance-and-gradient estimate and its "segmentation" is a
// luminance threshold -- honest approximations, labelled as such, never presented as a learned
// model's output.
[[nodiscard]] std::unique_ptr<InferenceBackend> makeBuiltinBackend();

// Caches results on disk. The key includes the model's identity and version, the parameters and a
// hash of the input, so asking twice for the same thing is free and asking after a model changes
// is not silently answered from the old one.
class InferenceCache {
public:
    explicit InferenceCache(std::filesystem::path root);

    [[nodiscard]] static std::string keyFor(const ModelInfo& model, const Tensor& input,
                                            std::string_view parameters);
    [[nodiscard]] std::optional<Tensor> lookup(const std::string& key) const;
    [[nodiscard]] Result<void> store(const std::string& key, const Tensor& value);
    [[nodiscard]] std::size_t hits() const { return hits_; }
    [[nodiscard]] std::size_t misses() const { return misses_; }
    [[nodiscard]] const std::filesystem::path& root() const { return root_; }

private:
    std::filesystem::path pathFor(const std::string& key) const;
    std::filesystem::path root_;
    mutable std::size_t hits_ = 0;
    mutable std::size_t misses_ = 0;
};

// What a frame's worth of inference cost, so the diagnostics UI can show where the time went rather
// than the user guessing.
struct InferenceTiming {
    double loadSeconds = 0.0;
    double preprocessSeconds = 0.0;
    double inferenceSeconds = 0.0;
    double postprocessSeconds = 0.0;
    bool cacheHit = false;
};

// Loads models across whatever backends are registered and runs them. Everything here is
// synchronous *within a job*; nothing calls it from the render or audio thread.
class InferenceService {
public:
    InferenceService();

    void addBackend(std::unique_ptr<InferenceBackend> backend);
    void setCache(std::shared_ptr<InferenceCache> cache) { cache_ = std::move(cache); }

    // Registers a model by path. Returns an error rather than throwing when no backend will take
    // it, because "no model available" is an ordinary state, not an exceptional one.
    [[nodiscard]] Result<ModelInfo> loadModel(const std::filesystem::path& path, ModelTask task);
    // The built-in analytic model for a task, always available, needing no file.
    [[nodiscard]] Result<ModelInfo> loadBuiltin(ModelTask task);

    [[nodiscard]] Result<Tensor> run(const ModelInfo& model, const Tensor& input,
                                     std::string_view parameters = {},
                                     InferenceTiming* timing = nullptr);

    [[nodiscard]] bool available() const { return !backends_.empty(); }
    [[nodiscard]] std::vector<std::string> backendNames() const;
    [[nodiscard]] const std::vector<ModelInfo>& models() const { return models_; }

private:
    [[nodiscard]] static std::string keyOf(const ModelInfo& model, const Tensor& input,
                                           std::string_view parameters);

    std::vector<std::unique_ptr<InferenceBackend>> backends_;
    std::vector<ModelInfo> models_;
    std::shared_ptr<InferenceCache> cache_;
};

// The realtime budget (brief section 16). Realtime AI is opt-in, bounded, and skipped rather than
// allowed to overrun: a frame that waits for inference is a dropped frame, and an engine that drops
// frames unpredictably is worse than one that never inferred at all.
struct RealtimeBudget {
    double milliseconds = 0.0;   // 0 disables realtime inference entirely
    int everyNFrames = 4;        // run at most this often
    float resolutionScale = 0.25f;

    // Whether inference may start this frame, given how long the last one took.
    [[nodiscard]] bool allows(std::uint64_t frameIndex, double lastCostMs) const;
};

} // namespace avgen::ai
