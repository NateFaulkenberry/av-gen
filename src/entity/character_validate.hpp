#pragma once

// Offline validation of autonomous-character definitions (Phase D §58).
//
// "A character definition should be validated before runtime." Every check here is a way a
// character can load cleanly and then do nothing for a reason no log states -- the shape of defect
// this repository has shipped most often (ADR-615's catalogue). Each is a question about the scene
// *data*: it needs no world, no GPU and no simulation, so it runs in milliseconds and can gate a
// commit.
//
// What it catches (§58's list, as it applies to this engine):
//   * missing capabilities       -- an `affordance` whose every offer requires something the
//                                   character lacks (an impossible interaction)
//   * invalid affordances        -- an `affordance` no entity in the scene offers
//   * invalid behaviour refs     -- an unknown considerer kind; a `react` naming an event nothing
//                                   raises; a tag filter naming a word no entity carries
//   * missing prerequisites      -- a considerer that reads what the body perceives or heard on a
//                                   body with no `perception` block or no `mind`
//   * unknown personality traits -- in a considerer's `traits`
//   * activities with no clip    -- an activity a considerer plays that the character maps to no
//                                   clip (R4: the mapping is the character's, so it is checkable)

#include "entity/entity.hpp"

#include <string>
#include <vector>

namespace avgen::entity {

struct ValidationIssue {
    enum class Severity : std::uint8_t { Error, Warning };
    Severity severity = Severity::Error;
    std::string entity;
    std::string message;
};

[[nodiscard]] std::vector<ValidationIssue>
validateCharacters(const std::vector<EntityDesc>& entities,
                   const std::vector<EntityWorld::EventProfile>& eventProfiles);

} // namespace avgen::entity
