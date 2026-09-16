#include "validation/ladder.hpp"

#include "external/vmaf.hpp"
#include "metrics/temporal.hpp"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>

#include <numbers>

namespace avgen::quality {
namespace {

constexpr std::uint32_t kW = 128;
constexpr std::uint32_t kH = 96;
constexpr std::uint32_t kSeqW = 96;
constexpr std::uint32_t kSeqH = 64;

std::uint8_t clamp8(double v) {
    return static_cast<std::uint8_t>(std::clamp(v, 0.0, 255.0));
}

Frame makeFrame(std::uint32_t w, std::uint32_t h) {
    Frame f;
    f.width = w;
    f.height = h;
    f.rgba.assign(static_cast<std::size_t>(w) * h * 4, 255);
    return f;
}

Plane constantPlane(std::uint32_t w, std::uint32_t h, float r, float g, float b, float a) {
    Plane p;
    p.width = w;
    p.height = h;
    p.rgba.assign(static_cast<std::size_t>(w) * h * 4, 0.0f);
    for (std::size_t i = 0; i < p.rgba.size(); i += 4) {
        p.rgba[i] = r;
        p.rgba[i + 1] = g;
        p.rgba[i + 2] = b;
        p.rgba[i + 3] = a;
    }
    return p;
}

// The sequence content: a DIAGONAL sinusoidal grating over a static colour wash.
//
// A sinusoid because the discrete Laplacian of sin(theta) is proportional to sin(theta), so the mean
// absolute Laplacian of the frame is the mean of |sin| over whatever phases the sampling grid
// visits. That is what lets the shimmer arm change the temporal measure while leaving the spatial
// one alone -- with a resampled shift instead, the shifted frame would also be blurrier and the arm
// would be testing two things at once.
//
// **Diagonal, and the reason is measured.** A grating that varies in x alone has eight sample phases
// per period, and the mean of |sin| over eight points still depends on where those points sit: the
// first version of this arm moved 7.1% between two sub-pixel phases, against a control that allows
// 5%. Adding a vertical period of sixteen pixels makes the grid visit sixteen phases instead of
// eight, and the residual dependence falls to well under one percent -- so the control can be tight
// enough to mean something. The arm's first failure is what found this.
double gratingLuma(double x, double y, double phase) {
    constexpr double kCyclesPerPixelX = 0.125;  // period 8
    constexpr double kCyclesPerPixelY = 0.0625; // period 16
    return 128.0 + 90.0 * std::sin(2.0 * std::numbers::pi *
                                       (kCyclesPerPixelX * x + kCyclesPerPixelY * y) +
                                   phase);
}

Frame gratingFrame(double phasePixels) {
    Frame f = makeFrame(kSeqW, kSeqH);
    constexpr double kCyclesPerPixelX = 0.125;
    const double phase = -2.0 * std::numbers::pi * kCyclesPerPixelX * phasePixels;
    for (std::uint32_t y = 0; y < kSeqH; ++y) {
        for (std::uint32_t x = 0; x < kSeqW; ++x) {
            const double v = gratingLuma(static_cast<double>(x), static_cast<double>(y), phase);
            std::uint8_t* p = f.rgba.data() + (static_cast<std::size_t>(y) * kSeqW + x) * 4;
            // A vertical tint so the frame is not separable in y, which would let a warp with the
            // wrong axis look correct.
            const double tint = 0.6 + 0.4 * (static_cast<double>(y) / static_cast<double>(kSeqH));
            p[0] = clamp8(v * tint);
            p[1] = clamp8(v);
            p[2] = clamp8(v * (1.6 - tint));
            p[3] = 255;
        }
    }
    return f;
}

Plane uniformVelocity(double pixelsPerFrameX) {
    Plane p = constantPlane(kSeqW, kSeqH, 0.0f, 0.0f, 0.0f, 0.0f);
    const auto vx = static_cast<float>(pixelsPerFrameX / static_cast<double>(kSeqW));
    for (std::size_t i = 0; i < p.rgba.size(); i += 4) {
        p.rgba[i] = vx;
        p.rgba[i + 1] = 0.0f;
    }
    return p;
}

void check(Arm& arm, std::string description, std::string expectation, bool passed, double observed,
           double reference, bool mustMove) {
    Check c;
    c.description = std::move(description);
    c.expectation = std::move(expectation);
    c.observed = observed;
    c.reference = reference;
    c.mustMove = mustMove;
    c.passed = passed;
    arm.checks.push_back(std::move(c));
}

// A frame pair's residual, with the disocclusion policy the sequence's AOVs support.
MotionResidual residualAt(const SyntheticSequence& sequence, std::size_t index,
                          const DisocclusionPolicy& policy) {
    MotionInputs inputs;
    inputs.previous = &sequence.frames[index - 1];
    inputs.current = &sequence.frames[index];
    inputs.velocity = &sequence.velocity[index];
    if (!sequence.depth.empty()) {
        inputs.depthPrevious = &sequence.depth[index - 1];
        inputs.depthCurrent = &sequence.depth[index];
    }
    if (!sequence.identifier.empty()) {
        inputs.idPrevious = &sequence.identifier[index - 1];
        inputs.idCurrent = &sequence.identifier[index];
    }
    return motionCompensatedResidual(inputs, policy);
}

double meanResidual(const SyntheticSequence& sequence, const DisocclusionPolicy& policy) {
    double total = 0.0;
    std::size_t counted = 0;
    for (std::size_t i = 1; i < sequence.frames.size(); ++i) {
        total += residualAt(sequence, i, policy).residual;
        ++counted;
    }
    return counted > 0 ? total / static_cast<double>(counted) : 0.0;
}

double meanDisocclusion(const SyntheticSequence& sequence, const DisocclusionPolicy& policy) {
    double total = 0.0;
    std::size_t counted = 0;
    for (std::size_t i = 1; i < sequence.frames.size(); ++i) {
        total += residualAt(sequence, i, policy).disocclusionFraction;
        ++counted;
    }
    return counted > 0 ? total / static_cast<double>(counted) : 0.0;
}

double meanAlternation(const SyntheticSequence& sequence) {
    double total = 0.0;
    std::size_t counted = 0;
    for (std::size_t i = 1; i + 1 < sequence.frames.size(); ++i) {
        total += temporalAlternation(sequence.frames[i - 1], sequence.frames[i],
                                     sequence.frames[i + 1])
                     .mean;
        ++counted;
    }
    return counted > 0 ? total / static_cast<double>(counted) : 0.0;
}

} // namespace

// ---- the spatial arms --------------------------------------------------------------------------

Frame ladderBase() {
    Frame f = makeFrame(kW, kH);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            std::uint8_t* p = f.rgba.data() + (static_cast<std::size_t>(y) * kW + x) * 4;
            if (y < kH / 2) {
                const auto v = static_cast<std::uint8_t>((x * 255) / (kW - 1));
                p[0] = v;
                p[1] = v;
                p[2] = 200;
            } else {
                const bool on = (x % 2) == 0;
                p[0] = on ? 240 : 20;
                p[1] = on ? 60 : 200;
                p[2] = on ? 40 : 90;
            }
            p[3] = 255;
        }
    }
    return f;
}

