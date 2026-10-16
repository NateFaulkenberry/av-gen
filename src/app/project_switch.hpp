#pragma once

// ADR-1390: what the editor shows a project with is decided when that project opens, never inherited from the one
// before it.
//
// The owner, 2026-10-15: after the Lighting Lab, Astral Forge drew red, green, blue and pale-yellow lines through
// the forge's face. They were the Lighting Lab's overlays -- the world axes and each light's direction -- which
// `onOpenLab` wrote into the World panel's switches and which nothing ever wrote back: every later File > Examples
// open kept them, and so did a render started from the editor, which copies the panel's overlays into the job.
// `Engine::loadProject` resets everything the *engine* owns (e3dc8e9e did the last of those); this is the half the
// engine cannot see, because the editor holds it.
//
// The rule is in one place for the reason e3dc8e9e gave for its own: a list of things to reset at each call site that
// opens a project is a list some call site forgets. So nothing here knows *how* a project was opened. The editor asks
// `observe` once a frame with `Engine::projectGeneration()`, which every replacing load bumps -- a menu, a drop, a
// lab, an assistant's tool -- and on a change gets back the overlays the new project opens with: the session's own
// (`--debug-draw`, the person's choice for the whole run) unless the open was a lab's, which asked for its own
// beforehand with `expectOverlays`. `Application::resetProjectViewState` applies that and resets the rest.
//
// Pure data, no device and no ImGui, so the carry-over test (tests/rendering/test_project_switch_gpu.cpp) runs the
// rule the editor runs rather than a copy of it.

#include "rendering/debug_view_options.hpp"

#include <cstdint>
#include <optional>
#include <utility>

namespace avgen::app {

class ProjectSwitch {
public:
    explicit ProjectSwitch(rendering::DebugViewOptions sessionOverlays = {})
        : session_(std::move(sessionOverlays)) {}

    // The next project to open brings these overlays rather than the session's (a lab: its fixture *and* what to look
    // at while it is open). Consumed by that open; dropped by `cancelExpected` when the open fails.
    void expectOverlays(rendering::DebugViewOptions overlays) { expected_ = std::move(overlays); }
    void cancelExpected() { expected_.reset(); }
    [[nodiscard]] bool expecting() const { return expected_.has_value(); }

    // Take the engine's current generation as already handled (start-up: whatever was loaded before the first frame
    // was set up by the code that loaded it).
    void sync(std::uint64_t generation) { seen_ = generation; }

    // Once a frame. Empty while the open project is the same one; on a change, the overlays the new one opens with.
    [[nodiscard]] std::optional<rendering::DebugViewOptions> observe(std::uint64_t generation) {
        if (generation == seen_) {
            return std::nullopt;
        }
        seen_ = generation;
        rendering::DebugViewOptions overlays = expected_ ? std::move(*expected_) : session_;
        expected_.reset();
        return overlays;
    }

    [[nodiscard]] const rendering::DebugViewOptions& sessionOverlays() const { return session_; }

private:
    rendering::DebugViewOptions session_;
    std::optional<rendering::DebugViewOptions> expected_;
    std::uint64_t seen_ = 0;
};

} // namespace avgen::app
