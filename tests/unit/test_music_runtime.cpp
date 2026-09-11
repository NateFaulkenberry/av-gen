// Musical events reaching the signal bus (ADR-073).
//
// ADR-063's own tests prove the classifier classifies. These prove it is *wired*: that the names a
// route author binds to exist and are spelled the way `musicalEventName()` spells them, that the
// detector's answers do not move when the frame rate does, that silence produces silence, and that
// a route on `music.beat` actually moves a parameter. Each one fails if the hook is removed.

#include "app/engine.hpp"
#include "app/music_runtime.hpp"
#include "audio/audio_file.hpp"
#include "core/time.hpp"
#include "signals/musical_events.hpp"
#include "signals/signal_bus.hpp"
#include "support/synth.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

using namespace avgen;

namespace {

constexpr std::uint32_t kRate = 48000;

// Every event kind, so a test can sweep them without retyping the list.
const std::array<signals::MusicalEvent, 11>& allEvents() {
    static const std::array<signals::MusicalEvent, 11> kAll{
        signals::MusicalEvent::Beat,       signals::MusicalEvent::Downbeat,
        signals::MusicalEvent::BarStart,   signals::MusicalEvent::PhraseStart,
        signals::MusicalEvent::SectionChange, signals::MusicalEvent::EnergyRise,
        signals::MusicalEvent::EnergyDrop, signals::MusicalEvent::Build,
        signals::MusicalEvent::Break,      signals::MusicalEvent::Drop,
        signals::MusicalEvent::Impact};
    return kAll;
}

// A piece with the two-part shape a Drop is defined by: loud, then a break long enough for the
// short energy to collapse against the decaying peak, then loud again inside the drop window.
// Clicks give the beat tracker something to lock to; they stop during the break, because a break
// with a click track running through it is not a break.
std::vector<float> dropFixture() {
    const std::size_t second = kRate;
    const std::size_t total = 7 * second;
    auto tone = testsupport::sine(60.0f, kRate, total, 0.85f);
    auto clicks = testsupport::clickTrack(120.0f, kRate, total);
    std::vector<float> out(total, 0.0f);
    for (std::size_t i = 0; i < total; ++i) {
        const bool quiet = i >= 3 * second && i < 4 * second + second / 2; // 3.0 s .. 4.5 s
        out[i] = quiet ? 0.0f : tone[i] + clicks[i] * 0.5f;
    }
    return out;
}

std::filesystem::path writeFixture(const char* name, const std::vector<float>& mono) {
    auto file = audio::AudioFile::fromInterleaved(testsupport::interleave(mono, 2), 2, kRate);
    const auto path = testsupport::processTempDir() / (std::string("music_runtime_") + name + ".wav");
    REQUIRE(file.writeWav(path).has_value());
    return path;
}

// Every moment the classifier recognised during an offline render, per event kind, on the analysis
// clock. Reconstructed from Engine::music().lastEventTime() rather than from the bus, because the
// bus clears event values at the end of every update() -- polling the signals from out here would
// only ever see zero, which is a trap worth writing down.
using Observed = std::map<int, std::vector<double>>;

Observed renderOffline(const std::filesystem::path& audio, double fps, double seconds) {
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(audio).has_value());
    FixedStepClock clock(fps);
    std::map<int, double> previous;
    for (const auto e : allEvents()) {
        previous[static_cast<int>(e)] = app::MusicRuntime::kNever;
    }
    Observed observed;
    const auto frames = static_cast<int>(seconds * fps);
    for (int i = 0; i < frames; ++i) {
        const FrameTime time = engine.tick(clock);
        engine.update(time);
        for (const auto e : allEvents()) {
            const int key = static_cast<int>(e);
            const double when = engine.music().lastEventTime(e);
            if (when != previous[key]) {
                previous[key] = when;
                observed[key].push_back(when);
            }
        }
    }
    return observed;
}

int total(const Observed& o) {
    int n = 0;
    for (const auto& [kind, times] : o) {
        n += static_cast<int>(times.size());
    }
    return n;
}

// One synthetic analysis frame, so the runtime can be driven without a decoder.
analysis::AnalysisFrame frameAt(std::uint64_t index, double seconds) {
    analysis::AnalysisFrame f;
    f.frameIndex = index;
    f.timeSeconds = seconds;
    f.rms = 0.5f;
    f.bandCount = 5;
    f.bands[0] = 0.5f;
    f.centroidNorm = 0.3f;
    return f;
}

} // namespace

TEST_CASE("Every musical event kind is declared as music.<name>", "[app][music]") {
    signals::SignalBus bus;
    app::MusicRuntime runtime;
    runtime.declare(bus);

    CHECK(runtime.eventCount() == allEvents().size());
    for (const auto e : allEvents()) {
        const std::string expected = std::string("music.") + signals::musicalEventName(e);
        const auto id = runtime.signal(e);
        REQUIRE(id != signals::kInvalidSignal);
        CHECK(bus.info(id).name == expected);
        // Momentary, exactly like beat.pulse, so the attack/decay processors shape it unchanged.
        CHECK(bus.info(id).isEvent);
        CHECK(bus.info(id).maxValue == 1.0f);
        // A route binds by name; find() is the lookup the Modulator itself performs.
        CHECK(bus.find(expected) == id);
    }
    // The contract another author is writing routes against, spelled out once so a rename of any
    // of these fails here rather than silently in somebody's project file.
    for (const char* name : {"music.beat", "music.downbeat", "music.bar", "music.phrase",
                             "music.section", "music.energyRise", "music.energyDrop", "music.build",
                             "music.break", "music.drop", "music.impact"}) {
        CHECK(bus.find(name).has_value());
    }
}