Frame ladderBlurred(const Frame& source, double sigma) {
    const int radius = std::max(1, static_cast<int>(std::ceil(sigma * 3.0)));
    std::vector<double> kernel;
    double total = 0.0;
    for (int i = -radius; i <= radius; ++i) {
        const double w = std::exp(-(i * i) / (2.0 * sigma * sigma));
        kernel.push_back(w);
        total += w;
    }
    for (double& w : kernel) {
        w /= total;
    }
    Frame out = source;
    for (int axis = 0; axis < 2; ++axis) {
        const Frame in = out;
        for (std::uint32_t y = 0; y < source.height; ++y) {
            for (std::uint32_t x = 0; x < source.width; ++x) {
                double acc[3] = {0.0, 0.0, 0.0};
                for (int k = -radius; k <= radius; ++k) {
                    const int sx = axis == 0 ? std::clamp(static_cast<int>(x) + k, 0,
                                                          static_cast<int>(source.width) - 1)
                                             : static_cast<int>(x);
                    const int sy = axis == 1 ? std::clamp(static_cast<int>(y) + k, 0,
                                                          static_cast<int>(source.height) - 1)
                                             : static_cast<int>(y);
                    const std::uint8_t* p =
                        in.pixel(static_cast<std::uint32_t>(sx), static_cast<std::uint32_t>(sy));
                    const double w = kernel[static_cast<std::size_t>(k + radius)];
                    for (int c = 0; c < 3; ++c) {
                        acc[c] += w * p[c];
                    }
                }
                std::uint8_t* q =
                    out.rgba.data() + (static_cast<std::size_t>(y) * source.width + x) * 4;
                for (int c = 0; c < 3; ++c) {
                    q[c] = clamp8(acc[c]);
                }
            }
        }
    }
    return out;
}

Frame ladderQuantised(const Frame& source, int bits) {
    Frame out = source;
    const int levels = 1 << bits;
    for (std::size_t i = 0; i < out.rgba.size(); i += 4) {
        for (int c = 0; c < 3; ++c) {
            const int v = out.rgba[i + c] * (levels - 1) / 255;
            out.rgba[i + c] = static_cast<std::uint8_t>(v * 255 / (levels - 1));
        }
    }
    return out;
}

Frame ladderExposed(const Frame& source, double scale) {
    Frame out = source;
    for (std::size_t i = 0; i < out.rgba.size(); i += 4) {
        for (int c = 0; c < 3; ++c) {
            out.rgba[i + c] = clamp8(out.rgba[i + c] * scale);
        }
    }
    return out;
}

Frame ladderColourShifted(const Frame& source, int amount) {
    Frame out = source;
    for (std::size_t i = 0; i < out.rgba.size(); i += 4) {
        out.rgba[i + 0] = clamp8(out.rgba[i + 0] + amount);
        out.rgba[i + 1] = clamp8(out.rgba[i + 1] - amount);
    }
    return out;
}

Frame ladderSharpened(const Frame& source, double amount) {
    const Frame blurred = ladderBlurred(source, 1.0);
    Frame out = source;
    for (std::size_t i = 0; i < out.rgba.size(); i += 4) {
        for (int c = 0; c < 3; ++c) {
            const double sharp = source.rgba[i + c] +
                                 amount * (static_cast<double>(source.rgba[i + c]) -
                                           static_cast<double>(blurred.rgba[i + c]));
            out.rgba[i + c] = clamp8(sharp);
        }
    }
    return out;
}

Frame ladderDiagonalEdge(bool antialiased) {
    Frame f = makeFrame(kW, kH);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const double d = static_cast<double>(y) - (0.35 * static_cast<double>(x) + 12.0);
            const double coverage =
                antialiased ? std::clamp(0.5 - d, 0.0, 1.0) : (d < 0.0 ? 1.0 : 0.0);
            std::uint8_t* p = f.rgba.data() + (static_cast<std::size_t>(y) * kW + x) * 4;
            const std::uint8_t v = clamp8(coverage * 235.0 + 10.0);
            p[0] = v;
            p[1] = v;
            p[2] = v;
            p[3] = 255;
        }
    }
    return f;
}

