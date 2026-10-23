#include "audio/stem_source.hpp"

#include "audio/audio_file.hpp"

#include <fmt/format.h>
#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace avgen::audio {

std::string makeStemKey(std::string_view label, std::span<const std::string> taken) {
    std::string base;
    bool pendingSeparator = false;
    for (const char raw : label) {
        const auto c = static_cast<unsigned char>(raw);
        const char lower = static_cast<char>(std::tolower(c));
        if ((lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9')) {
            if (pendingSeparator && !base.empty()) {
                base.push_back('_');
            }
            pendingSeparator = false;
            base.push_back(lower);
        } else {
            pendingSeparator = true;
        }
    }
    if (base.empty()) {
        base = "stem";
    }
    const auto isTaken = [&](const std::string& k) { return std::find(taken.begin(), taken.end(), k) != taken.end(); };
    if (!isTaken(base)) {
        return base;
    }
    for (int n = 2;; ++n) {
        std::string candidate = base + "_" + std::to_string(n);
        if (!isTaken(candidate)) {
            return candidate;
        }
    }
}

bool validStemKey(std::string_view key) {
    return !key.empty() && std::all_of(key.begin(), key.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    });
}

bool StemReport::hasError() const {
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [](const StemDiagnostic& d) { return d.severity == StemSeverity::Error; });
}

std::string nativeSampleFormat(const std::filesystem::path& path) {
    // A decoder with no output format asked for reports the source's own; nothing is decoded.
    ma_decoder_config config = ma_decoder_config_init(ma_format_unknown, 0, 0);
    ma_decoder decoder{};
    if (ma_decoder_init_file(path.string().c_str(), &config, &decoder) != MA_SUCCESS) {
        return "";
    }
    ma_format format = ma_format_unknown;
    ma_uint32 channels = 0;
    ma_uint32 rate = 0;
    ma_decoder_get_data_format(&decoder, &format, &channels, &rate, nullptr, 0);
    ma_decoder_uninit(&decoder);
    switch (format) {
    case ma_format_u8: return "u8";
    case ma_format_s16: return "s16";
    case ma_format_s24: return "s24";
    case ma_format_s32: return "s32";
    case ma_format_f32: return "f32";
    default: return "";
    }
}

StemFileFacts factsOf(const AudioFile& file) {
    StemFileFacts f;
    f.sampleRate = file.sampleRate();
    f.channels = file.channels();
    f.frames = file.frameCount();
    f.format = file.path().empty() ? std::string() : nativeSampleFormat(file.path());
    for (const float v : file.interleaved()) {
        if (!std::isfinite(v)) {
            ++f.nonFinite;
            continue;
        }
        f.peak = std::max(f.peak, std::fabs(v));
        f.silent = f.silent && v == 0.0f;
    }
    f.silent = f.silent && f.nonFinite == 0;
    f.probe = stemProbe(file.mono(), file.sampleRate()); // ADR-1632
    return f;
}

StemReport checkStem(const StemSource& source, const StemFileFacts* facts, const std::string& decodeError) {
    StemReport r;
    const std::string name = source.file.filename().string();
    const auto add = [&r](StemSeverity s, std::string m) { r.diagnostics.push_back({s, std::move(m)}); };
    if (facts == nullptr) {
        add(StemSeverity::Error, fmt::format("stem '{}' ({}): cannot be decoded: {}", source.label, name,
                                             decodeError.empty() ? std::string("unknown error") : decodeError));
        return r;
    }
    r.decoded = true;
    r.sampleRate = facts->sampleRate;
    r.channels = facts->channels;
    r.frames = facts->frames;
    r.durationSeconds = facts->sampleRate > 0 ? static_cast<double>(facts->frames) / facts->sampleRate : 0.0;
    r.format = facts->format;
    r.peak = facts->peak;
    r.nonFinite = facts->nonFinite;
    r.silent = facts->silent;
    if (r.nonFinite > 0) {
        add(StemSeverity::Error,
            fmt::format("stem '{}' ({}): {} sample(s) are not finite numbers; the file is damaged", source.label, name,
                        r.nonFinite));
    }
    if (r.silent) {
        add(StemSeverity::Warning,
            fmt::format("stem '{}' ({}): every sample is silent; its signals will stay at 0", source.label, name));
    }
    return r;
}

