#pragma once

// A project's feeds (ADR-1580, ADR-1581): which analysis source drives the beat clock and which feeds each part of
// `response.*`. The project block:
//
//   "feeds": {
//     "beat":     "stem.drums",    the beat clock (beat.*, the metre, music.*, the timeline's beats)
//     "hits":     "role.drums",    every response hit class: kick, low, snare, hat, onset
//     "kick":     "stem.kick_in",  ...one class, over "hits"
//     "levels":   "audio",         bass, level, levelSlow, intensity, sustain, transient, flux (one source: ADR-1510)
//     "history":  "role.drums",    ADR-1631: the audio history spectrum and onset fields hear (its spectrogram and its
//                                  low/mid/high onset rows; the beat row stays the mix's)
//     "historySpectrum": "role.bass",  ...its spectrogram alone, over "history"
//     "historyOnsets":   "role.drums"  ...its onset rows alone, over "history"
//   }
//
// A value is "audio" (the mix: what every project without the block uses), "stem.<key>" -- an offline stem of that
// key, or, while a live input is open, the input source of that key (the same names `stem.<key>.*` uses) -- or
// ADR-1630's "role.<role>": the first source of that role (offline the first stem, live the first input source, in
// list order), whatever its key. The block is written only when a feed is set, each key only when it is not the mix.
//
// ADR-1620: the kick comes only from a source. Unfed -- no `hits` and no `kick` key, or `"kick": "audio"` (a load
// warning) -- it does not fire: the mix gives low, onset, snare, hat and the levels, never a kick.
// ADR-1630: a class (kick, snare, hat) comes only from a drums-role source. One fed from another role is a project
// warning and feeds nothing (the kick) or the mix (the snare and hat); `low`, `onset` and the levels take any role.

#include "audio/source_role.hpp"
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
// ADR-1630: whether hit class `k` names an instrument (kick, snare, hat) -- measured identity, from a drums source only
// -- rather than broad activity (low, onset).
[[nodiscard]] constexpr bool feedHitIsClass(std::size_t k) {
    return k == 0 || k == 2 || k == 3;
}

struct Feeds {
    // Each a stem / input-source key, or a role reference "role.<role>" (ADR-1630); empty = the mix.
    std::string beat;
    std::string hits;                    // every class not named below
    std::array<std::optional<std::string>, 5> hit{}; // per class (kFeedHitNames), over `hits`; "" = the mix
    std::string levels;
    // ADR-1631: the audio history.
    std::string history;
    std::optional<std::string> historySpectrum; // over `history`; "" = the mix
    std::optional<std::string> historyOnsets;   // over `history`; "" = the mix

    // The key that feeds hit class `k` ("" = the mix).
    [[nodiscard]] const std::string& hitSource(std::size_t k) const { return hit[k] ? *hit[k] : hits; }
    // ADR-1631: the keys the history's spectrogram and onset rows come from ("" = the mix).
    [[nodiscard]] const std::string& spectrumSource() const { return historySpectrum ? *historySpectrum : history; }
    [[nodiscard]] const std::string& onsetsSource() const { return historyOnsets ? *historyOnsets : history; }
    [[nodiscard]] bool anyResponse() const;
    [[nodiscard]] bool anyHistory() const { return !spectrumSource().empty() || !onsetsSource().empty(); }
    [[nodiscard]] bool empty() const { return beat.empty() && !anyResponse() && !anyHistory(); }
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

// "audio" for "", "stem.<key>" for a key, a role reference as it is; and back ("audio" -> "", "role.<role>" kept).
[[nodiscard]] std::string feedValue(const std::string& key);
[[nodiscard]] Result<std::string> feedKey(std::string_view value);
// ADR-1630: the role a feed key refers to ("role.drums" -> Drums), nullopt for a stem / input key or the mix.
[[nodiscard]] std::optional<SourceRole> feedRole(std::string_view key);
// ADR-1630: the role reference for `role` ("role.drums").
[[nodiscard]] std::string roleFeedKey(SourceRole role);

} // namespace avgen::audio
