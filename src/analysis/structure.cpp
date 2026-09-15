#include "analysis/structure.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace avgen::analysis {
namespace {

// Chroma is taken from this range only. Below it the FFT bins are too coarse to place a pitch class
// at all -- at a 2048-point window and 48 kHz a bin is 23 Hz, and the semitone at A1 is 3.3 Hz wide
// -- and above it what dominates is harmonics and cymbals, which say more about timbre than about
// harmony. This is the band where "which note is this" is answerable.
constexpr float kChromaLowHz = 110.0f;
constexpr float kChromaHighHz = 2000.0f;

float median(std::vector<float>& v) {
    if (v.empty()) {
        return 0.0f;
    }
    const std::size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(mid), v.end());
    return v[mid];
}

// Median absolute deviation. Used instead of a standard deviation everywhere a threshold is set,
// because the thing being thresholded is a curve whose whole point is that it has large outliers --
// and a standard deviation computed over a curve with large outliers is mostly a measure of the
// outliers.
float medianAbsoluteDeviation(std::span<const float> v, float centre) {
    if (v.empty()) {
        return 0.0f;
    }
    std::vector<float> deviations(v.size());
    for (std::size_t i = 0; i < v.size(); ++i) {
        deviations[i] = std::fabs(v[i] - centre);
    }
    return median(deviations);
}

void normalise(std::span<float> v) {
    float sum = 0.0f;
    for (const float x : v) {
        sum += x * x;
    }
    const float length = std::sqrt(sum);
    // A silent frame stays all-zero rather than becoming a normalised noise floor. A zero vector has
    // a cosine similarity of zero with everything, which is the right answer: silence is not
    // similar to any music, and it is not similar to other silence either as far as *structure* is
    // concerned.
    if (length < 1e-6f) {
        return;
    }
    for (float& x : v) {
        x /= length;
    }
}

// Rescales to 0..1 against the sequence's own range. Returns all-zeroes for a flat input rather than
// dividing by nothing.
void rescale(std::vector<float>& v) {
    if (v.empty()) {
        return;
    }
    const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
    const float span = *hi - *lo;
    if (span < 1e-9f) {
        std::fill(v.begin(), v.end(), 0.0f);
        return;
    }
    const float low = *lo;
    for (float& x : v) {
        x = (x - low) / span;
    }
}

} // namespace

std::span<const SectionFunction> allSectionFunctions() {
    static constexpr std::array<SectionFunction, 13> kAll{
        SectionFunction::Intro,       SectionFunction::Verse,       SectionFunction::PreChorus,
        SectionFunction::Build,       SectionFunction::Chorus,      SectionFunction::Drop,
        SectionFunction::Break,       SectionFunction::Bridge,      SectionFunction::Instrumental,
        SectionFunction::Breakdown,   SectionFunction::FinalChorus, SectionFunction::Outro,
        SectionFunction::Other};
    // `Other` is the last enumerator, so this stops compiling if one is added without being listed.
    static_assert(kAll.size() == static_cast<std::size_t>(SectionFunction::Other) + 1);
    return kAll;
}

const char* sectionFunctionName(SectionFunction f) {
    switch (f) {
    case SectionFunction::Intro: return "intro";
    case SectionFunction::Verse: return "verse";
    case SectionFunction::PreChorus: return "preChorus";
    case SectionFunction::Build: return "build";
    case SectionFunction::Chorus: return "chorus";
    case SectionFunction::Drop: return "drop";
    case SectionFunction::Break: return "break";
    case SectionFunction::Bridge: return "bridge";
    case SectionFunction::Instrumental: return "instrumental";
    case SectionFunction::Breakdown: return "breakdown";
    case SectionFunction::FinalChorus: return "finalChorus";
    case SectionFunction::Outro: return "outro";
    case SectionFunction::Other: break;
    }
    return "other";
}

