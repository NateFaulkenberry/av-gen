#pragma once

// What an analysis source is (ADR-1630): the artist's word for the part a stem or live input source carries.
//
// A role is the artist's statement, not a measurement. AV Gen never infers it from the audio or the key: a source
// keyed `drums` with no role is `other`. Every role publishes the same onsets, levels and onset register -- broad
// musical activity, never an instrument's identity (ADR-1784 cut the drums role's kick/snare/hat classifier).
// `role.<role>.*` publishes the first active source of each role, so a scene (and `tools/role_signals.py`) names "the
// drums" without knowing any key.

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

} // namespace avgen::audio
