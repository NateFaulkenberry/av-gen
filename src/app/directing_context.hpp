#pragma once

// The engine's side of the Director's resolver (ADR-755). `src/directing/` never reads the engine
// (ADR-750); these two functions are where the engine's state becomes the plain values it does read.

#include "analysis/analysis_track.hpp"
#include "app/engine.hpp"
#include "directing/resolver.hpp"
#include "directing/time_ref.hpp"
#include "scene/composition.hpp"

#include <string>
#include <vector>

namespace avgen::app {

// The piece as the Director places times in it: the sequence's sections (authored timeline first),
// the analysed beat grid and tempo, and the piece's duration.
[[nodiscard]] inline directing::MusicalContext musicalContextFor(const Engine& engine) {
    std::vector<double> beats;
    double tempo = 0.0;
    if (const analysis::AnalysisTrack* track = engine.track(); track != nullptr) {
        beats = track->beats().beatTimes;
        tempo = static_cast<double>(track->beats().tempoBpm);
    }
    return directing::musicalContextFrom(engine.sequence(), beats, tempo, engine.durationSeconds());
}

// Everything in the scene a plan can name, plus every parameter path (by exact path only).
[[nodiscard]] inline directing::SubjectIndex subjectIndexFor(Engine& engine) {
    directing::SubjectIndex index;
    if (const scene::Composition* comp = engine.composition(); comp != nullptr) {
        index = directing::SubjectIndex::fromComposition(*comp);
    }
    std::vector<std::string> paths;
    paths.reserve(engine.params().size());
    for (const params::IParameter* p : engine.params().ordered()) {
        if (p != nullptr) {
            paths.emplace_back(p->path());
        }
    }
    index.addParameters(paths);
    return index;
}

} // namespace avgen::app