Frame ladderSupersampledEdge(std::uint32_t factor) {
    const std::uint32_t f = std::max(1u, factor);
    // Render the hard-edged version at f times the resolution, then box-average down. This is what
    // `--supersample` does and it is deliberately NOT the coverage-antialiased arm: the point is
    // that more samples of a hard edge converge to coverage, which is why both are calmer than the
    // unfiltered one and why ADR-243's reviewer preferred both.
    Frame big = makeFrame(kW * f, kH * f);
    for (std::uint32_t y = 0; y < kH * f; ++y) {
        for (std::uint32_t x = 0; x < kW * f; ++x) {
            const double fx = (static_cast<double>(x) + 0.5) / f - 0.5;
            const double fy = (static_cast<double>(y) + 0.5) / f - 0.5;
            const double d = fy - (0.35 * fx + 12.0);
            std::uint8_t* p = big.rgba.data() + (static_cast<std::size_t>(y) * kW * f + x) * 4;
            const std::uint8_t v = clamp8((d < 0.0 ? 1.0 : 0.0) * 235.0 + 10.0);
            p[0] = v;
            p[1] = v;
            p[2] = v;
            p[3] = 255;
        }
    }
    Frame out = makeFrame(kW, kH);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            double acc[3] = {0.0, 0.0, 0.0};
            for (std::uint32_t j = 0; j < f; ++j) {
                for (std::uint32_t i = 0; i < f; ++i) {
                    const std::uint8_t* p = big.pixel(x * f + i, y * f + j);
                    for (int c = 0; c < 3; ++c) {
                        acc[c] += p[c];
                    }
                }
            }
            std::uint8_t* q = out.rgba.data() + (static_cast<std::size_t>(y) * kW + x) * 4;
            for (int c = 0; c < 3; ++c) {
                q[c] = clamp8(acc[c] / static_cast<double>(f * f));
            }
        }
    }
    return out;
}

Frame ladderGradient() {
    Frame f = makeFrame(kW * 4, kH);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW * 4; ++x) {
            // 64 codes over 512 pixels: eight pixels per code, which is a visible band.
            const auto v = static_cast<std::uint8_t>(16 + (x * 64) / (kW * 4));
            std::uint8_t* p = f.rgba.data() + (static_cast<std::size_t>(y) * kW * 4 + x) * 4;
            p[0] = v;
            p[1] = v;
            p[2] = v;
            p[3] = 255;
        }
    }
    return f;
}

Frame ladderDithered(const Frame& source) {
    Frame out = source;
    // A deterministic hash-based dither at +-2 codes, ROUNDED rather than truncated.
    //
    // Three things here were found by the arm failing rather than designed in, and each one is a
    // fact about the instrument:
    //
    //   * `static_cast<uint8_t>` truncates toward zero, so a symmetric bias became a one-sided one:
    //     every pixel either stayed put or dropped a code. That is a brightness shift wearing a
    //     dither's clothes. Hence std::round.
    //   * +-1 code left CAMBI at 18.7 against the banded ramp's 23.0. One code of noise cannot
    //     decorrelate a band edge whose step is itself one code.
    //   * A 4x4 ORDERED dither at +-2 got CAMBI to 5.0 -- better, and still at the threshold where
    //     banding is "slightly annoying", because a 4x4 repeat is itself a structure CAMBI can
    //     find. Random dither of the same amplitude clears it. The lesson is the one a renderer
    //     wants: the pattern matters as much as the amplitude.
    const auto hash = [](std::uint32_t x, std::uint32_t y) {
        std::uint32_t h = x * 374761393u + y * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return (h ^ (h >> 16));
    };
    for (std::uint32_t y = 0; y < source.height; ++y) {
        for (std::uint32_t x = 0; x < source.width; ++x) {
            const double bias =
                (static_cast<double>(hash(x, y) & 0xFFFFu) / 65535.0 - 0.5) * 4.0;
            std::uint8_t* p = out.rgba.data() + (static_cast<std::size_t>(y) * source.width + x) * 4;
            for (int c = 0; c < 3; ++c) {
                p[c] = clamp8(std::round(static_cast<double>(p[c]) + bias));
            }
        }
    }
    return out;
}

// ---- the temporal arms -------------------------------------------------------------------------

SyntheticSequence staticScene(std::size_t frames) {
    SyntheticSequence sequence;
    sequence.name = "control: static scene";
    const Frame frame = gratingFrame(0.0);
    for (std::size_t i = 0; i < frames; ++i) {
        sequence.frames.push_back(frame);
        sequence.velocity.push_back(uniformVelocity(0.0));
        sequence.depth.push_back(constantPlane(kSeqW, kSeqH, 10.0f, 10.0f, 10.0f, 10.0f));
        sequence.identifier.push_back(constantPlane(kSeqW, kSeqH, 1.0f, 1.0f, 1.0f, 1.0f));
    }
    return sequence;
}

SyntheticSequence smoothMotion(std::size_t frames, double pixelsPerFrame) {
    SyntheticSequence sequence;
    sequence.name = "control: smooth motion";
    for (std::size_t i = 0; i < frames; ++i) {
        sequence.frames.push_back(gratingFrame(static_cast<double>(i) * pixelsPerFrame));
        sequence.velocity.push_back(uniformVelocity(pixelsPerFrame));
        sequence.depth.push_back(constantPlane(kSeqW, kSeqH, 10.0f, 10.0f, 10.0f, 10.0f));
        sequence.identifier.push_back(constantPlane(kSeqW, kSeqH, 1.0f, 1.0f, 1.0f, 1.0f));
    }
    return sequence;
}

