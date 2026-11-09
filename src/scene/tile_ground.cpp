#include "scene/tile_ground.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::scene {

void TileGround::set(const TileTerrainSpec& tiles, const TileDynamics& dyn, float pitch, glm::vec3 offset) {
    tiles_ = tiles;
    dyn_ = dyn;
    dyn_.springs.clear();
    // The springs the ground answers with are its own landings, in the generator's space.
    for (const TileEvent& e : landings_) {
        TileEvent g = e;
        g.position -= glm::vec2(offset.x, offset.z);
        dyn_.springs.push_back(g);
    }
    pitch_ = pitch;
    offset_ = offset;
}

world::GroundSample TileGround::at(float x, float z, double t) const {
    world::GroundSample s;
    if (!valid()) {
        return s;
    }
    const glm::vec2 c = cellCentre(x, z) - glm::vec2(offset_.x, offset_.z);
    const TileSample tile = tileSampleMoving(tiles_, dyn_, c.x, c.y, static_cast<float>(t), pitch_);
    s.height = tile.height + offset_.y;
    s.water = tile.water;
    s.flat = tile.flat;
    s.path = tile.path;
    return s;
}

glm::vec2 TileGround::cellCentre(float x, float z) const {
    const float gx = x - offset_.x;
    const float gz = z - offset_.z;
    return tileCentre(pitch_, tileIndexAt(pitch_, gx, gz)) + glm::vec2(offset_.x, offset_.z);
}

void TileGround::landed(glm::vec2 xz, double t, float strength) {
    const auto when = static_cast<float>(t);
    for (const TileEvent& e : landings_) {
        if (std::abs(e.time - when) < 1e-4f && glm::length(e.position - xz) < 1e-2f) {
            return; // the same landing again (a replay): it has already sprung
        }
    }
    TileEvent e;
    e.time = when;
    e.position = xz;
    e.strength = strength;
    landings_.push_back(e);
    // Keep only what can still ring, plus a margin: the list is the springs' history, not a log.
    const float horizon = dyn_.springSeconds() + 1.0f;
    std::erase_if(landings_, [&](const TileEvent& x) { return when - x.time > horizon; });
    dyn_.springs.push_back(TileEvent{e.time, e.position - glm::vec2(offset_.x, offset_.z), e.strength});
}

void TileGround::forgetAfter(double t) {
    const auto when = static_cast<float>(t);
    std::erase_if(landings_, [when](const TileEvent& e) { return e.time > when; });
    std::erase_if(dyn_.springs, [when](const TileEvent& e) { return e.time > when; });
}

void TileGround::restoreLandings(std::vector<TileEvent> landings) {
    landings_ = std::move(landings);
    dyn_.springs.clear();
    for (const TileEvent& e : landings_) {
        dyn_.springs.push_back(TileEvent{e.time, e.position - glm::vec2(offset_.x, offset_.z), e.strength});
    }
}

} // namespace avgen::scene
