#include "audio/stem_source.hpp"

#include "audio/audio_file.hpp"

#include <fmt/format.h>
#include <miniaudio.h>

#include <algorithm>
#include <cmath>

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

StemReport checkStem(const StemSource& source, const AudioFile* file, const std::string& decodeError,
                     const MixFacts& mix, double toleranceSeconds) {
    StemReport r;
    const std::string name = source.file.filename().string();
    const auto add = [&r](StemSeverity s, std::string m) { r.diagnostics.push_back({s, std::move(m)}); };
    if (file == nullptr) {
        add(StemSeverity::Error, fmt::format("stem '{}' ({}): cannot be decoded: {}", source.label, name,
                                             decodeError.empty() ? std::string("unknown error") : decodeError));
        return r;
    }
    r.decoded = true;
    r.sampleRate = file->sampleRate();
    r.channels = file->channels();
    r.frames = file->frameCount();
    r.durationSeconds = file->durationSeconds();
    r.format = file->path().empty() ? std::string() : nativeSampleFormat(file->path());
    bool allZero = true;
    for (const float v : file->interleaved()) {
        if (!std::isfinite(v)) {
            ++r.nonFinite;
            continue;
        }
        r.peak = std::max(r.peak, std::fabs(v));
        allZero = allZero && v == 0.0f;
    }
    r.silent = allZero && r.nonFinite == 0;
    if (r.nonFinite > 0) {
        add(StemSeverity::Error,
            fmt::format("stem '{}' ({}): {} sample(s) are not finite numbers; the file is damaged", source.label, name,
                        r.nonFinite));
    }
    if (r.silent) {
        add(StemSeverity::Warning,
            fmt::format("stem '{}' ({}): every sample is silent; its signals will stay at 0", source.label, name));
    }
    if (mix.sampleRate != 0 && mix.sampleRate != r.sampleRate) {
        add(StemSeverity::Info, fmt::format("stem '{}' ({}): {} Hz beside a {} Hz mix; analysed at its own rate "
                                            "(nothing is resampled)",
                                            source.label, name, r.sampleRate, mix.sampleRate));
    }
    if (mix.durationSeconds > 0.0) {
        const double start = source.offsetSeconds;
        const double end = source.offsetSeconds + r.durationSeconds;
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
    return r;
}

} // namespace avgen::audio
