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
};

} // namespace avgen::ui
