#include "ui/composition_panel.hpp"

#include "ui/style.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace avgen::ui {

namespace {

constexpr ImVec4 kKeyed(1.0f, 0.75f, 0.3f, 1.0f);
constexpr ImVec4 kMuted(0.45f, 0.45f, 0.5f, 1.0f);
constexpr ImVec4 kWarning(1.0f, 0.45f, 0.35f, 1.0f);

void copyInto(char* buffer, std::size_t size, const std::string& text) {
    const std::size_t n = std::min(text.size(), size - 1);
    std::memcpy(buffer, text.data(), n);
    buffer[n] = '\0';
}

// A labelled row with a fixed label column, so a panel full of them lines up.
void rowLabel(const char* label) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(ImGui::CalcTextSize("line spacing").x + ImGui::GetStyle().ItemSpacing.x * 2.0f);
    ImGui::SetNextItemWidth(-1.0f);
}

} // namespace

bool CompositionPanel::keyDot(app::Engine& engine, const std::string& path) {
    ImGui::PushID(path.c_str());
    const bool automated = engine.timeline().isAutomated(path);
    ImGui::PushStyleColor(ImGuiCol_CheckMark, kKeyed);
    ImGui::PushStyleColor(ImGuiCol_Text, automated ? kKeyed : kMuted);
    const bool clicked = ImGui::RadioButton("##key", automated);
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s\nkey at the playhead (%.2f s)%s", path.c_str(), engine.timelineClock().seconds,
                          automated ? "\nalready automated -- the slider is the base value" : "");
    }
    if (clicked) {
        engine.recordKey(path);
    }
    ImGui::PopID();
    ImGui::SameLine();
    return clicked;
}

void CompositionPanel::draw(app::Engine& engine) {
    // The inspector edits authored fields; the sliders in the Parameters panel, the timeline and
    // the modulation routes all write through the parameters. Pulling first is what keeps the two
    // views of the same value from disagreeing, in either direction.
    engine.layers().pullAuthored();

    drawToolbar(engine);
    ImGui::Separator();
    drawList(engine);
    ImGui::Separator();
    if (comp::Layer* layer = engine.layers().find(selected_)) {
        drawInspector(engine, *layer);
    } else {
        ImGui::TextDisabled("Select a layer, or add one.");
    }
    if (stats != nullptr) {
        ImGui::Separator();
        ImGui::TextDisabled("%u drawn, %u draw call(s), %u item(s), %u glyph(s)", stats->layers, stats->draws,
                            stats->items, stats->glyphs);
        if (stats->gpuMs >= 0.0) {
            ImGui::SameLine();
            ImGui::TextDisabled("  %.3f ms GPU", stats->gpuMs);
        }
    }
}

