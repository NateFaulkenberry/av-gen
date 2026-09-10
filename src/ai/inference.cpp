#include "ai/inference.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>

namespace avgen::ai {
namespace {
using Clock = std::chrono::steady_clock;

constexpr std::array<std::pair<ModelTask, const char*>, 5> kTaskNames{{
    {ModelTask::Depth, "depth"},
    {ModelTask::Segmentation, "segmentation"},
    {ModelTask::OpticalFlow, "opticalFlow"},
    {ModelTask::Classification, "classification"},
    {ModelTask::Embedding, "embedding"},
}};

// FNV-1a over the bytes of the input. Cheap, stable across runs and across machines, which is what
// a cache key has to be; not a cryptographic hash, and not pretending to be.
std::uint64_t hashBytes(const void* data, std::size_t bytes, std::uint64_t seed = 1469598103934665603ull) {
    const auto* p = static_cast<const unsigned char*>(data);
    std::uint64_t h = seed;
    for (std::size_t i = 0; i < bytes; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

float luminance(const Tensor& t, int x, int y) {
    if (t.channels >= 3) {
        return 0.2126f * t.at(x, y, 0) + 0.7152f * t.at(x, y, 1) + 0.0722f * t.at(x, y, 2);
    }
    return t.at(x, y, 0);
}

// The built-in analytic backend. Every result here is computed from the input by arithmetic that
// is written down in this file. It is genuinely useful -- a luminance-and-gradient depth estimate
// is a real cue, and it is enough to build and test the whole offline pipeline against -- but it
// is not a learned model and nothing in the codebase describes it as one.
class BuiltinBackend final : public InferenceBackend {
public:
    [[nodiscard]] std::string_view name() const override { return "builtin-analytic"; }

    [[nodiscard]] bool canLoad(const std::filesystem::path& path) const override {
        // It answers only for its own scheme, so registering it never shadows a real backend that
        // could have taken an actual model file.
        return path.empty() || path.string().rfind("builtin:", 0) == 0;
    }

    [[nodiscard]] Result<ModelInfo> load(const std::filesystem::path& path, ModelTask task) override {
        ModelInfo m;
        m.id = path.empty() ? std::string("builtin:") + modelTaskName(task) : path.string();
        m.task = task;
        m.backend = std::string(name());
        m.version = "1";
        switch (task) {
        case ModelTask::Depth:
        case ModelTask::Segmentation:
            m.outputChannels = 1;
            break;
        case ModelTask::OpticalFlow:
            m.outputChannels = 2;
            break;
        case ModelTask::Classification:
        case ModelTask::Embedding:
            m.outputChannels = 8;
            break;
        }
        return m;
    }

    [[nodiscard]] Result<Tensor> run(const ModelInfo& model, const Tensor& input) override {
        if (input.empty() || input.width <= 0 || input.height <= 0) {
            return fail("builtin {}: the input tensor is empty", modelTaskName(model.task));
        }
        switch (model.task) {
        case ModelTask::Depth:
            return depth(input);
        case ModelTask::Segmentation:
            return segmentation(input);
        case ModelTask::OpticalFlow:
            return flow(input);
        case ModelTask::Classification:
        case ModelTask::Embedding:
            return embedding(input, model.outputChannels);
        }
        return fail("builtin: unsupported task");
    }

    void unload(const ModelInfo&) override {}

private:
    // Depth from luminance and local contrast. Bright, low-contrast regions read as far (haze
    // flattens distance); dark, high-contrast ones read as near. A crude cue, but a real one, and
    // it responds to the actual image rather than returning a constant.
    static Result<Tensor> depth(const Tensor& in) {
        Tensor out;
        out.resize(in.width, in.height, 1);
        for (int y = 0; y < in.height; ++y) {
            for (int x = 0; x < in.width; ++x) {
                const float l = luminance(in, x, y);
                const int xm = std::max(x - 1, 0);
                const int xp = std::min(x + 1, in.width - 1);
                const int ym = std::max(y - 1, 0);
                const int yp = std::min(y + 1, in.height - 1);
                const float gx = luminance(in, xp, y) - luminance(in, xm, y);
                const float gy = luminance(in, x, yp) - luminance(in, x, ym);
                const float contrast = std::sqrt(gx * gx + gy * gy);
                const float far = std::clamp(l * 0.75f + (1.0f - std::min(contrast * 4.0f, 1.0f)) * 0.25f,
                                             0.0f, 1.0f);
                out.data[static_cast<std::size_t>(y) * static_cast<std::size_t>(in.width) +
                         static_cast<std::size_t>(x)] = far;
            }
        }
        return out;
    }

    // A luminance threshold against the image's own mean: subject against ground, nothing more.
    static Result<Tensor> segmentation(const Tensor& in) {
        double sum = 0.0;
        for (int y = 0; y < in.height; ++y) {
            for (int x = 0; x < in.width; ++x) {
                sum += static_cast<double>(luminance(in, x, y));
            }
        }
        const auto mean = static_cast<float>(sum / std::max(1.0, static_cast<double>(in.width) *
                                                                     static_cast<double>(in.height)));
        Tensor out;
        out.resize(in.width, in.height, 1);
        for (int y = 0; y < in.height; ++y) {
            for (int x = 0; x < in.width; ++x) {
                const float l = luminance(in, x, y);
                out.data[static_cast<std::size_t>(y) * static_cast<std::size_t>(in.width) +
                         static_cast<std::size_t>(x)] =
                    std::clamp((l - mean) * 4.0f + 0.5f, 0.0f, 1.0f);
            }
        }
        return out;
    }

    // The image's own gradient, which is what optical flow degenerates to with a single frame. It
    // is here so a consumer can be written and tested against the right shape of data; a real flow
    // model takes two frames and this backend is not one.
    static Result<Tensor> flow(const Tensor& in) {
        Tensor out;
        out.resize(in.width, in.height, 2);
        for (int y = 0; y < in.height; ++y) {
            for (int x = 0; x < in.width; ++x) {
                const int xm = std::max(x - 1, 0);
                const int xp = std::min(x + 1, in.width - 1);
                const int ym = std::max(y - 1, 0);
                const int yp = std::min(y + 1, in.height - 1);
                const std::size_t i =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(in.width) +
                     static_cast<std::size_t>(x)) * 2;
                out.data[i] = luminance(in, xp, y) - luminance(in, xm, y);
                out.data[i + 1] = luminance(in, x, yp) - luminance(in, x, ym);
            }
        }
        return out;
    }

    // A tiny descriptor: mean and variance of luminance, plus per-channel means and a coarse
    // spatial split. Enough to tell two images apart, which is all an embedding has to do to prove
    // the interface.
    static Result<Tensor> embedding(const Tensor& in, int channels) {
        Tensor out;
        out.resize(1, 1, channels);
        double sum = 0.0;
        double sumSq = 0.0;
        std::array<double, 4> quadrant{};
        for (int y = 0; y < in.height; ++y) {
            for (int x = 0; x < in.width; ++x) {
                const auto l = static_cast<double>(luminance(in, x, y));
                sum += l;
                sumSq += l * l;
                const int q = (x >= in.width / 2 ? 1 : 0) + (y >= in.height / 2 ? 2 : 0);
                quadrant[static_cast<std::size_t>(q)] += l;
            }
        }
        const double n = std::max(1.0, static_cast<double>(in.width) * static_cast<double>(in.height));
        const double mean = sum / n;
        out.data[0] = static_cast<float>(mean);
        if (channels > 1) {
            out.data[1] = static_cast<float>(std::sqrt(std::max(sumSq / n - mean * mean, 0.0)));
        }
        for (int c = 0; c < 4 && c + 2 < channels; ++c) {
            out.data[static_cast<std::size_t>(c) + 2] =
                static_cast<float>(quadrant[static_cast<std::size_t>(c)] / (n * 0.25));
        }
        for (int c = 0; c < in.channels && c + 6 < channels; ++c) {
            double channelSum = 0.0;
            for (int y = 0; y < in.height; ++y) {
                for (int x = 0; x < in.width; ++x) {
                    channelSum += static_cast<double>(in.at(x, y, c));
                }
            }
            out.data[static_cast<std::size_t>(c) + 6] = static_cast<float>(channelSum / n);
        }
        return out;
    }
};
} // namespace

const char* modelTaskName(ModelTask t) {
    for (const auto& [task, name] : kTaskNames) {
        if (task == t) {
            return name;
        }
    }
    return "depth";
}

std::optional<ModelTask> modelTaskFromName(std::string_view name) {
    for (const auto& [task, text] : kTaskNames) {
        if (name == text) {
            return task;
        }
    }
    return std::nullopt;
}

float Tensor::at(int x, int y, int c) const {
    const auto i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                    static_cast<std::size_t>(x)) *
                       static_cast<std::size_t>(channels) +
                   static_cast<std::size_t>(c);
    return i < data.size() ? data[i] : 0.0f;
}

void Tensor::resize(int w, int h, int ch) {
    width = w;
    height = h;
    channels = ch;
    data.assign(elements(), 0.0f);
}

std::unique_ptr<InferenceBackend> makeBuiltinBackend() { return std::make_unique<BuiltinBackend>(); }

// ---- cache ---------------------------------------------------------------------------------

InferenceCache::InferenceCache(std::filesystem::path root) : root_(std::move(root)) {}

std::string InferenceCache::keyFor(const ModelInfo& model, const Tensor& input,
                                   std::string_view parameters) {
    // Identity, version, parameters, shape and contents. Leaving the version out is the classic
    // way to serve last week's model's answers after an upgrade.
    std::uint64_t h = hashBytes(model.id.data(), model.id.size());
    h = hashBytes(model.version.data(), model.version.size(), h);
    h = hashBytes(parameters.data(), parameters.size(), h);
    const std::array<int, 3> shape{input.width, input.height, input.channels};
    h = hashBytes(shape.data(), sizeof(shape), h);
    h = hashBytes(input.data.data(), input.data.size() * sizeof(float), h);
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(h));
    return std::string(modelTaskName(model.task)) + "-" + buffer;
}

