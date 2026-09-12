#include "app/ai_edit_sink.hpp"

#include "app/engine.hpp"
#include "core/log.hpp"

namespace avgen::app {
namespace {

// Every parameter's base value, by path. Bases rather than finals: a final carries whatever
// modulation and automation are doing this frame, and an undo that wrote those back would bake a
// moment of an LFO into the document.
[[nodiscard]] std::unordered_map<std::string, std::vector<float>> captureBases(Engine& engine) {
    std::unordered_map<std::string, std::vector<float>> out;
    const params::ParameterSet& params = engine.params();
    out.reserve(params.size());
    for (const params::IParameter* parameter : params.ordered()) {
        if (parameter == nullptr) {
            continue;
        }
        std::vector<float> components;
        components.reserve(parameter->componentCount());
        for (std::size_t i = 0; i < parameter->componentCount(); ++i) {
            components.push_back(parameter->baseComponent(i));
        }
        out.emplace(parameter->path(), std::move(components));
    }
    return out;
}

} // namespace

void EditHistoryTransactionSink::begin(const std::string& label) {
    // The snapshot goes first and stays underneath: it is what makes an abort possible, and this
    // sink does not replace that promise, only adds an undoable one.
    fallback_->begin(label);
    before_ = captureBases(*engine_);
    open_ = true;
}

void EditHistoryTransactionSink::commit(const std::string& label) {
    fallback_->commit(label);
    if (!open_) {
        return;
    }
    open_ = false;

    ui::EditCommand command(label);
    const params::ParameterSet& params = engine_->params();
    for (const params::IParameter* parameter : params.ordered()) {
        if (parameter == nullptr) {
            continue;
        }
        const auto was = before_.find(std::string(parameter->path()));
        if (was == before_.end()) {
            continue; // registered during the task; there is no "before" to go back to
        }
        std::vector<float> now;
        now.reserve(parameter->componentCount());
        for (std::size_t i = 0; i < parameter->componentCount(); ++i) {
            now.push_back(parameter->baseComponent(i));
        }
        // Arity can change under a scene swap mid-task; a record whose two sides disagree cannot be
        // applied, so it is dropped rather than pushed as a command that would fail at undo time.
        if (now.size() != was->second.size() || now == was->second) {
            continue;
        }
        command.params.push_back(ui::ParamChange{std::string(parameter->path()), was->second, now});
    }
    before_.clear();

    if (command.empty()) {
        return; // a task that changed no parameter is not an edit, and must not leave an entry
    }
    log::info("ai: '{}' recorded as one undoable edit ({} parameter(s))", label,
              command.params.size());
    edits_->history().push(std::move(command));
}

void EditHistoryTransactionSink::abort() {
    // The snapshot puts the document back. Nothing is pushed: a task that was rolled back did not
    // happen, and an entry for it would offer to undo an edit the user never saw.
    fallback_->abort();
    before_.clear();
    open_ = false;
}

} // namespace avgen::app