std::vector<StemDiagnostic> mixDiagnostics(const StemSource& source, const StemReport& report, const MixFacts& mix,
                                           double toleranceSeconds) {
    std::vector<StemDiagnostic> out;
    if (!report.decoded) {
        return out;
    }
    const std::string name = source.file.filename().string();
    const auto add = [&out](StemSeverity s, std::string m) { out.push_back({s, std::move(m)}); };
    if (mix.sampleRate != 0 && mix.sampleRate != report.sampleRate) {
        add(StemSeverity::Info, fmt::format("stem '{}' ({}): {} Hz beside a {} Hz mix; analysed at its own rate "
                                            "(nothing is resampled)",
                                            source.label, name, report.sampleRate, mix.sampleRate));
    }
    if (mix.durationSeconds > 0.0) {
        const double start = source.offsetSeconds;
        const double end = source.offsetSeconds + report.durationSeconds;
        if (end <= 0.0 || start >= mix.durationSeconds) {
            add(StemSeverity::Warning,
                fmt::format("stem '{}' ({}): it lies wholly outside the mix ({:.3f}..{:.3f} s against 0..{:.3f} s); "
                            "check its offset",
                            source.label, name, start, end, mix.durationSeconds));
        } else if (std::abs(start) > toleranceSeconds || std::abs(end - mix.durationSeconds) > toleranceSeconds) {
            add(StemSeverity::Warning,
                fmt::format("stem '{}' ({}): it covers {:.3f}..{:.3f} s and the mix 0..{:.3f} s; outside its own span "
                            "it publishes nothing (its signals read 0)",
                            source.label, name, start, end, mix.durationSeconds));
        }
    }
    return out;
}

// ---- the stem-set check (ADR-1632) ---------------------------------------------------------------------------------

std::uint32_t stemProbeFactor(std::uint32_t sampleRate) {
    return std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::lround(static_cast<double>(sampleRate) / kStemProbeHz)));
}

StemProbeWriter::StemProbeWriter(std::uint32_t sampleRate)
    : factor_(stemProbeFactor(sampleRate))
    , rate_(sampleRate > 0 ? static_cast<double>(sampleRate) / static_cast<double>(stemProbeFactor(sampleRate)) : 0.0) {}

void StemProbeWriter::push(std::span<const float> mono) {
    for (const float v : mono) {
        sum_ += std::isfinite(v) ? static_cast<double>(v) : 0.0;
        if (++count_ == factor_) {
            probe_.push_back(static_cast<float>(sum_ / static_cast<double>(factor_)));
            sum_ = 0.0;
            count_ = 0;
        }
    }
}

std::vector<float> stemProbe(std::span<const float> mono, std::uint32_t sampleRate) {
    StemProbeWriter w(sampleRate);
    w.push(mono);
    return w.take();
}

