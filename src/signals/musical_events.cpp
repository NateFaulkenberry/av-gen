#include "signals/musical_events.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace avgen::signals {
namespace {
constexpr std::array<std::pair<MusicalEvent, const char*>, 11> kNames{{
    {MusicalEvent::Beat, "beat"},
    {MusicalEvent::Downbeat, "downbeat"},
    {MusicalEvent::BarStart, "bar"},
    {MusicalEvent::PhraseStart, "phrase"},
    {MusicalEvent::SectionChange, "section"},
    {MusicalEvent::EnergyRise, "energyRise"},
    {MusicalEvent::EnergyDrop, "energyDrop"},
    {MusicalEvent::Build, "build"},
    {MusicalEvent::Break, "break"},
    {MusicalEvent::Drop, "drop"},
    {MusicalEvent::Impact, "impact"},
}};

// One-pole smoothing with a time constant in seconds, so the classifier behaves the same at any
// frame rate. A per-frame coefficient would make a 30 fps offline render disagree with a 120 fps
// window about where a drop is, which for a deterministic engine is unacceptable.
float smooth(float current, float target, double dt, double tau) {
    if (tau <= 0.0) {
        return target;
    }
    const auto k = static_cast<float>(1.0 - std::exp(-dt / tau));
    return current + (target - current) * k;
}
} // namespace

const char* musicalEventName(MusicalEvent e) {
    for (const auto& [kind, name] : kNames) {
        if (kind == e) {
            return name;
        }
    }
    return "beat";
}

std::optional<MusicalEvent> musicalEventFromName(std::string_view name) {
    for (const auto& [kind, text] : kNames) {
        if (name == text) {
            return kind;
        }
    }
    return std::nullopt;
}

MusicalEventDetector::MusicalEventDetector(MusicalEventSettings settings)
    : settings_(settings) {
    moments_.reserve(8);
    reset();
}

void MusicalEventDetector::reset() {
    moments_.clear();
    for (auto& t : lastFired_) {
        t = -1e9;
    }
    shortEnergy_ = longEnergy_ = peakEnergy_ = 0.0f;
    lastCentroid_ = 0.0f;
    trendSince_ = 0.0;
    trendSign_ = 0;
    inBreak_ = false;
    breakSince_ = 0.0;
    breakEndedAt_ = 0.0;
    dropArmed_ = false;
    lastBar_ = lastPhrase_ = lastSection_ = 0;
    started_ = false;
    lastTime_ = 0.0;
}

bool MusicalEventDetector::allow(MusicalEvent e, double now) {
    const auto i = static_cast<std::size_t>(e);
    // Beats and bars are supposed to repeat; only the structural events get a cooldown, because a
    // Drop firing twice in a second is a classifier bug presenting as a visual one.
    if (e == MusicalEvent::Beat || e == MusicalEvent::Downbeat || e == MusicalEvent::BarStart) {
        return true;
    }
    return now - lastFired_[i] >= settings_.cooldownSeconds;
}

void MusicalEventDetector::emit(MusicalEvent e, double now, float strength) {
    lastFired_[static_cast<std::size_t>(e)] = now;
    moments_.push_back(MusicalMoment{e, now, std::clamp(strength, 0.0f, 1.0f)});
}