SyntheticSequence shimmer(std::size_t frames) {
    SyntheticSequence sequence;
    sequence.name = "shimmer";
    for (std::size_t i = 0; i < frames; ++i) {
        // Two sub-pixel phases, alternating, while the velocity says nothing moved -- which is what
        // a sub-pixel-thin blade catching and missing the sample grid looks like to the renderer.
        sequence.frames.push_back(gratingFrame((i % 2) == 0 ? 0.0 : 0.45));
        sequence.velocity.push_back(uniformVelocity(0.0));
        sequence.depth.push_back(constantPlane(kSeqW, kSeqH, 10.0f, 10.0f, 10.0f, 10.0f));
        sequence.identifier.push_back(constantPlane(kSeqW, kSeqH, 1.0f, 1.0f, 1.0f, 1.0f));
    }
    return sequence;
}

SyntheticSequence ghosting(std::size_t frames) {
    SyntheticSequence sequence = smoothMotion(frames);
    sequence.name = "ghosting";
    for (std::size_t i = 1; i < sequence.frames.size(); ++i) {
        const Frame previous = sequence.frames[i - 1];
        Frame& current = sequence.frames[i];
        for (std::size_t k = 0; k < current.rgba.size(); k += 4) {
            for (int c = 0; c < 3; ++c) {
                current.rgba[k + c] = clamp8(0.6 * current.rgba[k + c] + 0.4 * previous.rgba[k + c]);
            }
        }
    }
    return sequence;
}

SyntheticSequence disocclusion(std::size_t frames) {
    SyntheticSequence sequence;
    sequence.name = "disocclusion";
    const std::uint32_t occluderX0 = kSeqW / 3;
    const std::uint32_t occluderX1 = occluderX0 + kSeqW / 6;
    for (std::size_t i = 0; i < frames; ++i) {
        Frame frame = gratingFrame(static_cast<double>(i));
        Plane velocity = uniformVelocity(1.0);
        Plane depth = constantPlane(kSeqW, kSeqH, 10.0f, 10.0f, 10.0f, 10.0f);
        Plane identifier = constantPlane(kSeqW, kSeqH, 1.0f, 1.0f, 1.0f, 1.0f);
        for (std::uint32_t y = 0; y < kSeqH; ++y) {
            for (std::uint32_t x = occluderX0; x < occluderX1; ++x) {
                const std::size_t k = static_cast<std::size_t>(y) * kSeqW + x;
                std::uint8_t* p = frame.rgba.data() + k * 4;
                p[0] = 30;
                p[1] = 30;
                p[2] = 30;
                // The occluder is static, nearer, and a different object. All three are true of a
                // real foreground and each one is a separate reason the pixels behind its trailing
                // edge have no valid history.
                float* v = velocity.at(x, y);
                v[0] = 0.0f;
                v[1] = 0.0f;
                float* d = depth.at(x, y);
                d[0] = d[1] = d[2] = d[3] = 2.0f;
                float* id = identifier.at(x, y);
                id[0] = id[1] = id[2] = id[3] = 2.0f;
            }
        }
        sequence.frames.push_back(std::move(frame));
        sequence.velocity.push_back(std::move(velocity));
        sequence.depth.push_back(std::move(depth));
        sequence.identifier.push_back(std::move(identifier));
    }
    return sequence;
}

SyntheticSequence identifierPop(std::size_t frames) {
    SyntheticSequence sequence = staticScene(frames);
    sequence.name = "identifier pop";
    const std::uint32_t x0 = kSeqW / 2;
    const std::uint32_t x1 = x0 + 12;
    for (std::size_t i = 0; i < sequence.frames.size(); ++i) {
        if ((i % 2) == 0) {
            continue;
        }
        for (std::uint32_t y = 0; y < kSeqH; ++y) {
            for (std::uint32_t x = x0; x < x1; ++x) {
                // The identifier swaps while the velocity says nothing moved and the depth is
                // unchanged: a geometric level swapping under a stationary surface.
                float* id = sequence.identifier[i].at(x, y);
                id[0] = id[1] = id[2] = id[3] = 7.0f;
                std::uint8_t* p =
                    sequence.frames[i].rgba.data() + (static_cast<std::size_t>(y) * kSeqW + x) * 4;
                for (int c = 0; c < 3; ++c) {
                    p[c] = clamp8(static_cast<double>(p[c]) * 0.75);
                }
            }
        }
    }
    return sequence;
}

SyntheticSequence shadingFlicker(std::size_t frames) {
    SyntheticSequence sequence = staticScene(frames);
    sequence.name = "shading flicker";
    const std::uint32_t x0 = 10;
    const std::uint32_t x1 = 40;
    for (std::size_t i = 0; i < sequence.frames.size(); ++i) {
        Plane normal = constantPlane(kSeqW, kSeqH, 0.0f, 0.0f, 1.0f, 0.6f);
        Plane emission = constantPlane(kSeqW, kSeqH, 0.0f, 0.0f, 0.0f, 0.0f);
        for (std::uint32_t y = 0; y < kSeqH; ++y) {
            for (std::uint32_t x = x0; x < x1; ++x) {
                // Smooth and emissive: in the specular mask by both halves of it.
                float* n = normal.at(x, y);
                n[3] = 0.05f;
                float* e = emission.at(x, y);
                e[0] = e[1] = e[2] = 1.5f;
                if ((i % 2) == 1) {
                    std::uint8_t* p = sequence.frames[i].rgba.data() +
                                      (static_cast<std::size_t>(y) * kSeqW + x) * 4;
                    for (int c = 0; c < 3; ++c) {
                        p[c] = clamp8(static_cast<double>(p[c]) * 0.6);
                    }
                }
            }
        }
        sequence.normal.push_back(std::move(normal));
        sequence.emission.push_back(std::move(emission));
    }
    return sequence;
}

// ---- running the ladder ------------------------------------------------------------------------

