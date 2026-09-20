#include "assets/bvh_loader.hpp"
#include <fmt/format.h>
int main(int argc, char** argv) {
    avgen::assets::BvhLoadOptions o; o.scale = 0.01f;
    auto b = avgen::assets::loadBvh(argv[1], o);
    if (!b) { fmt::print("{}\n", b.error().message); return 1; }
    avgen::scene::Pose rest = avgen::scene::restPose(b->skeleton);
    std::vector<glm::mat4> m;
    avgen::scene::poseToModel(b->skeleton, rest, m);
    glm::vec3 lo(1e30f), hi(-1e30f);
    for (std::size_t i = 0; i < m.size(); ++i) {
        const glm::vec3 p(m[i][3]);
        lo = glm::min(lo, p); hi = glm::max(hi, p);
        if (i < 30) fmt::print("  [{:2}] {:<20} ({:+.3f},{:+.3f},{:+.3f})\n", i,
                               b->skeleton.joints[i].name, p.x, p.y, p.z);
    }
    fmt::print("extent x {:.3f}  y {:.3f}  z {:.3f}\n", hi.x-lo.x, hi.y-lo.y, hi.z-lo.z);
    return 0;
}
