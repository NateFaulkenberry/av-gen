#pragma once

// The real layer sink (ADR-089): overlay cues become `comp::LayerStack` layers.
//
// This is the one file that knows both vocabularies. Everything above it talks about cues -- a line
// of text, from here to there, bottom centre, fading -- and everything below it talks about text
// layers, glyph atlases and normalised frame coordinates. Keeping the translation in one place is
// what lets `seq::Sequence` stay a pure value and `comp::LayerStack` stay ignorant of songs.
//
// ## Ownership
//
// A layer this sink makes is named `seq:<cueId>` and belongs to the sequence, not to the author.
// `clear()` removes every such layer and reports the parameter paths they owned, so the installer
// can erase the timeline tracks that named them; layers the author made by hand are never touched.
// That rule is the whole of the ownership model, and it is why re-baking after an edit replaces the
// lyrics rather than accumulating a second copy of them.
//
// ## Styles
//
// A cue names a style, and a style is a handful of typographic defaults -- family, weight, size,
// tracking, case. Deliberately a tiny fixed set rather than a stylesheet system: the cue's `extra`
// object overrides any individual field, so anything the set does not cover is still authorable
// without inventing a second way to describe type.

#include "comp/layer_stack.hpp"
#include "seq/layers.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace avgen::seq {

// The typographic defaults a style name stands for. Every field is overridable per cue through
// `OverlayCue::extra`.
struct OverlayStyle {
    std::string family = "Helvetica Neue";
    float weight = 0.0f;      // CoreText trait, -1 ultralight .. 1 black
    bool italic = false;
    float sizeEm = 0.055f;    // fraction of the frame height, before OverlayCue::size scales it
    float tracking = 0.0f;    // em between glyphs
    float lineSpacing = 1.0f;
    float outlineWidth = 0.0f;
    float shadowOpacity = 0.0f;
    comp::TextAlign align = comp::TextAlign::Center;
};

// The built-in styles: "title", "lyric" (the default), "caption", "credit".
[[nodiscard]] OverlayStyle overlayStyle(std::string_view name);
[[nodiscard]] std::vector<std::string_view> overlayStyleNames();

class CompositionLayerSink final : public LayerSink {
public:
    // `params` may be null (a stack with no parameter set, as in a headless tool or a test that
    // only cares about layer content). With one, every layer the sink makes registers its
    // properties immediately, because a track naming a parameter that does not exist yet is a
    // track that silently does nothing (ADR-075).
    CompositionLayerSink(comp::LayerStack& stack, params::ParameterSet* params);

    [[nodiscard]] Result<std::string> realise(const OverlayCue& cue) override;
    [[nodiscard]] LayerTarget target(std::string_view layerId,
                                     std::string_view property) const override;
    // Removes every `seq:` layer in the stack and records the parameter paths they owned.
    void clear() override;

    // Parameter paths that belonged to layers this sink removed. The installer erases the timeline
    // tracks naming them; without that, a re-bake leaves tracks bound to nothing behind.
    [[nodiscard]] const std::vector<std::string>& retiredPaths() const { return retired_; }
    // Cues the sink could not realise, with the reason. Not errors: an image cue in a project
    // opened by a build with no image layer kind is a thing to say out loud, not to refuse.
    [[nodiscard]] const std::vector<std::string>& warnings() const { return warnings_; }
    // Layer ids this sink created, in creation order.
    [[nodiscard]] const std::vector<std::uint32_t>& created() const { return created_; }

    // The name a cue's layer takes. Public so the editor can recognise a sequencer-owned layer.
    [[nodiscard]] static std::string layerName(std::string_view cueId);
    [[nodiscard]] static bool ownedName(std::string_view name);
    static constexpr std::string_view kPrefix = "seq:";

private:
    comp::LayerStack& stack_;
    params::ParameterSet* params_ = nullptr;
    std::vector<std::string> retired_;
    std::vector<std::string> warnings_;
    std::vector<std::uint32_t> created_;
};

} // namespace avgen::seq
