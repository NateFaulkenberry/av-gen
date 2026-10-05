#include "spatial/audio_history.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::spatial {

const char* onsetSourceName(OnsetSource source) {
    switch (source) {
    case OnsetSource::Low:
        return "low";
    case OnsetSource::Mid:
        return "mid";
    case OnsetSource::High:
        return "high";
    case OnsetSource::Beat:
        return "beat";
    }
    return "low";
}

std::optional<OnsetSource> onsetSourceFromName(std::string_view name) {
    for (const auto s : {OnsetSource::Low, OnsetSource::Mid, OnsetSource::High, OnsetSource::Beat}) {
        if (name == onsetSourceName(s)) {
            return s;
        }
    }
    return std::nullopt;
}

AudioHistory AudioHistory::whole(double rowRate, double firstRowTime, std::vector<float> rows,
                                 std::array<std::vector<AudioOnset>, kOnsetSources> onsets) {
    AudioHistory h;
    h.live_ = false;
    h.rowRate_ = rowRate > 0.0 ? rowRate : 93.75;
    h.firstRowTime_ = firstRowTime;
    rows.resize(rows.size() - rows.size() % static_cast<std::size_t>(kAudioBins));
    h.rows_ = std::move(rows);
    for (auto& list : onsets) {
        std::sort(list.begin(), list.end(), [](const AudioOnset& a, const AudioOnset& b) { return a.time < b.time; });
    }
    h.onsets_ = std::move(onsets);
    return h;
}

AudioHistory AudioHistory::livePlaceholder(double rowRate) {
    AudioHistory h;
    h.live_ = true;
    h.rowRate_ = rowRate > 0.0 ? rowRate : 93.75;
    return h;
}

void AudioHistory::appendLive(double time, std::span<const float> row) {
    if (!live_ || row.size() < static_cast<std::size_t>(kAudioBins)) {
        return;
    }
    const auto index = static_cast<std::int64_t>(std::floor(time * rowRate_ + 1e-9));
    if (heldRows() > 0 && index < rowCount() - 1) {
        // The input restarted (its clock went backwards): forget everything.
        rows_.clear();
        firstHeld_ = 0;
        for (auto& list : onsets_) {
            list.clear();
        }
        ++revision_;
    }
    if (heldRows() == 0) {
        firstHeld_ = index;
    }
    // Fill forward to `index` (inclusive); a very long gap fills only the last ring's worth.
    std::int64_t next = rowCount();
    if (index - next > kAudioRingRows) {
        rows_.clear();
        firstHeld_ = index - kAudioRingRows;
        next = firstHeld_;
        ++revision_;
    }
    for (; next <= index; ++next) {
        rows_.insert(rows_.end(), row.begin(), row.begin() + kAudioBins);
    }
    // Keep two rings' worth.
    const std::int64_t keep = 2 * kAudioRingRows;
    if (heldRows() > keep) {
        const std::int64_t drop = heldRows() - keep;
        rows_.erase(rows_.begin(), rows_.begin() + drop * kAudioBins);
        firstHeld_ += drop;
    }
    liveNow_ = std::max(liveNow_, time);
    if (!onsets_.empty()) {
        const double oldest = liveNow_ - 2.0 * kAudioRingRows / rowRate_;
        for (auto& list : onsets_) {
            const auto it = std::find_if(list.begin(), list.end(), [&](const AudioOnset& o) { return o.time >= oldest; });
            list.erase(list.begin(), it);
        }
    }
}

void AudioHistory::addLiveOnset(OnsetSource source, double time, float strength) {
    if (!live_) {
        return;
    }
    auto& list = onsets_[static_cast<std::size_t>(source)];
    if (!list.empty() && time < list.back().time) {
        return;
    }
    list.push_back(AudioOnset{time, strength});
}

std::int64_t AudioHistory::newestRow(double clock) const {
    if (heldRows() == 0) {
        return -1;
    }
    std::int64_t row = 0;
    if (live_) {
        row = rowCount() - 1;
    } else {
        const double x = (clock - firstRowTime_) * rowRate_;
        if (x < 0.0) {
            return -1;
        }
        row = std::min(static_cast<std::int64_t>(std::floor(x + 1e-9)), rowCount() - 1);
    }
    return row;
}

float AudioHistory::value(std::int64_t row, int bin) const {
    if (row < firstHeld_ || row >= rowCount() || bin < 0 || bin >= kAudioBins) {
        return 0.0f;
    }
    return rows_[static_cast<std::size_t>(row - firstHeld_) * kAudioBins + static_cast<std::size_t>(bin)];
}

