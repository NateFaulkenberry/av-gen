#pragma once

// Unknown keys in an authoring format (ADR-278).
//
// Every JSON parser in this engine reads the keys it knows and walks past the rest. That is the
// normal and forgiving thing to do, and it is how a `"lights"` array sat in two lab fixtures for
// weeks while both scenes were lit by `defaultKeyLight()` at a different angle, colour and
// intensity than the file stated; how a rig that wrote `"coneDegrees"` got the 45-degree default;
// and how `"lodCount"` configured a ladder that was one rung. ADR-225 in an authoring format: a
// setting the application does not read is not a setting, and the file gives no sign.
//
// The answer is a **warning that names the key, the object it was in, and the file** rather than a
// refusal, for two reasons that are evidence rather than taste:
//
//  * Nine scene files in `examples/` carry a `_note`, deliberately and usefully. A leading
//    underscore is this repository's comment convention, so it is exempt by rule rather than by a
//    per-parser special case.
//  * A file written by a newer build and read by an older one loses a feature; refusing it loses
//    the whole world. The report has to survive being wrong about the future.
//
// `unknownKeys` is the whole of the decision and is a pure function, so a test can assert what a
// document would be warned about without capturing a log. `warnUnknownKeys` is the one-line caller.

#include <nlohmann/json_fwd.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::json_keys {

// Keys of `obj` that are not in `known` and do not begin with '_', in the document's own order.
// A non-object returns empty: "this is not an object" is a different complaint, and the parsers
// that care already make it.
[[nodiscard]] std::vector<std::string> unknownKeys(const nlohmann::json& obj,
                                                   std::span<const std::string_view> known);

// One `log::warn` per unknown key. `where` names the object and its file, e.g.
// "scene file 'examples/labs/lod-geometry-lab.scene.json'" or "light rig 'moonlight': light 'key'".
void warnUnknownKeys(const nlohmann::json& obj, std::span<const std::string_view> known,
                     std::string_view where);

} // namespace avgen::json_keys
