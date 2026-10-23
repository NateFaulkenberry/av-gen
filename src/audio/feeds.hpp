#pragma once

// A project's feeds (ADR-1580, ADR-1581): which analysis source drives the beat clock and which feeds each part of
// `response.*`. The project block:
//
//   "feeds": {
//     "beat":   "stem.drums",    the beat clock (beat.*, the metre, music.*, the timeline's beats)
//     "hits":   "stem.drums",    every response hit class: kick, low, snare, hat, onset
//     "kick":   "stem.kick_in",  ...one class, over "hits"
//     "levels": "audio"          bass, level, levelSlow, intensity, sustain, transient, flux (one source: ADR-1510)
//   }
//
// A value is "audio" (the mix: what every project without the block uses) or "stem.<key>": an offline stem of that
// key, or, while a live input is open, the input source of that key (the same names `stem.<key>.*` uses). The block is
// written only when a feed is set, each key only when it is not the mix.
//
// ADR-1620: the kick comes only from a source. Unfed -- no `hits` and no `kick` key, or `"kick": "audio"` (a load
// warning) -- it does not fire: the mix gives low, onset, snare, hat and the levels, never a kick.

#include "core/error.hpp"

#include <nlohmann/json_fwd.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::audio {

// The response's hit classes in `analysis::HitClass` order.
inline constexpr std::array<const char*, 5> kFeedHitNames{"kick", "low", "snare", "hat", "onset"};

struct Feeds {
    // Each a stem / input-source key; empty = the mix.
    std::string beat;
    std::string hits;                    // every class not named below
    std::array<std::optional<std::string>, 5> hit{}; // per class (kFeedHitNames), over `hits`; "" = the mix
    std::string levels;

    // The key that feeds hit class `k` ("" = the mix).
    [[nodiscard]] const std::string& hitSource(std::size_t k) const { return hit[k] ? *hit[k] : hits; }
    [[nodiscard]] bool anyResponse() const;
    [[nodiscard]] bool empty() const { return beat.empty() && !anyResponse(); }
    // Every key named, once each, in a fixed order (beat, hits, the classes, levels).
    [[nodiscard]] std::vector<std::string> keys() const;
    // Whether `key` feeds any part of the response (its source needs the hits tier).
    [[nodiscard]] bool feedsResponse(std::string_view key) const;
    // ADR-1620: whether the kick has a source (the mix has none).
    [[nodiscard]] bool kickFed() const { return !hitSource(0).empty(); }
    [[nodiscard]] bool operator==(const Feeds&) const = default;

    // Parses the block. An unknown key or a value that is neither "audio" nor "stem.<valid key>" fails the load (a
    // misspelt feed is a project bug, not a preference).
    // ADR-1620: `"kick": "audio"` loads with a warning in `warnings` and feeds the kick nothing.
    [[nodiscard]] static Result<Feeds> fromJson(const nlohmann::json& j, std::vector<std::string>* warnings = nullptr);
    [[nodiscard]] nlohmann::json toJson() const;
};

// "audio" for "", "stem.<key>" otherwise; and back ("audio" -> "").
[[nodiscard]] std::string feedValue(const std::string& key);
[[nodiscard]] Result<std::string> feedKey(std::string_view value);

} // namespace avgen::audio
