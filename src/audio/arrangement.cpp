#include "audio/arrangement.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace avgen::audio {
namespace {

// How many source frames a clip plays, given where it starts in the file and how long it was asked
// to run. A duration of zero means the rest of the file; a duration past the end is trimmed to it,
// because a clip cannot play samples the file does not have and silently padding it would put a gap
// in the middle of a piece rather than at the end of a clip.
[[nodiscard]] std::uint64_t sourceFrames(const AudioClip& clip, const AudioFile& file) {
    const auto rate = static_cast<double>(file.sampleRate());
    const double inFrames = std::max(0.0, clip.inSeconds) * rate;
    if (inFrames >= static_cast<double>(file.frameCount())) {
        return 0;
    }
    const auto start = static_cast<std::uint64_t>(inFrames);
    const std::uint64_t available = file.frameCount() - start;
    if (!(clip.durationSeconds > 0.0)) {
        return available;
    }
    const auto wanted = static_cast<std::uint64_t>(clip.durationSeconds * rate);
    return std::min(wanted, available);
}

} // namespace

double clipEndSeconds(const AudioClip& clip, const AudioFile* file) {
    if (file == nullptr) {
        // Without the file, the only thing that can be known is what was asked for. Better than
        // zero: a project whose audio has gone missing should still show the shape of its timeline.
        return clip.startSeconds + std::max(0.0, clip.durationSeconds);
    }
    const auto frames = static_cast<double>(sourceFrames(clip, *file));
    return clip.startSeconds + frames / static_cast<double>(file->sampleRate());
}

std::optional<std::size_t> splitClip(std::vector<AudioClip>& clips, std::size_t index,
                                     double seconds, double endSeconds, double minSeconds) {
    if (index >= clips.size()) {
        return std::nullopt;
    }
    const AudioClip& clip = clips[index];
    if (seconds <= clip.startSeconds + minSeconds || seconds >= endSeconds - minSeconds) {
        return std::nullopt;
    }
    const double head = seconds - clip.startSeconds;
    AudioClip tail = clip;
    tail.startSeconds = seconds;
    // The window into the file moves by exactly as much as the clip's place on the timeline did.
    tail.inSeconds = clip.inSeconds + head;
    // An open clip stays open; a bounded one loses the head's worth of length.
    tail.durationSeconds = clip.durationSeconds > 0.0 ? clip.durationSeconds - head : 0.0;
    tail.fadeInSeconds = 0.0;
    if (!clip.name.empty()) {
        tail.name = clip.name + " b";
    }
    clips[index].durationSeconds = head;
    clips[index].fadeOutSeconds = 0.0;
    clips.insert(clips.begin() + static_cast<std::ptrdiff_t>(index) + 1, std::move(tail));
    return index + 1;
}

double arrangementDuration(std::span<const AudioClip> clips, const ClipSources& sources) {
    double end = 0.0;
    for (const AudioClip& clip : clips) {
        // A clip whose file is missing contributes nothing. It is drawn on the strip from its own
        // numbers -- `clipEndSeconds` still answers for it -- but padding the mix with silence for a
        // file that is not there would make a broken project quietly *longer* than a working one.
        const auto file = sources.find(clip.file);
        if (!clip.enabled || !file) {
            continue;
        }
        end = std::max(end, clipEndSeconds(clip, file.get()));
    }
    return end;
}

// ---- ClipSources ---------------------------------------------------------------------------------

std::vector<std::string> ClipSources::sync(std::span<const AudioClip> clips) {
    std::vector<std::string> failures;
    // Drop what is no longer named. Done first so a project swap releases the old piece's memory
    // before the new one's is decoded rather than after.
    std::erase_if(files_, [&](const auto& entry) {
        return std::ranges::none_of(clips, [&](const AudioClip& c) { return c.file == entry.first; });
    });
    for (const AudioClip& clip : clips) {
        if (clip.file.empty() || find(clip.file) != nullptr) {
            continue;
        }
        auto loaded = AudioFile::load(clip.file);
        if (!loaded) {
            failures.push_back(fmt::format("audio clip '{}': {}", clip.file.filename().string(),
                                           loaded.error().message));
            continue;
        }
        files_.emplace_back(clip.file, std::make_shared<const AudioFile>(std::move(*loaded)));
    }
    return failures;
}

std::shared_ptr<const AudioFile> ClipSources::find(const std::filesystem::path& path) const {
    const auto it = std::ranges::find(files_, path, &std::pair<std::filesystem::path,
                                                               std::shared_ptr<const AudioFile>>::first);
    return it == files_.end() ? nullptr : it->second;
}

// ---- the mix -------------------------------------------------------------------------------------

