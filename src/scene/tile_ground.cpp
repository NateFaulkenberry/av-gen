#include "scene/tile_ground.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::scene {

void TileGround::set(const TileTerrainSpec& tiles, const TileDynamics& dyn, float pitch, glm::vec3 offset) {
    set(tiles, dyn, dyn, pitch, offset);
}

void TileGround::set(const TileTerrainSpec& tiles, const TileDynamics& dyn, const TileDynamics& plan, float pitch,
                     glm::vec3 offset) {
    tiles_ = tiles;
    dyn_ = dyn;
    plan_ = plan;
    still_ = plan;
    still_.springs.clear();
    pitch_ = pitch;
    offset_ = offset;
    // The springs the ground answers with are its own landings, in the generator's space (on both surfaces: a landing
    // is the characters' own doing and a seek replays it).
    dyn_.springs.clear();
    for (const TileEvent& e : landings_) {
        dyn_.springs.push_back(TileEvent{e.time, e.position - glm::vec2(offset.x, offset.z), e.strength});
    }
    plan_.springs = dyn_.springs;
}

world::GroundSample TileGround::sample(const TileDynamics& d, float x, float z, double t) const {
    world::GroundSample s;
    if (!valid()) {
        return s;
    }
    const glm::vec2 c = cellCentre(x, z) - glm::vec2(offset_.x, offset_.z);
    TileSample tile;
    if (motion_ != nullptr && !motion_->empty()) {
        TileDynamics moved = d; // ADR-1998: the music's lanes and rings at this instant
        motion_->apply(moved, t);
        tile = tileSampleMoving(tiles_, moved, c.x, c.y, static_cast<float>(t), pitch_);
    } else {
        tile = tileSampleMoving(tiles_, d, c.x, c.y, static_cast<float>(t), pitch_);
    }
    s.height = tile.height + offset_.y;
    s.water = tile.water;
    s.flat = tile.flat;
    s.path = tile.path;
    s.bank = tile.bank; // ADR-1999
    s.river = tile.river;
    s.pond = tile.pond;
    return s;
}

world::GroundSample TileGround::at(float x, float z, double t) const { return sample(dyn_, x, z, t); }

world::GroundSample TileGround::planned(float x, float z, double t) const { return sample(plan_, x, z, t); }

world::GroundSample TileGround::landscape(float x, float z, double t) const { return sample(still_, x, z, t); }

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
    plan_.springs.push_back(dyn_.springs.back());
}

void TileGround::forgetAfter(double t) {
    const auto when = static_cast<float>(t);
    std::erase_if(landings_, [when](const TileEvent& e) { return e.time > when; });
    std::erase_if(dyn_.springs, [when](const TileEvent& e) { return e.time > when; });
    plan_.springs = dyn_.springs;
}

void TileGround::restoreLandings(std::vector<TileEvent> landings) {
    landings_ = std::move(landings);
    dyn_.springs.clear();
    for (const TileEvent& e : landings_) {
        dyn_.springs.push_back(TileEvent{e.time, e.position - glm::vec2(offset_.x, offset_.z), e.strength});
    }
    plan_.springs = dyn_.springs;
}

} // namespace avgen::scene