std::optional<SectionFunction> sectionFunctionFromName(std::string_view name) {
    using F = SectionFunction;
    for (const F f : {F::Intro, F::Verse, F::PreChorus, F::Build, F::Chorus, F::Drop, F::Break,
                      F::Bridge, F::Instrumental, F::Breakdown, F::FinalChorus, F::Outro, F::Other}) {
        if (name == sectionFunctionName(f)) {
            return f;
        }
    }
    return std::nullopt;
}

const char* sectionOriginName(SectionOrigin o) {
    switch (o) {
    case SectionOrigin::Detected: return "detected";
    case SectionOrigin::Refined: return "refined";
    case SectionOrigin::Authored: break;
    }
    return "authored";
}

std::optional<SectionOrigin> sectionOriginFromName(std::string_view name) {
    for (const SectionOrigin o : {SectionOrigin::Detected, SectionOrigin::Refined, SectionOrigin::Authored}) {
        if (name == sectionOriginName(o)) {
            return o;
        }
    }
    return std::nullopt;
}

Result<void> SongStructure::validate() const {
    if (sections.empty()) {
        return {}; // a structure with nothing in it is empty, not invalid
    }
    for (std::size_t i = 0; i < sections.size(); ++i) {
        const SongSection& s = sections[i];
        if (!(s.endSeconds > s.startSeconds)) {
            return fail("song structure: section {} ('{}') ends at {} and starts at {}", i,
                        sectionFunctionName(s.function), s.endSeconds, s.startSeconds);
        }
        if (i > 0 && std::fabs(s.startSeconds - sections[i - 1].endSeconds) > 1e-6) {
            // Gapless *and* non-overlapping in one check, because they are the same property: the
            // start of one section is the end of the last one, and any daylight between them is a
            // stretch of the piece that belongs to nothing.
            return fail("song structure: section {} starts at {} but section {} ended at {}", i,
                        s.startSeconds, i - 1, sections[i - 1].endSeconds);
        }
    }
    return {};
}

const SongSection* SongStructure::sectionAt(double seconds) const {
    for (const SongSection& s : sections) {
        if (seconds >= s.startSeconds && seconds < s.endSeconds) {
            return &s;
        }
    }
    // The last section owns its own end, so a query exactly at the track duration answers.
    if (!sections.empty() && std::fabs(seconds - sections.back().endSeconds) < 1e-9) {
        return &sections.back();
    }
    return nullptr;
}

std::vector<std::array<float, 12>> chromagram(const AnalysisTrack& track) {
    std::vector<std::array<float, 12>> out;
    const auto& frames = track.frames();
    out.reserve(frames.size());
    const auto sampleRate = static_cast<float>(track.config().sampleRate);
    const auto windowSize = static_cast<float>(track.config().windowSize);

    for (const AnalysisFrame& frame : frames) {
        std::array<float, 12> chroma{};
        for (std::size_t bin = 1; bin < frame.magnitude.size(); ++bin) {
            const float hz = static_cast<float>(bin) * sampleRate / windowSize;
            if (hz < kChromaLowHz || hz > kChromaHighHz) {
                continue;
            }
            // Semitones above A4; the pitch class is that modulo twelve. `std::lround` rather than a
            // truncation, so a bin lands on the nearest semitone rather than always the one below.
            const float semitones = 12.0f * std::log2(hz / 440.0f);
            auto pitchClass = static_cast<int>(std::lround(semitones)) % 12;
            if (pitchClass < 0) {
                pitchClass += 12;
            }
            chroma[static_cast<std::size_t>(pitchClass)] += frame.magnitude[bin];
        }
        normalise(chroma);
        out.push_back(chroma);
    }
    return out;
}