Result<AudioFile> mixArrangement(std::span<const AudioClip> clips, const ClipSources& sources,
                                 MixReport* report) {
    const auto started = std::chrono::steady_clock::now();
    MixReport local;

    // The output format: the highest rate and channel count in the arrangement. The highest rather
    // than the first, so adding a 96 kHz clip to a 48 kHz piece resamples the *new* clip up rather
    // than throwing away half of it -- and so an arrangement where every clip agrees, which is
    // almost all of them, needs no resampling at all.
    std::uint32_t rate = 0;
    std::uint32_t channels = 0;
    for (const AudioClip& clip : clips) {
        if (!clip.enabled) {
            continue;
        }
        const auto file = sources.find(clip.file);
        if (!file) {
            ++local.clipsSkipped;
            continue;
        }
        rate = std::max(rate, file->sampleRate());
        channels = std::max(channels, file->channels());
    }
    if (rate == 0 || channels == 0) {
        // Nothing to mix. Not a failure: a project with no audio is an ordinary project, and this is
        // what it has always looked like from the engine's side.
        local.millis =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        if (report != nullptr) {
            *report = local;
        }
        return AudioFile::fromInterleaved({}, 1, 48000);
    }

    const double duration = arrangementDuration(clips, sources);
    const auto totalFrames = static_cast<std::uint64_t>(std::ceil(duration * rate));
    // Guard rather than trust: a hand-edited project can say a clip starts at 1e9 seconds, and the
    // allocation that follows would be the last thing the process did.
    constexpr std::uint64_t kMaxFrames = std::uint64_t{48000} * 60 * 60 * 4; // four hours at 48 kHz
    if (totalFrames > kMaxFrames) {
        return fail("audio arrangement is {:.0f} seconds long; the limit is four hours", duration);
    }
    std::vector<float> out(static_cast<std::size_t>(totalFrames) * channels, 0.0f);

    for (const AudioClip& clip : clips) {
        if (!clip.enabled) {
            continue;
        }
        const auto file = sources.find(clip.file);
        if (!file) {
            continue; // already counted, and warned about by the caller's sync()
        }
        const std::uint64_t inFrames = sourceFrames(clip, *file);
        if (inFrames == 0) {
            local.warnings.push_back(fmt::format("audio clip '{}' plays no samples (its in-point is "
                                                 "past the end of the file)",
                                                 clip.name.empty() ? clip.file.filename().string()
                                                                   : clip.name));
            ++local.clipsSkipped;
            continue;
        }
        const bool resampling = file->sampleRate() != rate;
        if (resampling) {
            // Said out loud. Linear interpolation is audible on music, and the fix is to convert the
            // file rather than to leave the engine quietly degrading it every time it mixes.
            local.warnings.push_back(fmt::format(
                "audio clip '{}' is {} Hz in a {} Hz arrangement and was resampled (linear); convert "
                "the file to match to avoid it",
                clip.file.filename().string(), file->sampleRate(), rate));
        }

        const double ratio = static_cast<double>(file->sampleRate()) / static_cast<double>(rate);
        const auto outFrames = static_cast<std::uint64_t>(static_cast<double>(inFrames) / ratio);
        const auto sourceStart = static_cast<std::uint64_t>(std::max(0.0, clip.inSeconds) *
                                                            static_cast<double>(file->sampleRate()));
        auto destStart = static_cast<std::int64_t>(std::llround(clip.startSeconds * rate));
        std::uint64_t skip = 0;
        if (destStart < 0) {
            // A clip dragged before zero plays the part of itself that is still on the timeline,
            // rather than being rejected or slid to the start.
            skip = static_cast<std::uint64_t>(-destStart);
            destStart = 0;
        }

        const auto fadeIn = static_cast<std::uint64_t>(std::max(0.0, clip.fadeInSeconds) * rate);
        const auto fadeOut = static_cast<std::uint64_t>(std::max(0.0, clip.fadeOutSeconds) * rate);
        const std::span<const float> src = file->interleaved();
        const std::uint32_t srcChannels = file->channels();

        for (std::uint64_t f = skip; f < outFrames; ++f) {
            const auto dest = static_cast<std::uint64_t>(destStart) + (f - skip);
            if (dest >= totalFrames) {
                break;
            }
            float gain = clip.gain;
            if (fadeIn > 0 && f < fadeIn) {
                gain *= static_cast<float>(f) / static_cast<float>(fadeIn);
            }
            if (fadeOut > 0 && f + fadeOut > outFrames) {
                gain *= static_cast<float>(outFrames - f) / static_cast<float>(fadeOut);
            }
            // The source position. At a matching rate this is an exact integer and the read is a
            // copy; otherwise it is a linear interpolation between two frames.
            const double srcPos = static_cast<double>(sourceStart) + static_cast<double>(f) * ratio;
            const auto i0 = static_cast<std::uint64_t>(srcPos);
            const std::uint64_t i1 = std::min<std::uint64_t>(i0 + 1, file->frameCount() - 1);
            const auto t = static_cast<float>(srcPos - static_cast<double>(i0));
            if (i0 >= file->frameCount()) {
                break;
            }
            for (std::uint32_t c = 0; c < channels; ++c) {
                // A mono source goes to every output channel; a source with fewer channels than the
                // output repeats its last. Silence in the extra channels would put the whole clip on
                // one side of the picture.
                const std::uint32_t sc = std::min(c, srcChannels - 1);
                const float a = src[static_cast<std::size_t>(i0) * srcChannels + sc];
                const float sample = resampling
                                         ? a + (src[static_cast<std::size_t>(i1) * srcChannels + sc] - a) * t
                                         : a;
                out[static_cast<std::size_t>(dest) * channels + c] += sample * gain;
            }
        }
        ++local.clipsMixed;
    }

    local.durationSeconds = duration;
    local.sampleRate = rate;
    local.channels = channels;
    local.millis =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    if (report != nullptr) {
        *report = local;
    }
    return AudioFile::fromInterleaved(std::move(out), channels, rate);
}

} // namespace avgen::audio
