#include "audio/analysis_stream.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

using namespace avgen;
using namespace avgen::audio;

namespace {

// Samples whose value encodes their PCM frame index (exact in float up to 2^24).
std::vector<float> indexed(std::uint64_t startFrame, std::size_t count) {
    std::vector<float> out(count);
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = static_cast<float>(startFrame + i);
    }
    return out;
}

} // namespace

TEST_CASE("AnalysisStream never reads across a discontinuity marker", "[audio][analysis]") {
    AnalysisStream stream(4096);
    stream.write(indexed(0, 100));
    stream.markDiscontinuity(5000);
    stream.write(indexed(5000, 50));
    REQUIRE(stream.available() == 150);

    std::vector<float> out(1024, -1.0f);

    auto first = stream.read(out);
    CHECK(first.count == 100);
    CHECK_FALSE(first.discontinuity);
    CHECK(out[0] == 0.0f);
    CHECK(out[99] == 99.0f);

    auto second = stream.read(out);
    CHECK(second.count == 50);
    CHECK(second.discontinuity);
    CHECK(second.startFrame == 5000);
    CHECK(out[0] == 5000.0f);
    CHECK(out[49] == 5049.0f);

    auto third = stream.read(out);
    CHECK(third.count == 0);
    CHECK_FALSE(third.discontinuity);
}

TEST_CASE("AnalysisStream reports a marker placed before any write on the first read", "[audio][analysis]") {
    AnalysisStream stream(1024);
    stream.markDiscontinuity(12345);
    stream.write(indexed(12345, 10));

    std::vector<float> out(64);
    auto r = stream.read(out);
    CHECK(r.count == 10);
    CHECK(r.discontinuity);
    CHECK(r.startFrame == 12345);
    CHECK(out[0] == 12345.0f);
}

TEST_CASE("AnalysisStream reads only up to a marker lying inside the requested range", "[audio][analysis]") {
    AnalysisStream stream(1024);
    stream.write(indexed(0, 10));
    stream.markDiscontinuity(100);
    stream.write(indexed(100, 10));
    stream.markDiscontinuity(200);
    stream.write(indexed(200, 10));

    std::vector<float> out(5);
    auto a = stream.read(out);
    CHECK(a.count == 5);
    CHECK_FALSE(a.discontinuity);
    auto b = stream.read(out); // remaining 5 of the first run
    CHECK(b.count == 5);
    CHECK_FALSE(b.discontinuity);
    CHECK(out[4] == 9.0f);

    std::vector<float> big(64);
    auto c = stream.read(big);
    CHECK(c.count == 10);
    CHECK(c.discontinuity);
    CHECK(c.startFrame == 100);
    auto d = stream.read(big);
    CHECK(d.count == 10);
    CHECK(d.discontinuity);
    CHECK(d.startFrame == 200);
    CHECK(big[9] == 209.0f);
}

TEST_CASE("AnalysisStream consecutive markers without samples: the latest wins", "[audio][analysis]") {
    AnalysisStream stream(1024);
    stream.markDiscontinuity(10);
    stream.markDiscontinuity(20);
    stream.write(indexed(20, 4));
    std::vector<float> out(16);
    auto r = stream.read(out);
    CHECK(r.count == 4);
    CHECK(r.discontinuity);
    CHECK(r.startFrame == 20);
}

TEST_CASE("AnalysisStream read into an empty span is a no-op", "[audio][analysis]") {
    AnalysisStream stream(1024);
    stream.write(indexed(0, 4));
    auto r = stream.read(std::span<float>{});
    CHECK(r.count == 0);
    CHECK(stream.available() == 4);
}

