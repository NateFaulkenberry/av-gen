// Musical structure (ADR-063, milestone 10). Events are instants; a film is cut to spans, and the
// folding from one to the other is where every "the visuals cut on every bar" failure gets decided.
//
// What is worth guarding is almost entirely what the fold *refuses* to do: the metre never becomes
// structure, an energy wobble never becomes a boundary, two boundaries a beat apart are one, and a
// section too short to hold a shot is not a section. The one thing it must never smooth away is the
// drop, because the drop is the only moment the rest of the film is arranged around.

#include "signals/musical_events.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <vector>

using namespace avgen::signals;

namespace {
MusicalMoment at(MusicalEvent e, double t, float strength = 1.0f) {
    return MusicalMoment{e, t, strength};
}

// The metre, laid over a piece: a beat every half second, a bar every two, a phrase every eight.
// Nothing here is structure and none of it may become a boundary.
void addMetre(std::vector<MusicalMoment>& out, double totalSeconds) {
    for (double t = 0.0; t < totalSeconds; t += 0.5) {
        out.push_back(at(MusicalEvent::Beat, t, 0.6f));
        if (std::fmod(t, 2.0) < 1e-9) {
            out.push_back(at(MusicalEvent::Downbeat, t, 0.7f));
            out.push_back(at(MusicalEvent::BarStart, t));
        }
        if (std::fmod(t, 8.0) < 1e-9) {
            out.push_back(at(MusicalEvent::PhraseStart, t));
        }
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const MusicalMoment& a, const MusicalMoment& b) {
                         return a.timeSeconds < b.timeSeconds;
                     });
}

bool isMetre(const MusicalMoment& m) {
    return m.event == MusicalEvent::Beat || m.event == MusicalEvent::Downbeat ||
           m.event == MusicalEvent::BarStart;
}
} // namespace

TEST_CASE("The metre never becomes structure", "[signals][musical][structure]") {
    // A piece with three hundred-odd beats in it and four things that actually happen. If the metre
    // leaked into the fold, this would come back with a section per bar and anything cut to it
    // would cut ninety times.
    std::vector<MusicalMoment> moments;
    addMetre(moments, 180.0);
    moments.push_back(at(MusicalEvent::Build, 40.0, 0.7f));
    moments.push_back(at(MusicalEvent::Drop, 56.0, 0.9f));
    moments.push_back(at(MusicalEvent::Break, 96.0, 0.8f));
    moments.push_back(at(MusicalEvent::Drop, 128.0, 1.0f));
    std::stable_sort(moments.begin(), moments.end(),
                     [](const MusicalMoment& a, const MusicalMoment& b) {
                         return a.timeSeconds < b.timeSeconds;
                     });

    const auto withMetre = MusicalStructure::fromMoments(moments, 180.0);
    CHECK(static_cast<int>(withMetre.sections.size()) == 5); // intro plus the four things

    // Stronger than a count: stripping every beat, downbeat and bar out of the stream must not
    // change a single boundary. The metre is allowed no influence at all, not merely a small one.
    std::vector<MusicalMoment> structural;
    std::copy_if(moments.begin(), moments.end(), std::back_inserter(structural),
                 [](const MusicalMoment& m) { return !isMetre(m); });
    const auto withoutMetre = MusicalStructure::fromMoments(structural, 180.0);
    REQUIRE(withoutMetre.sections.size() == withMetre.sections.size());
    for (std::size_t i = 0; i < withMetre.sections.size(); ++i) {
        INFO("section " << i);
        CHECK(withMetre.sections[i].kind == withoutMetre.sections[i].kind);
        CHECK_THAT(withMetre.sections[i].startSeconds,
                   Catch::Matchers::WithinAbs(withoutMetre.sections[i].startSeconds, 1e-9));
    }
}

TEST_CASE("The piece opens on an intro and the sections tile it without gaps",
          "[signals][musical][structure]") {
    std::vector<MusicalMoment> moments{at(MusicalEvent::Build, 30.0), at(MusicalEvent::Drop, 44.0),
                                       at(MusicalEvent::Break, 80.0)};
    const auto s = MusicalStructure::fromMoments(moments, 120.0);
    REQUIRE(s.sections.size() == 4);
    CHECK(s.sections.front().kind == MusicalSection::Intro);
    CHECK_THAT(s.sections.front().startSeconds, Catch::Matchers::WithinAbs(0.0, 1e-9));
    for (std::size_t i = 1; i < s.sections.size(); ++i) {
        CHECK_THAT(s.sections[i].startSeconds,
                   Catch::Matchers::WithinAbs(s.sections[i - 1].endSeconds(), 1e-9));
    }
    CHECK_THAT(s.durationSeconds(), Catch::Matchers::WithinAbs(120.0, 1e-9));

    REQUIRE(s.at(50.0) != nullptr);
    CHECK(s.at(50.0)->kind == MusicalSection::Drop);
    CHECK(s.at(500.0) == nullptr);
}

