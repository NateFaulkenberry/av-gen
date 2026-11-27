#pragma once

// ADR-2220: `entity::NavGrid` presented as an `INavSurface` -- the adapter, and the only place the
// crowd touches `entity/` (115 decision O7). The grid, the navigator and the obstacle field are
// used exactly as the entity layer uses them; nothing here changes how they are built.
//
// What each question is answered by:
//   * findPath        -- `NavGrid::path` (A*, string-pulled, tie-broken on cell index: deterministic)
//   * ground          -- `NavGrid::groundAt` where the grid exists, else `Navigator::groundHeight`
//   * staticObstacles -- the obstacle field's solids as discs (exact), plus the edges of cells whose
//                        *terrain* is not standable (water, cliffs, the world's edge) as segments.
//                        Cells blocked only by a solid emit no edge: the solid is already a disc,
//                        and a blocky edge round it would close gaps a body really fits through.
//   * lineClear       -- the obstacle field's swept test plus the grid's terrain flags along the line
#include "entity/navigation.hpp"
#include "nav/nav_surface.hpp"

namespace avgen::nav {

class GridNavSurface final : public INavSurface {
public:
    GridNavSurface() = default;
    // Takes the navigator by value (it shares its grid and obstacle field by pointer). The world
    // map it points at must outlive this surface; the composition owns both.
    explicit GridNavSurface(entity::Navigator navigator);

    [[nodiscard]] NavBackend backend() const override { return NavBackend::Grid; }
    [[nodiscard]] bool valid() const override;
    [[nodiscard]] NavPath findPath(glm::vec2 from, glm::vec2 to, float tolerance) const override;
    [[nodiscard]] bool ground(glm::vec2 p, float& y) const override;
    void staticObstacles(glm::vec2 p, float range, std::vector<NavDisc>& discs,
                         std::vector<NavSegment>& segments) const override;
    [[nodiscard]] bool lineClear(glm::vec2 a, glm::vec2 b, float radius) const override;
    [[nodiscard]] std::uint64_t contentKey() const override { return key_; }

    [[nodiscard]] const entity::Navigator& navigator() const { return nav_; }

private:
    entity::Navigator nav_;
    std::uint64_t key_ = 0;
    mutable std::vector<std::uint32_t> hits_; // scratch; the crowd step is single-threaded per surface query
};

} // namespace avgen::nav