std::vector<std::vector<float>> timbregram(const AnalysisTrack& track) {
    std::vector<std::vector<float>> out;
    const auto& frames = track.frames();
    out.reserve(frames.size());
    for (const AnalysisFrame& frame : frames) {
        const std::size_t n = std::max<std::size_t>(frame.bandCount, 1);
        std::vector<float> timbre(n, 0.0f);
        for (std::size_t i = 0; i < n && i < kMaxBands; ++i) {
            // Log-compressed, because the ear is and because a linear band vector is dominated by
            // whichever band holds the bass on every single frame of every single track.
            timbre[i] = std::log10(1.0f + 100.0f * frame.bandsRaw[i]);
        }
        // Mean removed, so what is left is the *shape* of the spectrum rather than its level. Two
        // passages at different volumes with the same instrumentation should look alike here; that
        // is the whole job of this feature, and energy is carried separately for the cases where
        // loudness is the thing that changed.
        const float mean = std::accumulate(timbre.begin(), timbre.end(), 0.0f) /
                           static_cast<float>(timbre.size());
        for (float& x : timbre) {
            x -= mean;
        }
        normalise(timbre);
        out.push_back(std::move(timbre));
    }
    return out;
}

std::vector<std::vector<float>> beatSynchronous(const std::vector<std::vector<float>>& perFrame,
                                                std::span<const double> frameTimes,
                                                std::span<const double> beatTimes) {
    std::vector<std::vector<float>> out;
    if (perFrame.empty() || beatTimes.size() < 2 || frameTimes.size() != perFrame.size()) {
        return out;
    }
    const std::size_t dims = perFrame.front().size();
    out.reserve(beatTimes.size() - 1);

    std::size_t cursor = 0;
    std::vector<float> column;
    for (std::size_t b = 0; b + 1 < beatTimes.size(); ++b) {
        const double from = beatTimes[b];
        const double to = beatTimes[b + 1];
        while (cursor < frameTimes.size() && frameTimes[cursor] < from) {
            ++cursor;
        }
        const std::size_t first = cursor;
        std::size_t last = cursor;
        while (last < frameTimes.size() && frameTimes[last] < to) {
            ++last;
        }

        std::vector<float> aggregate(dims, 0.0f);
        if (last > first) {
            for (std::size_t d = 0; d < dims; ++d) {
                column.clear();
                for (std::size_t f = first; f < last; ++f) {
                    if (d < perFrame[f].size()) {
                        column.push_back(perFrame[f][d]);
                    }
                }
                aggregate[d] = median(column);
            }
        }
        out.push_back(std::move(aggregate));
    }
    return out;
}

std::vector<float> selfSimilarity(const std::vector<std::vector<float>>& beatFeatures) {
    const std::size_t n = beatFeatures.size();
    std::vector<float> ssm(n * n, 0.0f);
    // Pre-normalised copies, so the inner loop is a dot product rather than a dot product and two
    // square roots. n is beats, so this is a few hundred rows, but the loop below is O(n^2 * dims).
    std::vector<std::vector<float>> unit = beatFeatures;
    for (std::vector<float>& row : unit) {
        normalise(row);
    }
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i; j < n; ++j) {
            float dot = 0.0f;
            const std::size_t dims = std::min(unit[i].size(), unit[j].size());
            for (std::size_t d = 0; d < dims; ++d) {
                dot += unit[i][d] * unit[j][d];
            }
            // Clamped at zero: a negative cosine means "pointing the other way", which for these
            // features is not more dissimilar than orthogonal, and letting it go negative makes the
            // novelty kernel below produce peaks where nothing happened.
            const float similarity = std::max(dot, 0.0f);
            ssm[i * n + j] = similarity;
            ssm[j * n + i] = similarity;
        }
    }
    return ssm;
}

