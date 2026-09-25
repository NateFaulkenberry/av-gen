#pragma once

// The Director panel (spec §35, ADR-762): the request beside its context, the plan's items marked
// ok / warning / blocked, the proposed changes, and Preview / Accept / Reject. Minimal on purpose --
// it visualises the plan, validator and compiler, and every decision it makes is in
// `director_panel_logic.hpp`, where a test can reach it.

#include "directing/compiler.hpp"

#include <cstdint>
#include <functional>
#include <map>
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

    // Preview stills (spec §55, ADR-764). The panel asks; the host renders them from a scratch copy
    // outside the ImGui frame and hands back one texture with a region per shot item. The request
    // carries the dry run the panel is showing, so what is rendered is what is listed.
    std::function<void(const std::string& task, const directing::Compilation&)> onRequestStills;
    struct Still {
        float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
        double seconds = 0.0;
    };
    struct Stills {
        std::string task;               // the proposal they are for
        std::uint64_t texture = 0;      // ImTextureID; 0 = none
        float width = 0.0f, height = 0.0f; // one still's size in the panel
        std::map<std::string, Still> byItem;
        std::string note;               // "rendered 1 still in 4.2 s", or why not
    };
    Stills stills;
    // The task whose proposal is being previewed or shown, for the host to match stills to it.
    [[nodiscard]] const std::string& shownTask() const { return cachedTask_; }
    // The history state the project is in with this panel's own preview taken off: what a cache of
    // the project (the stills' scratch session) is keyed on, so a preview does not invalidate it.
    [[nodiscard]] std::uint64_t baseState() const;

    void draw(app::Engine& engine);

    // Where the buttons were drawn last frame, in window coordinates, for the `director-*` UI
    // script arms (ADR-762), which press them through the pointer like a hand would. `valid` is
    // false until the panel has drawn them, and whenever they are scrolled or clipped out of view.
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
        Rect stills;
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
    std::uint64_t previewBase_ = 0; // the history state just before the preview edit
    std::string status_;
    Buttons buttons_;
};

} // namespace avgen::ui
