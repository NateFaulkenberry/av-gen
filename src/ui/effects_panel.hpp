#pragma once

// The Effects section (ADR-702): one owner's effect stack, drawn the same way for every owner.
//
// Before ADR-702 there was a "World Effects" panel with two halves -- ADR-207's surface waves and
// ADR-230's sky and medium effects -- each with its own add buttons, its own commit path and its own
// idea of what an effect was attached to (nothing). Now an effect is an instance attached to an
// owner, so its controls belong where the owner is edited: the World inspector's Environment
// selection shows the World's stack, a selected node shows that entity's, the camera selection the
// camera's, and the Lights panel the selected light's. There is no standalone effects panel.
//
// **One code path, no per-type branches.** Everything a card shows comes from the type's schema in
// the registry: its rows and their pages (Main above the fold, Advanced behind a tree node), the
// shared timing / ground / field rows it applies, its presets, its sky anchor, its source and target
// endpoints, whether it lights the ground, which leaf the Beat response slider drives. A new type is
// a new file in `world/effects/kinds/` and appears here -- in the Add Effect menu and as a card --
// with no edit to this file. `effects_panel_logic.hpp` holds the decisions, so a test can ask them.
//
// **Two kinds of edit, two paths.**
//   * A parameter -- every slider, colour, checkbox and choice row, and the card's enable box -- is
//     an ordinary `fx/<id>/<leaf>` parameter, written at its BASE so routes, timeline keys, presets
//     and cues keep working. Right-click a row to reset it or key it. Each gesture records one
//     `ParamChange` for undo when the gesture ends (a drag is one step, not sixty).
//   * A structural edit -- add, remove, duplicate, reorder, preset, reset, activation, anchor, ground
//     glow, field subscription, source, target -- goes through `Engine::editEffects`, the one
//     mutation path, and records one `EffectChange` (before/after) for undo.

#include "core/error.hpp"
#include "world/effects/effect_instance.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace avgen::app {
class Engine;
}
namespace avgen::world {
struct EffectField;
}

namespace avgen::ui {

class EditHistory;

class EffectsSection {
public:
    // Draws `owner`'s stack and the "+ Add Effect" menu. `history` may be null (no undo recorded).
    // Safe to call from several panels in one frame: every id is scoped to the owner.
    void draw(app::Engine& engine, const world::EffectOwner& owner, EditHistory* history);

private:
    using InstanceEdit = std::function<void(world::EffectInstance&)>;
    using ListEdit = std::function<Result<void>(std::vector<world::EffectInstance>&)>;

    void drawCard(app::Engine& engine, const world::EffectInstance& effect);
    void drawCardBody(app::Engine& engine, const world::EffectInstance& effect);
    void drawEndpoint(app::Engine& engine, const world::EffectInstance& effect, bool target);
    void drawFlowPicker(app::Engine& engine, const world::EffectInstance& effect);
    void drawBeatResponse(app::Engine& engine, const world::EffectInstance& effect);
    // One schema row, bound to `fx/<id>/<leaf>`.
    void drawField(app::Engine& engine, const world::EffectInstance& effect, const world::EffectField& field);
    // The enable box on the card header, bound to `fx/<id>/enabled`.
    void drawEnableBox(app::Engine& engine, const world::EffectInstance& effect);

    // Structural edits are queued while the list is being drawn and applied once after it, because
    // applying one re-registers every `fx/...` parameter the rest of the frame's widgets hold.
    void queue(std::string label, ListEdit edit);
    void queueInstance(std::string label, const std::string& id, InstanceEdit edit);

    // ---- parameter undo --------------------------------------------------------------------------
    // A gesture opens a record on its first changed frame (with the values from BEFORE the widget
    // wrote) and closes it when no item is active any more, so a slider drag is one undo step.
    void noteParamEdit(app::Engine& engine, const std::string& path, std::vector<float> before,
                       std::string label);
    void flushParamEdit(app::Engine& engine);

    EditHistory* history_ = nullptr;
    const world::EffectOwner* owner_ = nullptr;
    std::string status_;

    std::optional<world::EffectKind> pendingAdd_;
    std::string pendingLabel_;
    ListEdit pendingEdit_;

    bool paramOpen_ = false;
    std::string paramPath_;
    std::string paramLabel_;
    std::vector<float> paramBefore_;

    // A drag on a structural value (an endpoint position) is committed on release, not per frame:
    // committing re-registers the effect's parameters, which is not a thing to do sixty times a
    // second. Between press and release the widget shows this buffer.
    std::string dragKey_;
    glm::vec3 dragValue_{0.0f};
};

} // namespace avgen::ui
