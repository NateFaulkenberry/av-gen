#pragma once

// A live analysis source on an input device (stems-multichannel, ADR-1521): one device channel, or a linked pair,
// published as `stem.<key>.*` exactly as an offline stem is (signals/stem_signals.hpp).
//
// A pair is analysed as ONE source: the mono sum (L + R) / 2 -- as the main mix and a stereo stem are -- with
// `stem.<key>.width` its side/mid. Two channels are summed only when the artist makes them a pair; for independent
// left and right, make two mono sources.
//
// AV Gen does not know what is plugged into an input. A mixer's drum bus on input 1 is the artist's routing; the
// label is the artist's word for it, and so is the role (ADR-1630).

#include "audio/source_role.hpp"

#include <cstdint>
#include <string>

namespace avgen::audio {

struct InputSource {
    std::string key;            // [a-z0-9_]+, unique among the sources (audio::makeStemKey)
    std::string label;
    std::uint32_t channel = 0;  // 0-based device channel; a pair is channel and channel + 1
    bool pair = false;
    SourceRole role = SourceRole::Other; // ADR-1630: what the source is (`role.<role>.*`)
    float gainDb = 0.0f;        // ADR-1582: the source's trim before analysis, -24..+24 dB (on top of audio/inputGain)
    [[nodiscard]] bool operator==(const InputSource&) const = default;
};

} // namespace avgen::audio
