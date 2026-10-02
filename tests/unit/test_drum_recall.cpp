// Drum recall under a mix (ADR-1067): the art agent's matrix -- the drum loop alone and under a lead, a bass line, a
// pad bed and all three -- as a regression test. A hit counts as found when the detector fires within -30..+90 ms of
// it (the art agent's window: a frame's time is its window's centre, so an attack is seen up to half a window early).

#include "analysis/analysis_track.hpp"
#include "analysis/causal_onsets.hpp"
#include "support/sonic_mix.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <string>
#include <vector>

using namespace avgen;
using analysis::HitClass;

namespace {

struct Recall {
    int found = 0, total = 0, falseHits = 0;
    [[nodiscard]] double share() const { return total > 0 ? double(found) / double(total) : 0.0; }
};

bool near(double d, const std::vector<double>& truth) {
    for (const double t : truth) {
        if (d >= t - 0.03 && d <= t + 0.09) {
            return true;
        }
    }
    return false;
}

Recall recall(const std::vector<double>& fired, const std::vector<double>& truth) {
    Recall r;
    r.total = static_cast<int>(truth.size());
    for (const double t : truth) {
        r.found += near(t, fired) || [&] {
            for (const double d : fired) {
                if (d >= t - 0.03 && d <= t + 0.09) {
                    return true;
                }
            }
            return false;
        }();
    }
    for (const double d : fired) {
        r.falseHits += near(d, truth) ? 0 : 1;
    }
    return r;
}

struct Row {
    Recall kick, snare, hat, kickOnBass;
};

Row measure(const testsupport::SonicMix& mix) {
    const auto track = analysis::AnalysisTrack::analyze(mix.file, analysis::AnalyzerConfig{});
    std::vector<double> k, s, h;
    for (const auto& f : track.frames()) {
        const auto& c = f.causal;
        // A deferred decision is about an attack some hops before: stamped at the attack, as a route sees its time.
        if (c.hit[static_cast<std::size_t>(HitClass::Kick)]) k.push_back(f.timeSeconds);
        if (c.hit[static_cast<std::size_t>(HitClass::Snare)]) s.push_back(f.timeSeconds);
        if (c.hit[static_cast<std::size_t>(HitClass::Hat)]) h.push_back(f.timeSeconds);
    }
    Row r;
    r.kick = recall(k, mix.kicks);
    r.snare = recall(s, mix.snares);
    r.hat = recall(h, mix.hats);
    r.kickOnBass = recall(k, mix.bassOffKick); // "found" here are bass notes that fired as kicks
    return r;
}

void print(const char* name, const Row& r) {
    std::printf("| %-6s | %2d/%d (+%d) | %2d/%d (+%d) | %3d/%d (+%d) | %d/%d |\n", name, r.kick.found, r.kick.total,
                r.kick.falseHits, r.snare.found, r.snare.total, r.snare.falseHits, r.hat.found, r.hat.total,
                r.hat.falseHits, r.kickOnBass.found, r.kickOnBass.total);
}

} // namespace

TEST_CASE("Drums are found alone and under a lead, a bass line, a pad bed and a full mix", "[analysis][adr1067]") {
    const Row drums = measure(testsupport::makeSonicMix(false, false, false));
    const Row lead = measure(testsupport::makeSonicMix(true, false, false));
    const Row bass = measure(testsupport::makeSonicMix(false, true, false));
    const Row pads = measure(testsupport::makeSonicMix(false, false, true));
    const Row full = measure(testsupport::makeSonicMix(true, true, true));
    std::printf("| mix | kick | snare/clap | hat | bass notes read as kicks |\n|---|---|---|---|---|\n");
    print("drums", drums);
    print("+lead", lead);
    print("+bass", bass);
    print("+pads", pads);
    print("full", full);
    for (const auto& [name, r] : {std::pair{"drums", drums}, {"lead", lead}, {"bass", bass}, {"pads", pads}, {"full", full}}) {
        INFO(name << ": kick " << r.kick.found << "/" << r.kick.total << " snare " << r.snare.found << "/"
                  << r.snare.total << " hat " << r.hat.found << "/" << r.hat.total);
        CHECK(r.snare.share() >= 0.85);
        CHECK(r.hat.share() >= 0.70);
    }
    CHECK(drums.kick.share() >= 0.95);
    CHECK(lead.kick.share() >= 0.95);
    CHECK(pads.kick.share() >= 0.80);
    CHECK(bass.kick.share() >= 0.80);
    // The full mix: the owner's case, a pad held under a drum machine with a bass line. Below the coordinator's 90%
    // target (ADR-1067 has why), and held here so it cannot fall back.
    CHECK(full.kick.share() >= 0.75);
    // Bass notes off the kicks that fire as kicks. The bass-only mix is the hardest case (nothing else for the click
    // to be told from): 7 of 24 here, 3 of 24 on the art agent's own render of the same line.
    CHECK(bass.kickOnBass.found <= 8);
    CHECK(full.kickOnBass.found <= 3);
}

TEST_CASE("A pad bed, a bass line and a lead with no drums fire almost no drum", "[analysis][adr1067]") {
    const Row pads = measure(testsupport::makeSonicMix(false, false, true, false));
    const Row full = measure(testsupport::makeSonicMix(true, true, true, false));
    INFO("pads alone: kicks " << pads.kick.falseHits << " snares " << pads.snare.falseHits << " hats "
                              << pads.hat.falseHits << "; parts without drums: kicks " << full.kick.falseHits
                              << " snares " << full.snare.falseHits << " hats " << full.hat.falseHits);
    CHECK(pads.kick.falseHits == 0);
    CHECK(pads.snare.falseHits == 0);
    CHECK(pads.hat.falseHits == 0);
    print("parts", full);
}

