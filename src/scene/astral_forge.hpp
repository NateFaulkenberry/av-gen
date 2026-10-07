#pragma once

// THE ASTRAL FORGE (ADR-1221): the second Environment (ADR-1200's seam). A scene-level block, `"astral"`, that holds
// a god formed of two million particles of engraved, tempered metal, bound by coherence to an invisible anatomy and
// conducted by the music. `rendering::AstralRenderer` draws it; the Composition conducts it every frame and, when
// the block drives the camera, places the scene's camera.
//
// Two conductors, one vocabulary (docs/prototypes/astral-forge/03-parameter-state-model.md):
//   * SONG: an analysed track (an offline render, or the editor playing a file). The state is a pure function of
//     the song second -- sections choose the gods, 16-beat phrases build and hold, kick-opened phrases collapse --
//     so a seek or a `--range` lands exactly where play does.
//   * LIVE: live input. The same phrases, built as the beats arrive from the live analysis; collapses on a
//     phrase-opening kick or on a performer's trigger (`astral/collapse`).
// Either way every control below is a real parameter (`astral/...`), so routes, MIDI, macros, scene states, the
// timeline and the editor reach it.

#include "core/error.hpp"
#include "params/parameter_set.hpp"
#include "scene/astral_conductor.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace avgen::spatial {
class AudioHistory;
}

namespace avgen::scene {

enum class AstralConductorMode : std::uint8_t {
    Auto, // song when the frame's audio is an analysed track, live otherwise
    Song,
    Live,
};

// The song conductor's inputs, built once per track: immutable and shared by every frame (and every sim sub-step).
struct AstralSong {
    astral::SongAnalysis song;
    astral::Score score;
};

// The live conductor: the song as it has been heard so far (beats, onsets, band envelopes, phrases).
class AstralLiveConductor {
public:
    // Advances to the history's newest analysed second and returns the state there. `collapse` is true on the
    // frame a performer triggered a collapse.
    astral::State update(const spatial::AudioHistory& audio, double now, const astral::Controls& controls,
                         bool collapse);
    void reset();
    [[nodiscard]] const astral::Score& score() const { return score_; }
    [[nodiscard]] const astral::SongAnalysis& song() const { return song_; }

private:
    void ingest(const spatial::AudioHistory& audio, double now);
    void advancePhrases(double now, bool collapse);

    astral::SongAnalysis song_;
    astral::Score score_;
    std::int64_t nextRow_ = -1;
    float env_[5] = {0, 0, 0, 0, 0};
    float centroid_ = 0.5f;
    float rmsPeak_ = 1e-3f;
    std::size_t onsetsSeen_[4] = {0, 0, 0, 0};
    std::size_t phraseStartBeat_ = 0;
    int godCycle_ = 0;
    double lastNow_ = -1.0;
};

struct AstralForge {
    bool enabled = false;

    // ---- authored (JSON) ----
    std::uint32_t particles = 2u << 20; // the offline tier's count; live tiers simulate a prefix (ADR-1222)
    int gridRes = 192;                  // density grid resolution (cells per side)
    float gridSize = 20.0f;             // density box side (units; the god is ~7 units tall)
    AstralConductorMode conductor = AstralConductorMode::Auto;
    bool driveCamera = true; // the conductor places the scene's camera (`astral/camera` at run time)
    float preroll = 6.0f;    // seconds of simulation before a seek's landing (ADR-360's contract)
    astral::Controls controls; // rest values of the `astral/...` parameters

    // ---- this frame (written by the Composition) ----
    astral::State state;                       // the conductor at the frame's second
    double time = 0.0;                         // that second
    std::shared_ptr<const AstralSong> song;    // song mode: the renderer conducts its sub-steps itself
    astral::Controls live;                     // the parameters' finals this frame
    float cameraDrive = 1.0f;                  // `astral/camera` final
    float density = 1.0f;                      // `astral/density` final: fraction of the particles simulated
    std::uint32_t epoch = 0;                   // bumps when the composition resets the god (a seek it could see)

    [[nodiscard]] bool active() const { return enabled; }
};

[[nodiscard]] Result<AstralForge> astralFromJson(const nlohmann::json& j);
[[nodiscard]] nlohmann::json astralToJson(const AstralForge& a);

// `astral/<leaf>`: summon, hold, collapse, god, intensity, palette, light, atmosphere, godRays, legibility, zoom,
// exposure, camera, density.
struct AstralParameters {
    std::string prefix; // "astral/"
    std::vector<params::IParameter*> all;
};
[[nodiscard]] AstralParameters registerAstralParameters(params::ParameterSet& params, const AstralForge& rest,
                                                        const std::string& prefix);
// The finals into `live.live`, `live.cameraDrive`, `live.density`; returns the `collapse` level (a trigger: the
// Composition fires on its rising edge).
float applyAstralParameters(const AstralParameters& p, const AstralForge& rest, AstralForge& live);

} // namespace avgen::scene