std::filesystem::path InferenceCache::pathFor(const std::string& key) const {
    return root_ / (key + ".tensor");
}

std::optional<Tensor> InferenceCache::lookup(const std::string& key) const {
    std::ifstream in(pathFor(key), std::ios::binary);
    if (!in) {
        ++misses_;
        return std::nullopt;
    }
    std::int32_t header[3]{};
    in.read(reinterpret_cast<char*>(header), sizeof(header));
    if (!in || header[0] <= 0 || header[1] <= 0 || header[2] <= 0) {
        ++misses_;
        return std::nullopt;
    }
    Tensor t;
    t.resize(header[0], header[1], header[2]);
    in.read(reinterpret_cast<char*>(t.data.data()),
            static_cast<std::streamsize>(t.data.size() * sizeof(float)));
    if (!in) {
        // A truncated entry is a miss, not an error: a cache that can poison a render by being
        // half-written is worse than no cache.
        ++misses_;
        return std::nullopt;
    }
    ++hits_;
    return t;
}

Result<void> InferenceCache::store(const std::string& key, const Tensor& value) {
    std::error_code ec;
    std::filesystem::create_directories(root_, ec);
    if (ec) {
        return fail("inference cache: cannot create '{}': {}", root_.string(), ec.message());
    }
    // Written beside and renamed, so a crash mid-write cannot leave a half-entry that later reads
    // as a hit.
    const auto finalPath = pathFor(key);
    const auto tempPath = finalPath.string() + ".part";
    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            return fail("inference cache: cannot write '{}'", tempPath);
        }
        const std::int32_t header[3]{value.width, value.height, value.channels};
        out.write(reinterpret_cast<const char*>(header), sizeof(header));
        out.write(reinterpret_cast<const char*>(value.data.data()),
                  static_cast<std::streamsize>(value.data.size() * sizeof(float)));
        if (!out) {
            return fail("inference cache: writing '{}' failed", tempPath);
        }
    }
    std::filesystem::rename(tempPath, finalPath, ec);
    if (ec) {
        return fail("inference cache: cannot commit '{}': {}", finalPath.string(), ec.message());
    }
    return {};
}