TEST_CASE("AnalysisStream drain discards samples and keeps the frame mapping", "[audio][analysis]") {
    AnalysisStream stream(1024);
    stream.write(indexed(0, 100));
    stream.markDiscontinuity(5000);
    stream.write(indexed(5000, 50));
    stream.drain();
    CHECK(stream.available() == 0);

    std::vector<float> out(64);
    auto empty = stream.read(out);
    CHECK(empty.count == 0);

    // Samples written after the drain are still contiguous with the last mark: frame 5050.
    stream.write(indexed(5050, 8));
    auto r = stream.read(out);
    CHECK(r.count == 8);
    CHECK(r.discontinuity);
    CHECK(r.startFrame == 5050);
    CHECK(out[0] == 5050.0f);

    // A mark placed after the drain (pending) is still honoured.
    stream.drain();
    stream.markDiscontinuity(7);
    stream.write(indexed(7, 3));
    auto p = stream.read(out);
    CHECK(p.count == 3);
    CHECK(p.discontinuity);
    CHECK(p.startFrame == 7);
}

TEST_CASE("AnalysisStream drops samples when the consumer is behind but keeps marker positions",
          "[audio][analysis]") {
    AnalysisStream stream(64); // rounds up to 64
    const std::size_t cap = 64;
    CHECK(stream.write(indexed(0, cap + 10)) == cap);
    stream.markDiscontinuity(900);
    stream.write(indexed(900, 5)); // dropped: ring is full
    std::vector<float> out(256);
    auto a = stream.read(out);
    CHECK(a.count == cap);
    CHECK_FALSE(a.discontinuity);
    stream.write(indexed(900, 5)); // now fits, still after the mark
    auto b = stream.read(out);
    CHECK(b.count == 5);
    CHECK(b.discontinuity);
    CHECK(b.startFrame == 900);
}

TEST_CASE("AnalysisStream concurrent producer/consumer keeps frame indices contiguous", "[audio][analysis]") {
    constexpr std::size_t kCapacity = 4096;
    constexpr std::size_t kMarkEvery = 1000; // samples between marks
    constexpr std::size_t kTotal = 400'000;  // samples produced
    constexpr std::uint64_t kJump = 100'000; // each mark jumps the frame index forward

    AnalysisStream stream(kCapacity);
    std::atomic<bool> producerDone{false};

    std::thread producer([&] {
        std::uint64_t frame = 0;
        std::size_t produced = 0;
        std::size_t sinceMark = 0;
        std::vector<float> chunk;
        while (produced < kTotal) {
            if (sinceMark >= kMarkEvery) {
                frame += kJump;
                stream.markDiscontinuity(frame);
                sinceMark = 0;
            }
            const std::size_t want = std::min<std::size_t>({kMarkEvery - sinceMark, kTotal - produced, 137});
            chunk = indexed(frame, want);
            std::size_t written = 0;
            while (written < want) {
                written += stream.write(std::span<const float>(chunk.data() + written, want - written));
                if (written < want) {
                    std::this_thread::yield();
                }
            }
            frame += want;
            produced += want;
            sinceMark += want;
        }
        producerDone.store(true, std::memory_order_release);
    });

    std::vector<float> out(300);
    std::uint64_t expected = 0;
    std::size_t consumed = 0;
    std::size_t discontinuities = 0;
    std::size_t mismatches = 0;
    bool haveExpected = false;
    while (consumed < kTotal) {
        auto r = stream.read(out);
        if (r.discontinuity) {
            expected = r.startFrame;
            haveExpected = true;
            ++discontinuities;
        }
        if (r.count == 0) {
            if (producerDone.load(std::memory_order_acquire) && stream.available() == 0) {
                break;
            }
            std::this_thread::yield();
            continue;
        }
        if (!haveExpected) {
            haveExpected = true; // first chunk without a mark starts at frame 0
        }
        for (std::size_t i = 0; i < r.count; ++i) {
            if (out[i] != static_cast<float>(expected)) {
                ++mismatches;
            }
            ++expected;
        }
        consumed += r.count;
    }
    producer.join();

    CHECK(consumed == kTotal);
    CHECK(mismatches == 0);
    CHECK(discontinuities == kTotal / kMarkEvery - 1);
}
