#pragma once

// The tree as a `PrimitiveKind::Generated` source: what makes it a scene object rather than a mesh
// the renderer was handed.
//
// Until now the Tree of Life was assembled in C++ into a `scene::Scene`, which is why it never
// appeared in the examples menu: `examples/index.json` addresses a project or a recipe, and a scene
// built by calling a function is neither. Worse, a `Scene` is a per-frame derivation -- anything
// written straight into one does not survive an update -- so the tree could not be selected,
// inspected, keyed, undone or saved however it was loaded.
//
// `GeneratedSource` is the route (ADR-175). The unit a scene stores is the parameter VECTOR, not the
// geometry: the node regenerates from its nineteen numbers, an artist moving one gets a different
// tree, and the provenance -- which candidate of which search these numbers started as -- travels
// with it.
//
// THE PART SPLIT IS THE TIER SPLIT, because the editor picks by object. Eight parts, not six: the
// canopy is three because its three tints are three materials, and a material is the smallest thing
// a node can carry one of.

#include "core/error.hpp"
#include "scene/procedural.hpp"
#include "scene/scene_types.hpp"

#include <cstdint>
#include <string>

namespace avgen::scene {

// Part indices, in the order they appear in the outliner: base to tip.
enum class TreePart : int { Roots = 0, Trunk, Primary, Secondary, Tertiary, Foliage0, Foliage1, Foliage2 };
inline constexpr int kTreeParts = 8;
[[nodiscard]] const char* treePartName(int part);

// The environment the tree needs to read at its real size -- the ground it stands on and the ring of
// distant trees that give it scale. Generated rather than authored because the trees are geometry
// nobody would hand-place, and it takes the same parameter vector so one seed drives the whole shot.
enum class TreeEnvironmentPart : int { Ground = 0, DistantTrees };
inline constexpr int kTreeEnvironmentParts = 2;

// Registers "tree" and "tree-environment" with the generator registry. Called by the Engine beside
// the mushroom generator; a scene file naming a generator nobody registered fails to build with a
// message that says so, which is the behaviour wanted.
void registerTreeGenerator();

} // namespace avgen::scene
