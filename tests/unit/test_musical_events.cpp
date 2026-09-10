// The musical event classifier (ADR-063). These tests drive it with synthetic signal frames rather
// than audio, which is the point of taking a MusicalFrame instead of reaching for the bus: the
// question "does a break followed by a loud downbeat produce a Drop" should be answerable without
// a device, a file, or a decoder.

#include "signals/musical_events.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

using namespace avgen::signals;

namespace {
// Runs frames at a fixed rate and collects everything the detector recognised.
struct Run {
    MusicalEventDetector detector;
    std::vector<MusicalMoment> all;
    double t = 0.0;
    double dt = 1.0 / 60.0;

    void feed(int frames, float rms, float centroid = 0.3f, float onset = 0.0f,
              bool beatEvery = false) {
        for (int i = 0; i < frames; ++i) {
            MusicalFrame f;
            f.timeSeconds = t;
            f.rms = rms;
            f.bass = rms;
            f.spectralCentroid = centroid;
            f.onsetStrength = onset;
            if (beatEvery && i % 30 == 0) {
                f.beat = true;
                f.beatInBar = (i / 30) % 4;
            }
            for (const auto& m : detector.update(f)) {
                all.push_back(m);
            }
            t += dt;
        }
    }
    [[nodiscard]] int count(MusicalEvent e) const {
        return static_cast<int>(std::count_if(all.begin(), all.end(),
                                              [e](const MusicalMoment& m) { return m.event == e; }));
    }
    [[nodiscard]] bool saw(MusicalEvent e) const { return count(e) > 0; }
};
} // namespace

TEST_CASE("Beats, downbeats and bars come from the beat clock", "[signals][musical]") {
    Run r;
    MusicalFrame f;
    for (int i = 0; i < 8; ++i) {
        f.timeSeconds = i * 0.5;
        f.rms = 0.5f;
        f.beat = true;
        f.beatInBar = i % 4;
        f.barCount = static_cast<std::uint32_t>(i / 4);
        for (const auto& m : r.detector.update(f)) {
            r.all.push_back(m);
        }
    }
    // The first frame primes the detector and emits nothing, which is deliberate: a classifier that
    // fires on its very first sample has decided something from no history at all.
    CHECK(r.count(MusicalEvent::Beat) == 7);
    CHECK(r.count(MusicalEvent::Downbeat) == 1); // beatInBar 0 at i=4
    CHECK(r.count(MusicalEvent::BarStart) == 1);
}

TEST_CASE("A single very strong onset is an impact", "[signals][musical]") {
    Run r;
    r.feed(60, 0.4f);
    CHECK(!r.saw(MusicalEvent::Impact));
    r.feed(1, 0.4f, 0.3f, 2.4f);
    CHECK(r.count(MusicalEvent::Impact) == 1);

    SECTION("and the cooldown stops one onset becoming a burst of impacts") {
        r.feed(3, 0.4f, 0.3f, 2.4f);
        CHECK(r.count(MusicalEvent::Impact) == 1);
    }
}

TEST_CASE("A quiet passage after a loud one is a break", "[signals][musical]") {
    Run r;
    r.feed(300, 0.8f);            // five seconds loud, establishing the peak
    CHECK(!r.saw(MusicalEvent::Break));
    r.feed(180, 0.10f);           // three seconds very quiet
    CHECK(r.saw(MusicalEvent::Break));
    CHECK(r.detector.inBreak());
}

TEST_CASE("A break resolving into loudness is a drop; loudness alone is not", "[signals][musical]") {
    SECTION("break then loud: a drop") {
        Run r;
        r.feed(300, 0.85f);
        r.feed(150, 0.08f);       // the break
        r.feed(120, 0.95f);       // the resolution
        CHECK(r.saw(MusicalEvent::Break));
        CHECK(r.saw(MusicalEvent::Drop));
    }
    SECTION("merely getting louder is not a drop, which is the whole point") {
        Run r;
        r.feed(240, 0.35f);
        r.feed(240, 0.9f);
        CHECK(!r.saw(MusicalEvent::Drop));
    }
}