// ---- service -------------------------------------------------------------------------------

InferenceService::InferenceService() { backends_.push_back(makeBuiltinBackend()); }

void InferenceService::addBackend(std::unique_ptr<InferenceBackend> backend) {
    if (backend) {
        // Newer backends are tried first, so a real one registered later takes precedence over the
        // built-in fallback rather than being shadowed by it.
        backends_.insert(backends_.begin(), std::move(backend));
    }
}

std::vector<std::string> InferenceService::backendNames() const {
    std::vector<std::string> out;
    out.reserve(backends_.size());
    for (const auto& b : backends_) {
        out.emplace_back(b->name());
    }
    return out;
}

Result<ModelInfo> InferenceService::loadModel(const std::filesystem::path& path, ModelTask task) {
    for (auto& backend : backends_) {
        if (!backend->canLoad(path)) {
            continue;
        }
        auto info = backend->load(path, task);
        if (!info) {
            return std::unexpected(info.error());
        }
        models_.push_back(*info);
        log::info("ai: loaded {} model '{}' on backend '{}'", modelTaskName(task), info->id,
                  info->backend);
        return info;
    }
    // An ordinary state, not an exceptional one: a build with no backend able to read this file
    // simply has no such model, and the caller carries on without AI.
    return fail("no inference backend can load '{}' for {}", path.string(), modelTaskName(task));
}