bool Arm::passed() const {
    return std::all_of(checks.begin(), checks.end(), [](const Check& c) { return c.passed; });
}

bool LadderResult::passed() const {
    return std::all_of(arms.begin(), arms.end(), [](const Arm& a) { return a.passed(); });
}

std::size_t LadderResult::failedChecks() const {
    std::size_t failed = 0;
    for (const Arm& arm : arms) {
        for (const Check& c : arm.checks) {
            failed += c.passed ? 0u : 1u;
        }
    }
    return failed;
}

std::size_t LadderResult::totalChecks() const {
    std::size_t total = 0;
    for (const Arm& arm : arms) {
        total += arm.checks.size();
    }
    return total;
}

MetricBindings realMetrics() {
    MetricBindings bindings;
    bindings.spatialLaplacian = [](const Frame& f) { return spatialLaplacian(f); };
    bindings.quantisationSteps = [](const Frame& f) { return quantisationSteps(f); };
    bindings.msSsim = [](const Frame& a, const Frame& b) { return msSsim(a, b); };
    bindings.psnr = [](const Frame& a, const Frame& b) { return psnr(a, b); };
    bindings.ciede2000 = [](const Frame& a, const Frame& b) { return ciede2000(a, b); };
    return bindings;
}

LadderResult runSpatialLadder(const MetricBindings& metrics) {
    LadderResult result;
    const Frame base = ladderBase();

    {
        Arm arm;
        arm.name = "control: identity";
        arm.construction = "the same image twice";
        const double p = metrics.psnr(base, base);
        check(arm, "PSNR of an identical pair is infinite", "psnr == inf", std::isinf(p) && p > 0.0,
              p, 0.0, false);
        const double ms = metrics.msSsim(base, base);
        check(arm, "MS-SSIM of an identical pair is 1", "msSsim == 1",
              std::abs(ms - 1.0) < 1e-9, ms, 1.0, false);
        const ColourDifference d = metrics.ciede2000(base, base);
        check(arm, "CIEDE2000 of an identical pair is 0", "dE00 == 0", d.mean < 1e-9, d.mean, 0.0,
              false);
        result.arms.push_back(std::move(arm));
    }

    {
        Arm arm;
        arm.name = "blur";
        arm.construction = "separable Gaussian, sigma 0.5 and 3.0";
        const Frame slight = ladderBlurred(base, 0.5);
        const Frame severe = ladderBlurred(base, 3.0);
        const double b0 = metrics.spatialLaplacian(base);
        const double b1 = metrics.spatialLaplacian(slight);
        const double b2 = metrics.spatialLaplacian(severe);
        check(arm, "a slight blur removes high-frequency energy", "laplacian(slight) < base",
              b1 < b0, b1, b0, true);
        check(arm, "a heavy blur removes more", "laplacian(severe) < slight", b2 < b1, b2, b1, true);
        check(arm, "MS-SSIM falls further for the heavier blur", "msSsim(severe) < msSsim(slight)",
              metrics.msSsim(base, severe) < metrics.msSsim(base, slight),
              metrics.msSsim(base, severe), metrics.msSsim(base, slight), true);
        result.arms.push_back(std::move(arm));
    }

    {
        Arm arm;
        arm.name = "aliasing";
        arm.construction = "a shallow diagonal with coverage AA, against the same edge snapped to "
                           "whole pixels";
        const Frame filtered = ladderDiagonalEdge(true);
        const Frame jagged = ladderDiagonalEdge(false);
        const double smooth = metrics.spatialLaplacian(filtered);
        const double stairs = metrics.spatialLaplacian(jagged);
        check(arm, "an unfiltered edge has MORE high-frequency energy, not less",
              "laplacian(jagged) > laplacian(filtered)", stairs > smooth, stairs, smooth, true);
        check(arm, "MS-SSIM sees a structural difference", "msSsim < 1",
              metrics.msSsim(filtered, jagged) < 1.0, metrics.msSsim(filtered, jagged), 1.0, true);
        const ColourDifference d = metrics.ciede2000(filtered, jagged);
        check(arm, "an edge treatment is not a colour change", "dE00 mean < 8", d.mean < 8.0,
              d.mean, 8.0, false);
        result.arms.push_back(std::move(arm));
    }

    {
        // metrics.md §4.2. ADR-243's reviewer ranked these three and the answer is already known;
        // a metric set that orders them differently is wrong, and the fix is the metric.
        //
        // **This is a check on the metric's direction, not on the renderer.** The arms are
        // synthetic edges, not Glowmere's grass, and no synthetic arm can substitute for the
        // person who said "more distracting".
        Arm arm;
        arm.name = "direction check (ADR-243's three arms, synthesised)";
        arm.construction = "AA off / AA on / the same edge supersampled 4x and box-resolved";
        const double off = metrics.spatialLaplacian(ladderDiagonalEdge(false));
        const double on = metrics.spatialLaplacian(ladderDiagonalEdge(true));
        const double super = metrics.spatialLaplacian(ladderSupersampledEdge(4));
        check(arm, "AA off scores worse than the baseline, as the reviewer said",
              "laplacian(off) > laplacian(on)", off > on, off, on, true);
        check(arm, "supersampling scores better than the baseline, as the reviewer said",
              "laplacian(supersampled) <= laplacian(on)", super <= on, super, on, true);
        result.arms.push_back(std::move(arm));
    }

    {
        Arm arm;
        arm.name = "over-sharpen";
        arm.construction = "unsharp mask, amount 1.5";
        const Frame sharp = ladderSharpened(base, 1.5);
        const double s = metrics.spatialLaplacian(sharp);
        const double b = metrics.spatialLaplacian(base);
        check(arm, "sharpening adds high-frequency energy -- the SAME direction as aliasing",
              "laplacian(sharpened) > base", s > b, s, b, true);
        check(arm, "and it is a real difference", "msSsim < 1", metrics.msSsim(base, sharp) < 1.0,
              metrics.msSsim(base, sharp), 1.0, true);
        result.arms.push_back(std::move(arm));
    }

    {
        Arm arm;
        arm.name = "banding (coarse posterisation)";
        arm.construction = "the base image quantised to 5 bits -- steps of about 8 luma";
        const Frame base5 = ladderQuantised(base, 5);
        const double baseSteps = metrics.quantisationSteps(base);
        const double bandedSteps = metrics.quantisationSteps(base5);
        check(arm, "the native banding proxy rises on a posterised gradient",
              "steps(quantised) > steps(base)", bandedSteps > baseSteps, bandedSteps, baseSteps,
              true);
        const double ratio =
            metrics.spatialLaplacian(base5) / std::max(1e-9, metrics.spatialLaplacian(base));
        check(arm, "quantising does not sharpen or soften the detail below the gradient",
              "laplacian ratio within 0.35 of 1", std::abs(ratio - 1.0) < 0.35, ratio, 1.0, false);
        const Frame severeBlur = ladderBlurred(base, 3.0);
        check(arm, "and blurring does not invent banding",
              "steps(blurred) <= steps(base) + 0.01",
              metrics.quantisationSteps(severeBlur) <= baseSteps + 0.01,
              metrics.quantisationSteps(severeBlur), baseSteps, false);
        result.arms.push_back(std::move(arm));
    }

    {
        // **Two recorded failures of the native banding proxy, asserted so they cannot drift.**
        //
        // metrics.md §4.1 specified one banding arm -- "quantise to 5 bits on a gradient region,
        // cambi rises sharply". Implementing it found that BOTH candidate banding metrics have a
        // band-limited response, they do not overlap, and the native one is worse than
        // band-limited:
        //
        //   * `quantisationSteps` looks for a 3-to-12 luma step in an otherwise flat
        //     neighbourhood. Ordinary 8-bit banding steps by ONE code, so it does not see it.
        //   * Worse, a +-2 code dither -- the remedy a renderer applies, which took CAMBI from
        //     22.99 to 0.00 on this exact pair -- LOOKS like a 3-to-12 luma step to it, so it
        //     ranks the remedy as 3% banded against the artifact's 0%. **It is anti-correlated
        //     with the fix on this artifact class**, which is ADR-243's shape in a third metric.
        //   * CAMBI is the opposite blind spot: measured here, the same ramp quantised to 17-code
        //     steps scores ~0, because a step above its max_log_contrast reads as a genuine edge
        //     (ADR-252).
        //
        // So the metric is KEPT, because it answers for the coarse band CAMBI cannot see, and it is
        // scoped: CAMBI is the banding number wherever libvmaf exists, and the report carries both
        // of these failures in `quantisationSteps`'s limitations. metrics.md §4.3 is explicit that
        // this is what to do -- record it and keep or drop on the merits, never quietly reweight.
        Arm arm;
        arm.name = "banding: the native proxy's two recorded failures";
        arm.construction = "a smooth single-code 8-bit ramp against its hash-dithered twin";
        const Frame banded = ladderGradient();
        const Frame dithered = ladderDithered(banded);
        const double bandedSteps = metrics.quantisationSteps(banded);
        const double ditheredSteps = metrics.quantisationSteps(dithered);
        check(arm,
              "failure 1: the native proxy is blind to single-code banding. CAMBI scores this same "
              "frame 22.99",
              "steps(banded) < 0.01", bandedSteps < 0.01, bandedSteps, 0.01, false);
        check(arm,
              "failure 2: and it scores the REMEDY as banding. CAMBI scores the dithered frame "
              "0.00. A banding decision taken on this number alone would be backwards",
              "steps(dithered) > steps(banded)", ditheredSteps > bandedSteps, ditheredSteps,
              bandedSteps, true);
        result.arms.push_back(std::move(arm));
    }

    {
        Arm arm;
        arm.name = "colour shift";
        arm.construction = "+12 red, -12 green, leaving luma structure alone";
        const Frame shifted = ladderColourShifted(base, 12);
        const ColourDifference d = metrics.ciede2000(base, shifted);
        check(arm, "the colour metric moves", "dE00 mean > 1", d.mean > 1.0, d.mean, 1.0, true);
        const double ratio = metrics.spatialLaplacian(shifted) /
                             std::max(1e-9, metrics.spatialLaplacian(base));
        check(arm, "and the structural one does not -- which is why it is not redundant",
              "laplacian ratio within 0.1 of 1", std::abs(ratio - 1.0) < 0.1, ratio, 1.0, false);
        result.arms.push_back(std::move(arm));
    }

    {
        Arm arm;
        arm.name = "exposure";
        arm.construction = "x1.05 linear -- a grade, not a defect";
        const Frame brighter = ladderExposed(base, 1.05);
        const double p = metrics.psnr(base, brighter);
        const double ms = metrics.msSsim(base, brighter);
        check(arm, "PSNR collapses, because every pixel moved", "psnr < 45 dB", p < 45.0, p, 45.0,
              true);
        check(arm, "MS-SSIM barely moves, because it is the same picture", "msSsim > 0.95",
              ms > 0.95, ms, 0.95, false);
        result.arms.push_back(std::move(arm));
    }

    return result;
}

