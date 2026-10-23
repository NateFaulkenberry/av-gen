#pragma once

// What an analysis source is (ADR-1630): the artist's word for the part a stem or live input source carries.
//
// A role is measured identity's gate, not a measurement. AV Gen never infers it from the audio or the key: a source
// keyed `drums` with no role is `other`. Only a `drums` source runs the kick/snare/hat classifier and publishes the
// class names (`stem.<key>.kick/snare/hat`, `role.drums.kick/...`); every other role publishes onsets, levels and the
// onset register -- broad musical activity -- and never a class name. `role.<role>.*` publishes the first active
// source of each role, so a scene (and `tools/drum_signals.py`) names "the drums" without knowing any key.

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace avgen::audio {

enum class SourceRole : std::uint8_t { Drums, Bass, Synth, Vocal, Other };

inline constexpr std::size_t kSourceRoleCount = 5;
inline constexpr std::array<const char*, kSourceRoleCount> kSourceRoleNames{"drums", "bass", "synth", "vocal", "other"};

[[nodiscard]] constexpr const char* sourceRoleName(SourceRole role) {
    return kSourceRoleNames[static_cast<std::size_t>(role)];
}

[[nodiscard]] constexpr std::optional<SourceRole> sourceRoleFromName(std::string_view name) {
    for (std::size_t i = 0; i < kSourceRoleCount; ++i) {
        if (name == kSourceRoleNames[i]) {
            return static_cast<SourceRole>(i);
        }
    }
    return std::nullopt;
}

// Whether a source of `role` runs the kick/snare/hat classifier and publishes the class names (only the drums).
[[nodiscard]] constexpr bool classifiesHits(SourceRole role) {
    return role == SourceRole::Drums;
}

} // namespace avgen::audio