Result<ModelInfo> InferenceService::loadBuiltin(ModelTask task) { return loadModel({}, task); }

Result<Tensor> InferenceService::run(const ModelInfo& model, const Tensor& input,
                                     std::string_view parameters, InferenceTiming* timing) {
    InferenceTiming local;
    InferenceTiming& t = timing != nullptr ? *timing : local;

    if (cache_) {
        const auto start = Clock::now();
        const auto key = keyOf(model, input, parameters);
        if (auto hit = cache_->lookup(key)) {
            t.cacheHit = true;
            t.preprocessSeconds = std::chrono::duration<double>(Clock::now() - start).count();
            return *hit;
        }
        t.preprocessSeconds = std::chrono::duration<double>(Clock::now() - start).count();
    }

    for (auto& backend : backends_) {
        if (backend->name() != model.backend) {
            continue;
        }
        const auto start = Clock::now();
        auto result = backend->run(model, input);
        t.inferenceSeconds = std::chrono::duration<double>(Clock::now() - start).count();
        if (!result) {
            return result;
        }
        if (cache_) {
            const auto post = Clock::now();
            if (auto stored = cache_->store(keyOf(model, input, parameters), *result); !stored) {
                // A cache that cannot be written is a slower run, not a failed one.
                log::warn("ai: {}", stored.error().message);
            }
            t.postprocessSeconds = std::chrono::duration<double>(Clock::now() - post).count();
        }
        return result;
    }
    return fail("ai: model '{}' names backend '{}', which is not registered", model.id,
                model.backend);
}

std::string InferenceService::keyOf(const ModelInfo& model, const Tensor& input,
                                    std::string_view parameters) {
    return InferenceCache::keyFor(model, input, parameters);
}

bool RealtimeBudget::allows(std::uint64_t frameIndex, double lastCostMs) const {
    if (milliseconds <= 0.0) {
        return false; // realtime AI is opt-in, and off is the default
    }
    const int every = std::max(everyNFrames, 1);
    if (frameIndex % static_cast<std::uint64_t>(every) != 0) {
        return false;
    }
    // Overran last time: skip rather than overrun again. A frame that waits for inference is a
    // dropped frame, and an engine that drops frames unpredictably is worse than one that never
    // inferred at all.
    return lastCostMs <= milliseconds;
}

} // namespace avgen::ai