std::vector<float> footeNovelty(std::span<const float> ssm, int n, int kernelBeats) {
    std::vector<float> novelty(static_cast<std::size_t>(std::max(n, 0)), 0.0f);
    const int half = std::max(kernelBeats / 2, 1);
    if (n <= 0 || static_cast<int>(ssm.size()) < n * n) {
        return novelty;
    }

    // Foote's checkerboard: +1 on the two diagonal quadrants (past-to-past, future-to-future) and -1
    // on the two off-diagonal ones (past-to-future). It is large where the music before a point
    // resembles itself, the music after resembles itself, and the two do not resemble each other --
    // which is what a section boundary is.
    //
    // Gaussian-tapered, so the kernel's own edges do not register as structure. Without the taper a
    // boundary produces a peak flanked by two troughs and the peak-picker finds all three.
    std::vector<float> kernel(static_cast<std::size_t>(2 * half * 2 * half), 0.0f);
    const auto sigma = static_cast<float>(half) * 0.5f;
    for (int a = -half; a < half; ++a) {
        for (int b = -half; b < half; ++b) {
            const float taper = std::exp(-(static_cast<float>(a * a + b * b)) / (2.0f * sigma * sigma));
            const float sign = (a < 0) == (b < 0) ? 1.0f : -1.0f;
            kernel[static_cast<std::size_t>((a + half) * 2 * half + (b + half))] = sign * taper;
        }
    }

    for (int centre = 0; centre < n; ++centre) {
        float sum = 0.0f;
        for (int a = -half; a < half; ++a) {
            const int i = centre + a;
            if (i < 0 || i >= n) {
                continue;
            }
            for (int b = -half; b < half; ++b) {
                const int j = centre + b;
                if (j < 0 || j >= n) {
                    continue;
                }
                sum += kernel[static_cast<std::size_t>((a + half) * 2 * half + (b + half))] *
                       ssm[static_cast<std::size_t>(i) * static_cast<std::size_t>(n) +
                           static_cast<std::size_t>(j)];
            }
        }
        novelty[static_cast<std::size_t>(centre)] = std::max(sum, 0.0f);
    }
    rescale(novelty);
    return novelty;
}

std::vector<int> pickBoundaries(std::span<const float> novelty, float thresholdMad,
                                int minSeparation, int maxCount) {
    std::vector<int> peaks;
    const auto n = static_cast<int>(novelty.size());
    if (n < 3) {
        return peaks;
    }
    std::vector<float> copy(novelty.begin(), novelty.end());
    const float centre = median(copy);
    float spread = medianAbsoluteDeviation(novelty, centre);
    float threshold = centre + thresholdMad * spread;
    if (spread < 1e-6f) {
        // A median absolute deviation of zero means *more than half the samples are identical*, which
        // is not the same as "the curve is flat" -- a curve sitting at a constant with three sharp
        // peaks in it has a MAD of zero and three obvious boundaries. Found by a test with exactly
        // that shape: it returned nothing.
        //
        // So fall back to the range above the median. A genuinely flat curve has no range either and
        // still returns nothing, which is the case the guard was written for.
        const float highest = *std::max_element(novelty.begin(), novelty.end());
        if (highest - centre < 1e-6f) {
            return peaks;
        }
        spread = highest - centre;
        threshold = centre + 0.5f * spread;
    }

    // Every local maximum over the threshold, strongest first, taking each only if it is far enough
    // from one already taken. Strongest-first rather than left-to-right, because with a minimum
    // separation the order decides the answer -- and the strongest boundary in a neighbourhood is
    // the one a listener would name.
    std::vector<int> candidates;
    for (int i = 1; i + 1 < n; ++i) {
        if (novelty[static_cast<std::size_t>(i)] >= threshold &&
            novelty[static_cast<std::size_t>(i)] >= novelty[static_cast<std::size_t>(i - 1)] &&
            novelty[static_cast<std::size_t>(i)] > novelty[static_cast<std::size_t>(i + 1)]) {
            candidates.push_back(i);
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(), [&](int a, int b) {
        return novelty[static_cast<std::size_t>(a)] > novelty[static_cast<std::size_t>(b)];
    });
    for (const int c : candidates) {
        if (static_cast<int>(peaks.size()) >= maxCount) {
            break;
        }
        const bool tooClose = std::any_of(peaks.begin(), peaks.end(), [&](int p) {
            return std::abs(p - c) < minSeparation;
        });
        if (!tooClose) {
            peaks.push_back(c);
        }
    }
    std::sort(peaks.begin(), peaks.end());
    return peaks;
}

namespace {

// A segment of the beat grid, before it becomes a section.
struct Segment {
    int firstBeat = 0;
    int lastBeat = 0; // exclusive
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    std::vector<float> signature; // mean chroma ++ timbre, normalised: what this segment sounds like
    float energy = 0.0f;
    float density = 0.0f;
    int group = -1;
    int occurrence = 0;
    float startConfidence = 1.0f;
    float endConfidence = 1.0f;
};

float cosine(std::span<const float> a, std::span<const float> b) {
    float dot = 0.0f;
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        dot += a[i] * b[i];
    }
    return dot;
}

// Greedy single-link grouping over segment signatures. Deliberately not k-means: the number of
// repetition families is exactly what we do not know, and a method that has to be told it would be
// answering the question with its own input. A threshold on cosine similarity asks the only question
// there is -- "is this the same music as that?" -- and leaves a segment ungrouped when the answer is
// no, which is a real answer rather than a failure.
void groupByRepetition(std::vector<Segment>& segments, float threshold) {
    int nextGroup = 0;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (segments[i].group >= 0) {
            continue;
        }
        std::vector<std::size_t> family{i};
        for (std::size_t j = i + 1; j < segments.size(); ++j) {
            if (segments[j].group >= 0) {
                continue;
            }
            if (cosine(segments[i].signature, segments[j].signature) >= threshold) {
                family.push_back(j);
            }
        }
        // A family of one is not a repetition. Left at -1 so "this material appears once" and "this
        // material repeats" are distinguishable downstream, which is most of what tells a bridge
        // from a chorus.
        if (family.size() < 2) {
            continue;
        }
        for (std::size_t k = 0; k < family.size(); ++k) {
            segments[family[k]].group = nextGroup;
            segments[family[k]].occurrence = static_cast<int>(k);
        }
        ++nextGroup;
    }
}

} // namespace