TEST_CASE("A break resolving into a drop is one boundary, and the drop is the one that survives",
          "[signals][musical][structure]") {
    // The detector emits Break before the Drop it resolves into, so a first-wins merge would keep
    // the breakdown and lose the payoff every single time.
    std::vector<MusicalMoment> moments{at(MusicalEvent::Build, 20.0), at(MusicalEvent::Break, 60.0),
                                       at(MusicalEvent::Drop, 60.9)};
    const auto s = MusicalStructure::fromMoments(moments, 120.0);
    const auto* landed = s.at(61.0);
    REQUIRE(landed != nullptr);
    CHECK(landed->kind == MusicalSection::Drop);
    // ...and it keeps the earlier time, because the drop is where the break ended.
    CHECK_THAT(landed->startSeconds, Catch::Matchers::WithinAbs(60.0, 1e-9));
    CHECK(s.count(MusicalSection::Breakdown) == 0);
}

TEST_CASE("The last drop is a different thing from the first", "[signals][musical][structure]") {
    std::vector<MusicalMoment> moments{at(MusicalEvent::Build, 18.0), at(MusicalEvent::Drop, 30.0),
                                       at(MusicalEvent::Break, 70.0), at(MusicalEvent::Build, 100.0),
                                       at(MusicalEvent::Drop, 130.0)};
    const auto s = MusicalStructure::fromMoments(moments, 180.0);
    CHECK(s.count(MusicalSection::Drop) == 1);
    CHECK(s.count(MusicalSection::FinalDrop) == 1);
    CHECK(s.count(MusicalSection::FinalBuild) == 1);
    REQUIRE(s.at(140.0) != nullptr);
    CHECK(s.at(140.0)->kind == MusicalSection::FinalDrop);
    REQUIRE(s.at(35.0) != nullptr);
    CHECK(s.at(35.0)->kind == MusicalSection::Drop);
}

TEST_CASE("A single early drop is a drop, not a finale", "[signals][musical][structure]") {
    // "Last" only means something once the piece is far enough through; promoting the only drop in
    // the first ten seconds would give every piece a finale before it started.
    std::vector<MusicalMoment> moments{at(MusicalEvent::Drop, 10.0)};
    const auto s = MusicalStructure::fromMoments(moments, 180.0);
    CHECK(s.count(MusicalSection::FinalDrop) == 0);
    CHECK(s.count(MusicalSection::Drop) == 1);
}

TEST_CASE("A boundary near a phrase start moves onto it", "[signals][musical][structure]") {
    std::vector<MusicalMoment> moments{at(MusicalEvent::PhraseStart, 32.0),
                                       at(MusicalEvent::Build, 32.7),
                                       at(MusicalEvent::PhraseStart, 64.0),
                                       at(MusicalEvent::Drop, 67.4)};
    const auto s = MusicalStructure::fromMoments(moments, 120.0);
    REQUIRE(s.sections.size() == 3);
    // Inside the snap window: the cut lands on the phrase.
    CHECK_THAT(s.sections[1].startSeconds, Catch::Matchers::WithinAbs(32.0, 1e-9));
    // Three and a half seconds out is not a late cut, it is a different place; leave it alone.
    CHECK_THAT(s.sections[2].startSeconds, Catch::Matchers::WithinAbs(67.4, 1e-9));
    // ...and a phrase start never opens a section on its own.
    CHECK(s.count(MusicalSection::Verse) == 0);
}

TEST_CASE("A section too short to hold a shot is not a section", "[signals][musical][structure]") {
    // The analyzer's own section counter twitching four times in ten seconds is not four sections.
    std::vector<MusicalMoment> moments{
        at(MusicalEvent::SectionChange, 20.0), at(MusicalEvent::SectionChange, 22.5),
        at(MusicalEvent::SectionChange, 25.0), at(MusicalEvent::SectionChange, 27.5),
        at(MusicalEvent::SectionChange, 60.0)};
    const auto s = MusicalStructure::fromMoments(moments, 120.0);
    for (const auto& section : s.sections) {
        INFO(musicalSectionName(section.kind) << " at " << section.startSeconds);
        CHECK(section.durationSeconds >= 6.0);
    }
    CHECK(static_cast<int>(s.sections.size()) <= 3);

    // A drop is never absorbed however tight the sections around it are.
    std::vector<MusicalMoment> tight{at(MusicalEvent::SectionChange, 20.0),
                                     at(MusicalEvent::Build, 23.0), at(MusicalEvent::Drop, 25.0)};
    const auto s2 = MusicalStructure::fromMoments(tight, 120.0);
    CHECK(s2.count(MusicalSection::Drop) == 1);
    REQUIRE(s2.at(30.0) != nullptr);
    CHECK_THAT(s2.at(30.0)->startSeconds, Catch::Matchers::WithinAbs(25.0, 1e-9));
    // The build that fed it survives even though it only ran two seconds: a build exists to end.
    CHECK(s2.count(MusicalSection::Build) == 1);
}

