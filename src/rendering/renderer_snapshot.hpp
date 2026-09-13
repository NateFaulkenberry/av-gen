#pragma once

// Frame capture and replay for the renderer forensics (plan Phase 9.1).
//
// The investigation's evidence standard says a captured frame is not an image hash: "an image hash
// says something differs; it never says what, and the Phase 9.2 defect was localised in one
// comparison by checking palettes and transforms separately". This is that comparison, made
// portable -- a frame's *state* written to a file, and two of them diffed into sentences naming the
// object and the field.
//
// What is captured is `RendererDiagnosticFrame`, which the renderer already builds every frame, plus
// the isolation arms that were set when it was drawn. The arms matter more than they look: a capture
// taken with shadows off and compared against one taken with them on would report a difference in
// every shaded pixel and none of the state, and somebody would spend an afternoon on it.

#include "core/error.hpp"
#include "rendering/scene_renderer.hpp"

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <vector>

namespace avgen::rendering {

// One captured frame: the diagnostic state and the arms it was drawn with.
struct FrameSnapshot {
    RendererDiagnosticFrame frame;
    SceneRenderer::PassToggles toggles;
    std::string scene;   // what was being rendered, for the human reading the file
    std::string note;    // why it was captured
};

[[nodiscard]] nlohmann::json snapshotToJson(const FrameSnapshot& snapshot);
[[nodiscard]] Result<FrameSnapshot> snapshotFromJson(const nlohmann::json& doc);
[[nodiscard]] Result<void> writeSnapshot(const FrameSnapshot& snapshot, const std::filesystem::path& path);
[[nodiscard]] Result<FrameSnapshot> readSnapshot(const std::filesystem::path& path);

// What differs between two captures, in sentences. Empty means they agree.
//
// Ordered so the most localising differences come first: the arms (which invalidate the rest of the
// comparison), then the camera (which moves everything), then per-object state. `epsilon` is applied
// to positions and matrices; ids, names and flags compare exactly.
//
// Objects are matched by *name*, not by index: a comparison that pairs the fourth object with the
// fourth object reports every object as different the moment one is added, which is the report being
// least useful exactly when something structural has changed.
// `counters` includes the monotonic bookkeeping -- the rig palette version -- which is **not**
// evidence of a difference in state: it increments on every upload, so two arrivals at the same
// second of the same piece legitimately disagree about it. That is the trap Phase 9.2 fell into when
// it tried to use the diagnostic state hash as a replay identity. It is reported when asked for
// because it can explain a pixel difference, and never by default.
[[nodiscard]] std::vector<std::string> compareSnapshots(const FrameSnapshot& expected,
                                                        const FrameSnapshot& actual,
                                                        float epsilon = 1e-5f,
                                                        bool counters = false);

} // namespace avgen::rendering
