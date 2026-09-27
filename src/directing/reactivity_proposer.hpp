#pragma once

// ADR-927: the Director's default reactivity proposal -- the reactive catalogue (ADR-925) crossed with
// the layers the music offers, as routes at the owner's three levels (brief section 5).
//
// It is not "everything pulses to the beat". The rules, each measured against the GV3 revision's
// plan (03-revision-plan.md section 5) and its stream reports:
//
//   * **One owner per musical layer; nothing important shares a source.** The heroes, most important
//     first, take the kick, the clap, the bar, a two-bar breath, the phrase turn, the lead and the bass
//     in turn; every further hero relights on the section change, later the farther it stands from the
//     first (a wave outward at walking pace, not a strobe). Each hero's parts answer 35 ms apart, the
//     practical light beside it 50 ms later and its spores 90 ms later: the hero reads as one body,
//     and no two heroes share a source, a timing and an amplitude.
//   * **micro** -- hats and claps on small, fast, local things: the small scatter layers (the smallest
//     echoes the kick 90 ms after the lead hero, the fungi's +0.30), the others flicker on the hats
//     30-110 ms apart, a layer of lamps flares on the claps; free particle systems glint on the hats,
//     staggered 0-120 ms.
//   * **meso** -- the kick and the bar on heroes, and waves through the mushrooms: a layer that names a
//     triggered wave field (ADR-906) gets its wave's depth from the section.
//   * **macro** -- the section on the world: the ecology light, the fog, the wind's strength, the free
//     particles' density, the water's glow, all gliding over seconds; the glowing layers' hue keyed per
//     section through a timeline source (cooler in the quiet sections, warmer in the drop -- never
//     driven by the audio); the lead on a world effect (the aurora).
//   * **Depth follows the section.** Every route that answers a beat takes `depthSource`
//     section.energy (ADR-900), with depthMin/depthMax chosen so the song's quietest section answers
//     at 35% and its loudest in full -- whatever scale its energies were written on.
//
// What it will not do: pulse the key light or the ambient (the style shift the owner ruled out, brief
// section 7), route a shared material (every surface on it moves in lockstep -- the per-node and
// per-layer lanes are the handles), drive a hue from the audio, move a rate whose phase is time x
// rate, or touch a target the author already routes (listed in `notes`). Deterministic: the same
// catalogue and music give the same plan, byte for byte.

#include "directing/plan.hpp"
#include "directing/reactive_catalog.hpp"
#include "directing/time_ref.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace avgen::directing {

struct ReactivityOptions {
    std::string planId = "reactivity";
    std::string title = "Reactivity";
    // Leave a target alone when the author already routes it.
    bool skipAuthored = true;
    // How fast a response travels from one hero to the next, for the heroes that share the section
    // change: 120 m/s puts GV3's heroes 0.2-2.4 s apart (the modulation audit's crash ring).
    float propagationMetresPerSecond = 120.0f;
    // The share of its full response the song's quietest section keeps (depthMin at its energy).
    float quietestDepth = 0.35f;
};

// One layer of the music, as the proposer found it.
struct MusicalLayer {
    std::string name;     // "kick", "clap", "hats", "bar", "breath", "phrase", "lead", "bass", "section"
    std::string signal;   // what a route reads: "audio.onsetLow", ...
    bool present = false;
    std::string evidence; // "kicks 2.2/s at most", "no measured audio"
};

struct ReactivityProposal {
    Plan plan;
    std::vector<MusicalLayer> layers;
    // The depth every beat-driven route takes: its source and range, empty when the song gives none.
    std::string depthSource;
    float depthMin = 0.0f;
    float depthMax = 1.0f;
    // What was left alone and why, and what the scene does not offer ("no wave field is named by a
    // glowing layer ..."), one sentence each.
    std::vector<std::string> notes;
    [[nodiscard]] nlohmann::json summaryJson() const;
};

[[nodiscard]] ReactivityProposal proposeReactivity(const ReactiveCatalog& catalog, const MusicalContext& music,
                                                   const ReactivityOptions& options = {});

// The layers the music offers, from the context's measured section profiles (ADR-899) and its grid.
[[nodiscard]] std::vector<MusicalLayer> musicalLayers(const MusicalContext& music);

} // namespace avgen::directing