namespace {

// Solves the n x n system a x = b in place (Gaussian elimination, partial pivoting). A singular column reads 0.
std::vector<double> solve(std::vector<double> a, std::vector<double> b, std::size_t n) {
    std::vector<double> x(n, 0.0);
    std::vector<bool> dead(n, false);
    for (std::size_t c = 0; c < n; ++c) {
        std::size_t pivot = c;
        for (std::size_t r = c + 1; r < n; ++r) {
            if (std::abs(a[r * n + c]) > std::abs(a[pivot * n + c])) {
                pivot = r;
            }
        }
        if (std::abs(a[pivot * n + c]) < 1e-12) {
            dead[c] = true;
            continue;
        }
        if (pivot != c) {
            for (std::size_t k = 0; k < n; ++k) {
                std::swap(a[c * n + k], a[pivot * n + k]);
            }
            std::swap(b[c], b[pivot]);
        }
        for (std::size_t r = 0; r < n; ++r) {
            if (r == c) {
                continue;
            }
            const double f = a[r * n + c] / a[c * n + c];
            for (std::size_t k = c; k < n; ++k) {
                a[r * n + k] -= f * a[c * n + k];
            }
            b[r] -= f * b[c];
        }
    }
    for (std::size_t c = 0; c < n; ++c) {
        x[c] = dead[c] ? 0.0 : b[c] / a[c * n + c];
    }
    return x;
}

double energy(std::span<const float> v) {
    double e = 0.0;
    for (const float x : v) {
        e += static_cast<double>(x) * static_cast<double>(x);
    }
    return e;
}

} // namespace