const std::vector<MusicalMoment>& MusicalEventDetector::update(const MusicalFrame& f) {
    moments_.clear();
    const double now = f.timeSeconds;
    const double dt = started_ ? std::max(now - lastTime_, 0.0) : 0.0;
    lastTime_ = now;

    // ---- energy trends -------------------------------------------------------------------------
    // Two time constants: a short one that follows the music and a long one that remembers what it
    // has been doing. Their ratio is the trend, which is far steadier than a derivative.
    const float level = std::max(f.rms, f.bass * 0.6f);
    if (!started_) {
        shortEnergy_ = longEnergy_ = peakEnergy_ = level;
        lastCentroid_ = f.spectralCentroid;
        lastBar_ = f.barCount;
        lastPhrase_ = f.phraseCount;
        lastSection_ = f.sectionCount;
        trendSince_ = now;
        started_ = true;
        return moments_;
    }
    shortEnergy_ = smooth(shortEnergy_, level, dt, 0.45);
    longEnergy_ = smooth(longEnergy_, level, dt, 4.0);
    peakEnergy_ = std::max(peakEnergy_ * static_cast<float>(std::exp(-dt / 12.0)), shortEnergy_);

    // ---- the metre -----------------------------------------------------------------------------
    if (f.beat) {
        emit(MusicalEvent::Beat, now, std::clamp(shortEnergy_, 0.0f, 1.0f));
        if (f.beatInBar == 0) {
            emit(MusicalEvent::Downbeat, now, std::clamp(shortEnergy_, 0.0f, 1.0f));
        }
    }
    if (f.barCount != lastBar_) {
        lastBar_ = f.barCount;
        emit(MusicalEvent::BarStart, now, 1.0f);
    }
    if (f.phraseCount != lastPhrase_) {
        lastPhrase_ = f.phraseCount;
        if (allow(MusicalEvent::PhraseStart, now)) {
            emit(MusicalEvent::PhraseStart, now, 1.0f);
        }
    }
    if (f.sectionCount != lastSection_) {
        lastSection_ = f.sectionCount;
        if (allow(MusicalEvent::SectionChange, now)) {
            emit(MusicalEvent::SectionChange, now, 1.0f);
        }
    }

    // ---- impacts -------------------------------------------------------------------------------
    if (f.onsetStrength >= settings_.impactOnset && allow(MusicalEvent::Impact, now)) {
        emit(MusicalEvent::Impact, now,
             std::clamp(f.onsetStrength / (settings_.impactOnset * 2.0f), 0.0f, 1.0f));
    }

    // ---- breaks --------------------------------------------------------------------------------
    // A break is energy sitting well under what this piece has recently been capable of. Measured
    // against a decaying peak rather than an absolute level, so a quiet piece is not permanently
    // in a break.
    const float relative = peakEnergy_ > 1e-5f ? shortEnergy_ / peakEnergy_ : 1.0f;
    const bool quiet = relative < settings_.breakLevel;
    if (quiet && !inBreak_) {
        inBreak_ = true;
        breakSince_ = now;
        if (allow(MusicalEvent::Break, now)) {
            emit(MusicalEvent::Break, now, std::clamp(1.0f - relative, 0.0f, 1.0f));
        }
    } else if (!quiet && inBreak_) {
        inBreak_ = false;
        breakEndedAt_ = now;
        dropArmed_ = true;
    }
    // A break that resolves into loudness is a drop, and that two-part shape is the whole
    // definition: without the break beforehand a loud bar is just a loud bar, and firing Drop on
    // every loud bar is exactly the failure this classifier exists to avoid.
    //
    // The test has to be a *window* rather than the instant the break ends. At that instant energy
    // has only just crept back over the break threshold, so it is by definition not yet loud --
    // the first version checked there and never fired once.
    if (dropArmed_) {
        if (now - breakEndedAt_ > settings_.dropWindowSeconds) {
            dropArmed_ = false; // it recovered, but only into an ordinary passage
        } else if (relative > 0.75f && allow(MusicalEvent::Drop, now)) {
            dropArmed_ = false;
            emit(MusicalEvent::Drop, now, std::clamp(relative, 0.0f, 1.0f));
        }
    }

    // ---- sustained trends ----------------------------------------------------------------------
    const float ratio = longEnergy_ > 1e-5f ? shortEnergy_ / longEnergy_ : 1.0f;
    const int sign = ratio > settings_.riseRatio ? 1 : (ratio < settings_.dropRatio ? -1 : 0);
    if (sign != trendSign_) {
        trendSign_ = sign;
        trendSince_ = now;
    } else if (sign != 0 && now - trendSince_ >= settings_.sustainSeconds) {
        const float strength = std::clamp(std::abs(ratio - 1.0f), 0.0f, 1.0f);
        if (sign > 0) {
            // Brightening as well as loudening is what separates a build from merely getting
            // louder: a build is going somewhere.
            const bool brightening = f.spectralCentroid - lastCentroid_ > settings_.buildCentroid;
            const MusicalEvent e = brightening ? MusicalEvent::Build : MusicalEvent::EnergyRise;
            if (allow(e, now)) {
                emit(e, now, strength);
                trendSince_ = now; // a sustained trend re-arms rather than firing every frame
            }
        } else if (allow(MusicalEvent::EnergyDrop, now)) {
            emit(MusicalEvent::EnergyDrop, now, strength);
            trendSince_ = now;
        }
    }
    lastCentroid_ = smooth(lastCentroid_, f.spectralCentroid, dt, 2.0);
    return moments_;
}

