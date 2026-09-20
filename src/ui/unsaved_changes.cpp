// The wording and the modal for ADR-440. The state machine is in the header and has no ImGui in it.

#include "ui/unsaved_changes.hpp"

#include <imgui.h>

#include <string>

namespace avgen::ui {

UnsavedAnswer drawUnsavedChangesModal(const UnsavedChangesGate& gate, const std::string& projectName,
                                      bool* answered) {
    if (answered != nullptr) {
        *answered = false;
    }
    // ImGui's modal has to be opened in the same frame stack it is begun in, and `OpenPopup` is
    // idempotent while the popup is already open, so this can simply be asserted every frame the
    // gate is prompting. The alternative -- remembering "I have opened it" -- is a second copy of
    // the gate's state, and two answers to "is the dialog up" is how a dialog ends up invisible
    // with the application still blocked behind it.
    const char* kId = "Unsaved changes##avgen";
    if (!gate.prompting()) {
        return UnsavedAnswer::Cancel;
    }
    ImGui::OpenPopup(kId);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
    UnsavedAnswer answer = UnsavedAnswer::Cancel;
    bool chose = false;
    // `nullptr` for `p_open`: no close box. A window-manager close on a dialog whose whole subject
    // is losing work is an ambiguous answer, and this dialog has no ambiguous answer.
    if (ImGui::BeginPopupModal(kId, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(unsavedChangesQuestion(gate.intent(), projectName).c_str());
        ImGui::Spacing();
        ImGui::TextDisabled("If you don't save, your changes will be lost.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Yes first and focused, because the safe answer should be the one Return takes.
        // `SetItemDefaultFocus` applies to the item *just submitted*, so it goes after the button
        // and not before it -- placed first it would focus the separator above.
        if (ImGui::Button("Yes", ImVec2(96.0f, 0.0f)) ||
            ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
            ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
            answer = UnsavedAnswer::Save;
            chose = true;
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("No", ImVec2(96.0f, 0.0f))) {
            answer = UnsavedAnswer::Discard;
            chose = true;
        }
        ImGui::SameLine();
        // Escape answers Cancel, which is the convention everywhere and the only key binding where
        // guessing wrong is free.
        if (ImGui::Button("Cancel", ImVec2(96.0f, 0.0f)) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            answer = UnsavedAnswer::Cancel;
            chose = true;
        }
        if (chose) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (answered != nullptr) {
        *answered = chose;
    }
    return answer;
}

} // namespace avgen::ui
