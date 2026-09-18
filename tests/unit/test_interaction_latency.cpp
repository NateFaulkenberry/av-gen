// The interaction-latency instrument, checked as arithmetic.
//
// ADR-182's rule applied to an instrument rather than to a renderer: a probe that cannot fail
// proves nothing. For a latency harness the acute form is that a measurement which reports
// plausible numbers for an interaction it never performed is worse than no measurement at all,
// because it will be trusted. So every test below that asserts a number has a sibling that asserts
// the *absence* of a number, and the last group feeds the instrument a deliberately slowed
// interaction and requires it to say so.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <chrono>
#include <string>
#include <vector>

#include "core/interaction_latency.hpp"

using avgen::core::Clock;
using avgen::core::distributionOf;
using avgen::core::Interaction;
using avgen::core::InteractionLog;
using avgen::core::InteractionRecord;
using avgen::core::interactionFromName;
using avgen::core::interactionName;
using avgen::core::LatencyBand;
using avgen::core::Stamp;
using avgen::core::bandFor;
using avgen::core::formatCsv;
using avgen::core::formatReport;
using avgen::core::summarise;

namespace {

Stamp at(double ms) {
    return Stamp{} + std::chrono::duration_cast<Clock::duration>(
                         std::chrono::duration<double, std::milli>(ms));
}

// A record built from explicit stamps, so the arithmetic under test is the derivation and not a
// clock. Every helper below sets exactly the stages it names and leaves the rest absent.
InteractionRecord synthetic(double input, double command, double firstVisible, double visible) {
    InteractionRecord r;
    r.kind = Interaction::TimelineClick;
    r.input = at(input);
    r.receipt = at(input);
    r.command = at(command);
    r.model = at(command);
    r.presentation = at(command);
    r.submit = at(firstVisible);
    r.firstVisible = at(firstVisible);
    r.visible = at(visible);
    return r;
}

} // namespace

TEST_CASE("interaction names round-trip, and an unknown name is refused", "[latency]") {
    for (std::size_t k = 0; k < static_cast<std::size_t>(Interaction::Count); ++k) {
        const auto kind = static_cast<Interaction>(k);
        const std::string_view name = interactionName(kind);
        CHECK(name != "unknown");
        const auto back = interactionFromName(name);
        REQUIRE(back.has_value());
        CHECK(*back == kind);
    }
    // The control. A mistyped arm name that silently resolved to the first enumerator would make
    // every subsequent measurement a measurement of the wrong interaction, and nothing downstream
    // could tell.
    CHECK_FALSE(interactionFromName("timeline-clik").has_value());
    CHECK_FALSE(interactionFromName("").has_value());
}

TEST_CASE("a stage that did not happen is unavailable, never zero", "[latency]") {
    InteractionRecord r;
    r.kind = Interaction::HeroStar;
    // A value arm writes the parameter directly and produces no SDL event, so there is no T0.
    r.command = at(10.0);
    r.model = at(300.0);
    r.presentation = at(300.0);
    r.firstVisible = at(1450.0);
    r.visible = at(1450.0);

    CHECK_FALSE(r.inputToAck().has_value());
    CHECK_FALSE(r.inputToFinalVisual().has_value());
    // ...but it is a complete record: it has a command and a final visible frame. "We could not
    // time the input" and "the interaction never finished" are different facts.
    CHECK(r.complete());

    InteractionRecord unfinished;
    unfinished.kind = Interaction::HeroStar;
    unfinished.input = at(0.0);
    unfinished.command = at(1.0);
    CHECK_FALSE(unfinished.complete());
    CHECK(unfinished.inputToAck().has_value());
    CHECK_FALSE(unfinished.inputToFinalVisual().has_value());
}

