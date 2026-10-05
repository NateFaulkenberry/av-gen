#include "metrics/ab.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace avgen::quality {
namespace {

Frame crop(const Frame& f, const Region& r) {
    Frame out;
    out.width = r.x1 - r.x0;
    out.height = r.y1 - r.y0;
    out.rgba.resize(static_cast<std::size_t>(out.width) * out.height * 4);
    for (std::uint32_t y = 0; y < out.height; ++y) {
        const std::uint8_t* src = f.pixel(r.x0, r.y0 + y);
        std::copy(src, src + static_cast<std::size_t>(out.width) * 4,
                  out.rgba.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * out.width * 4));
    }
    return out;
}

double meanOf(const std::vector<float>& v) {
    if (v.empty()) {
        return 0.0;
    }
    double s = 0.0;
    for (const float x : v) {
        s += static_cast<double>(x);
    }
    return s / static_cast<double>(v.size());
}

double percentileOf(std::vector<float> v, double fraction) {
    if (v.empty()) {
        return 0.0;
    }
    const auto k = static_cast<std::ptrdiff_t>(std::clamp(fraction, 0.0, 1.0) * static_cast<double>(v.size() - 1));
    std::nth_element(v.begin(), v.begin() + k, v.end());
    return static_cast<double>(v[static_cast<std::size_t>(k)]);
}

} // namespace

Frame scaledToLuma(const Frame& frame, double meanLuma) {
    Frame out = frame;
    const double current = meanOf(luma(frame));
    if (current <= 0.5) {
        return out; // black: no gain recovers a mean from nothing
    }
    const double gain = meanLuma / current;
    for (std::size_t i = 0; i < out.rgba.size(); i += 4) {
        for (int c = 0; c < 3; ++c) {
            out.rgba[i + c] = static_cast<std::uint8_t>(
                std::clamp(std::lround(static_cast<double>(frame.rgba[i + c]) * gain), 0L, 255L));
        }
    }
    return out;
}

std::vector<float> sobelMagnitude(const Frame& frame) {
    const std::vector<float> l = luma(frame);
    const std::uint32_t w = frame.width;
    const std::uint32_t h = frame.height;
    std::vector<float> g(static_cast<std::size_t>(w) * h, 0.0f);
    if (w < 3 || h < 3) {
        return g;
    }
    const auto at = [&](std::uint32_t x, std::uint32_t y) { return l[static_cast<std::size_t>(y) * w + x]; };
    for (std::uint32_t y = 1; y + 1 < h; ++y) {
        for (std::uint32_t x = 1; x + 1 < w; ++x) {
            const float gx = (at(x + 1, y - 1) + 2.0f * at(x + 1, y) + at(x + 1, y + 1)) -
                             (at(x - 1, y - 1) + 2.0f * at(x - 1, y) + at(x - 1, y + 1));
            const float gy = (at(x - 1, y + 1) + 2.0f * at(x, y + 1) + at(x + 1, y + 1)) -
                             (at(x - 1, y - 1) + 2.0f * at(x, y - 1) + at(x + 1, y - 1));
            g[static_cast<std::size_t>(y) * w + x] = std::sqrt(gx * gx + gy * gy);
        }
    }
    return g;
}