void CompositionPanel::drawToolbar(app::Engine& engine) {
    if (ImGui::Button("+ Add Layer")) {
        ImGui::OpenPopup("addlayer");
    }
    if (ImGui::BeginPopup("addlayer")) {
        if (ImGui::MenuItem("Text")) {
            // A default string rather than an empty one: an empty text layer looks like a bug.
            selected_ = engine.addTextLayer("Your words here", engine.timelineClock().seconds).id;
        }
        if (ImGui::MenuItem("Rectangle")) {
            selected_ = engine.addShapeLayer(comp::ShapeKind::Rectangle).id;
        }
        if (ImGui::MenuItem("Ellipse")) {
            selected_ = engine.addShapeLayer(comp::ShapeKind::Ellipse).id;
        }
        if (ImGui::MenuItem("Line")) {
            selected_ = engine.addShapeLayer(comp::ShapeKind::Line).id;
        }
        ImGui::Separator();
        ImGui::TextDisabled("Image, video and nested compositions are not in this version.");
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu layer(s)  |  t = %.2f s", engine.layers().size(), engine.timelineClock().seconds);

    // Tracks pointing at parameters this build does not have. Silence here is what made two
    // earlier features do nothing without saying so (ADR-075, ADR-080), so it is on screen.
    const auto& unbound = engine.timeline().unboundTargets();
    if (!unbound.empty()) {
        ImGui::TextColored(kWarning, "%zu timeline track(s) name a parameter that does not exist:",
                           unbound.size());
        for (std::size_t i = 0; i < unbound.size() && i < 6; ++i) {
            ImGui::TextColored(kWarning, "    %s", unbound[i].c_str());
        }
    }
}

void CompositionPanel::drawList(app::Engine& engine) {
    comp::LayerStack& stack = engine.layers();
    const std::size_t count = stack.size();
    std::uint32_t remove = 0;
    std::uint32_t duplicate = 0;
    int dragFrom = -1;
    int dragTo = -1;

    // Top of the list is the top of the stack, which is how every compositor reads.
    ImGui::TextDisabled("Layers (top of the list draws last)");
    const float listHeight = std::min(190.0f, std::max(60.0f, static_cast<float>(count) * 22.0f + 8.0f));
    if (ImGui::BeginChild("layers", ImVec2(0, listHeight), ImGuiChildFlags_Borders)) {
        for (std::size_t row = 0; row < count; ++row) {
            const std::size_t index = count - 1 - row;
            comp::Layer* layer = stack.at(index);
            if (layer == nullptr) {
                continue;
            }
            ImGui::PushID(static_cast<int>(layer->id));
            // Nothing to do on a change: the stack skips a disabled layer when it builds the frame.
            ImGui::Checkbox("##on", &layer->enabled);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("visible");
            }
            ImGui::SameLine();
            char label[160];
            if (layer->endTime > layer->startTime) {
                std::snprintf(label, sizeof(label), "%s   %.2f - %.2f s", layer->name.c_str(), layer->startTime,
                              layer->endTime);
            } else if (layer->startTime > 0.0) {
                std::snprintf(label, sizeof(label), "%s   from %.2f s", layer->name.c_str(), layer->startTime);
            } else {
                std::snprintf(label, sizeof(label), "%s", layer->name.c_str());
            }
            const bool live = layer->enabled && layer->liveAt(engine.timelineClock().seconds);
            if (!live) {
                ImGui::PushStyleColor(ImGuiCol_Text, kMuted);
            }
            if (ImGui::IsItemHovered()) {
                // These rows reorder by drag-and-drop (the BeginDragDropSource below), which is
                // exactly the kind of affordance nobody discovers without being told.
                setHoverCursor(ImGuiMouseCursor_ResizeAll);
            }
            if (ImGui::Selectable(label, selected_ == layer->id)) {
                selected_ = layer->id;
            }
            if (!live) {
                ImGui::PopStyleColor();
            }
            // Drag to reorder. Layer order is compositing order, so this is the one gesture that
            // changes what is in front of what.
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoDisableHover)) {
                const int payload = static_cast<int>(index);
                ImGui::SetDragDropPayload("avgen.layer", &payload, sizeof(payload));
                ImGui::TextUnformatted(layer->name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("avgen.layer")) {
                    dragFrom = *static_cast<const int*>(payload->Data);
                    dragTo = static_cast<int>(index);
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::BeginPopupContextItem("layermenu")) {
                if (ImGui::MenuItem("Duplicate")) {
                    duplicate = layer->id;
                }
                if (ImGui::MenuItem("Delete")) {
                    remove = layer->id;
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    const int index = stack.indexOf(selected_);
    ImGui::BeginDisabled(index < 0);
    if (ImGui::Button("Up") && index >= 0) {
        stack.moveTo(selected_, static_cast<std::size_t>(index) + 1);
    }
    ImGui::SameLine();
    if (ImGui::Button("Down") && index > 0) {
        stack.moveTo(selected_, static_cast<std::size_t>(index) - 1);
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate")) {
        duplicate = selected_;
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        remove = selected_;
    }
    ImGui::EndDisabled();

    if (dragFrom >= 0 && dragTo >= 0 && dragFrom != dragTo) {
        if (comp::Layer* moving = stack.at(static_cast<std::size_t>(dragFrom))) {
            stack.moveTo(moving->id, static_cast<std::size_t>(dragTo));
        }
    }
    if (duplicate != 0) {
        if (const comp::Layer* copy = engine.duplicateLayer(duplicate)) {
            selected_ = copy->id;
        }
    }
    if (remove != 0) {
        engine.removeLayer(remove);
        if (selected_ == remove) {
            selected_ = stack.empty() ? 0 : stack.at(stack.size() - 1)->id;
        }
    }
}

void CompositionPanel::drawInspector(app::Engine& engine, comp::Layer& layer) {
    if (bufferOwner_ != layer.id) {
        bufferOwner_ = layer.id;
        copyInto(nameBuffer_, sizeof(nameBuffer_), layer.name);
        if (auto* text = dynamic_cast<comp::TextLayer*>(&layer)) {
            copyInto(textBuffer_, sizeof(textBuffer_), text->text());
        }
    }
    ImGui::PushID(static_cast<int>(layer.id));
    rowLabel("name");
    if (ImGui::InputText("##name", nameBuffer_, sizeof(nameBuffer_))) {
        layer.name = nameBuffer_;
    }

    if (auto* text = dynamic_cast<comp::TextLayer*>(&layer)) {
        drawTextInspector(engine, *text);
    } else if (auto* shape = dynamic_cast<comp::ShapeLayer*>(&layer)) {
        drawShapeInspector(engine, *shape);
    }

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        keyDot(engine, layer.parameterPath("position"));
        rowLabel("position");
        ImGui::DragFloat2("##position", &layer.position.x, 0.002f, -4.0f, 5.0f, "%.3f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("fraction of the frame, origin bottom left. (0.5, 0.5) is the centre "
                              "at every resolution.");
        }
        keyDot(engine, layer.parameterPath("scale"));
        rowLabel("scale");
        ImGui::DragFloat2("##scale", &layer.scale.x, 0.005f, 0.0f, 20.0f, "%.3f");
        keyDot(engine, layer.parameterPath("rotation"));
        rowLabel("rotation");
        ImGui::DragFloat("##rotation", &layer.rotation, 0.25f, -3600.0f, 3600.0f, "%.1f deg");
        keyDot(engine, layer.parameterPath("anchor"));
        rowLabel("anchor");
        ImGui::DragFloat2("##anchor", &layer.anchor.x, 0.005f, -2.0f, 3.0f, "%.3f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("what the layer turns and scales about, as a fraction of its own box");
        }
    }

    if (ImGui::CollapsingHeader("Timing", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto start = static_cast<float>(layer.startTime);
        auto end = static_cast<float>(layer.endTime);
        rowLabel("in");
        if (ImGui::DragFloat("##in", &start, 0.02f, 0.0f, 100000.0f, "%.2f s")) {
            layer.startTime = std::max(0.0f, start);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("now##in")) {
            layer.startTime = engine.timelineClock().seconds;
        }
        rowLabel("out");
        if (ImGui::DragFloat("##out", &end, 0.02f, 0.0f, 100000.0f, "%.2f s")) {
            layer.endTime = std::max(0.0f, end);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("now##out")) {
            layer.endTime = engine.timelineClock().seconds;
        }
        if (layer.endTime <= layer.startTime) {
            ImGui::SameLine();
            ImGui::TextDisabled("(to the end)");
        }
        int blend = static_cast<int>(layer.blend);
        static const char* kBlends[] = {"normal", "add", "screen", "multiply"};
        rowLabel("blend");
        if (ImGui::Combo("##blend", &blend, kBlends, 4)) {
            layer.blend = static_cast<comp::BlendMode>(blend);
        }
    }
    ImGui::PopID();

    // Every edit above writes the authored field; this is where it reaches the parameter, which is
    // what the renderer, the timeline and the modulation routes all read.
    layer.pushAuthored();
}

void CompositionPanel::drawTextInspector(app::Engine& engine, comp::TextLayer& text) {
    if (ImGui::CollapsingHeader("Text", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::InputTextMultiline("##text", textBuffer_, sizeof(textBuffer_),
                                      ImVec2(-1.0f, ImGui::GetTextLineHeight() * 3.2f))) {
            text.setText(textBuffer_);
        }
        drawFontPicker(text);
        const comp::FontResolution& resolved = text.resolvedFont();
        if (!resolved.available) {
            ImGui::TextColored(kWarning, "no usable face for '%s'", text.font().family.c_str());
        } else if (resolved.substituted) {
            ImGui::TextColored(kWarning, "'%s' is not installed; drawing with %s", text.font().family.c_str(),
                               resolved.postScriptName.c_str());
        } else {
            ImGui::TextDisabled("%s", resolved.postScriptName.c_str());
        }
    }

    if (ImGui::CollapsingHeader("Typography", ImGuiTreeNodeFlags_DefaultOpen)) {
        keyDot(engine, text.parameterPath("size"));
        rowLabel("size");
        ImGui::DragFloat("##size", &text.size, 0.001f, 0.001f, 4.0f, "%.3f of frame height");
        int align = static_cast<int>(text.align());
        static const char* kAligns[] = {"left", "center", "right"};
        rowLabel("align");
        if (ImGui::Combo("##align", &align, kAligns, 3)) {
            text.setAlign(static_cast<comp::TextAlign>(align));
        }
        float tracking = text.tracking();
        rowLabel("tracking");
        if (ImGui::DragFloat("##tracking", &tracking, 0.001f, -0.3f, 1.0f, "%.3f em")) {
            text.setTracking(tracking);
        }
        float spacing = text.lineSpacing();
        rowLabel("line spacing");
        if (ImGui::DragFloat("##spacing", &spacing, 0.005f, 0.2f, 4.0f, "%.2f x")) {
            text.setLineSpacing(spacing);
        }
    }

    if (ImGui::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen)) {
        keyDot(engine, text.parameterPath("color"));
        rowLabel("colour");
        ImGui::ColorEdit4("##colour", &text.color.x, ImGuiColorEditFlags_Float);
        keyDot(engine, text.parameterPath("opacity"));
        rowLabel("opacity");
        ImGui::SliderFloat("##opacity", &text.opacity, 0.0f, 1.0f);
        keyDot(engine, text.parameterPath("outlineWidth"));
        rowLabel("outline");
        ImGui::DragFloat("##outline", &text.outlineWidth, 0.0005f, 0.0f, comp::GlyphAtlas::kSpreadEm, "%.4f em");
        ImGui::SameLine();
        ImGui::ColorEdit4("##outlinecolour", &text.outlineColor.x,
                          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_Float);
        keyDot(engine, text.parameterPath("glow"));
        rowLabel("glow");
        ImGui::DragFloat("##glow", &text.glowRadius, 0.0005f, 0.0f, comp::GlyphAtlas::kSpreadEm, "%.4f em");
        ImGui::SameLine();
        ImGui::ColorEdit4("##glowcolour", &text.glowColor.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_Float);
        keyDot(engine, text.parameterPath("shadowOpacity"));
        rowLabel("shadow");
        ImGui::SliderFloat("##shadow", &text.shadowOpacity, 0.0f, 1.0f);
        keyDot(engine, text.parameterPath("shadowOffset"));
        rowLabel("shadow offset");
        ImGui::DragFloat2("##shadowoffset", &text.shadowOffset.x, 0.0005f, -comp::GlyphAtlas::kSpreadEm,
                          comp::GlyphAtlas::kSpreadEm, "%.4f em");
    }
}

void CompositionPanel::drawShapeInspector(app::Engine& engine, comp::ShapeLayer& shape) {
    if (ImGui::CollapsingHeader("Shape", ImGuiTreeNodeFlags_DefaultOpen)) {
        int kind = static_cast<int>(shape.shape);
        static const char* kShapes[] = {"rectangle", "ellipse", "line"};
        rowLabel("shape");
        if (ImGui::Combo("##shape", &kind, kShapes, 3)) {
            shape.shape = static_cast<comp::ShapeKind>(kind);
        }
        keyDot(engine, shape.parameterPath("size"));
        rowLabel("size");
        ImGui::DragFloat2("##size", &shape.size.x, 0.002f, 0.0f, 8.0f, "%.3f of frame height");
        keyDot(engine, shape.parameterPath("cornerRadius"));
        rowLabel("corner");
        ImGui::DragFloat("##corner", &shape.cornerRadius, 0.001f, 0.0f, 4.0f, "%.3f");
    }
    if (ImGui::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen)) {
        keyDot(engine, shape.parameterPath("color"));
        rowLabel("fill");
        ImGui::ColorEdit4("##fill", &shape.color.x, ImGuiColorEditFlags_Float);
        keyDot(engine, shape.parameterPath("opacity"));
        rowLabel("opacity");
        ImGui::SliderFloat("##opacity", &shape.opacity, 0.0f, 1.0f);
        keyDot(engine, shape.parameterPath("strokeWidth"));
        rowLabel("stroke");
        ImGui::DragFloat("##stroke", &shape.strokeWidth, 0.0005f, 0.0f, 1.0f, "%.4f");
        ImGui::SameLine();
        ImGui::ColorEdit4("##strokecolour", &shape.strokeColor.x,
                          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_Float);
        keyDot(engine, shape.parameterPath("glow"));
        rowLabel("glow");
        ImGui::DragFloat("##glow", &shape.glowRadius, 0.0005f, 0.0f, 1.0f, "%.4f");
        ImGui::SameLine();
        ImGui::ColorEdit4("##glowcolour", &shape.glowColor.x,
                          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_Float);
    }
}