int AudioHistory::lastOnsets(OnsetSource source, double clock, std::span<float> ages, std::span<float> strengths) const {
    const auto& list = onsets_[static_cast<std::size_t>(source)];
    const std::size_t n = std::min(ages.size(), strengths.size());
    // The first onset strictly after `clock`; everything before it has happened.
    auto it = std::upper_bound(list.begin(), list.end(), clock,
                               [](double c, const AudioOnset& o) { return c < o.time; });
    int written = 0;
    while (it != list.begin() && static_cast<std::size_t>(written) < n) {
        --it;
        ages[static_cast<std::size_t>(written)] = static_cast<float>(clock - it->time);
        strengths[static_cast<std::size_t>(written)] = it->strength;
        ++written;
    }
    return written;
}

std::size_t AudioHistory::bytes() const {
    std::size_t b = rows_.capacity() * sizeof(float);
    for (const auto& list : onsets_) {
        b += list.capacity() * sizeof(AudioOnset);
    }
    return b;
}

namespace {

// The ring's view of one row: 0 outside [newest - kAudioRingRows + 1, newest] (the GPU holds only
// that window), else the held value.
float ringValue(const AudioHistory& audio, std::int64_t newest, std::int64_t row, int bin) {
    if (row > newest || row <= newest - kAudioRingRows) {
        return 0.0f;
    }
    return audio.value(row, bin);
}

// Row-interpolated value of integer bin `bin` at fractional row x (x <= newest).
float rowLerp(const AudioHistory& audio, std::int64_t newest, float x, int bin) {
    const float r0f = std::floor(x);
    const float f = x - r0f;
    const auto r0 = static_cast<std::int64_t>(r0f);
    const std::int64_t r1 = std::min(r0 + 1, newest);
    const float a = ringValue(audio, newest, r0, bin);
    const float b = ringValue(audio, newest, r1, bin);
    return a + (b - a) * f;
}

float delayedRow(std::int64_t newest, double rate, float delaySeconds) {
    return static_cast<float>(newest) - std::max(delaySeconds, 0.0f) * static_cast<float>(rate);
}

} // namespace

float spectrumAt(const AudioHistory& audio, double clock, float delaySeconds, float binPos) {
    const std::int64_t newest = audio.newestRow(clock);
    if (newest < 0) {
        return 0.0f;
    }
    const float x = delayedRow(newest, audio.rowRate(), delaySeconds);
    const float b = std::clamp(binPos, 0.0f, 1.0f) * static_cast<float>(kAudioBins - 1);
    const float b0f = std::floor(b);
    const float fb = b - b0f;
    const int b0 = static_cast<int>(b0f);
    const int b1 = std::min(b0 + 1, kAudioBins - 1);
    const float v0 = rowLerp(audio, newest, x, b0);
    const float v1 = rowLerp(audio, newest, x, b1);
    return v0 + (v1 - v0) * fb;
}

float spectrumRange(const AudioHistory& audio, double clock, float delaySeconds, float binLow, float binHigh) {
    const std::int64_t newest = audio.newestRow(clock);
    if (newest < 0) {
        return 0.0f;
    }
    const float x = delayedRow(newest, audio.rowRate(), delaySeconds);
    const float top = static_cast<float>(kAudioBins - 1);
    const int lo = static_cast<int>(std::floor(std::clamp(binLow, 0.0f, 1.0f) * top + 0.5f));
    const int hi = std::max(lo, static_cast<int>(std::floor(std::clamp(binHigh, 0.0f, 1.0f) * top + 0.5f)));
    float sum = 0.0f;
    for (int bin = lo; bin <= hi; ++bin) {
        sum += rowLerp(audio, newest, x, bin);
    }
    return sum / static_cast<float>(hi - lo + 1);
}

float onsetResponse(std::span<const float> ages, std::span<const float> strengths, float distance, float speed,
                    float width, float decay) {
    float sum = 0.0f;
    const std::size_t n = std::min(ages.size(), strengths.size());
    for (std::size_t k = 0; k < n; ++k) {
        const float age = ages[k];
        if (age < 0.0f) {
            continue;
        }
        float front = 1.0f;
        if (width > 0.0f) {
            const float x = (distance - age * speed) / width;
            front = std::exp(-(x * x));
        }
        sum += strengths[k] * std::exp(-std::max(decay, 0.0f) * age) * front;
    }
    return sum;
}

} // namespace avgen::spatial