AbFrame abCompare(const Frame& a, const Frame& b, const std::vector<Region>& regions, const Frame* previousA,
                  const Frame* previousB) {
    AbFrame r;
    if (!a.valid() || !b.valid() || !a.sameShapeAs(b)) {
        r.ssim = 0.0;
        r.msSsim = 0.0;
        r.ssimMatched = 0.0;
        r.meanAbsDiff = 255.0;
        r.changedFraction = 1.0;
        return r;
    }
    const std::size_t pixels = static_cast<std::size_t>(a.width) * a.height;
    // pixel
    double absSum = 0.0;
    std::size_t changed = 0;
    for (std::size_t i = 0; i < a.rgba.size(); i += 4) {
        int largest = 0;
        for (int c = 0; c < 3; ++c) {
            const int d = std::abs(static_cast<int>(a.rgba[i + c]) - static_cast<int>(b.rgba[i + c]));
            absSum += d;
            largest = std::max(largest, d);
        }
        changed += largest > 8 ? 1 : 0;
    }
    r.meanAbsDiff = absSum / static_cast<double>(pixels * 3);
    r.changedFraction = static_cast<double>(changed) / static_cast<double>(pixels);
    r.psnr = psnr(a, b);
    // structural
    r.ssim = ssim(a, b);
    r.msSsim = msSsim(a, b);
    // luminance, and the structure at matched luminance
    const std::vector<float> la = luma(a);
    const std::vector<float> lb = luma(b);
    r.lumaOriginal = meanOf(la);
    r.lumaOptimized = meanOf(lb);
    r.lumaDelta = r.lumaOptimized - r.lumaOriginal;
    const Frame matched = scaledToLuma(b, r.lumaOriginal);
    r.ssimMatched = ssim(a, matched);
    {
        double s = 0.0;
        for (std::size_t i = 0; i < a.rgba.size(); i += 4) {
            for (int c = 0; c < 3; ++c) {
                s += std::abs(static_cast<int>(a.rgba[i + c]) - static_cast<int>(matched.rgba[i + c]));
            }
        }
        r.meanAbsDiffMatched = s / static_cast<double>(pixels * 3);
    }
    // edge
    const std::vector<float> ga = sobelMagnitude(a);
    const std::vector<float> gb = sobelMagnitude(b);
    const double meanGa = meanOf(ga);
    const double meanGb = meanOf(gb);
    {
        double d = 0.0;
        for (std::size_t i = 0; i < ga.size(); ++i) {
            d += std::abs(static_cast<double>(ga[i]) - static_cast<double>(gb[i]));
        }
        d /= static_cast<double>(std::max<std::size_t>(ga.size(), 1));
        r.edgeDifference = meanGa > 1e-6 ? d / meanGa : (d > 0.0 ? 1.0 : 0.0);
        r.edgeStrengthRatio = meanGa > 1e-6 ? meanGb / meanGa : 1.0;
        const double threshold = std::max(8.0, percentileOf(ga, 0.9));
        std::size_t edgesA = 0, lost = 0, added = 0;
        for (std::size_t i = 0; i < ga.size(); ++i) {
            const bool ea = ga[i] > threshold;
            const bool eb = gb[i] > threshold;
            edgesA += ea ? 1 : 0;
            lost += ea && gb[i] < threshold * 0.5 ? 1 : 0;
            added += eb && ga[i] < threshold * 0.5 ? 1 : 0;
        }
        r.edgesLost = edgesA > 0 ? static_cast<double>(lost) / static_cast<double>(edgesA) : 0.0;
        r.edgesAdded = edgesA > 0 ? static_cast<double>(added) / static_cast<double>(edgesA) : 0.0;
    }
    // temporal
    if (previousA != nullptr && previousB != nullptr && previousA->sameShapeAs(a) && previousB->sameShapeAs(b)) {
        const std::vector<float> pa = luma(*previousA);
        const std::vector<float> pb = luma(*previousB);
        double diff = 0.0, actA = 0.0, actB = 0.0;
        for (std::size_t i = 0; i < la.size(); ++i) {
            const double ta = std::abs(static_cast<double>(la[i]) - static_cast<double>(pa[i]));
            const double tb = std::abs(static_cast<double>(lb[i]) - static_cast<double>(pb[i]));
            diff += std::abs(ta - tb);
            actA += ta;
            actB += tb;
        }
        r.haveTemporal = true;
        r.temporalDifference = diff / static_cast<double>(la.size());
        r.temporalActivityRatio = actA > 1e-9 ? actB / actA : (actB > 1e-9 ? std::numeric_limits<double>::infinity() : 1.0);
    }
    // regions
    std::vector<std::uint8_t> mask(pixels, 0);
    double ssimWeighted = 0.0, ssimMatchedWeighted = 0.0, ssimArea = 0.0;
    for (const Region& in : regions) {
        Region c = in;
        c.x0 = std::min(c.x0, a.width);
        c.x1 = std::min(c.x1, a.width);
        c.y0 = std::min(c.y0, a.height);
        c.y1 = std::min(c.y1, a.height);
        if (c.x1 <= c.x0 || c.y1 <= c.y0) {
            continue;
        }
        for (std::uint32_t y = c.y0; y < c.y1; ++y) {
            std::fill(mask.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * a.width + c.x0),
                      mask.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * a.width + c.x1), 1);
        }
        if (c.x1 - c.x0 >= 8 && c.y1 - c.y0 >= 8) {
            const double area = static_cast<double>(c.x1 - c.x0) * static_cast<double>(c.y1 - c.y0);
            const Frame ca = crop(a, c);
            ssimWeighted += area * ssim(ca, crop(b, c));
            ssimMatchedWeighted += area * ssim(ca, crop(matched, c));
            ssimArea += area;
        }
    }
    std::size_t inside = 0;
    double regionAbs = 0.0, regionLa = 0.0, regionLb = 0.0;
    for (std::size_t p = 0; p < pixels; ++p) {
        if (mask[p] == 0) {
            continue;
        }
        ++inside;
        for (int c = 0; c < 3; ++c) {
            regionAbs += std::abs(static_cast<int>(a.rgba[p * 4 + c]) - static_cast<int>(b.rgba[p * 4 + c]));
        }
        regionLa += la[p];
        regionLb += lb[p];
    }
    if (inside > 0) {
        r.haveRegions = true;
        r.regionCoverage = static_cast<double>(inside) / static_cast<double>(pixels);
        r.regionMeanAbsDiff = regionAbs / static_cast<double>(inside * 3);
        r.regionLumaDelta = (regionLb - regionLa) / static_cast<double>(inside);
        r.regionSsim = ssimArea > 0.0 ? ssimWeighted / ssimArea : 1.0;
        r.regionSsimMatched = ssimArea > 0.0 ? ssimMatchedWeighted / ssimArea : 1.0;
    }
    return r;
}