TEST_CASE("The same analysis frame delivered twice is classified once", "[app][music]") {
    signals::SignalBus bus;
    app::MusicRuntime runtime;
    runtime.declare(bus);
    const auto beatSignal = runtime.signal(signals::MusicalEvent::Beat);

    runtime.consume(frameAt(0, 0.0), 4, 4); // primes the detector; emits nothing by design
    runtime.publish(bus);
    CHECK(!bus.event(beatSignal));

    auto beat = frameAt(512, 0.5);
    beat.beat = true;
    beat.beatCount = 1;
    runtime.consume(beat, 4, 4);
    runtime.publish(bus);
    CHECK(bus.event(beatSignal));
    const double when = runtime.lastEventTime(signals::MusicalEvent::Beat);
    CHECK(when == 0.5);

    bus.clearEvents();
    // The engine hands the same frame over twice -- the offline catch-up loop walks it, then
    // publishFrame() publishes the last of the batch. Without the frame-index guard the detector
    // sees a second beat at dt = 0 and fires again.
    runtime.consume(beat, 4, 4);
    runtime.publish(bus);
    CHECK(!bus.event(beatSignal));
    CHECK(runtime.consumedFrames() == 2);
}

TEST_CASE("Musical events land at the same times at 30 fps and at 120 fps", "[app][music]") {
    const auto audio = writeFixture("drop", dropFixture());
    const Observed slow = renderOffline(audio, 30.0, 7.0);
    const Observed fast = renderOffline(audio, 120.0, 7.0);

    // Two empty runs are trivially equal and prove nothing, so insist the fixture's shape was
    // recognised before comparing the two readings of it. A break at 3.5 s, one drop at 4.9 s, and
    // beats throughout: 26 moments over seven kinds, which is enough to be worth agreeing about.
    const auto momentsOf = [&slow](signals::MusicalEvent e) -> std::vector<double> {
        const auto it = slow.find(static_cast<int>(e));
        return it == slow.end() ? std::vector<double>{} : it->second;
    };
    const auto breaks = momentsOf(signals::MusicalEvent::Break);
    const auto drops = momentsOf(signals::MusicalEvent::Drop);
    REQUIRE(breaks.size() == 1);
    REQUIRE(drops.size() == 1);
    CHECK(breaks[0] > 3.0);          // the tone stops at 3 s; the break is called once it collapses
    CHECK(drops[0] > breaks[0]);     // a drop is a break resolving, never the other way round
    CHECK(drops[0] < breaks[0] + 2.5); // ...and inside the drop window, or it is only a recovery
    CHECK(momentsOf(signals::MusicalEvent::Beat).size() >= 8);
    CHECK(!momentsOf(signals::MusicalEvent::Impact).empty());
    CHECK(total(slow) > 20);

    REQUIRE(slow.size() == fast.size());
    for (const auto& [kind, times] : slow) {
        INFO("event kind " << signals::musicalEventName(static_cast<signals::MusicalEvent>(kind)));
        REQUIRE(fast.count(kind) == 1);
        const auto& other = fast.at(kind);
        REQUIRE(times.size() == other.size());
        for (std::size_t i = 0; i < times.size(); ++i) {
            // Exactly equal, not merely close: both runs feed the classifier the same analysis
            // frames in the same order, so a difference here means the render clock has leaked
            // into a decision it has no business in.
            CHECK(times[i] == other[i]);
        }
    }
}

TEST_CASE("Silence produces no musical events", "[app][music]") {
    SECTION("no audio at all") {
        app::Engine engine(app::EngineMode::Offline);
        FixedStepClock clock(60.0);
        for (int i = 0; i < 120; ++i) {
            engine.update(engine.tick(clock));
        }
        CHECK(engine.music().consumedFrames() == 0);
        for (const auto e : allEvents()) {
            CHECK(engine.music().lastEventTime(e) == app::MusicRuntime::kNever);
            CHECK(!engine.signals().event(engine.music().signal(e)));
        }
    }
    SECTION("three seconds of digital silence") {
        const auto audio = writeFixture("silence", testsupport::silence(3 * kRate));
        const Observed observed = renderOffline(audio, 60.0, 3.0);
        // Analysis frames were consumed -- the pipeline ran -- and still found nothing, which is
        // the distinction that matters: a classifier that is merely starved proves nothing.
        CHECK(total(observed) == 0);
    }
}

TEST_CASE("A modulation route reads music.beat", "[app][music]") {
    const auto audio = writeFixture("route", dropFixture());
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(audio).has_value());

    // camera/height carries no default route, so anything that moves it came from here.
    params::ModRoute route{.source = "music.beat", .target = "camera/height", .amount = 4.0f};
    route.chain.envelope = params::EnvelopeMode::PeakHold;
    route.chain.envelopeHoldMs = 50.0f;
    route.chain.envelopeFallPerSecond = 4.0f;
    engine.modulator().addRoute(route);
    engine.rebind();

    auto* height = engine.params().find("camera/height");
    REQUIRE(height != nullptr);
    const float base = height->baseComponent(0);

    FixedStepClock clock(60.0);
    float peak = base;
    for (int i = 0; i < 420; ++i) { // 7 s
        engine.update(engine.tick(clock));
        peak = std::max(peak, height->finalComponent(0));
    }
    CHECK(engine.music().lastEventTime(signals::MusicalEvent::Beat) > 0.0);
    CHECK(peak > base + 0.5f);
}