TEST_CASE("An energy trend colours a section without dividing it",
          "[signals][musical][structure]") {
    std::vector<MusicalMoment> plain{at(MusicalEvent::Build, 30.0, 0.5f),
                                     at(MusicalEvent::Drop, 50.0, 0.5f)};
    auto wobbly = plain;
    for (double t = 52.0; t < 90.0; t += 3.0) {
        wobbly.push_back(at(MusicalEvent::EnergyRise, t, 0.8f));
        wobbly.push_back(at(MusicalEvent::EnergyDrop, t + 1.5, 0.8f));
    }
    std::stable_sort(wobbly.begin(), wobbly.end(),
                     [](const MusicalMoment& a, const MusicalMoment& b) {
                         return a.timeSeconds < b.timeSeconds;
                     });
    const auto a = MusicalStructure::fromMoments(plain, 120.0);
    const auto b = MusicalStructure::fromMoments(wobbly, 120.0);
    // Twenty-five trend events, not one extra boundary.
    CHECK(a.sections.size() == b.sections.size());

    // They do move the intensity, which is the whole of their influence.
    std::vector<MusicalMoment> rising{at(MusicalEvent::Build, 30.0, 0.5f),
                                      at(MusicalEvent::EnergyRise, 40.0, 1.0f)};
    const auto c = MusicalStructure::fromMoments(rising, 120.0);
    REQUIRE(c.at(41.0) != nullptr);
    CHECK(c.at(41.0)->intensity > 0.5f);
}

TEST_CASE("Structure names round-trip", "[signals][musical][structure]") {
    // Over `allMusicalSections()` rather than a list written out here: a hand-written list of nine
    // is what this test used to be, and when the vocabulary grew to fifteen (ADR-215) it went on
    // passing while testing nine of them.
    for (const auto s : avgen::signals::allMusicalSections()) {
        const auto again = musicalSectionFromName(musicalSectionName(s));
        REQUIRE(again.has_value());
        CHECK(*again == s);
    }
    CHECK(!musicalSectionFromName("hook").has_value());
}

TEST_CASE("A structure with no length has no sections", "[signals][musical][structure]") {
    std::vector<MusicalMoment> moments{at(MusicalEvent::Drop, 10.0)};
    CHECK(MusicalStructure::fromMoments(moments, 0.0).sections.empty());
    // Moments past the end of the piece are ignored rather than extending it.
    CHECK(MusicalStructure::fromMoments(moments, 5.0).sections.size() == 1);
}

TEST_CASE("The detector's own output folds into a structure", "[signals][musical][structure]") {
    // End to end, so the two halves of ADR-063 are known to fit: synthetic signal frames in, named
    // spans out, with no audio device anywhere.
    MusicalEventDetector detector;
    std::vector<MusicalMoment> moments;
    double t = 0.0;
    const double dt = 1.0 / 60.0;
    const auto feed = [&](int frames, float rms, float centroid) {
        for (int i = 0; i < frames; ++i) {
            MusicalFrame f;
            f.timeSeconds = t;
            f.rms = rms;
            f.bass = rms;
            f.spectralCentroid = centroid;
            for (const auto& m : detector.update(f)) {
                moments.push_back(m);
            }
            t += dt;
        }
    };
    feed(1800, 0.55f, 0.30f);  // 30 s of steady piece
    feed(600, 0.08f, 0.20f);   // 10 s break
    feed(1800, 0.95f, 0.65f);  // 30 s loud

    const auto s = MusicalStructure::fromMoments(moments, t);
    REQUIRE(!s.sections.empty());
    CHECK(s.sections.front().kind == MusicalSection::Intro);
    // The break and whatever it resolved into are both after it, and there are a handful of
    // sections rather than dozens.
    CHECK(static_cast<int>(s.sections.size()) <= 5);
    CHECK(s.count(MusicalSection::Breakdown) + s.count(MusicalSection::Drop) +
              s.count(MusicalSection::FinalDrop) >=
          1);
}