TEST_CASE("first and final visual are the same number for a synchronous interaction", "[latency]") {
    const InteractionRecord sync = synthetic(0.0, 2.0, 2500.0, 2500.0);
    REQUIRE(sync.inputToFirstVisual().has_value());
    REQUIRE(sync.inputToFinalVisual().has_value());
    CHECK_THAT(*sync.inputToFirstVisual(), Catch::Matchers::WithinAbs(2500.0, 0.01));
    CHECK_THAT(*sync.inputToFinalVisual(), Catch::Matchers::WithinAbs(2500.0, 0.01));

    // ...and they separate when the interaction is deferred. This is the whole reason there are two
    // stamps: an architecture that decoupled the response from the evaluation and left these equal
    // has not decoupled anything, whatever its diagram says.
    InteractionRecord deferred = synthetic(0.0, 2.0, 8.0, 2500.0);
    CHECK_THAT(*deferred.inputToFirstVisual(), Catch::Matchers::WithinAbs(8.0, 0.01));
    CHECK_THAT(*deferred.inputToFinalVisual(), Catch::Matchers::WithinAbs(2500.0, 0.01));
}

TEST_CASE("percentiles are nearest-rank and an empty set has no minimum", "[latency]") {
    const auto empty = distributionOf({});
    CHECK_FALSE(empty.available());
    CHECK(empty.samples == 0);

    // 1..100, so the nearest-rank answer is exactly the sample's own value and an interpolation
    // error would be visible rather than absorbed.
    std::vector<double> ramp;
    for (int i = 1; i <= 100; ++i) {
        ramp.push_back(static_cast<double>(i));
    }
    const auto d = distributionOf(ramp);
    CHECK(d.available());
    CHECK(d.samples == 100);
    CHECK_THAT(d.min, Catch::Matchers::WithinAbs(1.0, 1e-9));
    CHECK_THAT(d.median, Catch::Matchers::WithinAbs(50.0, 1e-9));
    CHECK_THAT(d.p90, Catch::Matchers::WithinAbs(90.0, 1e-9));
    CHECK_THAT(d.p95, Catch::Matchers::WithinAbs(95.0, 1e-9));
    CHECK_THAT(d.p99, Catch::Matchers::WithinAbs(99.0, 1e-9));
    CHECK_THAT(d.max, Catch::Matchers::WithinAbs(100.0, 1e-9));

    // The case the whole instrument exists for: a distribution whose minimum is fine and whose tail
    // is not. ADR-170 would report 16 ms here and be wrong about the experience.
    std::vector<double> tailed(99, 16.0);
    tailed.push_back(5000.0);
    const auto t = distributionOf(tailed);
    CHECK_THAT(t.min, Catch::Matchers::WithinAbs(16.0, 1e-9));
    CHECK_THAT(t.median, Catch::Matchers::WithinAbs(16.0, 1e-9));
    CHECK_THAT(t.p99, Catch::Matchers::WithinAbs(16.0, 1e-9));
    CHECK_THAT(t.max, Catch::Matchers::WithinAbs(5000.0, 1e-9));
    // One sample, so a single-sample set's percentiles are all that sample rather than out of range.
    const auto one = distributionOf({7.0});
    CHECK_THAT(one.p99, Catch::Matchers::WithinAbs(7.0, 1e-9));
    CHECK_THAT(one.median, Catch::Matchers::WithinAbs(7.0, 1e-9));
}

TEST_CASE("bands follow Nielsen's thresholds at their boundaries", "[latency]") {
    CHECK(bandFor(0.0) == LatencyBand::Preferred);
    CHECK(bandFor(49.999) == LatencyBand::Preferred);
    CHECK(bandFor(50.0) == LatencyBand::Interactive);
    CHECK(bandFor(99.999) == LatencyBand::Interactive);
    CHECK(bandFor(100.0) == LatencyBand::Noticed);
    CHECK(bandFor(249.999) == LatencyBand::Noticed);
    CHECK(bandFor(250.0) == LatencyBand::Defect);
    CHECK(bandFor(999.999) == LatencyBand::Defect);
    CHECK(bandFor(1000.0) == LatencyBand::FlowBreak);
}

