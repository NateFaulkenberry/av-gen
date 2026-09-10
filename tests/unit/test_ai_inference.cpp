// The AI runtime abstraction (ADR-065). What matters here is the guarantees, not the arithmetic:
// AI stays optional, a missing model is an ordinary state, the cache keys on the model's version
// so an upgrade is not answered from the old model's results, and the realtime budget refuses
// rather than overruns.

#include "ai/inference.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>

using namespace avgen;
namespace fs = std::filesystem;

namespace {
ai::Tensor gradientImage(int w, int h, float bias = 0.0f) {
    ai::Tensor t;
    t.resize(w, h, 3);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float v = static_cast<float>(x) / static_cast<float>(std::max(w - 1, 1)) + bias;
            const std::size_t i =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 3;
            t.data[i] = v;
            t.data[i + 1] = v * 0.8f;
            t.data[i + 2] = v * 0.6f;
        }
    }
    return t;
}
} // namespace

TEST_CASE("The built-in backend is always available and needs no file", "[ai][inference]") {
    ai::InferenceService svc;
    CHECK(svc.available());
    const auto names = svc.backendNames();
    REQUIRE(!names.empty());
    CHECK(names.back() == "builtin-analytic");

    auto model = svc.loadBuiltin(ai::ModelTask::Depth);
    INFO((model ? std::string() : model.error().message));
    REQUIRE(model.has_value());
    CHECK(model->task == ai::ModelTask::Depth);
    CHECK(model->outputChannels == 1);
}

TEST_CASE("A model nothing can load is an ordinary error, not a crash", "[ai][inference]") {
    // "No model available" has to be a state the caller can carry on from: AI is optional and an
    // optional failure must never become a total failure.
    ai::InferenceService svc;
    auto model = svc.loadModel("/models/depth_anything_v2.onnx", ai::ModelTask::Depth);
    REQUIRE(!model.has_value());
    CHECK(model.error().message.find("no inference backend") != std::string::npos);
}

TEST_CASE("The built-in models produce results that depend on their input", "[ai][inference]") {
    ai::InferenceService svc;
    const auto image = gradientImage(32, 24);

    SECTION("depth is single channel, in range, and varies across the image") {
        auto model = svc.loadBuiltin(ai::ModelTask::Depth);
        REQUIRE(model.has_value());
        auto out = svc.run(*model, image);
        REQUIRE(out.has_value());
        CHECK(out->width == 32);
        CHECK(out->height == 24);
        CHECK(out->channels == 1);
        float lo = 1e9f;
        float hi = -1e9f;
        for (const float v : out->data) {
            CHECK(v >= 0.0f);
            CHECK(v <= 1.0f);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        CHECK(hi - lo > 0.1f);   // it responded to the image rather than returning a constant
    }
    SECTION("optical flow has two channels") {
        auto model = svc.loadBuiltin(ai::ModelTask::OpticalFlow);
        REQUIRE(model.has_value());
        auto out = svc.run(*model, image);
        REQUIRE(out.has_value());
        CHECK(out->channels == 2);
    }
    SECTION("an embedding tells two different images apart") {
        auto model = svc.loadBuiltin(ai::ModelTask::Embedding);
        REQUIRE(model.has_value());
        auto a = svc.run(*model, gradientImage(16, 16, 0.0f));
        auto b = svc.run(*model, gradientImage(16, 16, 0.4f));
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        CHECK(std::abs(a->data[0] - b->data[0]) > 0.1f);
    }
    SECTION("an empty input is refused rather than producing zeros") {
        auto model = svc.loadBuiltin(ai::ModelTask::Depth);
        REQUIRE(model.has_value());
        auto out = svc.run(*model, ai::Tensor{});
        REQUIRE(!out.has_value());
    }
}

TEST_CASE("Inference is deterministic", "[ai][inference]") {
    // Without this the offline pipeline cannot be a deterministic render target, which is the whole
    // reason the engine owns reality and AI only decorates it.
    ai::InferenceService svc;
    auto model = svc.loadBuiltin(ai::ModelTask::Depth);
    REQUIRE(model.has_value());
    const auto image = gradientImage(24, 18);
    auto a = svc.run(*model, image);
    auto b = svc.run(*model, image);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(a->data.size() == b->data.size());
    for (std::size_t i = 0; i < a->data.size(); ++i) {
        CHECK(a->data[i] == b->data[i]);
    }
}

TEST_CASE("The cache answers a repeat and keys on the model's version", "[ai][inference][cache]") {
    const auto root = fs::temp_directory_path() / "avgen_ai_cache_test";
    fs::remove_all(root);
    auto cache = std::make_shared<ai::InferenceCache>(root);

    ai::InferenceService svc;
    svc.setCache(cache);
    auto model = svc.loadBuiltin(ai::ModelTask::Depth);
    REQUIRE(model.has_value());
    const auto image = gradientImage(20, 16);

    ai::InferenceTiming first;
    auto a = svc.run(*model, image, "", &first);
    REQUIRE(a.has_value());
    CHECK(!first.cacheHit);
    CHECK(first.inferenceSeconds >= 0.0);

    ai::InferenceTiming second;
    auto b = svc.run(*model, image, "", &second);
    REQUIRE(b.has_value());
    CHECK(second.cacheHit);
    CHECK(second.inferenceSeconds == 0.0);   // it never ran
    REQUIRE(a->data.size() == b->data.size());
    for (std::size_t i = 0; i < a->data.size(); ++i) {
        CHECK(a->data[i] == b->data[i]);
    }
    CHECK(cache->hits() >= 1);

    SECTION("a different model version is a different key") {
        // Leaving the version out of the key is the classic way to serve last week's model's
        // answers after an upgrade.
        auto upgraded = *model;
        upgraded.version = "2";
        const auto keyOld = ai::InferenceCache::keyFor(*model, image, "");
        const auto keyNew = ai::InferenceCache::keyFor(upgraded, image, "");
        CHECK(keyOld != keyNew);
    }
    SECTION("different parameters are a different key") {
        CHECK(ai::InferenceCache::keyFor(*model, image, "scale=1") !=
              ai::InferenceCache::keyFor(*model, image, "scale=2"));
    }
    SECTION("a different input is a different key") {
        CHECK(ai::InferenceCache::keyFor(*model, image, "") !=
              ai::InferenceCache::keyFor(*model, gradientImage(20, 16, 0.5f), ""));
    }
    SECTION("a truncated entry is a miss, not a poisoned render") {
        const auto key = ai::InferenceCache::keyFor(*model, image, "");
        const auto file = root / (key + ".tensor");
        REQUIRE(fs::exists(file));
        fs::resize_file(file, 8);
        CHECK(!cache->lookup(key).has_value());
    }
    fs::remove_all(root);
}

TEST_CASE("The realtime budget is off by default and refuses rather than overruns",
          "[ai][inference][budget]") {
    ai::RealtimeBudget budget;
    CHECK(budget.milliseconds == 0.0);
    // Opt-in: a default that quietly spends frame time is the one thing a realtime engine must not
    // ship with.
    CHECK(!budget.allows(0, 0.0));
    CHECK(!budget.allows(100, 0.0));

    budget.milliseconds = 2.0;
    budget.everyNFrames = 4;
    CHECK(budget.allows(0, 0.5));
    CHECK(!budget.allows(1, 0.5));    // not this frame
    CHECK(!budget.allows(2, 0.5));
    CHECK(budget.allows(4, 0.5));
    // Overran last time: skip rather than overrun again.
    CHECK(!budget.allows(8, 3.5));
    CHECK(budget.allows(8, 1.9));
}
