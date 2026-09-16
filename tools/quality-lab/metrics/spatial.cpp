#include "metrics/spatial.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace avgen::quality {
namespace {

constexpr double kL = 255.0;
constexpr double kC1 = (0.01 * kL) * (0.01 * kL);
constexpr double kC2 = (0.03 * kL) * (0.03 * kL);

double srgbToLinear(double c) {
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

struct Lab {
    double l = 0.0;
    double a = 0.0;
    double b = 0.0;
};

Lab toLab(std::uint8_t r8, std::uint8_t g8, std::uint8_t b8) {
    const double r = srgbToLinear(r8 / 255.0);
    const double g = srgbToLinear(g8 / 255.0);
    const double b = srgbToLinear(b8 / 255.0);
    // sRGB D65 primaries.
    const double x = (0.4124564 * r + 0.3575761 * g + 0.1804375 * b) / 0.95047;
    const double y = (0.2126729 * r + 0.7151522 * g + 0.0721750 * b) / 1.00000;
    const double z = (0.0193339 * r + 0.1191920 * g + 0.9503041 * b) / 1.08883;
    const auto f = [](double t) {
        return t > 216.0 / 24389.0 ? std::cbrt(t) : (841.0 / 108.0) * t + 4.0 / 29.0;
    };
    return {116.0 * f(y) - 16.0, 500.0 * (f(x) - f(y)), 200.0 * (f(y) - f(z))};
}

// Downsample by 2 with a 2x2 box, which is what MS-SSIM's scale pyramid specifies.
std::vector<float> halve(const std::vector<float>& plane, std::uint32_t w, std::uint32_t h,
                         std::uint32_t& outW, std::uint32_t& outH) {
    outW = std::max(1u, w / 2);
    outH = std::max(1u, h / 2);
    std::vector<float> out(static_cast<std::size_t>(outW) * outH, 0.0f);
    for (std::uint32_t y = 0; y < outH; ++y) {
        for (std::uint32_t x = 0; x < outW; ++x) {
            const std::uint32_t x0 = std::min(w - 1, x * 2);
            const std::uint32_t y0 = std::min(h - 1, y * 2);
            const std::uint32_t x1 = std::min(w - 1, x0 + 1);
            const std::uint32_t y1 = std::min(h - 1, y0 + 1);
            out[static_cast<std::size_t>(y) * outW + x] =
                0.25f * (plane[static_cast<std::size_t>(y0) * w + x0] +
                         plane[static_cast<std::size_t>(y0) * w + x1] +
                         plane[static_cast<std::size_t>(y1) * w + x0] +
                         plane[static_cast<std::size_t>(y1) * w + x1]);
        }
    }
    return out;
}

// SSIM over 8x8 windows on two 0..255 planes of the same size.
double ssimPlanes(const std::vector<float>& a, const std::vector<float>& b, std::uint32_t w,
                  std::uint32_t h) {
    constexpr std::uint32_t kWin = 8;
    if (w < kWin || h < kWin) {
        return 1.0;
    }
    double total = 0.0;
    std::size_t windows = 0;
    for (std::uint32_t y = 0; y + kWin <= h; y += kWin) {
        for (std::uint32_t x = 0; x + kWin <= w; x += kWin) {
            double sa = 0.0;
            double sb = 0.0;
            double saa = 0.0;
            double sbb = 0.0;
            double sab = 0.0;
            for (std::uint32_t j = 0; j < kWin; ++j) {
                for (std::uint32_t i = 0; i < kWin; ++i) {
                    const double va = static_cast<double>(a[static_cast<std::size_t>(y + j) * w + x + i]);
                    const double vb = static_cast<double>(b[static_cast<std::size_t>(y + j) * w + x + i]);
                    sa += va;
                    sb += vb;
                    saa += va * va;
                    sbb += vb * vb;
                    sab += va * vb;
                }
            }
            const double n = kWin * kWin;
            const double ma = sa / n;
            const double mb = sb / n;
            const double va = std::max(0.0, saa / n - ma * ma);
            const double vb = std::max(0.0, sbb / n - mb * mb);
            const double cov = sab / n - ma * mb;
            total += ((2.0 * ma * mb + kC1) * (2.0 * cov + kC2)) /
                     ((ma * ma + mb * mb + kC1) * (va + vb + kC2));
            ++windows;
        }
    }
    return windows == 0 ? 1.0 : total / static_cast<double>(windows);
}

double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) {
        return 0.0;
    }
    const auto index = static_cast<std::size_t>(fraction * static_cast<double>(values.size() - 1));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index),
                     values.end());
    return values[index];
}

} // namespace