// ---- structure ---------------------------------------------------------------------------------

namespace {
constexpr std::array<std::pair<MusicalSection, const char*>, 9> kSectionNames{{
    {MusicalSection::Intro, "intro"},
    {MusicalSection::Build, "build"},
    {MusicalSection::Phrase, "phrase"},
    {MusicalSection::Drop, "drop"},
    {MusicalSection::Verse, "verse"},
    {MusicalSection::Breakdown, "breakdown"},
    {MusicalSection::FinalBuild, "finalBuild"},
    {MusicalSection::FinalDrop, "finalDrop"},
    {MusicalSection::Outro, "outro"},
}};

// Which events may open a section, and which of two colliding boundaries survives. The list is
// short on purpose: Beat, Downbeat, BarStart, PhraseStart, EnergyRise, EnergyDrop and Impact are
// all absent, and their absence is the entire reason this produces a handful of sections over a
// piece instead of one per bar.
//
// Priority resolves a collision rather than time order, because the detector emits Break before the
// Drop it resolves into and a first-wins rule would keep the wrong one every single time.
int boundaryPriority(MusicalEvent e) {
    switch (e) {
    case MusicalEvent::Drop:
        return 4;
    case MusicalEvent::Break:
        return 3;
    case MusicalEvent::Build:
        return 2;
    case MusicalEvent::SectionChange:
        return 1;
    default:
        return 0; // not a boundary at all
    }
}

MusicalSection sectionForEvent(MusicalEvent e) {
    switch (e) {
    case MusicalEvent::Drop:
        return MusicalSection::Drop;
    case MusicalEvent::Break:
        return MusicalSection::Breakdown;
    case MusicalEvent::Build:
        return MusicalSection::Build;
    default:
        return MusicalSection::Verse;
    }
}

struct Boundary {
    double time = 0.0;
    MusicalSection kind = MusicalSection::Verse;
    float intensity = 0.5f;
    int priority = 0;
};
} // namespace

const char* musicalSectionName(MusicalSection s) {
    for (const auto& [kind, name] : kSectionNames) {
        if (kind == s) {
            return name;
        }
    }
    return "phrase";
}

std::optional<MusicalSection> musicalSectionFromName(std::string_view name) {
    for (const auto& [kind, text] : kSectionNames) {
        if (name == text) {
            return kind;
        }
    }
    return std::nullopt;
}

double MusicalStructure::durationSeconds() const {
    double end = 0.0;
    for (const auto& s : sections) {
        end = std::max(end, s.endSeconds());
    }
    return end;
}

const StructureSection* MusicalStructure::at(double seconds) const {
    for (const auto& s : sections) {
        if (seconds >= s.startSeconds && seconds < s.endSeconds()) {
            return &s;
        }
    }
    return nullptr;
}

int MusicalStructure::count(MusicalSection kind) const {
    int n = 0;
    for (const auto& s : sections) {
        if (s.kind == kind) {
            ++n;
        }
    }
    return n;
}