TEST_CASE("a record is not filed until the evaluation lands", "[latency]") {
    InteractionLog log;
    log.begin(Interaction::TimelineClick, 7, Clock::now(), Clock::now());
    CHECK(log.open());
    log.markCommand();
    log.markSubmit();
    // The frame presented, but the model has not changed yet -- a deferred interaction, mid-flight.
    log.markFrameVisible();
    CHECK(log.open());
    CHECK(log.all().empty());
    CHECK(log.completed(Interaction::TimelineClick) == 0);
    // Filing it here would report a deferral as a fast interaction: the single most flattering lie
    // this instrument could tell, and the one a naive harness tells by default.

    log.markModel();
    log.markPresentation();
    log.markFrameVisible();
    CHECK_FALSE(log.open());
    REQUIRE(log.all().size() == 1);
    CHECK(log.completed(Interaction::TimelineClick) == 1);
    const InteractionRecord& r = log.all().front();
    REQUIRE(r.inputToFirstVisual().has_value());
    REQUIRE(r.inputToFinalVisual().has_value());
    CHECK(*r.inputToFinalVisual() >= *r.inputToFirstVisual());
}

TEST_CASE("an abandoned interaction is counted, not reported as fast", "[latency]") {
    InteractionLog log;
    log.begin(Interaction::GizmoDrag, 1, Clock::now(), Clock::now());
    log.markCommand();
    log.abandon();
    CHECK(log.begun(Interaction::GizmoDrag) == 1);
    CHECK(log.completed(Interaction::GizmoDrag) == 0);
    CHECK(log.abandoned(Interaction::GizmoDrag) == 1);
    CHECK(log.all().empty());

    const auto summaries = summarise(log);
    REQUIRE(summaries.size() == 1);
    CHECK(summaries.front().kind == Interaction::GizmoDrag);
    CHECK(summaries.front().begun == 1);
    CHECK(summaries.front().completed == 0);
    CHECK_FALSE(summaries.front().finalVisual.available());

    // And the report says so in words, because a table of dashes is read as "fine" by anybody
    // skimming it.
    const std::string text = formatReport(summaries, 1.0);
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("MEASURED NOTHING"));
}

TEST_CASE("an interaction nobody performed is absent, not a row of zeros", "[latency]") {
    InteractionLog log;
    const auto summaries = summarise(log);
    CHECK(summaries.empty());
    const std::string text = formatReport(summaries, 3.5);
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("nothing here measured anything"));
    // The load average is required by ADR-170 whenever a timing is claimed, so it is in the header
    // rather than in a caller's log line that can be dropped.
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("load average 3.50"));
}

TEST_CASE("overlapping interactions are counted rather than silently merged", "[latency]") {
    InteractionLog log;
    log.begin(Interaction::TimelineDrag, 1, Clock::now(), Clock::now());
    log.markCommand();
    // A second interaction opening before the first closed means the main thread stopped
    // serialising them, which is a fact worth knowing rather than a fact to absorb.
    log.begin(Interaction::Selection, 1, Clock::now(), Clock::now());
    CHECK(log.overlaps() == 1);
    CHECK(log.abandoned(Interaction::TimelineDrag) == 1);
    CHECK(log.openKind() == Interaction::Selection);
}

// ---- the control: the instrument must detect a deliberately slowed interaction -----------------
//
// Everything above tests that the instrument computes. This tests that it *notices*. Without it,
// every number this harness ever prints is unfalsifiable.