StemSetCheck checkStemSet(std::span<const StemSetMember> members, const MixProbe& mix) {
    StemSetCheck out;
    const std::size_t n = members.size();
    out.perStem.resize(n);
    out.gains.assign(n, 0.0f);
    out.lagSeconds.assign(n, 0.0);
    out.match.assign(n, 0.0f);
    const auto add = [&](std::size_t i, StemSeverity s, std::string m) { out.perStem[i].push_back({s, std::move(m)}); };
    const auto nameOf = [&](std::size_t i) {
        return fmt::format("stem '{}' ({})", members[i].source->label, members[i].source->file.filename().string());
    };

    // Channels: info only (a stem is analysed as mono; its width needs two channels).
    for (std::size_t i = 0; i < n; ++i) {
        const StemSetMember& m = members[i];
        if (m.source == nullptr || m.report == nullptr || !m.report->decoded || mix.channels == 0) {
            continue;
        }
        if (m.report->channels != mix.channels) {
            add(i, StemSeverity::Info,
                fmt::format("{}: {} channel(s) beside a {}-channel mix; analysed as mono{}", nameOf(i),
                            m.report->channels, mix.channels,
                            m.report->channels < 2 ? " (its width reads 0)" : ""));
        }
    }

    // The members that take part: decoded, at the mix's rate, with a probe.
    std::vector<std::size_t> part;
    for (std::size_t i = 0; i < n; ++i) {
        const StemSetMember& m = members[i];
        if (m.source != nullptr && m.report != nullptr && m.report->decoded && !m.probe.empty() &&
            mix.sampleRate != 0 && m.report->sampleRate == mix.sampleRate) {
            part.push_back(i);
        }
    }
    if (part.empty() || mix.probe.empty()) {
        return out;
    }
    const double rate = static_cast<double>(mix.sampleRate) / static_cast<double>(stemProbeFactor(mix.sampleRate));
    const std::size_t length = mix.probe.size();
    const double mixEnergy = energy(mix.probe);
    if (!(mixEnergy > 1e-9)) {
        return out; // a silent mix: nothing to align against
    }
    // Each stem placed on the mix's probe timeline at its offset.
    std::vector<std::vector<float>> placed(part.size(), std::vector<float>(length, 0.0f));
    for (std::size_t p = 0; p < part.size(); ++p) {
        const StemSetMember& m = members[part[p]];
        const auto shift = static_cast<std::int64_t>(std::llround(m.source->offsetSeconds * rate));
        for (std::size_t k = 0; k < m.probe.size(); ++k) {
            const std::int64_t at = static_cast<std::int64_t>(k) + shift;
            if (at >= 0 && at < static_cast<std::int64_t>(length)) {
                placed[p][static_cast<std::size_t>(at)] = m.probe[k];
            }
        }
    }
    // The gains: least squares, mix ~ sum g_p placed_p (a mix has faders). A small ridge keeps a silent stem at 0.
    const std::size_t np = part.size();
    std::vector<double> a(np * np, 0.0);
    std::vector<double> b(np, 0.0);
    for (std::size_t p = 0; p < np; ++p) {
        for (std::size_t q = p; q < np; ++q) {
            double dot = 0.0;
            for (std::size_t k = 0; k < length; ++k) {
                dot += static_cast<double>(placed[p][k]) * static_cast<double>(placed[q][k]);
            }
            a[p * np + q] = a[q * np + p] = dot;
        }
        a[p * np + p] += 1e-9 * mixEnergy;
        double dot = 0.0;
        for (std::size_t k = 0; k < length; ++k) {
            dot += static_cast<double>(placed[p][k]) * static_cast<double>(mix.probe[k]);
        }
        b[p] = dot;
    }
    const std::vector<double> g = solve(a, b, np);
    std::vector<float> residual(mix.probe.begin(), mix.probe.end());
    for (std::size_t p = 0; p < np; ++p) {
        out.gains[part[p]] = static_cast<float>(g[p]);
        for (std::size_t k = 0; k < length; ++k) {
            residual[k] -= static_cast<float>(g[p]) * placed[p][k];
        }
    }
    out.residualDb = 10.0 * std::log10(std::max(energy(residual), 1e-20) / mixEnergy);
    out.measured = true;
    out.set.push_back({StemSeverity::Info,
                       fmt::format("stems: {} of {} sum to the mix with a residual {:.1f} dB below it{}", np, n,
                                   -out.residualDb,
                                   out.residualDb > -20.0 ? " (an incomplete set leaves the rest of the mix; a "
                                                            "misaligned one leaves its stems)"
                                                          : "")});

    // The lag: each stem against what the others leave of the mix (the residual plus its own fitted part), over the
    // loudest 1 s blocks of the stem, normalised cross-correlation within +-kStemProbeMaxLagSeconds.
    const auto maxLag = static_cast<std::int64_t>(std::llround(kStemProbeMaxLagSeconds * rate));
    const auto block = static_cast<std::size_t>(std::llround(rate));
    constexpr std::size_t kBlocks = 16;
    for (std::size_t p = 0; p < np; ++p) {
        const std::size_t i = part[p];
        const std::vector<float>& x = placed[p];
        std::vector<float> target = residual;
        for (std::size_t k = 0; k < length; ++k) {
            target[k] += static_cast<float>(g[p]) * x[k];
        }
        // The loudest blocks of this stem, clear of the edges by the lag.
        std::vector<std::pair<double, std::size_t>> blocks;
        for (std::size_t start = static_cast<std::size_t>(maxLag); start + block + static_cast<std::size_t>(maxLag) <= length;
             start += block) {
            blocks.emplace_back(energy(std::span<const float>(x).subspan(start, block)), start);
        }
        std::sort(blocks.begin(), blocks.end(), [](const auto& l, const auto& r) { return l.first > r.first; });
        if (blocks.size() > kBlocks) {
            blocks.resize(kBlocks);
        }
        double xx = 0.0;
        for (const auto& [e, start] : blocks) {
            xx += e;
        }
        if (blocks.empty() || !(xx > 1e-12)) {
            continue; // a silent stem: checkStem has said so
        }
        // The normalised correlation of the blocks against the target `lag` probe samples later.
        const auto correlate = [&](std::int64_t lag) {
            double xy = 0.0;
            double yy = 0.0;
            for (const auto& [e, start] : blocks) {
                const float* xs = x.data() + start;
                const float* ys = target.data() + static_cast<std::int64_t>(start) + lag;
                for (std::size_t k = 0; k < block; ++k) {
                    xy += static_cast<double>(xs[k]) * static_cast<double>(ys[k]);
                    yy += static_cast<double>(ys[k]) * static_cast<double>(ys[k]);
                }
            }
            return yy > 1e-12 ? xy / std::sqrt(xx * yy) : 0.0;
        };
        // Coarse: the same blocks box-averaged by kCoarse, every kCoarse-th lag.
        constexpr std::int64_t kCoarse = 8;
        const std::size_t coarseBlock = block / static_cast<std::size_t>(kCoarse);
        const auto coarseOf = [&](const std::vector<float>& v, std::int64_t from, std::size_t n) {
            std::vector<float> out(n, 0.0f);
            for (std::size_t c = 0; c < n; ++c) {
                double sum = 0.0;
                for (std::int64_t k = 0; k < kCoarse; ++k) {
                    const std::int64_t at = from + static_cast<std::int64_t>(c) * kCoarse + k;
                    sum += at >= 0 && at < static_cast<std::int64_t>(v.size()) ? static_cast<double>(v[static_cast<std::size_t>(at)]) : 0.0;
                }
                out[c] = static_cast<float>(sum / static_cast<double>(kCoarse));
            }
            return out;
        };
        const std::int64_t coarseLags = maxLag / kCoarse;
        std::vector<std::vector<float>> cx;
        std::vector<std::vector<float>> cy; // each block's target, from -maxLag to +maxLag around it
        double cxx = 0.0;
        for (const auto& [e, start] : blocks) {
            cx.push_back(coarseOf(x, static_cast<std::int64_t>(start), coarseBlock));
            cy.push_back(coarseOf(target, static_cast<std::int64_t>(start) - coarseLags * kCoarse,
                                  coarseBlock + static_cast<std::size_t>(2 * coarseLags)));
            cxx += energy(cx.back());
        }
        std::int64_t coarseBest = 0;
        double coarseR = -2.0;
        for (std::int64_t c = -coarseLags; c <= coarseLags; ++c) {
            double xy = 0.0;
            double yy = 0.0;
            for (std::size_t b = 0; b < cx.size(); ++b) {
                const float* ys = cy[b].data() + (c + coarseLags);
                for (std::size_t k = 0; k < coarseBlock; ++k) {
                    xy += static_cast<double>(cx[b][k]) * static_cast<double>(ys[k]);
                    yy += static_cast<double>(ys[k]) * static_cast<double>(ys[k]);
                }
            }
            const double r = yy > 1e-12 && cxx > 1e-12 ? xy / std::sqrt(cxx * yy) : 0.0;
            if (r > coarseR) {
                coarseR = r;
                coarseBest = c;
            }
        }
        // Fine: around the coarse peak, and at 0.
        const double atZero = correlate(0);
        double best = atZero;
        std::int64_t bestLag = 0;
        for (std::int64_t lag = std::max(-maxLag, coarseBest * kCoarse - 2 * kCoarse);
             lag <= std::min(maxLag, coarseBest * kCoarse + 2 * kCoarse); ++lag) {
            const double r = correlate(lag);
            if (r > best) {
                best = r;
                bestLag = lag;
            }
        }
        out.match[i] = static_cast<float>(std::max(best, 0.0));
        const double lagSeconds = static_cast<double>(bestLag) / rate;
        if (best < 0.2) {
            add(i, StemSeverity::Warning,
                fmt::format("{}: it does not match the mix within +-{:.0f} ms of its offset (correlation {:.2f}): "
                            "another song's stem, or an offset further out; check its offset",
                            nameOf(i), kStemProbeMaxLagSeconds * 1000.0, best));
        } else if (std::abs(bestLag) >= 2 && best > atZero + 0.02) {
            out.lagSeconds[i] = lagSeconds;
            const double aligned = members[i].source->offsetSeconds + lagSeconds;
            add(i, StemSeverity::Warning,
                fmt::format("{}: it lines up with the mix {:.1f} ms {} than its offset puts it (correlation {:.2f} "
                            "there, {:.2f} at its offset); the offset that aligns it is {:.4f} s",
                            nameOf(i), std::abs(lagSeconds) * 1000.0, lagSeconds > 0.0 ? "later" : "earlier", best,
                            atZero, aligned));
        }
    }
    return out;
}

} // namespace avgen::audio
