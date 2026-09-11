#pragma once

// Fonts for the composition layer (ADR-083).
//
// Text is shaped and rasterised by the platform's own type engine -- CoreText on macOS. Nothing
// is bundled: a project stores a font by family and PostScript name, and the machine that opens
// it supplies the face. That is the only arrangement that is honest about licensing (no font file
// is copied into this repository or into an exported bundle) and it is the reason `resolve()`
// reports a substitution instead of performing one quietly: a render whose type silently changed
// is a render whose determinism claim is false.
//
// Everything here is in **em units**: a glyph's box, its pen advance and the line height are
// fractions of the font size, with the baseline at y = 0 and y increasing upwards. The layer
// decides how many pixels an em is, so one shaping result serves every size and every output
// resolution, and an animated font size never re-shapes or re-rasterises anything.

#include "comp/sdf_build.hpp"
#include "core/error.hpp"

#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::comp {

// How a font is named in a project file. `postScriptName` pins an exact face when it is known;
// `family` plus the traits finds one when it is not, which is what survives a font being updated.
struct FontDesc {
    std::string family = "Helvetica Neue";
    std::string postScriptName;      // exact face, e.g. "HelveticaNeue-Bold"; may be empty
    float weight = 0.0f;             // CoreText weight trait, -1 (ultralight) .. 1 (black); 0 = regular
    bool italic = false;
    std::string fallback = "Helvetica"; // used, loudly, when the family is not installed

    [[nodiscard]] std::string key() const; // cache key; stable for equal descriptions
    [[nodiscard]] bool operator==(const FontDesc&) const = default;

    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static Result<FontDesc> fromJson(const nlohmann::json& j);
};

// What `resolve()` found. `substituted` is the flag the editor and the log care about.
struct FontResolution {
    std::string postScriptName; // the face that will actually be used
    std::string family;
    bool substituted = false;   // the requested family/face was not installed
    bool available = false;     // something usable was found at all
};

// One glyph's field, positioned relative to the pen in em units.
struct GlyphImage {
    SdfBitmap sdf;
    float originX = 0.0f; // em: left edge of the bitmap, relative to the pen
    float originY = 0.0f; // em: bottom edge of the bitmap, relative to the baseline
    float sizeX = 0.0f;   // em: bitmap width  (includes the SDF spread margin)
    float sizeY = 0.0f;   // em: bitmap height
};

struct ShapedGlyph {
    std::uint16_t glyphId = 0;
    float x = 0.0f;      // em, pen position from the line's origin
    float y = 0.0f;      // em, baseline of this glyph's line (0 for the first line, negative below)
    std::uint32_t line = 0;
};

struct ShapedText {
    std::vector<ShapedGlyph> glyphs;
    std::vector<float> lineWidths; // em, one per line (advance width, tracking not included)
    float ascent = 0.8f;           // em above the baseline
    float descent = 0.2f;          // em below (positive)
    float lineHeight = 1.2f;       // em, the font's natural leading
    [[nodiscard]] std::uint32_t lineCount() const { return static_cast<std::uint32_t>(lineWidths.size()); }
};

// The platform type engine. One process-wide instance; the implementation caches shaping and
// rasterisation results itself, because the same lyric is shaped on every geometry rebuild and the
// same glyph is rasterised once for every layer that uses it.
class FontBackend {
public:
    virtual ~FontBackend() = default;
    // Installed families, sorted, without the hidden ones (leading '.').
    [[nodiscard]] virtual const std::vector<std::string>& families() = 0;
    [[nodiscard]] virtual FontResolution resolve(const FontDesc& desc) = 0;
    // Shapes `utf8` with the face `desc` resolves to. Line breaks are '\n'; every other character
    // goes to the type engine, so ligatures and kerning are the platform's.
    [[nodiscard]] virtual Result<ShapedText> shape(const FontDesc& desc, std::string_view utf8) = 0;
    [[nodiscard]] virtual Result<GlyphImage> glyph(const FontDesc& desc, std::uint16_t glyphId) = 0;
    // True when this build has a real type engine. False on the stub: text layers keep their
    // content and their parameters, and draw nothing.
    [[nodiscard]] virtual bool available() const = 0;
};

[[nodiscard]] FontBackend& fontBackend();

} // namespace avgen::comp
