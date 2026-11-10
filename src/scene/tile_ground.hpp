#pragma once

// ADR-1955: a tiles ground as a world::GroundSource -- the moving surface (ADR-1953) with its landing springs
// (ADR-1954), in world space. The composition owns one per scene with a tiles ground, refreshes it every frame with
// the live tiles spec and dynamics, and drains the landings into the moving ground's spring list.

#include "scene/tile_terrain.hpp"
#include "world/ground_source.hpp"

#include <vector>

namespace avgen::scene {

class TileGround final : public world::GroundSource {
public:
    // `offset` is the ground's world translation (a tiles generator's transform is never rotated or scaled).
    void set(const TileTerrainSpec& tiles, const TileDynamics& dyn, float pitch, glm::vec3 offset);
    // ADR-1964: with a planning surface as well -- `plan` is the dynamics without what routes modulate (the parameters'
    // bases, no live rings). `set` without it plans on `dyn`.
    void set(const TileTerrainSpec& tiles, const TileDynamics& dyn, const TileDynamics& plan, float pitch,
             glm::vec3 offset);
    [[nodiscard]] bool valid() const { return pitch_ > 0.0f; }

    [[nodiscard]] world::GroundSample at(float x, float z, double t) const override;
    [[nodiscard]] world::GroundSample planned(float x, float z, double t) const override;
    [[nodiscard]] float pitch() const override { return pitch_; }
    [[nodiscard]] glm::vec2 cellCentre(float x, float z) const override;
    void landed(glm::vec2 xz, double t, float strength) override;

    // The landings so far, world space (the composition copies them into the dynamics' spring list each frame).
    [[nodiscard]] const std::vector<TileEvent>& landings() const { return landings_; }
    // A seek backwards: forget the landings that had not happened by `t`.
    void forgetAfter(double t);
    // A seek checkpoint's restore: the landings exactly as they were at the checkpoint.
    void restoreLandings(std::vector<TileEvent> landings);

private:
    TileTerrainSpec tiles_;
    TileDynamics dyn_;
    TileDynamics plan_;
    [[nodiscard]] world::GroundSample sample(const TileDynamics& d, float x, float z, double t) const;
    float pitch_ = 0.0f;
    glm::vec3 offset_{0.0f};
    std::vector<TileEvent> landings_;
};

} // namespace avgen::scene
