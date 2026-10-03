#include "sonic/live.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#if defined(__APPLE__)
#include <mach/mach_time.h>
#endif

namespace avgen::sonic {

std::uint64_t hostNowNs() {
#if defined(__APPLE__)
    // The clock CoreMIDI stamps packets with (see control/midi_coremidi.cpp), so a message's timestamp and this
    // clock are directly comparable, across processes too.
    static const mach_timebase_info_data_t timebase = [] {
        mach_timebase_info_data_t info{};
        mach_timebase_info(&info);
        return info;
    }();
    const std::uint64_t ticks = mach_absolute_time();
    if (timebase.denom == 0) {
        return ticks;
    }
    const std::uint64_t whole = ticks / timebase.denom;
    const std::uint64_t rest = ticks % timebase.denom;
    return whole * timebase.numer + (rest * timebase.numer) / timebase.denom;
#else
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
#endif
}

// ---- the timbre tap (analysis thread) ----------------------------------------------------------------------------

LiveTimbre::LiveTimbre(const analysis::AnalyzerConfig& config, TimbreConfig timbre)
    : analyzer_(config.sampleRate, config.windowSize, timbre)
    , hopSeconds_(static_cast<double>(config.hopSize) / static_cast<double>(std::max<std::uint32_t>(config.sampleRate, 1)))
    , sampleRate_(config.sampleRate) {}

void LiveTimbre::onFrame(const analysis::AnalysisFrame& frame) {
    if (!enabled_.load(std::memory_order_acquire)) {
        return;
    }
    const auto started = std::chrono::steady_clock::now();
    TimbreSnapshot s;
    s.features = analyzer_.analyze(frame); // allocation-free after its first call
    s.hopSeconds = hopSeconds_;
    s.audioSeconds = frame.timeSeconds;
    s.analysedNs = hostNowNs();
    if (queue_.push(s)) {
        produced_.fetch_add(1, std::memory_order_relaxed);
    } else {
        dropped_.fetch_add(1, std::memory_order_relaxed); // the render thread stalled for seconds: drop, never wait
    }
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count();
    const double previous = micros_.load(std::memory_order_relaxed);
    micros_.store(previous == 0.0 ? us : previous + (us - previous) * 0.05, std::memory_order_relaxed);
}

// ---- live notes ------------------------------------------------------------------------------------------------

namespace {

double horizonFor(const ContextSettings& s) {
    // contextAt looks back `window` for onsets, `phraseSeconds` for coverage and `rangeSeconds` for the range;
    // eventsBetween looks back `phraseGapSeconds` from an onset. Twice the longest, plus a second, is ample.
    return 2.0 * std::max({s.window, s.phraseSeconds, s.rangeSeconds, s.phraseGapSeconds}) + 1.0;
}

} // namespace

LiveNotes::LiveNotes(const ContextSettings& settings) : horizon_(horizonFor(settings)) {
    track_.notes.reserve(256);
    open_.reserve(32);
}

void LiveNotes::clear() {
    track_ = NoteTrack{};
    track_.notes.reserve(256);
    open_.clear();
    pedal_.fill(false);
    nextId_ = 0;
    received_ = 0;
}

NoteEvent* LiveNotes::find(std::uint32_t id) {
    // Held notes are recent: search from the end.
    for (auto it = track_.notes.rbegin(); it != track_.notes.rend(); ++it) {
        if (it->id == id) {
            return &*it;
        }
    }
    return nullptr;
}

void LiveNotes::end(std::uint32_t id, double seconds) {
    if (NoteEvent* n = find(id)) {
        // Never past the release: a note-off drained in the frame after its note-on (or the same one) must end at
        // or before the instant that frame publishes, or the note still counts as sounding there.
        n->duration = std::max(seconds - n->start, 0.0);
    }
}

void LiveNotes::noteOn(double seconds, std::uint8_t channel, std::uint8_t key, float velocity) {
    ++received_;
    // A second note-on for a key already held (a retrigger without a note-off) ends the first.
    for (auto it = open_.begin(); it != open_.end();) {
        if (it->channel == channel && it->key == key) {
            end(it->id, seconds);
            it = open_.erase(it);
        } else {
            ++it;
        }
    }
    NoteEvent n;
    n.start = seconds;
    n.duration = kOpenMargin;
    n.velocity = std::clamp(velocity, 0.0f, 1.0f);
    n.pitch = static_cast<float>(key);
    n.key = key;
    n.channel = channel;
    n.id = nextId_++;
    // Live time only moves forward, so this is an append; an out-of-order stamp is inserted where it belongs.
    auto& notes = track_.notes;
    if (notes.empty() || notes.back().start < seconds ||
        (notes.back().start == seconds && notes.back().key <= key)) {
        notes.push_back(n);
        assignVoice(track_, notes.size() - 1); // ADR-1062: the slot it owns until it ends
    } else {
        const auto at = std::upper_bound(notes.begin(), notes.end(), n, [](const NoteEvent& a, const NoteEvent& b) {
            return a.start != b.start ? a.start < b.start : a.key < b.key;
        });
        const auto index = static_cast<std::size_t>(notes.insert(at, n) - notes.begin());
        assignVoice(track_, index);
    }
    open_.push_back(Open{n.id, channel, key, false});
    if (notes.size() > kMaxNotes) {
        notes.erase(notes.begin(), notes.begin() + static_cast<std::ptrdiff_t>(notes.size() - kMaxNotes));
    }
}

void LiveNotes::noteOff(double seconds, std::uint8_t channel, std::uint8_t key) {
    for (auto it = open_.begin(); it != open_.end(); ++it) {
        if (it->channel == channel && it->key == key && !it->released) {
            if (pedal_[channel & 15u]) {
                it->released = true; // the pedal holds it
            } else {
                end(it->id, seconds);
                open_.erase(it);
            }
            return;
        }
    }
}

void LiveNotes::sustain(double seconds, std::uint8_t channel, bool down) {
    pedal_[channel & 15u] = down;
    if (down) {
        return;
    }
    for (auto it = open_.begin(); it != open_.end();) {
        if (it->channel == channel && it->released) {
            end(it->id, seconds);
            it = open_.erase(it);
        } else {
            ++it;
        }
    }
}

void LiveNotes::allOff(double seconds, int channel) {
    for (auto it = open_.begin(); it != open_.end();) {
        if (channel < 0 || it->channel == channel) {
            end(it->id, seconds);
            it = open_.erase(it);
        } else {
            ++it;
        }
    }
    if (channel < 0) {
        pedal_.fill(false);
    } else {
        pedal_[static_cast<std::size_t>(channel) & 15u] = false;
    }
}

const NoteTrack& LiveNotes::at(double seconds) {
    for (const Open& o : open_) {
        if (NoteEvent* n = find(o.id)) {
            n->duration = std::max(seconds - n->start, 0.0) + kOpenMargin;
        }
    }
    // Drop what can no longer matter: ended before the horizon. The newest note is always kept (the pitch
    // centre persists through a rest).
    auto& notes = track_.notes;
    const double cutoff = seconds - horizon_;
    std::size_t drop = 0;
    while (drop + 1 < notes.size() && notes[drop].end() < cutoff && notes[drop].start < cutoff) {
        ++drop;
    }
    // Only a prefix is dropped, so a long held note keeps everything after it; that is bounded by kMaxNotes.
    if (drop > 0) {
        notes.erase(notes.begin(), notes.begin() + static_cast<std::ptrdiff_t>(drop));
    }
    track_.longest = 0.0;
    for (const NoteEvent& n : notes) {
        track_.longest = std::max(track_.longest, n.duration);
    }
    return track_;
}

// ---- the session -----------------------------------------------------------------------------------------------

LiveSonic::LiveSonic() = default;

void LiveSonic::begin() {
    const float scale = runtime_.timeScale();
    runtime_.reset();
    runtime_.setTimeScale(scale);
    notes_.clear();
    originNs_ = hostNowNs();
    lastPublish_ = 0.0;
    lastMidiSeconds_ = -1e9;
    lastAudioSeconds_ = -1e9;
    status_ = Status{};
    running_ = true;
}

void LiveSonic::end() {
    running_ = false;
    notes_.clear();
    runtime_.reset();
}

double LiveSonic::seconds(std::uint64_t hostNs) const {
    const std::uint64_t t = hostNs != 0 ? hostNs : hostNowNs();
    return t >= originNs_ ? static_cast<double>(t - originNs_) * 1e-9 : -static_cast<double>(originNs_ - t) * 1e-9;
}

bool LiveSonic::midi(const control::MidiMessage& m, double frameSeconds) {
    using K = control::MidiKind;
    if (!running_) {
        return false;
    }
    ++status_.midiMessages;
    lastMidiSeconds_ = frameSeconds;
    // The message's own time, inside (lastPublish, frameSeconds]: early enough to keep the rhythm the player
    // played, never so early that the publish already passed it.
    double t = m.timestampNs != 0 ? seconds(m.timestampNs) : frameSeconds;
    t = std::clamp(t, lastPublish_ + 1e-6, std::max(frameSeconds, lastPublish_ + 1e-6));
    if (m.timestampNs != 0) {
        const double ms = std::max(0.0, (frameSeconds - seconds(m.timestampNs)) * 1e3);
        status_.midiToFrameMs = status_.midiToFrameMs == 0.0 ? ms : status_.midiToFrameMs + (ms - status_.midiToFrameMs) * 0.2;
    }
    switch (m.kind) {
    case K::NoteOn:
        notes_.noteOn(t, m.channel, m.data1, static_cast<float>(m.data2) / 127.0f);
        ++status_.notes;
        status_.lastVelocity = static_cast<float>(m.data2) / 127.0f;
        status_.lastKey = m.data1;
        status_.lastChannel = m.channel;
        return true;
    case K::NoteOff:
        notes_.noteOff(t, m.channel, m.data1);
        return true;
    case K::ControlChange:
        if (m.data1 == 64) {
            notes_.sustain(t, m.channel, m.data2 >= 64);
            return true;
        }
        if (m.data1 == 120 || m.data1 == 123) {
            notes_.allOff(t, m.channel);
            return true;
        }
        return false;
    default:
        return false;
    }
}

void LiveSonic::frame(const SonicSetup& setup, LiveTimbre* timbre, signals::SignalBus& bus, double frameSeconds,
                      std::uint64_t frameNs) {
    if (!running_) {
        return;
    }
    frameSeconds = std::max(frameSeconds, lastPublish_ + 1e-6);
    lastStepped_ = 0;
    if (timbre != nullptr) {
        TimbreSnapshot s;
        while (timbre->pop(s)) {
            runtime_.step(setup, s.features, s.hopSeconds);
            ++lastStepped_;
            status_.audioLevelDb = s.features.loudnessDb;
            if (s.features.loudnessDb > -60.0f) {
                lastAudioSeconds_ = frameSeconds;
            }
            const double ms = frameNs > s.analysedNs ? static_cast<double>(frameNs - s.analysedNs) * 1e-6 : 0.0;
            status_.snapshotToFrameMs =
                status_.snapshotToFrameMs == 0.0 ? ms : status_.snapshotToFrameMs + (ms - status_.snapshotToFrameMs) * 0.05;
        }
        status_.audioFrames += lastStepped_;
        status_.audioDropped = timbre->dropped();
        status_.timbreMicros = timbre->averageMicros();
    }
    const NoteTrack& track = notes_.at(frameSeconds);
    runtime_.publish(setup, track, bus, frameSeconds);
    lastPublish_ = frameSeconds;
}

LiveSonic::Status LiveSonic::status(double frameSeconds) const {
    Status s = status_;
    s.held = notes_.held();
    s.midiReceiving = running_ && frameSeconds - lastMidiSeconds_ <= kReceivingSeconds;
    s.audioReceiving = running_ && frameSeconds - lastAudioSeconds_ <= kReceivingSeconds;
    return s;
}

} // namespace avgen::sonic
