#pragma once

// The Director panel (spec §35, ADR-762): the request beside its context, the plan's items marked
// ok / warning / blocked, the proposed changes, and Preview / Accept / Reject. Minimal on purpose --
// it visualises the plan, validator and compiler, and every decision it makes is in
// `director_panel_logic.hpp`, where a test can reach it.

#include "directing/compiler.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace avgen::app {
class EditSystem;
class Engine;
} // namespace avgen::app

namespace avgen::ai {
class AgentTask;
class ControlPlane;
} // namespace avgen::ai

namespace avgen::ui {

class DirectorPanel {
public:
    avgen::ai::ControlPlane* plane = nullptr;
    app::EditSystem* edits = nullptr;

    void draw(app::Engine& engine);

    // Where the buttons were drawn last frame, in window coordinates, for the `director-*` UI
    // script arms (ADR-762), which press them through the pointer like a hand would. `valid` is
    // false until the panel has drawn them.
    struct Rect {
        float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
        bool valid = false;
        [[nodiscard]] float cx() const { return x + (w * 0.5f); }
        [[nodiscard]] float cy() const { return y + (h * 0.5f); }
    };
    struct Buttons {
        Rect preview; // "Preview", or "End preview" while previewing
        Rect accept;
        Rect reject;
    };
    [[nodiscard]] const Buttons& buttons() const { return buttons_; }
    [[nodiscard]] bool previewing() const { return !previewTask_.empty(); }
    [[nodiscard]] const std::string& status() const { return status_; }

private:
    // The proposal's dry run, recompiled only when the proposal or the project changed.
    void refresh(app::Engine& engine, const std::shared_ptr<avgen::ai::AgentTask>& task);
    bool endPreview(app::Engine& engine);

    std::string cachedTask_;
    std::uint64_t cachedState_ = ~std::uint64_t{0};
    std::optional<directing::Compilation> compiled_;
    std::string compileError_;

    // The preview edit: which proposal it is for, and the history state just after it was made.
    std::string previewTask_;
    std::uint64_t previewState_ = 0;
    std::string status_;
    Buttons buttons_;
};

} // namespace avgen::ui
