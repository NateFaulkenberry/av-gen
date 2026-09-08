#pragma once

// PCG32 (Melissa O'Neill, Apache-2.0/MIT reference algorithm). Seedable, tiny, and identical on
// every platform, which std::mt19937 distributions are not. std::random_device is banned.

#include <cstdint>

namespace avgen {

class Rng {
public:
    explicit Rng(std::uint64_t seed = 0x853c49e6748fea9bULL, std::uint64_t stream = 0xda3e39cb94b95bdbULL) {
        state_ = 0U;
        inc_ = (stream << 1u) | 1u;
        nextU32();
        state_ += seed;
        nextU32();
    }

    // Derives a generator for (globalSeed, frameIndex, systemId) so every system and frame gets
    // an independent, reproducible stream.
    static Rng forFrame(std::uint64_t globalSeed, std::uint64_t frameIndex, std::uint64_t systemId) {
        return Rng(globalSeed ^ (frameIndex * 0x9E3779B97F4A7C15ULL), systemId + 1);
    }

    std::uint32_t nextU32() {
        const std::uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + inc_;
        const auto xorshifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
        const auto rot = static_cast<std::uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((-rot) & 31u));
    }

    // Uniform in [0, 1).
    float nextFloat() { return static_cast<float>(nextU32() >> 8) * (1.0f / 16777216.0f); }

    // Uniform in [lo, hi).
    float range(float lo, float hi) { return lo + (hi - lo) * nextFloat(); }

private:
    std::uint64_t state_;
    std::uint64_t inc_;
};

} // namespace avgen