AbSequence abCompareSequence(const std::vector<Frame>& a, const std::vector<Frame>& b,
                             const std::vector<Region>& regions) {
    AbSequence s;
    const std::size_t n = std::min(a.size(), b.size());
    s.frames = n;
    for (std::size_t i = 0; i < n; ++i) {
        s.perFrame.push_back(abCompare(a[i], b[i], regions, i > 0 ? &a[i - 1] : nullptr, i > 0 ? &b[i - 1] : nullptr));
    }
    if (n == 0) {
        return s;
    }
    // higherIsWorse: the worst is the maximum; else the minimum.
    const auto pool = [&](auto pick, bool higherIsWorse, bool onlyTemporal = false, bool onlyRegions = false) {
        AbPooled p;
        std::size_t count = 0;
        double worst = higherIsWorse ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
        for (const AbFrame& f : s.perFrame) {
            if ((onlyTemporal && !f.haveTemporal) || (onlyRegions && !f.haveRegions)) {
                continue;
            }
            const double v = pick(f);
            if (!std::isfinite(v)) {
                continue;
            }
            p.mean += v;
            ++count;
            worst = higherIsWorse ? std::max(worst, v) : std::min(worst, v);
        }
        if (count == 0) {
            p.mean = std::numeric_limits<double>::quiet_NaN();
            p.worst = std::numeric_limits<double>::quiet_NaN();
            return p;
        }
        p.mean /= static_cast<double>(count);
        p.worst = worst;
        return p;
    };
    s.meanAbsDiff = pool([](const AbFrame& f) { return f.meanAbsDiff; }, true);
    s.changedFraction = pool([](const AbFrame& f) { return f.changedFraction; }, true);
    s.psnr = pool([](const AbFrame& f) { return f.psnr; }, false); // NaN when every frame was identical
    s.ssim = pool([](const AbFrame& f) { return f.ssim; }, false);
    s.msSsim = pool([](const AbFrame& f) { return f.msSsim; }, false);
    s.edgeDifference = pool([](const AbFrame& f) { return f.edgeDifference; }, true);
    s.edgeStrengthRatio = pool([](const AbFrame& f) { return f.edgeStrengthRatio; }, false);
    s.edgesLost = pool([](const AbFrame& f) { return f.edgesLost; }, true);
    s.edgesAdded = pool([](const AbFrame& f) { return f.edgesAdded; }, true);
    s.lumaDelta = pool([](const AbFrame& f) { return std::abs(f.lumaDelta); }, true);
    s.ssimMatched = pool([](const AbFrame& f) { return f.ssimMatched; }, false);
    s.meanAbsDiffMatched = pool([](const AbFrame& f) { return f.meanAbsDiffMatched; }, true);
    s.temporalDifference = pool([](const AbFrame& f) { return f.temporalDifference; }, true, true);
    s.temporalActivityRatio = pool([](const AbFrame& f) { return f.temporalActivityRatio; }, false, true);
    s.regionMeanAbsDiff = pool([](const AbFrame& f) { return f.regionMeanAbsDiff; }, true, false, true);
    s.regionLumaDelta = pool([](const AbFrame& f) { return std::abs(f.regionLumaDelta); }, true, false, true);
    s.regionSsim = pool([](const AbFrame& f) { return f.regionSsim; }, false, false, true);
    s.regionSsimMatched = pool([](const AbFrame& f) { return f.regionSsimMatched; }, false, false, true);
    double lo = 0.0, lz = 0.0, cov = 0.0;
    std::size_t regionFrames = 0;
    for (const AbFrame& f : s.perFrame) {
        lo += f.lumaOriginal;
        lz += f.lumaOptimized;
        s.haveTemporal = s.haveTemporal || f.haveTemporal;
        if (f.haveRegions) {
            s.haveRegions = true;
            cov += f.regionCoverage;
            ++regionFrames;
        }
    }
    s.lumaOriginal = lo / static_cast<double>(n);
    s.lumaOptimized = lz / static_cast<double>(n);
    s.regionCoverage = regionFrames > 0 ? cov / static_cast<double>(regionFrames) : 0.0;
    return s;
}

} // namespace avgen::quality