Result<SongStructure> detectStructure(const AnalysisTrack& track, const StructureConfig& config) {
    const auto& frames = track.frames();
    if (frames.empty()) {
        return fail("song structure: the track has no analysis frames");
    }
    const std::vector<double>& beatTimes = track.beats().beatTimes;
    if (beatTimes.size() < 8) {
        // Refused rather than quietly analysed at hop resolution. Everything below is beat
        // synchronous; running it on a grid that is not beats would be a different algorithm
        // wearing this one's name, and its output would be indistinguishable from this one's.
        return fail("song structure: needs a beat grid, and this track has {} beat(s). Run the "
                    "offline beat tracker first",
                    beatTimes.size());
    }

    std::vector<double> frameTimes;
    frameTimes.reserve(frames.size());
    for (const AnalysisFrame& f : frames) {
        frameTimes.push_back(f.timeSeconds);
    }

    const auto chroma = chromagram(track);
    std::vector<std::vector<float>> chromaRows;
    chromaRows.reserve(chroma.size());
    for (const auto& c : chroma) {
        chromaRows.emplace_back(c.begin(), c.end());
    }
    const auto timbre = timbregram(track);

    std::vector<std::vector<float>> energyRows;
    energyRows.reserve(frames.size());
    for (const AnalysisFrame& f : frames) {
        energyRows.push_back({f.rms, f.onset ? 1.0f : 0.0f});
    }

    const auto beatChroma = beatSynchronous(chromaRows, frameTimes, beatTimes);
    const auto beatTimbre = beatSynchronous(timbre, frameTimes, beatTimes);
    const auto beatEnergy = beatSynchronous(energyRows, frameTimes, beatTimes);
    const auto beatCount = static_cast<int>(beatChroma.size());
    if (beatCount < 8) {
        return fail("song structure: only {} beat interval(s) carry features", beatCount);
    }

    // Three matrices, weighted into one. Separately rather than by concatenating the features,
    // because concatenation would let whichever family has more dimensions dominate -- twelve chroma
    // bins against eight bands is not a considered weighting, it is an accident of the feature
    // sizes.
    const auto ssmChroma = selfSimilarity(beatChroma);
    const auto ssmTimbre = selfSimilarity(beatTimbre);
    const auto ssmEnergy = selfSimilarity(beatEnergy);
    const float weightSum = std::max(config.chromaWeight + config.timbreWeight + config.energyWeight, 1e-6f);
    std::vector<float> ssm(static_cast<std::size_t>(beatCount) * static_cast<std::size_t>(beatCount), 0.0f);
    for (std::size_t i = 0; i < ssm.size(); ++i) {
        ssm[i] = (config.chromaWeight * ssmChroma[i] + config.timbreWeight * ssmTimbre[i] +
                  config.energyWeight * ssmEnergy[i]) / weightSum;
    }

    // Several kernel widths, summed. A section change is visible at the scale of the thing that
    // changed: sixteen beats catches a turnaround and misses a key change, sixty-four does the
    // reverse. Summing rather than choosing means a boundary visible at any scale survives, and one
    // visible at every scale outranks it -- which is the ordering the peak-picker wants anyway.
    std::vector<float> novelty(static_cast<std::size_t>(beatCount), 0.0f);
    for (const int width : config.kernelBeats) {
        if (width < 4 || width >= beatCount) {
            continue;
        }
        const auto curve = footeNovelty(ssm, beatCount, width);
        for (std::size_t i = 0; i < novelty.size() && i < curve.size(); ++i) {
            novelty[i] += curve[i];
        }
    }
    rescale(novelty);

    const double trackSeconds = frames.back().timeSeconds;
    const double beatSeconds = (beatTimes.back() - beatTimes.front()) /
                               static_cast<double>(beatTimes.size() - 1);
    const int minSeparation = std::max(
        static_cast<int>(std::lround(config.minSectionSeconds / std::max(beatSeconds, 1e-3))), 2);
    const auto peaks = pickBoundaries(novelty, config.peakThresholdMad, minSeparation,
                                      std::max(config.maxSections - 1, 0));

    // ---- segments -------------------------------------------------------------------------------

    std::vector<int> edges{0};
    edges.insert(edges.end(), peaks.begin(), peaks.end());
    edges.push_back(beatCount);

    std::vector<Segment> segments;
    for (std::size_t i = 0; i + 1 < edges.size(); ++i) {
        Segment seg;
        seg.firstBeat = edges[i];
        seg.lastBeat = edges[i + 1];
        // The *first* section starts at zero, not at the first beat the tracker found. A beat grid
        // begins at the first beat it is confident about -- 0.49 s into the fixture here -- and the
        // audio before that is music somebody wrote. Left at the beat time it orphaned half a second
        // of every track, which `validate()` could not catch because the structure was still
        // internally consistent: ordered, gapless, non-overlapping, and starting in the wrong place.
        seg.startSeconds = i == 0 ? 0.0 : beatTimes[static_cast<std::size_t>(seg.firstBeat)];
        // The last segment runs to the end of the audio, not to the last beat: the beat tracker
        // stops at the last beat it is confident about and the music usually carries on past it.
        seg.endSeconds = seg.lastBeat >= beatCount
                             ? trackSeconds
                             : beatTimes[static_cast<std::size_t>(seg.lastBeat)];
        // The boundary that opens this segment is the one the novelty curve found; the track's own
        // start and end are certain by construction and carry 1.
        seg.startConfidence = i == 0 ? 1.0f : novelty[static_cast<std::size_t>(seg.firstBeat)];
        seg.endConfidence = seg.lastBeat >= beatCount
                                ? 1.0f
                                : novelty[static_cast<std::size_t>(seg.lastBeat)];

        const std::size_t dims = beatChroma.front().size() + beatTimbre.front().size();
        seg.signature.assign(dims, 0.0f);
        float rms = 0.0f;
        float onsets = 0.0f;
        int counted = 0;
        for (int b = seg.firstBeat; b < seg.lastBeat && b < beatCount; ++b) {
            const auto bi = static_cast<std::size_t>(b);
            for (std::size_t d = 0; d < beatChroma[bi].size(); ++d) {
                seg.signature[d] += beatChroma[bi][d];
            }
            for (std::size_t d = 0; d < beatTimbre[bi].size(); ++d) {
                seg.signature[beatChroma[bi].size() + d] += beatTimbre[bi][d];
            }
            if (!beatEnergy[bi].empty()) {
                rms += beatEnergy[bi][0];
            }
            if (beatEnergy[bi].size() > 1) {
                onsets += beatEnergy[bi][1];
            }
            ++counted;
        }
        const auto divisor = static_cast<float>(std::max(counted, 1));
        normalise(seg.signature);
        seg.energy = rms / divisor;
        seg.density = onsets / divisor;
        segments.push_back(std::move(seg));
    }
    if (segments.empty()) {
        return fail("song structure: no segments were produced");
    }

    // Energy and density rescaled across the piece, because "high energy" is a claim about this
    // track and not about audio in general. A quiet ballad has a loudest section too.
    {
        std::vector<float> e, d;
        e.reserve(segments.size());
        d.reserve(segments.size());
        for (const Segment& s : segments) {
            e.push_back(s.energy);
            d.push_back(s.density);
        }
        rescale(e);
        rescale(d);
        for (std::size_t i = 0; i < segments.size(); ++i) {
            segments[i].energy = e[i];
            segments[i].density = d[i];
        }
    }

    groupByRepetition(segments, 0.86f);

    // ---- labels ---------------------------------------------------------------------------------
    //
    // Structural evidence first, position second, energy third -- in that order, because that is the
    // order of how much each one actually tells you. Repetition is the strongest signal there is:
    // material that comes back is doing a job, and material that comes back loud is doing the job a
    // chorus does.
    //
    // Every branch sets a confidence from the margin it decided on, so a marginal call reports
    // itself as one. `Other` carries a confidence of zero, which is not a low score -- it is the
    // absence of a claim, and it is the right answer for music without verses and choruses.

    const bool anyRepetition = std::any_of(segments.begin(), segments.end(),
                                           [](const Segment& s) { return s.group >= 0; });
    std::vector<float> energies;
    energies.reserve(segments.size());
    for (const Segment& s : segments) {
        energies.push_back(s.energy);
    }
    std::vector<float> energyCopy = energies;
    const float medianEnergy = median(energyCopy);

    // How many times each family recurs, and where its last occurrence is.
    std::vector<int> familySize;
    std::vector<int> familyLast;
    for (const Segment& s : segments) {
        if (s.group < 0) {
            continue;
        }
        const auto g = static_cast<std::size_t>(s.group);
        if (familySize.size() <= g) {
            familySize.resize(g + 1, 0);
            familyLast.resize(g + 1, 0);
        }
        ++familySize[g];
        familyLast[g] = std::max(familyLast[g], s.occurrence);
    }

    SongStructure out;
    out.durationSeconds = trackSeconds;
    out.tempoBpm = track.beats().tempoBpm;
    out.tempoConfidence = track.beats().confidence;
    out.beatTimes = beatTimes;
    out.novelty = novelty;

    for (std::size_t i = 0; i < segments.size(); ++i) {
        const Segment& seg = segments[i];
        SongSection section;
        section.startSeconds = seg.startSeconds;
        section.endSeconds = seg.endSeconds;
        section.startConfidence = std::clamp(seg.startConfidence, 0.0f, 1.0f);
        section.endConfidence = std::clamp(seg.endConfidence, 0.0f, 1.0f);
        section.repetitionGroup = seg.group;
        section.occurrence = seg.occurrence;
        section.energy = seg.energy;
        section.density = seg.density;
        section.origin = SectionOrigin::Detected;

        const bool first = i == 0;
        const bool last = i + 1 == segments.size();
        const float previousEnergy = first ? 0.0f : segments[i - 1].energy;
        const float nextEnergy = last ? 0.0f : segments[i + 1].energy;

        if (!anyRepetition) {
            // No repeated material anywhere: ambient, through-composed, orchestral, experimental.
            // Naming verses here would be inventing a form the music does not have. Position still
            // supports the two labels that are positional rather than formal.
            section.function = first ? SectionFunction::Intro
                              : last  ? SectionFunction::Outro
                                      : SectionFunction::Other;
            section.labelConfidence = first || last ? 0.5f : 0.0f;
        } else if (seg.group >= 0 && familySize[static_cast<std::size_t>(seg.group)] >= 2 &&
                   seg.energy >= medianEnergy) {
            // Repeated and loud. Chorus unless it is the family's last appearance, which is the one
            // a listener hears as the final chorus.
            const bool isLast = seg.occurrence == familyLast[static_cast<std::size_t>(seg.group)];
            const bool droppedInto = !first && seg.energy - previousEnergy > 0.45f && seg.density > 0.6f;
            section.function = droppedInto     ? SectionFunction::Drop
                               : isLast && i > segments.size() / 2 ? SectionFunction::FinalChorus
                                                                   : SectionFunction::Chorus;
            // The margin the call was made on: how far above the median this section's energy is,
            // and how many times the material recurs.
            section.labelConfidence = std::clamp(
                0.45f + (seg.energy - medianEnergy) +
                    0.1f * static_cast<float>(familySize[static_cast<std::size_t>(seg.group)] - 2),
                0.0f, 1.0f);
        } else if (seg.group >= 0 && familySize[static_cast<std::size_t>(seg.group)] >= 2) {
            section.function = SectionFunction::Verse;
            section.labelConfidence = std::clamp(0.45f + (medianEnergy - seg.energy), 0.0f, 1.0f);
        } else if (first) {
            section.function = SectionFunction::Intro;
            section.labelConfidence = std::clamp(0.5f + (medianEnergy - seg.energy), 0.0f, 1.0f);
        } else if (last) {
            section.function = SectionFunction::Outro;
            section.labelConfidence = std::clamp(0.5f + (medianEnergy - seg.energy), 0.0f, 1.0f);
        } else if (nextEnergy - seg.energy > 0.3f && seg.energy >= previousEnergy) {
            // Unique, rising, and handing over to something louder. A build if it climbs a long way,
            // a pre-chorus if it is merely on the way up.
            section.function = nextEnergy - seg.energy > 0.5f ? SectionFunction::Build
                                                              : SectionFunction::PreChorus;
            section.labelConfidence = std::clamp(0.3f + (nextEnergy - seg.energy), 0.0f, 1.0f);
        } else if (previousEnergy - seg.energy > 0.35f) {
            // The floor fell out. A break if it is brief, a breakdown if the piece sits in it.
            section.function = seg.endSeconds - seg.startSeconds > 16.0 ? SectionFunction::Breakdown
                                                            : SectionFunction::Break;
            section.labelConfidence = std::clamp(0.3f + (previousEnergy - seg.energy), 0.0f, 1.0f);
        } else if (i > segments.size() / 2) {
            // Unique material, late, not a build and not a break: the shape of a bridge.
            section.function = SectionFunction::Bridge;
            section.labelConfidence = 0.35f;
        } else {
            section.function = SectionFunction::Other;
            section.labelConfidence = 0.0f;
        }

        out.sections.push_back(std::move(section));
    }

    if (auto ok = out.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    return out;
}

} // namespace avgen::analysis
