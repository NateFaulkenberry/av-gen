// A scrub of the Glowmere film lands where the play does -- the craft, the animals it lifts, and the
// five aliens that watch it (ADR-671, the owner's ruling of 2026-09-21; ADR-360).
//
// Before ADR-671 a scrub reset the director (ADR-209) and replayed the entities alone, so the
// abduction restarted from its top at every scrub and anything that perceived the craft or the
// animals diverged: measured on this film, 30.6 m apart at 30 s and 101 m at 90 s once the aliens
// ran the awareness layer. The bound here is the one `test_entity_seek.cpp` already holds a scrub
// to: exactly zero.
//
// And the second half of the ruling: a render that starts mid-film -- which is a seek followed by
// ordinary frames -- produces the same frames as one that started at zero, including the tractor
// beam's cut-out, which is director state (`beamSize`, the drain) the replay now reconstructs.
//
// testing.md #33: the generators this film's mushrooms and trees come from are registered here and
// asserted, not inherited from whatever ran before.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "entity/entity.hpp"
#include "organism/mushroom.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/procedural.hpp"
#include "scene/tree_generated.hpp"
#include "signals/signal_bus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

fs::path film() {
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2-multicam.scene.json";
}
bool assetsPresent() {
    return fs::exists(fs::path(AVGEN_SOURCE_DIR) / "assets" / "farm" / "cow.glb") &&
           fs::exists(fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb");
}

struct Film {
    assets::AssetRegistry registry;
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    std::unique_ptr<scene::Composition> comp;
    long long frame = 0;

    Film() : registry(film().parent_path()) {
        organism::registerMushroomGenerator();
        scene::registerTreeGenerator();
        REQUIRE(scene::hasGenerator("mushroom"));
        auto loaded = scene::Composition::loadFile(film(), registry);
        REQUIRE(loaded.has_value());
        comp = std::move(*loaded);
        comp->attach(params, modulator);
        comp->setViewport(640, 360);
        comp->scene().detailLimits.entityDistanceCull = false;
    }
    [[nodiscard]] double now() const { return static_cast<double>(frame) / 60.0; }
    // One ordinary frame at the film's current second, exactly as the application ticks it.
    void tick() {
        FrameTime time;
        time.renderTime = now();
        time.deltaTime = frame == 0 ? 0.0 : 1.0 / 60.0;
        time.frameIndex = static_cast<std::uint64_t>(frame);
        params.resetFinals();
        comp->updateFields(time, bus, modulator);
        modulator.applyRoutes(bus, params, time.deltaTime);
        comp->updateBehaviour(time, bus);
        comp->update(time);
        ++frame;
    }
    // Frames 0 .. seconds*60 inclusive: the last one is *at* `seconds`.
    void playTo(double seconds) {
        const auto last = static_cast<long long>(std::llround(seconds * 60.0));
        while (frame <= last) {
            tick();
        }
    }
    // What `Engine::seekSeconds` does to the entities and the director, landing on `seconds`; the
    // next `tick` is the frame after it.
    void seekTo(double seconds) {
        comp->seekWithDirector(seconds, params, entity::SeekBudget{.maxSeconds = 90.0, .maxBodySteps = 180000},
                               1.0 / 60.0);
        frame = static_cast<long long>(std::llround(seconds * 60.0)) + 1;
    }
    [[nodiscard]] std::map<std::string, glm::vec3> drawn() const {
        std::map<std::string, glm::vec3> out;
        for (const auto& e : comp->entityWorld().entities()) {
            out[e->name()] = e->visualPosition();
        }
        return out;
    }
    [[nodiscard]] std::string beat() const {
        std::string b;
        for (const auto& e : comp->director().log()) {
            if (e.kind == stage::StageEventKind::Beat) {
                b = e.beat;
            }
        }
        return b;
    }
};

float worst(const std::map<std::string, glm::vec3>& a, const std::map<std::string, glm::vec3>& b,
            std::string& who) {
    float w = 0.0f;
    for (const auto& [name, p] : a) {
        const auto it = b.find(name);
        if (it == b.end()) {
            who = name + " (missing)";
            return 1e9f;
        }
        const float d = glm::length(p - it->second);
        if (d > w) {
            w = d;
            who = name;
        }
    }
    return w;
}

} // namespace

TEST_CASE("a scrub of the Glowmere film lands every body where the play did, inside an abduction too",
          "[glowmere][seek][determinism][adr671]") {
    if (!assetsPresent()) {
        SKIP("farm or alien assets missing");
    }
    // 30 s is inside the first abduction's lift; 45 s is in the second cycle's beam; 90 s is the
    // end of the engine's replay window.
    Film played;
    for (const double t : {30.0, 45.0, 90.0}) {
        played.playTo(t);
        Film scrubbed;
        scrubbed.seekTo(t);
        INFO("t = " << t << " s, play beat '" << played.beat() << "', scrub beat '" << scrubbed.beat() << "'");
        // Subject: the director is running, and it has moved things.
        REQUIRE_FALSE(played.beat().empty());
        CHECK(scrubbed.beat() == played.beat());
        std::string who;
        const float w = worst(played.drawn(), scrubbed.drawn(), who);
        INFO("worst body: " << who << ", " << w << " m");
        CHECK(w == 0.0f);
    }
}

TEST_CASE("a render that starts mid-film draws the frames a render from zero draws, beam and all",
          "[glowmere][seek][determinism][beam][adr671]") {
    if (!assetsPresent()) {
        SKIP("farm or alien assets missing");
    }
    // Start inside the second cycle's beam and run four seconds, through the abduction and into the
    // depart -- the beam's fade and cut-out are director state the replay has to have rebuilt.
    constexpr double kStart = 44.0;
    constexpr double kSpan = 4.0;
    Film full;
    Film partial;
    full.playTo(kStart);
    partial.seekTo(kStart);
    REQUIRE(full.frame == partial.frame);
    std::size_t frames = 0;
    std::size_t beamFrames = 0;
    std::vector<std::string> beats;
    float worstBody = 0.0f;
    std::string worstWho;
    bool particlesAgree = true;
    const auto last = static_cast<long long>(std::llround((kStart + kSpan) * 60.0));
    while (full.frame <= last) {
        full.tick();
        partial.tick();
        ++frames;
        std::string who;
        const float w = worst(full.drawn(), partial.drawn(), who);
        if (w > worstBody) {
            worstBody = w;
            worstWho = who;
        }
        const auto& a = full.comp->scene().particles;
        const auto& b = partial.comp->scene().particles;
        REQUIRE(a.size() == b.size());
        for (std::size_t p = 0; p < a.size(); ++p) {
            particlesAgree = particlesAgree && a[p].enabled == b[p].enabled &&
                             a[p].spawnRate == b[p].spawnRate && a[p].sizeStart == b[p].sizeStart &&
                             a[p].position == b[p].position;
            if (a[p].enabled && a[p].spawnRate > 0.0f && a[p].name.find("beam") != std::string::npos) {
                ++beamFrames;
            }
        }
        if (beats.empty() || beats.back() != full.beat()) {
            beats.push_back(full.beat());
        }
    }
    std::string sequence;
    for (const std::string& b : beats) {
        sequence += b + " ";
    }
    INFO(frames << " frames; beats " << sequence << "; beam emitting on " << beamFrames
                << " frames; worst body " << worstWho << " " << worstBody << " m");
    // Subject: the window really does contain a beam that switches off.
    REQUIRE(beamFrames > 0);
    REQUIRE(beats.size() >= 2);
    CHECK(worstBody == 0.0f);
    CHECK(particlesAgree);
}