void CompositionPanel::drawFontPicker(comp::TextLayer& text) {
    comp::FontDesc font = text.font();
    rowLabel("font");
    if (ImGui::BeginCombo("##font", font.family.c_str())) {
        // A machine has hundreds of families. A filter at the top of the list is the difference
        // between picking a font and scrolling for one.
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##fontfilter", "filter", fontFilter_, sizeof(fontFilter_));
        std::string needle = fontFilter_;
        std::transform(needle.begin(), needle.end(), needle.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        for (const std::string& family : comp::fontBackend().families()) {
            if (!needle.empty()) {
                std::string lower = family;
                std::transform(lower.begin(), lower.end(), lower.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (lower.find(needle) == std::string::npos) {
                    continue;
                }
            }
            if (ImGui::Selectable(family.c_str(), family == font.family)) {
                font.family = family;
                font.postScriptName.clear(); // the family plus the traits picks the face
                text.setFont(font);
            }
        }
        ImGui::EndCombo();
    }
    rowLabel("weight");
    float weight = font.weight;
    if (ImGui::SliderFloat("##weight", &weight, -1.0f, 1.0f, "%.2f")) {
        font.weight = weight;
        font.postScriptName.clear();
        text.setFont(font);
    }
    ImGui::SameLine();
    bool italic = font.italic;
    if (ImGui::Checkbox("italic", &italic)) {
        font.italic = italic;
        font.postScriptName.clear();
        text.setFont(font);
    }
}

} // namespace avgen::ui
