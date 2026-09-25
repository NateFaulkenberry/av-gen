// An `Engine` scrub of the Glowmere film lands where an `Engine` play does (ADR-800).
//
// `test_glowmere_scrub.cpp` holds the composition to exactly zero, but its harness goes through no
// `Engine`. Through one, on 2026-09-24, a scrub put the saucer 12 m and the aliens 98 m from where a
// play put them at 150 s. Three causes were measured, each with the other two removed:
//
//   1. The live entity distance cull. A play culls far bodies and the replay never does. This is
//      the preview's documented trade (ADR-186); offline renders lift it (ADR-191), so the cases
//      here lift it as well.
//   2. Audio-reactive world events and behaviours. The replay has no signal bus. This one is the
//      hidden `[.known-defect]` case below.
//   3. The landing instant was evaluated twice. `loadProject` ends in a seek to zero and the first
//      frame is at zero, and a render seeks to its start and then ticks at its start. The second
//      pass ran the saucer's director again and entered its next beat a frame early; everything
//      downstream of the saucer stayed a frame early. That is fixed, and the first case holds it.

#include "app/engine.hpp"
#include "core/time.hpp"
#include "entity/entity.hpp"
#include "scene/composition.hpp"

#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>

#include <algorithm>
#include <filesystem>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

fs::path project() { return fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2-multicam.json"; }
bool assetsPresent() {
    return fs::exists(fs::path(AVGEN_SOURCE_DIR) / "assets" / "farm" / "cow.glb") &&
           fs::exists(fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb");
}

void frameAt(app::Engine& engine, long long frame) {
    engine.update(FrameTime{static_cast<double>(frame) / 60.0, frame == 0 ? 0.0 : 1.0 / 60.0,
                            static_cast<std::uint64_t>(frame)});
}

std::map<std::string, glm::vec3> drawn(const app::Engine& engine) {
    std::map<std::string, glm::vec3> out;
    for (const auto& e : engine.composition()->entityWorld().entities()) {
        out[e->name()] = e->visualPosition();
    }
    return out;
}

// The film as an offline render sees it: the entity distance cull lifted (ADR-191). `withAudio`
// false removes the audio, and with it every audio-reactive signal.
void load(app::Engine& engine, bool withAudio) {
    REQUIRE(engine.loadProject(project()).has_value());
    scene::DetailLimits limits = engine.detailLimits();
    limits.entityDistanceCull = false;
    engine.setDetailLimits(limits);
    if (!withAudio) {
        REQUIRE(engine.setAudioClips({}).has_value());
    }
}

// For each target frame: a play from zero through it and one frame more, against a fresh engine that
// scrubs to it and plays that one frame. The worst body over the whole cast, by name.
void requireScrubEqualsPlay(bool withAudio, const std::vector<long long>& targets) {
    app::Engine played(app::EngineMode::Offline);
    load(played, withAudio);
    std::map<long long, std::map<std::string, glm::vec3>> playedAt;
    long long frame = 0;
    for (long long t : targets) {
        for (; frame <= t + 1; ++frame) {
            frameAt(played, frame);
        }
        playedAt[t] = drawn(played);
    }
    for (long long t : targets) {
        app::Engine scrubbed(app::EngineMode::Offline);
        load(scrubbed, withAudio);
        scrubbed.seekSeconds(static_cast<double>(t) / 60.0);
        frameAt(scrubbed, t + 1);
        const auto b = drawn(scrubbed);
        REQUIRE(b.size() == playedAt[t].size());
        float worst = 0.0f;
        std::ostringstream who;
        for (const auto& [name, p] : playedAt[t]) {
            const float d = glm::length(p - b.at(name));
            if (d > 0.0f) {
                who << name << ' ' << d << " m; ";
            }
            worst = std::max(worst, d);
        }
        INFO("scrub to " << static_cast<double>(t) / 60.0 << " s: " << who.str());
        CHECK(worst == 0.0f);
    }
}

} // namespace

// Cause 3. One second is where the saucer first left the play (0.033 m), and thirty is well past its
// second and third beats. Measured with the fix at 1, 2, 3, 5, 10, 30, 60, 90, 120 and 150 s: every
// body exactly equal at every one.
TEST_CASE("an Engine scrub of the Glowmere film lands where an Engine play does", "[seek][engine][glowmere][adr800]") {
    if (!assetsPresent()) {
        SKIP("farm or alien assets missing");
    }
    requireScrubEqualsPlay(false, {60, 1800});
}

// Cause 2, recorded rather than fixed yet: with the film's audio, its audio-reactive world events
// and behaviours fire in a play and not in the replay, and the aliens part company (Vane 92 m by 90 s).
TEST_CASE("an Engine scrub of the Glowmere film with its audio lands where the play does", "[.known-defect][seek][engine][glowmere][adr800]") {
    if (!assetsPresent()) {
        SKIP("farm or alien assets missing");
    }
    requireScrubEqualsPlay(true, {5400});
}