std::vector<float> luma(const Frame& frame) {
    std::vector<float> out(static_cast<std::size_t>(frame.width) * frame.height, 0.0f);
    for (std::size_t i = 0; i < out.size(); ++i) {
        const std::uint8_t* p = frame.rgba.data() + i * 4;
        out[i] = static_cast<float>(0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2]);
    }
    return out;
}

double psnr(const Frame& a, const Frame& b) {
    if (!a.valid() || !a.sameShapeAs(b)) {
        return 0.0;
    }
    double sum = 0.0;
    std::size_t n = 0;
    for (std::size_t i = 0; i < a.rgba.size(); i += 4) {
        for (int c = 0; c < 3; ++c) {
            const double d = static_cast<double>(a.rgba[i + c]) - static_cast<double>(b.rgba[i + c]);
            sum += d * d;
            ++n;
        }
    }
    if (n == 0) {
        return 0.0;
    }
    const double mse = sum / static_cast<double>(n);
    // Identical frames are the alignment test's pass condition, and infinity is the honest answer:
    // clamping it to some large number would make "these are the same image" indistinguishable from
    // "these are very nearly the same image", which is the one distinction this metric is for.
    return mse <= 0.0 ? std::numeric_limits<double>::infinity()
                      : 10.0 * std::log10((kL * kL) / mse);
}

double ssim(const Frame& a, const Frame& b) {
    if (!a.valid() || !a.sameShapeAs(b)) {
        return 0.0;
    }
    return ssimPlanes(luma(a), luma(b), a.width, a.height);
}

double msSsim(const Frame& a, const Frame& b) {
    if (!a.valid() || !a.sameShapeAs(b)) {
        return 0.0;
    }
    // Wang 2003's five-scale exponents. They sum to 1, so an identical pair still gives exactly 1.
    static constexpr double kWeights[] = {0.0448, 0.2856, 0.3001, 0.2363, 0.1333};
    std::vector<float> pa = luma(a);
    std::vector<float> pb = luma(b);
    std::uint32_t w = a.width;
    std::uint32_t h = a.height;
    double product = 1.0;
    for (int scale = 0; scale < 5; ++scale) {
        const double s = std::max(0.0, ssimPlanes(pa, pb, w, h));
        product *= std::pow(s, kWeights[scale]);
        if (w < 16 || h < 16) {
            // Out of scales. The remaining exponents are dropped rather than applied to a 1-pixel
            // image, and the value is renormalised so a small frame is not penalised for its size.
            double used = 0.0;
            for (int k = 0; k <= scale; ++k) {
                used += kWeights[k];
            }
            return std::pow(product, 1.0 / used);
        }
        std::uint32_t nw = 0;
        std::uint32_t nh = 0;
        pa = halve(pa, w, h, nw, nh);
        pb = halve(pb, w, h, nw, nh);
        w = nw;
        h = nh;
    }
    return product;
}

