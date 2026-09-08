#include "analysis/fft.hpp"
#include "support/synth.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

using namespace avgen;
using namespace avgen::analysis;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {
constexpr std::uint32_t kRate = 48000;
constexpr std::size_t kSize = 2048;
} // namespace

TEST_CASE("RealFFT reports size and bin count", "[analysis][fft]") {
    RealFFT fft(kSize);
    CHECK(fft.size() == kSize);
    CHECK(fft.binCount() == kSize / 2 + 1);
}

TEST_CASE("RealFFT magnitude peaks at the sine bin with unit amplitude", "[analysis][fft]") {
    RealFFT fft(kSize);
    const auto input = testsupport::sine(440.0f, kRate, kSize);
    std::vector<float> mag(fft.binCount());
    fft.magnitude(input, mag);

    const auto peakIt = std::max_element(mag.begin(), mag.end());
    const auto peakBin = static_cast<std::size_t>(std::distance(mag.begin(), peakIt));
    const auto expectedBin =
        static_cast<std::size_t>(std::lround(440.0 * static_cast<double>(kSize) / kRate));
    CHECK(peakBin == expectedBin);
    // Rectangular window, 0.23 bins off-centre: scalloping loss keeps this above 0.9.
    CHECK_THAT(static_cast<double>(*peakIt), WithinAbs(1.0, 0.1));
    // Far from the peak the spectrum is small (rectangular-window sidelobes < -13 dB).
    CHECK(mag[expectedBin + 40] < 0.05f);
    CHECK(mag[expectedBin / 2] < 0.05f);
}

TEST_CASE("RealFFT DC input lands entirely in bin 0", "[analysis][fft]") {
    RealFFT fft(kSize);
    const std::vector<float> input(kSize, 0.75f);
    std::vector<float> mag(fft.binCount());
    fft.magnitude(input, mag);
    CHECK_THAT(static_cast<double>(mag[0]), WithinAbs(0.75, 1e-5));
    for (std::size_t k = 1; k < mag.size(); ++k) {
        CHECK(mag[k] < 1e-4f);
    }
}

TEST_CASE("RealFFT silence produces all-zero output", "[analysis][fft]") {
    RealFFT fft(kSize);
    const auto input = testsupport::silence(kSize);
    std::vector<float> mag(fft.binCount(), 1.0f);
    fft.magnitude(input, mag);
    CHECK(std::all_of(mag.begin(), mag.end(), [](float m) { return m == 0.0f; }));

    std::vector<std::complex<float>> bins(fft.binCount(), {1.0f, 1.0f});
    fft.forward(input, bins);
    CHECK(std::all_of(bins.begin(), bins.end(),
                      [](std::complex<float> c) { return c == std::complex<float>{}; }));
}

TEST_CASE("RealFFT forward satisfies Parseval's theorem on noise", "[analysis][fft]") {
    RealFFT fft(kSize);
    const auto input = testsupport::whiteNoise(kSize, 1.0f, 12345);
    std::vector<std::complex<float>> bins(fft.binCount());
    fft.forward(input, bins);

    double timeEnergy = 0.0;
    for (const float x : input) {
        timeEnergy += static_cast<double>(x) * static_cast<double>(x);
    }
    // For a real signal: sum |x|^2 = (1/N) (|X_0|^2 + 2 sum_{0<k<N/2} |X_k|^2 + |X_{N/2}|^2).
    double freqEnergy = std::norm(std::complex<double>(bins.front()));
    for (std::size_t k = 1; k + 1 < bins.size(); ++k) {
        freqEnergy += 2.0 * std::norm(std::complex<double>(bins[k]));
    }
    freqEnergy += std::norm(std::complex<double>(bins.back()));
    freqEnergy /= static_cast<double>(kSize);
    CHECK_THAT(freqEnergy, WithinRel(timeEnergy, 1e-4));
}

TEST_CASE("RealFFT forward matches a naive DFT for a small size", "[analysis][fft]") {
    constexpr std::size_t n = 16;
    RealFFT fft(n);
    const auto input = testsupport::whiteNoise(n, 1.0f, 99);
    std::vector<std::complex<float>> bins(fft.binCount());
    fft.forward(input, bins);
    for (std::size_t k = 0; k < bins.size(); ++k) {
        std::complex<double> expected;
        for (std::size_t i = 0; i < n; ++i) {
            const double angle =
                -2.0 * std::numbers::pi * static_cast<double>(k * i) / static_cast<double>(n);
            expected +=
                static_cast<double>(input[i]) * std::complex<double>(std::cos(angle), std::sin(angle));
        }
        CHECK_THAT(static_cast<double>(bins[k].real()), WithinAbs(expected.real(), 1e-4));
        CHECK_THAT(static_cast<double>(bins[k].imag()), WithinAbs(expected.imag(), 1e-4));
    }
}

TEST_CASE("RealFFT is movable", "[analysis][fft]") {
    RealFFT a(kSize);
    RealFFT b(std::move(a));
    CHECK(b.size() == kSize);
    const auto input = testsupport::sine(1000.0f, kRate, kSize);
    std::vector<float> mag(b.binCount());
    b.magnitude(input, mag);
    const auto peakBin =
        static_cast<std::size_t>(std::distance(mag.begin(), std::max_element(mag.begin(), mag.end())));
    CHECK(peakBin == static_cast<std::size_t>(std::lround(1000.0 * kSize / kRate)));
}
