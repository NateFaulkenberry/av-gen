#include "core/ring_buffer.hpp"
#include "core/rng.hpp"
#include "core/time.hpp"
#include "core/triple_buffer.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <thread>
#include <vector>

using namespace avgen;

TEST_CASE("FixedStepClock produces frameIndex / fps", "[core][time]") {
    FixedStepClock clock(30.0);
    auto t0 = clock.tick();
    CHECK(t0.frameIndex == 0);
    CHECK(t0.renderTime == 0.0);
    CHECK(t0.deltaTime == 0.0);
    auto t1 = clock.tick();
    CHECK(t1.frameIndex == 1);
    CHECK_THAT(t1.renderTime, Catch::Matchers::WithinAbs(1.0 / 30.0, 1e-12));
    CHECK_THAT(t1.deltaTime, Catch::Matchers::WithinAbs(1.0 / 30.0, 1e-12));
    for (int i = 0; i < 100; ++i) {
        clock.tick();
    }
    CHECK_THAT(clock.current().renderTime, Catch::Matchers::WithinAbs(101.0 / 30.0, 1e-9));
}

TEST_CASE("FixedStepClock seek restarts the timeline at the requested time", "[core][time]") {
    FixedStepClock clock(60.0);
    clock.tick();
    clock.tick();
    clock.seek(10.0);
    CHECK(clock.current().renderTime == 10.0);
    CHECK(clock.current().deltaTime == 0.0);
    auto t = clock.tick();
    CHECK_THAT(t.renderTime, Catch::Matchers::WithinAbs(10.0 + 1.0 / 60.0, 1e-12));
}

TEST_CASE("RealtimeClock clamps large deltas and never goes backwards", "[core][time]") {
    RealtimeClock clock(0.05);
    auto t0 = clock.tick();
    CHECK(t0.deltaTime == 0.0);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    auto t1 = clock.tick();
    CHECK(t1.deltaTime > 0.0);
    CHECK(t1.deltaTime <= 0.05);
    CHECK(t1.renderTime >= t0.renderTime);
    CHECK(t1.frameIndex == 1);
}

TEST_CASE("Rng is deterministic for a seed and differs across seeds", "[core][rng]") {
    Rng a(42), b(42), c(43);
    for (int i = 0; i < 1000; ++i) {
        const auto va = a.nextU32();
        CHECK(va == b.nextU32());
    }
    CHECK(Rng(42).nextU32() != c.nextU32());
    Rng f(7);
    for (int i = 0; i < 10000; ++i) {
        const float v = f.nextFloat();
        REQUIRE(v >= 0.0f);
        REQUIRE(v < 1.0f);
    }
    // Known PCG32 reference output for seed 42, stream 54 (from the pcg32 minimal C reference).
    Rng ref(42, 54);
    CHECK(ref.nextU32() == 0xa15c02b7u);
    CHECK(ref.nextU32() == 0x7b47f409u);
}

TEST_CASE("SpscRingBuffer round-trips samples and reports capacity", "[core][ring]") {
    SpscRingBuffer ring(100);
    CHECK(ring.capacity() == 128);
    CHECK(ring.available() == 0);
    std::vector<float> in(50);
    for (std::size_t i = 0; i < in.size(); ++i) {
        in[i] = static_cast<float>(i);
    }
    CHECK(ring.write(in) == 50);
    CHECK(ring.available() == 50);
    std::vector<float> out(30);
    CHECK(ring.read(out) == 30);
    for (std::size_t i = 0; i < out.size(); ++i) {
        CHECK(out[i] == static_cast<float>(i));
    }
    CHECK(ring.available() == 20);
    // Overfill: only free space is written.
    std::vector<float> big(200, 1.0f);
    CHECK(ring.write(big) == 128 - 20);
    CHECK(ring.available() == 128);
    ring.drain();
    CHECK(ring.available() == 0);
}

TEST_CASE("SpscRingBuffer survives a concurrent producer/consumer", "[core][ring]") {
    SpscRingBuffer ring(1024);
    constexpr std::size_t total = 200000;
    std::thread producer([&] {
        std::size_t sent = 0;
        std::array<float, 64> chunk{};
        while (sent < total) {
            const std::size_t n = std::min<std::size_t>(chunk.size(), total - sent);
            for (std::size_t i = 0; i < n; ++i) {
                chunk[i] = static_cast<float>(sent + i);
            }
            std::size_t written = 0;
            while (written < n) {
                written += ring.write(std::span<const float>(chunk.data() + written, n - written));
            }
            sent += n;
        }
    });
    std::size_t received = 0;
    bool ordered = true;
    std::array<float, 64> out{};
    while (received < total) {
        const std::size_t n = ring.read(out);
        for (std::size_t i = 0; i < n; ++i) {
            if (out[i] != static_cast<float>(received + i)) {
                ordered = false;
            }
        }
        received += n;
    }
    producer.join();
    CHECK(ordered);
    CHECK(received == total);
}

TEST_CASE("TripleBuffer delivers the latest published value", "[core][triple]") {
    TripleBuffer<int> tb;
    CHECK_FALSE(tb.acquire());
    tb.back() = 1;
    tb.publish();
    tb.back() = 2;
    tb.publish();
    tb.back() = 3;
    tb.publish();
    CHECK(tb.acquire());
    CHECK(tb.front() == 3);
    CHECK_FALSE(tb.acquire());
    CHECK(tb.front() == 3);
}