ColourDifference ciede2000(const Frame& a, const Frame& b) {
    ColourDifference out;
    if (!a.valid() || !a.sameShapeAs(b)) {
        return out;
    }
    std::vector<double> deltas;
    deltas.reserve(static_cast<std::size_t>(a.width) * a.height);
    for (std::size_t i = 0; i < a.rgba.size(); i += 4) {
        const Lab la = toLab(a.rgba[i], a.rgba[i + 1], a.rgba[i + 2]);
        const Lab lb = toLab(b.rgba[i], b.rgba[i + 1], b.rgba[i + 2]);
        // CIEDE2000, Sharma/Wu/Dalal 2005 formulation.
        const double lBar = 0.5 * (la.l + lb.l);
        const double c1 = std::hypot(la.a, la.b);
        const double c2 = std::hypot(lb.a, lb.b);
        const double cBar = 0.5 * (c1 + c2);
        const double c7 = std::pow(cBar, 7.0);
        const double g = 0.5 * (1.0 - std::sqrt(c7 / (c7 + std::pow(25.0, 7.0))));
        const double a1p = (1.0 + g) * la.a;
        const double a2p = (1.0 + g) * lb.a;
        const double c1p = std::hypot(a1p, la.b);
        const double c2p = std::hypot(a2p, lb.b);
        const double cBarP = 0.5 * (c1p + c2p);
        const auto hue = [](double ap, double bp) {
            if (ap == 0.0 && bp == 0.0) {
                return 0.0;
            }
            double angle = std::atan2(bp, ap) * 180.0 / M_PI;
            return angle < 0.0 ? angle + 360.0 : angle;
        };
        const double h1p = hue(a1p, la.b);
        const double h2p = hue(a2p, lb.b);
        double dhp = 0.0;
        if (c1p * c2p != 0.0) {
            dhp = h2p - h1p;
            if (dhp > 180.0) dhp -= 360.0;
            if (dhp < -180.0) dhp += 360.0;
        }
        const double dLp = lb.l - la.l;
        const double dCp = c2p - c1p;
        const double dHp = 2.0 * std::sqrt(c1p * c2p) * std::sin(dhp * M_PI / 360.0);
        double hBarP = h1p + h2p;
        if (c1p * c2p != 0.0) {
            if (std::fabs(h1p - h2p) > 180.0) {
                hBarP += (hBarP < 360.0) ? 360.0 : -360.0;
            }
            hBarP *= 0.5;
        }
        const double t = 1.0 - 0.17 * std::cos((hBarP - 30.0) * M_PI / 180.0) +
                         0.24 * std::cos(2.0 * hBarP * M_PI / 180.0) +
                         0.32 * std::cos((3.0 * hBarP + 6.0) * M_PI / 180.0) -
                         0.20 * std::cos((4.0 * hBarP - 63.0) * M_PI / 180.0);
        const double sl = 1.0 + (0.015 * (lBar - 50.0) * (lBar - 50.0)) /
                                    std::sqrt(20.0 + (lBar - 50.0) * (lBar - 50.0));
        const double sc = 1.0 + 0.045 * cBarP;
        const double sh = 1.0 + 0.015 * cBarP * t;
        const double cBarP7 = std::pow(cBarP, 7.0);
        const double rt = -2.0 * std::sqrt(cBarP7 / (cBarP7 + std::pow(25.0, 7.0))) *
                          std::sin(60.0 * std::exp(-std::pow((hBarP - 275.0) / 25.0, 2.0)) * M_PI /
                                   180.0);
        const double dl = dLp / sl;
        const double dc = dCp / sc;
        const double dh = dHp / sh;
        deltas.push_back(std::sqrt(dl * dl + dc * dc + dh * dh + rt * dc * dh));
    }
    out.mean = deltas.empty() ? 0.0
                              : std::accumulate(deltas.begin(), deltas.end(), 0.0) /
                                    static_cast<double>(deltas.size());
    out.p95 = percentile(std::move(deltas), 0.95);
    return out;
}

double spatialLaplacian(const Frame& frame) {
    if (!frame.valid() || frame.width < 3 || frame.height < 3) {
        return 0.0;
    }
    const std::vector<float> plane = luma(frame);
    const std::uint32_t w = frame.width;
    double sum = 0.0;
    std::size_t n = 0;
    for (std::uint32_t y = 1; y + 1 < frame.height; ++y) {
        for (std::uint32_t x = 1; x + 1 < w; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * w + x;
            const double lap = 4.0 * static_cast<double>(plane[i]) -
                               static_cast<double>(plane[i - 1]) -
                               static_cast<double>(plane[i + 1]) -
                               static_cast<double>(plane[i - w]) -
                               static_cast<double>(plane[i + w]);
            sum += std::fabs(lap);
            ++n;
        }
    }
    return n == 0 ? 0.0 : sum / static_cast<double>(n);
}

double quantisationSteps(const Frame& frame) {
    if (!frame.valid() || frame.width < 3 || frame.height < 3) {
        return 0.0;
    }
    const std::vector<float> plane = luma(frame);
    const std::uint32_t w = frame.width;
    std::size_t stepped = 0;
    std::size_t n = 0;
    for (std::uint32_t y = 1; y + 1 < frame.height; ++y) {
        for (std::uint32_t x = 1; x + 1 < w; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * w + x;
            // A band edge is a place where luma steps by roughly one 5-bit level while its
            // neighbourhood is otherwise flat -- a gradient that should have been smooth arriving in
            // stairs. Texture fails the flatness test and dither fails the step test, which is what
            // keeps this from simply counting edges.
            const double left = static_cast<double>(plane[i - 1]);
            const double right = static_cast<double>(plane[i + 1]);
            const double step = std::fabs(right - left);
            const double curvature = std::fabs(2.0 * static_cast<double>(plane[i]) - left - right);
            if (step > 3.0 && step < 12.0 && curvature > 1.5) {
                ++stepped;
            }
            ++n;
        }
    }
    return n == 0 ? 0.0 : static_cast<double>(stepped) / static_cast<double>(n);
}

} // namespace avgen::quality
