#pragma once

// The AI panel (ADR-094, spec §8-§10, §43-§46).
//
// §8 asks for something that feels like a native creative-tool workspace rather than a web chatbot
// embedded in a C++ program, and §9 is emphatic that it must not simply show "Thinking...". So the
// panel is built on the structured activity stream `ai::AgentTask` publishes -- a plan, then each
// tool call with its own description, duration and outcome -- and it never parses model prose to
// work out what is happening. The one thing it deliberately does not show is private model
// reasoning (addendum §14).
//
// It follows the panel convention exactly: default-constructed, held by value on `ControlPanel`,
// given its dependencies as public members by the host, drawing from `app::Engine&` and a pointer
// to the control plane. It owns no scheduling and no engine state.
//
// With nothing configured it shows "AI assistant is not configured" and a button that opens
// Settings -- §46, and ADR-065's rule that an optional failure is not a total one.

#include <cstddef>
#include <functional>
#include <string>

namespace avgen::app {
class Engine;
} // namespace avgen::app

namespace avgen::ai {
class ControlPlane;
} // namespace avgen::ai

namespace avgen::ui {

class AiPanel {
public:
    // Installed by the host. Null means this build or this session has no control plane, which the
    // panel says plainly rather than drawing an empty box.
    avgen::ai::ControlPlane* plane = nullptr;
    // Opens the Settings panel on the AI section. The panel does not reach into the layout itself.
    std::function<void()> onOpenSettings;

    void draw(app::Engine& engine);

private:
    void drawConversation(app::Engine& engine);
    void drawComposer();

    static constexpr std::size_t kPromptCapacity = 4096;
    std::string prompt_ = std::string(kPromptCapacity, '\0');
    // Whether the activity list should stick to the bottom. Released when the user scrolls up, so
    // reading an earlier step is not fought by the stream.
    bool followTail_ = true;
    std::size_t lastActivityCount_ = 0;
    std::string status_;
};

} // namespace avgen::ui
