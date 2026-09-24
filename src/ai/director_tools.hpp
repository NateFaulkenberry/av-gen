#pragma once

// The Director's semantic tools (spec §45, ADR-757): thin wrappers over `src/directing/`, so a model
// works in subjects, musical times and camera language, and the engine -- not the model -- does the
// resolving, the checking and the arithmetic.
//
//   director.inspect_scene         subjects, cameras, sections, existing plans, vocabularies
//   director.inspect_subject       what a name resolves to, and what that subject can do
//   director.inspect_capabilities  the generated registry, whole or for one subject
//   director.resolve_time          "the second chorus" -> seconds, with how it was placed
//   director.plan_schema           the Director Plan contract, generated from the vocabularies
//   director.validate_plan         every finding, per item; changes nothing
//   director.propose_plan          a dry-run compilation and its diff, handed to the person to
//                                  approve (`requiresApproval`); changes nothing
//
// There is deliberately no tool that applies a plan. Applying is the person's act -- the task waits
// in `AwaitingApproval` and `ControlPlane::approveCurrentTask` commits -- so no model can bypass
// approval on persistent state (spec §46-§47). The low-level tools stay as the escape hatch.

#include "ai/tool_api.hpp"
#include "ai/tool_context.hpp"
#include "core/error.hpp"

#include <cstddef>
#include <string>

namespace avgen::app {
class Engine;
}

namespace avgen::ai {

void registerDirectorTools(ToolRegistry& registry);

struct CommitReport {
    std::string planId;
    int revision = 0;
    std::size_t produced = 0;
};

// What an approval runs, on the main thread, inside the task's transaction: re-compile the proposed
// plan against the project as it is now, refuse if the diff differs from the one the person
// approved, install, and verify the installed content against the plan's fingerprints. Any failure
// is returned for the caller to roll back.
[[nodiscard]] Result<CommitReport> commitProposal(app::Engine& engine, const ToolContext::Proposal& proposal);

} // namespace avgen::ai