MusicalStructure MusicalStructure::fromMoments(std::span<const MusicalMoment> moments,
                                               double totalSeconds, StructureSettings settings) {
    MusicalStructure out;
    if (!(totalSeconds > 0.0)) {
        return out;
    }

    std::vector<double> phraseStarts;
    std::vector<Boundary> raw;
    for (const auto& m : moments) {
        if (m.timeSeconds < 0.0 || m.timeSeconds >= totalSeconds) {
            continue;
        }
        if (m.event == MusicalEvent::PhraseStart) {
            // Phrases do not open a section; they say where a boundary is *allowed* to land. A cut
            // that arrives a beat and a half after the phrase turned over reads as a mistake to
            // people who could not name what is wrong with it.
            phraseStarts.push_back(m.timeSeconds);
            continue;
        }
        const int priority = boundaryPriority(m.event);
        if (priority == 0) {
            continue;
        }
        raw.push_back(Boundary{m.timeSeconds, sectionForEvent(m.event),
                               std::clamp(m.strength, 0.0f, 1.0f), priority});
    }
    std::stable_sort(raw.begin(), raw.end(),
                     [](const Boundary& a, const Boundary& b) { return a.time < b.time; });

    // Snap to the nearest phrase start, then collapse boundaries that landed on top of each other.
    for (auto& b : raw) {
        double best = b.time;
        double bestGap = settings.phraseSnapSeconds;
        for (const double p : phraseStarts) {
            const double gap = std::abs(p - b.time);
            if (gap < bestGap) {
                bestGap = gap;
                best = p;
            }
        }
        b.time = best;
    }
    std::stable_sort(raw.begin(), raw.end(),
                     [](const Boundary& a, const Boundary& b) { return a.time < b.time; });

    std::vector<Boundary> merged;
    for (const auto& b : raw) {
        if (!merged.empty() && b.time - merged.back().time < settings.mergeSeconds) {
            if (b.priority > merged.back().priority) {
                // Keep the louder claim but the earlier time: the drop is where the break ended.
                const double when = merged.back().time;
                merged.back() = b;
                merged.back().time = when;
            }
            continue;
        }
        merged.push_back(b);
    }

    // Absorb sections too short to hold a shot. A build is allowed to be brief because a build
    // exists to end -- the drop after it is the point -- but everything else that cannot survive
    // `minSectionSeconds` is a boundary the music did not really make.
    std::vector<Boundary> kept;
    for (std::size_t i = 0; i < merged.size(); ++i) {
        const double next = (i + 1 < merged.size()) ? merged[i + 1].time : totalSeconds;
        const double span = next - merged[i].time;
        const bool feedsADrop = merged[i].kind == MusicalSection::Build && i + 1 < merged.size() &&
                                merged[i + 1].kind == MusicalSection::Drop;
        const double floorSeconds =
            feedsADrop ? settings.minBuildSeconds : settings.minSectionSeconds;
        if (span < floorSeconds && merged[i].priority < 4) {
            continue; // a drop is never absorbed; it is the thing everything else is arranged around
        }
        kept.push_back(merged[i]);
    }

    // Promote the last build and the last drop, but only once the piece is far enough through that
    // "last" means something. A single drop three seconds in is a drop, not a finale.
    const double finalAfter = totalSeconds * settings.finalFraction;
    for (auto it = kept.rbegin(); it != kept.rend(); ++it) {
        if (it->kind == MusicalSection::Drop && it->time >= finalAfter) {
            it->kind = MusicalSection::FinalDrop;
            break;
        }
    }
    for (auto it = kept.rbegin(); it != kept.rend(); ++it) {
        if (it->kind == MusicalSection::Build && it->time >= finalAfter) {
            it->kind = MusicalSection::FinalBuild;
            break;
        }
    }

    // The opening is an intro whatever the detector thought, because nothing has happened yet.
    const double firstBoundary = kept.empty() ? totalSeconds : kept.front().time;
    if (firstBoundary > 0.0) {
        out.sections.push_back(StructureSection{MusicalSection::Intro, 0.0, firstBoundary, 0.25f});
    }
    for (std::size_t i = 0; i < kept.size(); ++i) {
        const double end = (i + 1 < kept.size()) ? kept[i + 1].time : totalSeconds;
        out.sections.push_back(
            StructureSection{kept[i].kind, kept[i].time, end - kept[i].time, kept[i].intensity});
    }

    // Energy trends describe rather than divide: they raise or lower the intensity of whatever
    // section they land in. This is the whole of their influence, which is why a piece that wobbles
    // in loudness does not acquire a section boundary every time it does so.
    for (const auto& m : moments) {
        if (m.event != MusicalEvent::EnergyRise && m.event != MusicalEvent::EnergyDrop) {
            continue;
        }
        for (auto& s : out.sections) {
            if (m.timeSeconds >= s.startSeconds && m.timeSeconds < s.endSeconds()) {
                const float sign = m.event == MusicalEvent::EnergyRise ? 1.0f : -1.0f;
                s.intensity = std::clamp(s.intensity + sign * 0.25f * m.strength, 0.0f, 1.0f);
                break;
            }
        }
    }
    return out;
}

} // namespace avgen::signals
