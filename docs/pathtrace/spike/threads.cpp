// Does Embree let AV Gen own the threads? (spec §36: no second uncontrolled global pool)
#include <embree4/rtcore.h>
#include <cstdio>
#include <vector>
#include <thread>
#include <cmath>
#include <atomic>

static int baselineThreads() {
    // crude: count threads of this process via task_info is messy; instead report what Embree says.
    return 0;
}

static RTCScene bigScene(RTCDevice dev, int n) {
    RTCScene s = rtcNewScene(dev);
    RTCGeometry g = rtcNewGeometry(dev, RTC_GEOMETRY_TYPE_TRIANGLE);
    auto* v = (float*)rtcSetNewGeometryBuffer(g, RTC_BUFFER_TYPE_VERTEX, 0, RTC_FORMAT_FLOAT3, 12, 3*n);
    auto* i = (unsigned*)rtcSetNewGeometryBuffer(g, RTC_BUFFER_TYPE_INDEX, 0, RTC_FORMAT_UINT3, 12, n);
    for (int t = 0; t < n; ++t) {
        float a = t * 0.0137f, r = 1.0f + (t % 100) * 0.01f;
        for (int k = 0; k < 3; ++k) {
            v[(t*3+k)*3+0] = r * std::cos(a + k) ;
            v[(t*3+k)*3+1] = r * std::sin(a + k * 2.1f);
            v[(t*3+k)*3+2] = -2.0f - (t % 50) * 0.1f;
        }
        i[t*3+0]=t*3; i[t*3+1]=t*3+1; i[t*3+2]=t*3+2;
    }
    rtcCommitGeometry(g); rtcAttachGeometry(s, g); rtcReleaseGeometry(g);
    return s;
}

int main() {
    const int N = 200000;
    for (const char* cfg : {"", "threads=1", "threads=4,set_affinity=0"}) {
        RTCDevice dev = rtcNewDevice(cfg);
        if (!dev) { std::printf("cfg '%s': rtcNewDevice FAILED\n", cfg); continue; }
        RTCScene s = bigScene(dev, N);
        rtcCommitScene(s);
        // verify the scene is usable, not just built
        RTCRayHit rh{}; rh.ray.org_z=10; rh.ray.dir_z=-1; rh.ray.tfar=1e30f; rh.ray.mask=-1;
        rh.hit.geomID=RTC_INVALID_GEOMETRY_ID;
        rtcIntersect1(s, &rh);
        std::printf("cfg '%-24s' -> device OK, commit OK, probe hit=%s\n", cfg,
                    rh.hit.geomID != RTC_INVALID_GEOMETRY_ID ? "yes" : "no");
        rtcReleaseScene(s); rtcReleaseDevice(dev);
    }

    // rtcJoinCommitScene: can AV Gen's OWN threads perform the BVH build?
    {
        RTCDevice dev = rtcNewDevice("threads=1");
        RTCScene s = bigScene(dev, N);
        std::atomic<int> joined{0};
        std::vector<std::thread> ts;
        for (int t = 0; t < 4; ++t)
            ts.emplace_back([&]{ rtcJoinCommitScene(s); joined++; });
        for (auto& t : ts) t.join();
        RTCRayHit rh{}; rh.ray.org_z=10; rh.ray.dir_z=-1; rh.ray.tfar=1e30f; rh.ray.mask=-1;
        rh.hit.geomID=RTC_INVALID_GEOMETRY_ID;
        rtcIntersect1(s, &rh);
        std::printf("rtcJoinCommitScene from 4 caller threads -> joined=%d, probe hit=%s\n",
                    joined.load(), rh.hit.geomID != RTC_INVALID_GEOMETRY_ID ? "yes" : "no");
        rtcReleaseScene(s); rtcReleaseDevice(dev);
    }
    return 0;
}