LadderResult runTemporalLadder() {
    LadderResult result;
    DisocclusionPolicy policy;
    const SyntheticSequence still = staticScene(6);
    const SyntheticSequence smooth = smoothMotion(6, 1.0);
    const SyntheticSequence subPixel = smoothMotion(6, 0.5);
    const SyntheticSequence shimmering = shimmer(6);
    const SyntheticSequence ghosted = ghosting(6);
    const SyntheticSequence occluded = disocclusion(6);

    const double stillResidual = meanResidual(still, policy);
    const double smoothResidual = meanResidual(smooth, policy);
    const double floor = warpFloor(subPixel.frames[0], subPixel.velocity[0]);

    {
        Arm arm;
        arm.name = "control: zero velocity";
        arm.construction = "a static scene, static camera, six frames";
        check(arm, "the residual is zero when nothing moved", "residual < 0.01",
              stillResidual < 0.01, stillResidual, 0.01, false);
        check(arm, "and nothing is masked out", "disocclusionFraction < 0.001",
              meanDisocclusion(still, policy) < 0.001, meanDisocclusion(still, policy), 0.001,
              false);
        check(arm, "the temporal alternation is also zero", "alternation < 0.01",
              meanAlternation(still) < 0.01, meanAlternation(still), 0.01, false);
        result.arms.push_back(std::move(arm));
    }

    {
        // **The arm that makes every other one non-vacuous, and the arm that is ADR-243 in
        // miniature.** Content translating one pixel per frame is authored motion, not a defect.
        // The motion-compensated residual reports ~0. The temporal alternation reports a LARGE
        // number, because the second difference in time of a translating sinusoid is not zero --
        // which is exactly how a correct temporal measure came to rank two anti-aliasing remedies
        // backwards against a human.
        Arm arm;
        arm.name = "control: smooth motion";
        arm.construction = "a grating translating exactly 1 px/frame, with the velocity that says so";
        check(arm, "the motion-compensated residual is ~zero on authored motion", "residual < 0.5",
              smoothResidual < 0.5, smoothResidual, 0.5, false);
        check(arm, "the disocclusion mask does not fire on pure translation",
              "disocclusionFraction < 0.05", meanDisocclusion(smooth, policy) < 0.05,
              meanDisocclusion(smooth, policy), 0.05, false);
        check(arm,
              "and temporal alternation is LARGE on the same footage -- authored motion reads as "
              "instability to a second-difference measure (ADR-243)",
              "alternation > 20", meanAlternation(smooth) > 20.0, meanAlternation(smooth), 20.0,
              true);
        result.arms.push_back(std::move(arm));
    }

    {
        Arm arm;
        arm.name = "warp floor";
        arm.construction = "a half-pixel translation warped back and forward with the same velocity";
        check(arm, "a sub-pixel warp costs something, and the number is measured rather than assumed",
              "0 < floor < 12", floor > 0.0 && floor < 12.0, floor, 0.0, true);
        result.arms.push_back(std::move(arm));
    }

    {
        Arm arm;
        arm.name = "shimmer";
        arm.construction = "two sub-pixel phases of one grating, alternating, velocity zero";
        const double residual = meanResidual(shimmering, policy);
        check(arm, "the residual rises sharply over the static control",
              "residual > 10x the static control", residual > 10.0 * std::max(stillResidual, 0.01),
              residual, stillResidual, true);
        const double a = spatialLaplacian(shimmering.frames[0]);
        const double b = spatialLaplacian(shimmering.frames[1]);
        check(arm,
              "and the per-frame spatial energy does NOT change -- so this arm is shimmer and not "
              "blur",
              "laplacian ratio within 0.05 of 1", std::abs(b / std::max(1e-9, a) - 1.0) < 0.05,
              b / std::max(1e-9, a), 1.0, false);
        result.arms.push_back(std::move(arm));
    }

    {
        Arm arm;
        arm.name = "ghosting";
        arm.construction = "each frame blended 60/40 with its predecessor, on smooth motion";
        const double residual = meanResidual(ghosted, policy);
        check(arm, "the residual rises over the smooth-motion control",
              "residual > 5x the smooth-motion control",
              residual > 5.0 * std::max(smoothResidual, 0.05), residual, smoothResidual, true);
        result.arms.push_back(std::move(arm));
    }

    {
        // The arm the whole detector rests on. A foreground occluder disoccludes the pixels behind
        // its trailing edge; unmasked they are pure invention and dominate the mean. The assertion
        // is not "the residual is small" -- it is that MASKING is what makes it small, which is a
        // property only a working mask has.
        Arm arm;
        arm.name = "disocclusion";
        arm.construction = "smooth motion past a static foreground bar, with id and depth AOVs";
        const double masked = meanResidual(occluded, policy);
        DisocclusionPolicy none;
        none.useIdentifier = false;
        none.useDepth = false;
        const double unmasked = meanResidual(occluded, none);
        const double fraction = meanDisocclusion(occluded, policy);
        check(arm, "the mask fires", "disocclusionFraction > 0.01", fraction > 0.01, fraction, 0.01,
              true);
        check(arm, "and it is not swallowing the frame", "disocclusionFraction < 0.5",
              fraction < 0.5, fraction, 0.5, false);
        check(arm,
              "masking is what makes the residual small: unmasked it is dominated by pixels with "
              "no history",
              "unmasked > 2x masked", unmasked > 2.0 * std::max(masked, 1e-6), unmasked, masked,
              true);
        check(arm, "and the masked residual stays near the smooth-motion control",
              "masked < 4x the smooth-motion control",
              masked < 4.0 * std::max(smoothResidual, 0.25), masked, smoothResidual, false);
        result.arms.push_back(std::move(arm));
    }

    return result;
}


