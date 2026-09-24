#pragma once

// A compiled plan as one undoable edit (ADR-752, ADR-756). Separate from `directing_context.hpp`
// because it needs `app::EditCapture`, an application source: the AI layer (in the core library)
// includes that header and must not pull this one in.

#include "app/directing_context.hpp"
#include "app/edit_capture.hpp"
#include "ui/edit_history.hpp"

namespace avgen::app {

// Installs a compilation as ONE command on the editor's history: the content and the plan together,
// so one undo takes back the content and its provenance at once. A refused install is undone before
// returning and pushes nothing.
[[nodiscard]] inline Result<void> applyCompilation(Engine& engine, ui::EditHistory& history,
                                                   const directing::Compilation& compilation) {
    EditCapture capture;
    capture.begin(engine);
    if (auto r = installCompilation(engine, compilation); !r) {
        ui::EditCommand partial = capture.finish(engine, "refused");
        (void)ui::applyEdit(engine, partial, false);
        return r;
    }
    history.push(capture.finish(engine, "Director: " + (compilation.plan.title.empty() ? compilation.plan.id
                                                                                         : compilation.plan.title)));
    return {};
}

} // namespace avgen::app