TEST_CASE("A sustained rise is a rise; a rise that also brightens is a build",
          "[signals][musical]") {
    SECTION("louder at a constant timbre is an energy rise") {
        Run r;
        r.feed(240, 0.25f, 0.30f);
        r.feed(300, 0.85f, 0.30f);
        CHECK(r.saw(MusicalEvent::EnergyRise));
        CHECK(!r.saw(MusicalEvent::Build));
    }
    SECTION("louder and brighter is a build: it is going somewhere") {
        Run r;
        r.feed(240, 0.25f, 0.20f);
        for (int i = 0; i < 300; ++i) {
            r.feed(1, 0.85f, 0.20f + static_cast<float>(i) * 0.002f);
        }
        CHECK(r.saw(MusicalEvent::Build));
    }
    SECTION("a sustained fall is an energy drop") {
        Run r;
        r.feed(240, 0.9f);
        r.feed(300, 0.3f);
        CHECK(r.saw(MusicalEvent::EnergyDrop));
    }
}

TEST_CASE("A steady passage produces no structural events at all", "[signals][musical]") {
    // The most important negative test. A classifier that finds drama in a constant tone will find
    // it everywhere, and a visual system cut to a false drop looks broken in a way that one cut to
    // nothing does not.
    Run r;
    r.feed(1200, 0.55f, 0.3f);
    CHECK(!r.saw(MusicalEvent::Drop));
    CHECK(!r.saw(MusicalEvent::Build));
    CHECK(!r.saw(MusicalEvent::Break));
    CHECK(!r.saw(MusicalEvent::EnergyRise));
    CHECK(!r.saw(MusicalEvent::EnergyDrop));
    CHECK(!r.saw(MusicalEvent::Impact));
}

TEST_CASE("Classification does not depend on the frame rate", "[signals][musical]") {
    // The same music at 30 and 120 fps must agree about what happened, or an offline render and a
    // window disagree about where the drop is -- which for a deterministic engine is unacceptable.
    const auto play = [](double dt) {
        MusicalEventDetector d;
        std::vector<MusicalMoment> out;
        double t = 0.0;
        const auto run = [&](double seconds, float rms) {
            const int n = static_cast<int>(seconds / dt);
            for (int i = 0; i < n; ++i) {
                MusicalFrame f;
                f.timeSeconds = t;
                f.rms = rms;
                f.bass = rms;
                f.spectralCentroid = 0.3f;
                for (const auto& m : d.update(f)) {
                    out.push_back(m);
                }
                t += dt;
            }
        };
        run(5.0, 0.85f);
        run(2.5, 0.08f);
        run(2.0, 0.95f);
        return out;
    };
    const auto slow = play(1.0 / 30.0);
    const auto fast = play(1.0 / 120.0);
    const auto kinds = [](const std::vector<MusicalMoment>& v) {
        std::vector<MusicalEvent> k;
        for (const auto& m : v) {
            k.push_back(m.event);
        }
        return k;
    };
    CHECK(kinds(slow) == kinds(fast));
    REQUIRE(!slow.empty());
    for (std::size_t i = 0; i < slow.size(); ++i) {
        // Same events, and within a beat of the same place.
        CHECK(std::abs(slow[i].timeSeconds - fast[i].timeSeconds) < 0.25);
    }
}

TEST_CASE("Event names round-trip", "[signals][musical]") {
    for (const auto e : {MusicalEvent::Beat, MusicalEvent::Downbeat, MusicalEvent::BarStart,
                         MusicalEvent::PhraseStart, MusicalEvent::SectionChange,
                         MusicalEvent::EnergyRise, MusicalEvent::EnergyDrop, MusicalEvent::Build,
                         MusicalEvent::Break, MusicalEvent::Drop, MusicalEvent::Impact}) {
        INFO(musicalEventName(e));
        const auto back = musicalEventFromName(musicalEventName(e));
        REQUIRE(back.has_value());
        CHECK(*back == e);
    }
    CHECK(!musicalEventFromName("crescendo").has_value());
}