TEST_CASE("the instrument detects a deliberately slowed interaction", "[latency]") {
    const auto run = [](double workMs) {
        InteractionLog log;
        for (int i = 0; i < 20; ++i) {
            const Stamp t0 = Clock::now();
            log.begin(Interaction::TimelineClick, static_cast<std::uint64_t>(i), t0, t0);
            log.markCommand();
            // The deliberate slowdown, spent rather than asserted -- a sleep would be a different
            // measurement (a sleeping thread is not a working one) but for detecting that the
            // instrument sees elapsed wall clock either serves.
            const Stamp until = Clock::now() + std::chrono::duration_cast<Clock::duration>(
                                                   std::chrono::duration<double, std::milli>(workMs));
            while (Clock::now() < until) {
            }
            log.markModel();
            log.markPresentation();
            log.markSubmit();
            log.markFrameVisible();
        }
        return summarise(log);
    };

    const auto fast = run(0.0);
    const auto slow = run(12.0);
    REQUIRE(fast.size() == 1);
    REQUIRE(slow.size() == 1);
    REQUIRE(fast.front().finalVisual.available());
    REQUIRE(slow.front().finalVisual.available());

    // The arm is shown capable of failing before its null result would be read: the slowed run's
    // median must clear the fast run's by most of the injected amount. A machine under contention
    // can only make this larger, never smaller, so the one-sided bound is safe on a shared box.
    const double delta = slow.front().finalVisual.median - fast.front().finalVisual.median;
    INFO("fast median " << fast.front().finalVisual.median << " ms, slow median "
                        << slow.front().finalVisual.median << " ms, delta " << delta << " ms");
    CHECK(delta > 10.0);
    // And the fast arm must not itself be slow, or the comparison proves nothing about the
    // instrument and only that the machine is busy.
    CHECK(fast.front().finalVisual.median < 5.0);
}

TEST_CASE("a calibration sample is excluded from every distribution", "[latency]") {
    InteractionLog log;
    for (int i = 0; i < 4; ++i) {
        const Stamp t0 = Clock::now();
        log.begin(Interaction::TimelineClick, static_cast<std::uint64_t>(i), t0, t0);
        log.markCommand();
        log.markModel();
        log.markPresentation();
        log.markSubmit();
        if (i == 3) {
            log.addInjectedMs(500.0); // the control's own sample
        }
        log.markFrameVisible();
    }
    const auto s = summarise(log);
    REQUIRE(s.size() == 1);
    CHECK(s.front().completed == 4);
    CHECK(s.front().injectedSamples == 1);
    CHECK(s.front().finalVisual.samples == 3);
    const std::string text = formatReport(s, 2.0);
    CHECK_THAT(text, Catch::Matchers::ContainsSubstring("calibration sample"));
}

TEST_CASE("the CSV writes an absent stage as an empty field, never as zero", "[latency]") {
    InteractionLog log;
    // No input stamp: a value-driven interaction, which is most of the editor's own commands.
    log.begin(Interaction::HeroStar, 3, std::nullopt, std::nullopt);
    log.markCommand();
    log.markModel();
    log.markPresentation();
    log.markSubmit();
    log.markFrameVisible();
    const std::string csv = formatCsv(log);
    INFO(csv);
    CHECK_THAT(csv, Catch::Matchers::ContainsSubstring("hero-star,3,,,,,"));
    CHECK_THAT(csv, !Catch::Matchers::ContainsSubstring("hero-star,3,0.0000"));
}

TEST_CASE("counters are attributed to the interaction that caused them", "[latency]") {
    InteractionLog log;
    log.begin(Interaction::HeroStar, 1, std::nullopt, std::nullopt);
    log.markCommand();
    log.addCounters(/*flattens=*/1, /*seeks=*/0, /*proceduralRegens=*/0, /*texturesUploaded=*/44,
                    /*entitySimBodies=*/0);
    log.markModel();
    log.markPresentation();
    log.markFrameVisible();

    log.begin(Interaction::TimelineClick, 2, std::nullopt, std::nullopt);
    log.markCommand();
    log.addCounters(0, 1, 0, 0, 124'200);
    log.markModel();
    log.markPresentation();
    log.markFrameVisible();

    const auto s = summarise(log);
    REQUIRE(s.size() == 2);
    // "one flatten per click" and "one per frame of the drag" are different defects, and a
    // millisecond figure on a contended machine tells them apart badly. The counters do not care
    // what the load average is.
    for (const auto& row : s) {
        if (row.kind == Interaction::HeroStar) {
            CHECK(row.flattens == 1);
            CHECK(row.texturesUploaded == 44);
            CHECK(row.seeks == 0);
        } else {
            CHECK(row.kind == Interaction::TimelineClick);
            CHECK(row.flattens == 0);
            CHECK(row.seeks == 1);
            CHECK(row.entitySimBodies == 124'200);
        }
    }
}