namespace {

// Writes `frame` repeated `count` times into `directory`, which is how a still becomes a sequence
// libvmaf will accept. Four frames rather than one because VMAF's motion feature has no previous
// frame on frame 0 and the arms below need at least one frame that is scored normally.
Result<std::vector<std::filesystem::path>> writeStill(const Frame& frame,
                                                      const std::filesystem::path& directory,
                                                      std::size_t count) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    std::vector<std::filesystem::path> paths;
    for (std::size_t i = 0; i < count; ++i) {
        const std::filesystem::path path = directory / fmt::format("frame_{:06d}.png", i);
        if (auto written = writeFrame(path, frame); !written) {
            return std::unexpected(written.error());
        }
        paths.push_back(path);
    }
    return paths;
}

} // namespace

LadderResult runExternalLadder(const std::filesystem::path& workDirectory) {
    LadderResult result;
    const ExternalTool tool = findVmafTool();
    if (!tool.available) {
        return result;
    }
    std::error_code ec;
    const std::filesystem::path root = workDirectory / "external-ladder";
    std::filesystem::remove_all(root, ec);

    const Frame banded = ladderGradient();
    const Frame dithered = ladderDithered(banded);
    const Frame posterised = ladderQuantised(banded, 4); // steps of 17 codes

    auto bandedFrames = writeStill(banded, root / "banded", 4);
    auto ditheredFrames = writeStill(dithered, root / "dithered", 4);
    auto posterisedFrames = writeStill(posterised, root / "posterised", 4);
    if (!bandedFrames || !ditheredFrames || !posterisedFrames) {
        return result;
    }

    const auto measure = [&](const std::vector<std::filesystem::path>& candidate,
                             const std::string& model) {
        VmafRequest request;
        request.candidateFrames = candidate;
        request.referenceFrames = *bandedFrames;
        request.workDirectory = root;
        request.model = model;
        return runVmaf(tool, request);
    };

    const std::string neg = "version=vmaf_v0.6.1neg";
    const std::string plain = "version=vmaf_v0.6.1";
    auto bandedNeg = measure(*bandedFrames, neg);
    auto ditheredNeg = measure(*ditheredFrames, neg);
    auto posterisedNeg = measure(*posterisedFrames, neg);
    auto bandedPlain = measure(*bandedFrames, plain);
    auto posterisedPlain = measure(*posterisedFrames, plain);
    if (!bandedNeg || !ditheredNeg || !posterisedNeg || !bandedPlain || !posterisedPlain) {
        return result;
    }

    {
        // CAMBI in its own domain, and the direction that matters: the remedy for banding is
        // dithering, and CAMBI must rank the dithered frame better.
        Arm arm;
        arm.name = "CAMBI: single-code banding and its remedy";
        arm.construction = "a smooth 8-bit ramp against its 4x4-ordered-dithered twin";
        const double bandedScore = bandedNeg->cambi.mean;
        const double ditheredScore = ditheredNeg->cambi.mean;
        check(arm, "a single-code 8-bit ramp scores as banded", "cambi > 4", bandedScore > 4.0,
              bandedScore, 4.0, true);
        check(arm, "and dithering it -- the remedy a renderer would apply -- scores clean",
              "cambi(dithered) < 1", ditheredScore < 1.0, ditheredScore, 1.0, true);
        result.arms.push_back(std::move(arm));
    }
    {
        // The blind spot, pinned. metrics.md §4.1 asked for the opposite of this.
        Arm arm;
        arm.name = "CAMBI blind spot: coarse posterisation (recorded, not fixed)";
        arm.construction = "the same ramp quantised to 4 bits -- steps of 17 codes";
        const double score = posterisedNeg->cambi.mean;
        check(arm,
              "a step above CAMBI's max_log_contrast reads as a genuine edge, not a band, so "
              "coarse posterisation scores ~0. The native quantisationSteps metric answers for "
              "this band instead",
              "cambi(posterised) < 1", score < 1.0, score, 1.0, false);
        result.arms.push_back(std::move(arm));
    }
    {
        // ADR-252. The default model's enhancement gain, demonstrated rather than cited.
        Arm arm;
        arm.name = "VMAF enhancement gain on the default model (recorded, not fixed)";
        arm.construction = "vmaf_v0.6.1 scoring a posterised ramp against the ramp itself, beside "
                           "the ramp scored against itself";
        const double identity = bandedPlain->vmaf.mean;
        const double defect = posterisedPlain->vmaf.mean;
        check(arm,
              "the default model scores a POSTERISED frame at least as well as an identical pair, "
              "because ADM reads added contrast as added detail",
              "vmaf(posterised) >= vmaf(identity)", defect >= identity, defect, identity, true);
        check(arm, "and an identical pair does not reach 100, because the motion feature is zero",
              "vmaf(identity) < 100", identity < 100.0, identity, 100.0, false);
        result.arms.push_back(std::move(arm));
    }
    {
        Arm arm;
        arm.name = "VMAF: the neg model removes the enhancement gain";
        arm.construction = "the same two arms under vmaf_v0.6.1neg, which is what the Lab uses";
        const double identity = bandedNeg->vmaf.mean;
        const double defect = posterisedNeg->vmaf.mean;
        check(arm, "under neg, the posterised frame scores WORSE than an identical pair",
              "vmaf_neg(posterised) < vmaf_neg(identity)", defect < identity, defect, identity,
              true);
        const double remedy = ditheredNeg->vmaf.mean;
        check(arm,
              "and even under neg, VMAF ranks the banding REMEDY below the banding -- it dislikes "
              "noise, and dither is noise. This is why VMAF may not arbitrate a renderer "
              "configuration (ADR-250, ADR-252)",
              "vmaf_neg(dithered) < vmaf_neg(identity)", remedy < identity, remedy, identity, true);
        result.arms.push_back(std::move(arm));
    }

    std::filesystem::remove_all(root, ec);
    return result;
}

} // namespace avgen::quality
