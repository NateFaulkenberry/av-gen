#pragma once

// The Composition panel (ADR-081): the layer stack, and an inspector for the selected layer.
//
// The authoring loop this exists to make fast: Add Text, type the words, drag it where you want
// it, pick a font, move the playhead, press the key dot, move the playhead, change the value,
// press the key dot again, play. No dialogs, no modes, no Apply button.
//
// Every animatable row carries a key dot on its left. It records a key for that parameter at the
// playhead on the engine's own timeline -- the same timeline the camera and the scene are on --
// and it is lit when the parameter is already automated, so "is this animated?" is answerable by
// looking rather than by opening the timeline.

#include "app/engine.hpp"
#include "rendering/composition_renderer.hpp"

#include <cstdint>
#include <string>

namespace avgen::ui {

class CompositionPanel {
public:
    // Live statistics from the compositor, for the line along the foot. Optional.
    const rendering::CompositionStats* stats = nullptr;

    void draw(app::Engine& engine);

    [[nodiscard]] std::uint32_t selected() const { return selected_; }
    void select(std::uint32_t id) { selected_ = id; }

private:
    void drawToolbar(app::Engine& engine);
    void drawList(app::Engine& engine);
    void drawInspector(app::Engine& engine, comp::Layer& layer);
    void drawTextInspector(app::Engine& engine, comp::TextLayer& text);
    void drawShapeInspector(app::Engine& engine, comp::ShapeLayer& shape);
    void drawFontPicker(comp::TextLayer& text);
    // The key dot for one parameter path. Returns true when it recorded a key this frame.
    bool keyDot(app::Engine& engine, const std::string& path);

    std::uint32_t selected_ = 0;
    std::uint32_t bufferOwner_ = 0; // which layer textBuffer_ currently holds
    char textBuffer_[1024] = "";
    char nameBuffer_[96] = "";
    char fontFilter_[64] = "";
    int addKind_ = 0;
};

} // namespace avgen::ui
